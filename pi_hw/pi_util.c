/*
 * Copyright (c) 2018 Brian Starkey <stark3y@gmail.com>
 * Portions derived from servod.
 * Copyright (c) 2013 Richard Hirst <richardghirst@gmail.com>
 *
 * This program provides very similar functionality to servoblaster, except
 * that rather than implementing it as a kernel module, servod implements
 * the functionality as a usr space daemon.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <bcm_host.h>

#include "mailbox.h"
#include "pi_util.h"

#define BUS_TO_PHYS(x) ((x)&~0xC0000000)

#ifndef ALIGN_UP
#define ALIGN_UP(_x, _to) (((_x) + ((_to) - 1)) & ~((_to) - 1))
#endif

/* Determining the board revision is a lot more complicated than it should be
 * (see comments in wiringPi for details).  We will just look at the last two
 * digits of the Revision string and treat '00' and '01' as errors, '02' and
 * '03' as rev 1, and any other hex value as rev 2.  'Pi1 and Pi2 are
 * differentiated by the Hardware being BCM2708 or BCM2709.
 */
int get_model_and_revision(struct board_cfg *board)
{
	/*char buf[128], modelstr[128];
	char *res;
	FILE *fp;

	modelstr[0] = '\0';

	fp = fopen("/proc/cpuinfo", "r");
	if (!fp) {
		perror("Unable to open /proc/cpuinfo");
		return -1;
	}

	while ((res = fgets(buf, 128, fp))) {
		if (!strncasecmp("model name", buf, 10))
			memcpy(modelstr, buf, 128);
	}
	fclose(fp);

	if (modelstr[0] == '\0') {
		perror("No 'model name' record in /proc/cpuinfo");
		return -1;
	}*/

	/*
	 * Inspired by pigpio, use the CPU arch to determine the mem attributes.
	 * The method used by servod seems to be incorrect (gives 0x4 on my Pi
	 * Zero)
	 */
	/*
	 * See https://github.com/raspberrypi/firmware/wiki/Mailbox-property-interface
	 *
	 * 1:  MEM_FLAG_DISCARDABLE = 1 << 0	// can be resized to 0 at any time. Use for cached data
	 *     MEM_FLAG_NORMAL = 0 << 2		// normal allocating alias. Don't use from ARM
	 * 4:  MEM_FLAG_DIRECT = 1 << 2		// 0xC alias uncached
	 * 8:  MEM_FLAG_COHERENT = 2 << 2	// 0x8 alias. Non-allocating in L2 but coherent
	 *     MEM_FLAG_L1_NONALLOCATING =	// Allocating in L2
	 *       (MEM_FLAG_DIRECT | MEM_FLAG_COHERENT)
	 * 16: MEM_FLAG_ZERO = 1 << 4		// initialise buffer to all zeros
	 * 32: MEM_FLAG_NO_INIT = 1 << 5	// don't initialise (default is initialise to all ones
	 * 64: MEM_FLAG_HINT_PERMALOCK = 1 << 6	// Likely to be locked for long periods of time
	 *
	 */
/*	if (strstr (modelstr, "ARMv6") != NULL)
	{
		// On BCM2835 we can take "advantage" of the GPU L2 
		board->mem_flag  = 0x0C;
	}
	else if (strstr (modelstr, "ARMv7") != NULL)
	{
		board->mem_flag  = 0x04;
	}
	else if (strstr (modelstr, "ARMv8") != NULL)
	{
		board->mem_flag  = 0x04;
	}
	*/

	board->mem_flag  = 0x04;  // (DIRECT)
	board->periph_virt_base = bcm_host_get_peripheral_address();
	board->dram_phys_base = bcm_host_get_sdram_address();
	board->periph_phys_base = 0x7e000000;

	return 0;
}

uint64_t *map_peripheral(uint32_t base, size_t len)
{
	int fd = open("/dev/mem", O_RDWR|O_SYNC);
	void * vaddr;

	if (fd < 0) {
		perror("Failed to open /dev/mem");
		return MAP_FAILED;
	}

	vaddr = mmap(NULL, len, PROT_READ|PROT_WRITE, MAP_SHARED, fd, base);
	if (vaddr == MAP_FAILED) {
		perror("Failed to map peripheral");
		/* Fallthrough */
	}

	close(fd);

	return vaddr;
}

struct phys *phys_alloc(struct board_cfg *board, size_t len)
{
	struct phys *p = calloc(1, sizeof(*p));
	if (!p) {
		return NULL;
	}

	/* Use the mailbox interface to the VC to ask for physical memory */
	// Use the mailbox interface to request memory from the VideoCore
	// We specifiy (-1) for the handle rather than calling mbox_open()
	// so multiple users can share the resource.
	p->handle = -1; // mbox_open();
	p->bus_addr = ~((unsigned)0);
	p->mem_ref = -1;
	p->virt_addr = NULL;

	p->size = ALIGN_UP(len, 4096);
	p->mem_ref = mem_alloc(p->handle, p->size, 4096, board->mem_flag);
	if (p->mem_ref < 0) {
		perror("Failed to alloc memory from VideoCore");
		goto fail;
	}

	p->bus_addr = mem_lock(p->handle, p->mem_ref);
	if (p->bus_addr == ~0) {
		perror("Failed to lock memory");
		goto fail;
	}
	p->phys_addr = BUS_TO_PHYS(p->bus_addr);

	p->virt_addr = mapmem(BUS_TO_PHYS(p->bus_addr), p->size);
	if (p->virt_addr == MAP_FAILED) {
		perror("mapmem failed");
		goto fail;
	}
	//memset(p->virt_addr, 0, p->size);

	return p;

fail:

	return NULL;
}

void phys_free(struct phys *p)
{
	if (p->virt_addr) {
		unmapmem(p->virt_addr, p->size);
	}
	if (p->bus_addr != ~((unsigned)0)) {
		mem_unlock(p->handle, p->mem_ref);
	}
	if (p->mem_ref >= 0) {
		mem_free(p->handle, p->mem_ref);
	}
	free(p);
}

uint32_t phys_virt_to_bus(struct phys *p, void *virt)
{
	uint32_t offset = (uint8_t *)virt - p->virt_addr;

	return p->bus_addr + offset;
}

uint32_t phys_virt_to_phys(struct phys *p, void *virt)
{
	uint32_t offset = (uint8_t *)virt - p->virt_addr;

	return p->phys_addr + offset;
}
