/*
 * shlte/mm.h - Memory management declarations
 */

#ifndef SHLTE_MM_H
#define SHLTE_MM_H

#include <shlte/types.h>

/* Buddy allocator constants */
#define PAGE_SHIFT       12
#define PAGE_SIZE        (1UL << PAGE_SHIFT)
#define MAX_ORDER        10          /* 2^10 * 4KB = 4MB max allocation */
#define ORDER0_SIZE      PAGE_SIZE

/* Kernel heap allocation */
void *kmalloc(size_t size);
void kfree(void *ptr);
void *krealloc(void *ptr, size_t new_size);
size_t kmalloc_usable(void *ptr);

/* Simple bump allocator (early boot + user-space) */
void *kmalloc_simple(unsigned long size);

/* Buddy system page allocator */
int buddy_init(uintptr_t start, size_t total_pages);
int alloc_pages(unsigned int order, uint64_t *phys_addr);
void free_pages(uint64_t phys_addr, unsigned int order);
size_t buddy_free_pages(void);
size_t buddy_total_pages(void);

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

/* W^X + ASLR for user space */
void mmu_setup_user_space(uint64_t code_start, uint64_t code_size,
                           uint64_t data_start, uint64_t data_size);
int  mmu_get_aslr_offset(void);

#endif /* SHLTE_MM_H */
