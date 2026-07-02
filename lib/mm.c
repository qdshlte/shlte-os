/*
 * mm.c - Memory management (simple bump allocator)
 */

#include <shlte/types.h>
#include <shlte/mm.h>
#include <shlte/printk.h>
#include <shlte/string.h>

/* Simple kernel heap - bump allocator */
static char *heap_start = NULL;
static char *heap_ptr = NULL;
static char *heap_end = NULL;

#define HEAP_SIZE (1024 * 1024)  /* 1MB heap */

/* Static heap buffer */
static char kernel_heap[HEAP_SIZE] __attribute__((aligned(16)));

/**
 * mm_init - Initialize memory manager
 */
void mm_init(void)
{
    heap_start = kernel_heap;
    heap_ptr = kernel_heap;
    heap_end = kernel_heap + HEAP_SIZE;

    printk("[MM] Heap: %p - %p (%d KB)\n", heap_start, heap_end, HEAP_SIZE / 1024);
}

/**
 * kmalloc - Allocate memory from kernel heap
 */
void *kmalloc(size_t size)
{
    if (size == 0) return NULL;
    if (heap_ptr == NULL) {
        mm_init();
    }

    /* Align to 16 bytes */
    size = (size + 15) & ~15;

    if (heap_ptr + size > heap_end) {
        printk("[MM] Out of memory! Requested %zu bytes\n", size);
        return NULL;
    }

    void *ptr = heap_ptr;
    heap_ptr += size;

    /* Zero the memory */
    memset(ptr, 0, size);

    return ptr;
}

/**
 * kfree - Free memory (stub - bump allocator doesn't support free)
 */
void kfree(void *ptr)
{
    /* Bump allocator doesn't support individual frees */
    /* In a real implementation, we'd use a free list or bitmap */
    (void)ptr;
}

/**
 * krealloc - Reallocate memory (stub)
 */
void *krealloc(void *ptr, size_t new_size)
{
    if (ptr == NULL) return kmalloc(new_size);
    if (new_size == 0) { kfree(ptr); return NULL; }

    void *new_ptr = kmalloc(new_size);
    if (new_ptr == NULL) return NULL;

    /* TODO: Copy old data */
    (void)ptr;

    return new_ptr;
}

/**
 * phys_alloc - Allocate physical memory (stub)
 */
void *phys_alloc(size_t size)
{
    return kmalloc(size);
}

/**
 * phys_free - Free physical memory (stub)
 */
void phys_free(void *ptr, size_t size)
{
    kfree(ptr);
}

/**
 * page_alloc - Allocate a page (stub)
 */
int page_alloc(uint64_t *page)
{
    /* TODO: Implement physical page allocation */
    return -1;
}

/**
 * page_free - Free a page (stub)
 */
void page_free(uint64_t page)
{
    /* TODO: Implement physical page deallocation */
}

/**
 * stack_alloc - Allocate a stack
 */
void *stack_alloc(size_t size)
{
    /* Allocate from heap, aligned to 4KB */
    size = (size + 0xFFF) & ~0xFFF;
    return kmalloc(size);
}

/**
 * stack_free - Free a stack
 */
void stack_free(void *stack)
{
    kfree(stack);
}
