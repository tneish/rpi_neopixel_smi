#ifndef MEM_UTILS_H
#define MEM_UTILS_H

#include <stdio.h>
#include <stdint.h>
#include "mailbox.h"
#include "pi_board.h"

typedef struct {
	unsigned handle;
	size_t size;
	uint32_t bus_addr;
	void *virt_addr;
	
} dma_mem_t;

// DMA control block (must be 32-byte aligned)
typedef struct {
    uint32_t ti,    // Transfer info
        srce_ad,    // Source address
        dest_ad,    // Destination address
        tfr_len,    // Transfer length
        stride,     // Transfer stride
        next_cb,    // Next control block
        debug,      // Debug register, zero in control block
        unused;
} DMA_CB_t __attribute__ ((aligned(32)));
#define DMA_CB_DEST_INC (1<<4)
#define DMA_CB_SRC_INC  (1<<8)


static const unsigned DMA_CHAN 	= 5;



/** DMA MEMORY **/
// TODO: error checking
void dma_mem_alloc (size_t size, dma_mem_t *d);
void dma_mem_release(dma_mem_t *d);
void *dm_safe_memset(void *s, int c, size_t n);

uint32_t dma_mem_virt_offset_to_bus(dma_mem_t *m, void *virt);
uint32_t dma_mem_virt_to_bus(dma_mem_t *m);

// Enable DMA channel
void enable_dma();

// Start DMA, given dma_mem_t pointer for first control block (see DMA_CB_t)
void start_dma(dma_mem_t *m);

void disp_dma();
void print_free_dma_channels();

/*** GPIO **/
void set_gpio_mode_in(int pin);
void set_gpio_mode_out(int pin);
void set_gpio_out(int pin, int val);

// 32-bit bus address for gpio bank 0 set register
uint32_t gpio_bus_set0();
// 32-bit bus address for gpio bank 0 clear register
uint32_t gpio_bus_clear0();



#endif
