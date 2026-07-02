/*
 * shlte/mm.h - Memory management declarations
 */

#ifndef SHLTE_MM_H
#define SHLTE_MM_H

#include <shlte/types.h>

/* Kernel heap allocation */
void *kmalloc(size_t size);
void kfree(void *ptr);
void *krealloc(void *ptr, size_t new_size);

/* Physical memory management */
void *phys_alloc(size_t size);
void phys_free(void *ptr, size_t size);

/* Page management */
int page_alloc(uint64_t *page);
void page_free(uint64_t page);

/* Stack management */
void *stack_alloc(size_t size);
void stack_free(void *stack);

/* External symbols from linker script */
extern char __kernel_end[];
extern char __bss_start[];
extern char __bss_end[];

/* Initialize memory manager */
void mm_init(void);

#endif /* SHLTE_MM_H */
