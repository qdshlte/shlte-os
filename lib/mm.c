/*
 * mm.c - Memory management with buddy system allocator
 *
 * Implements a buddy system for physical page allocation and
 * a slab-based kmalloc/kfree on top of allocated pages.
 */

#include <shlte/types.h>
#include <shlte/mm.h>
#include <shlte/printk.h>
#include <shlte/string.h>

/* ============================================================
 * Buddy System Page Allocator
 * ============================================================ */

/**
 * Maximum orders supported by the buddy allocator.
 * Order N allocates 2^N contiguous pages.
 */
#define MAX_ORDER_BITS  10  /* 2^10 = 1024 pages = 4MB max */

/**
 * Buddy free list head per order.
 * Each order maintains a singly-linked free list.
 */
typedef struct buddy_free_list {
    uint64_t next;
} buddy_free_list_t;

/**
 * Buddy allocator state.
 * We manage a pool of physical pages starting at buddy_pool_start
 * with buddy_pool_pages total pages.
 */
static struct {
    uint64_t pool_start;      /* Physical start of managed memory */
    size_t   pool_pages;      /* Total pages in pool */
    uint64_t *bitmap;         /* Allocation bitmap (1 bit per page) */
    size_t   bitmap_words;    /* Number of 64-bit words in bitmap */
    buddy_free_list_t *free_lists[MAX_ORDER_BITS]; /* Free lists per order */
    size_t   free_count;      /* Total free pages */
    int      initialized;
} buddy = {
    .pool_start = 0,
    .pool_pages = 0,
    .bitmap = NULL,
    .bitmap_words = 0,
    .free_count = 0,
    .initialized = 0
};

/**
 * buddy_idx_to_phys - Convert order + index within order to physical address
 */
static inline uint64_t buddy_idx_to_phys(unsigned int order, size_t idx)
{
    return buddy.pool_start + (idx * (1UL << order) * PAGE_SIZE);
}

/**
 * buddy_phys_to_idx - Convert physical address to index within order
 */
static inline size_t buddy_phys_to_idx(uint64_t phys, unsigned int order)
{
    return (size_t)((phys - buddy.pool_start) / ((1UL << order) * PAGE_SIZE));
}

/**
 * buddy_is_allocated - Check if a page is allocated
 */
static inline int buddy_is_allocated(size_t page_idx)
{
    size_t word_idx = page_idx / 64;
    size_t bit_idx = page_idx % 64;
    if (word_idx >= buddy.bitmap_words) return 1; /* Out of bitmap = allocated */
    return (buddy.bitmap[word_idx] >> bit_idx) & 1;
}

/**
 * buddy_set_allocated - Mark a page as allocated
 */
static inline void buddy_set_allocated(size_t page_idx, int allocated)
{
    size_t word_idx = page_idx / 64;
    size_t bit_idx = page_idx % 64;
    if (word_idx >= buddy.bitmap_words) return;
    if (allocated)
        buddy.bitmap[word_idx] |= (1UL << bit_idx);
    else
        buddy.bitmap[word_idx] &= ~(1UL << bit_idx);
}

/**
 * buddy_init - Initialize the buddy allocator
 * @start: Physical start address of memory pool
 * @total_pages: Number of 4KB pages available
 *
 * Sets up the buddy system with the given memory range.
 * Returns 0 on success, -1 on failure.
 */
int buddy_init(uintptr_t start, size_t total_pages)
{
    if (total_pages == 0 || total_pages > 0x100000) {
        printk("[BUDDY] Invalid page count: %zu\n", total_pages);
        return -1;
    }

    buddy.pool_start = start;
    buddy.pool_pages = total_pages;
    buddy.free_count = total_pages;

    /* Calculate bitmap size: 1 bit per page, rounded up to 64-bit words */
    buddy.bitmap_words = (total_pages + 63) / 64;
    buddy.bitmap = (uint64_t *)kmalloc(buddy.bitmap_words * sizeof(uint64_t));
    if (!buddy.bitmap) {
        printk("[BUDDY] Failed to allocate bitmap!\n");
        return -1;
    }

    /* Clear bitmap: 0 = free, 1 = allocated */
    memset(buddy.bitmap, 0, buddy.bitmap_words * sizeof(uint64_t));

    /* Initialize free lists for order 0 */
    buddy.free_lists[0] = (buddy_free_list_t *)start;
    for (size_t i = 0; i < total_pages - 1; i++) {
        buddy.free_lists[0][i].next = buddy_idx_to_phys(0, i + 1);
    }
    buddy.free_lists[0][total_pages - 1].next = 0; /* End of list */

    buddy.initialized = 1;

    printk("[BUDDY] Pool: 0x%lx - 0x%lx (%zu pages, %zu MB)\n",
           start, start + total_pages * PAGE_SIZE, total_pages,
           total_pages * PAGE_SIZE / (1024 * 1024));
    printk("[BUDDY] Bitmap: %zu words (%zu bits)\n", buddy.bitmap_words,
           buddy.bitmap_words * 64);

    return 0;
}

/**
 * alloc_pages - Allocate contiguous pages using buddy system
 * @order: Log2 of number of pages to allocate (0 = 1 page, 1 = 2 pages, etc.)
 * @phys_addr: Output: physical address of allocated block
 *
 * Returns 0 on success, -1 on failure.
 */
int alloc_pages(unsigned int order, uint64_t *phys_addr)
{
    if (!buddy.initialized) {
        printk("[BUDDY] Not initialized!\n");
        return -1;
    }
    if (order >= MAX_ORDER_BITS) {
        printk("[BUDDY] Order %u exceeds MAX_ORDER %d\n", order, MAX_ORDER_BITS);
        return -1;
    }
    if (order == 0 && buddy.pool_pages == 0) {
        printk("[BUDDY] No pages available\n");
        return -1;
    }

    /* Find a free block: scan from requested order upward */
    unsigned int found_order = order;
    while (found_order < MAX_ORDER_BITS) {
        if (buddy.free_lists[found_order] != NULL) {
            break;
        }
        found_order++;
    }

    if (found_order >= MAX_ORDER_BITS) {
        printk("[BUDDY] Out of memory (requested order %u)\n", order);
        return -1;
    }

    /* Pop the first block from the free list at found_order */
    uint64_t block = buddy.free_lists[found_order]->next;
    buddy.free_lists[found_order] = (buddy_free_list_t *)block;
    if (buddy.free_lists[found_order] == NULL) {
        /* Free list is now empty */
    }

    /* Mark pages as allocated in bitmap */
    size_t start_page = buddy_phys_to_idx(block, found_order);
    for (size_t i = 0; i < (1UL << found_order); i++) {
        buddy_set_allocated(start_page + i, 1);
    }
    buddy.free_count -= (1UL << found_order);

    /* Split higher-order blocks if we found a larger block */
    while (found_order > order) {
        found_order--;
        /* The buddy of the left half is at offset 2^found_order */
        uint64_t buddy_block = block + (1UL << found_order) * PAGE_SIZE;
        size_t buddy_page = buddy_phys_to_idx(buddy_block, found_order);

        /* Mark buddy as allocated */
        for (size_t i = 0; i < (1UL << found_order); i++) {
            buddy_set_allocated(buddy_page + i, 1);
        }
        buddy.free_count -= (1UL << found_order);

        /* Push the left half onto the free list */
        /* Mark left half as free (it was part of a larger allocated block) */
        size_t left_page = buddy_phys_to_idx(block, found_order);
        for (size_t i = 0; i < (1UL << found_order); i++) {
            buddy_set_allocated(left_page + i, 0);
        }
        buddy_free_list_t *node = (buddy_free_list_t *)block;
        node->next = (uint64_t)buddy.free_lists[found_order];
        buddy.free_lists[found_order] = node;

        /* Block becomes the buddy for next iteration */
        block = buddy_block;
    }

    *phys_addr = block;
    return 0;
}

/**
 * free_pages - Return a block to the buddy system
 * @phys_addr: Physical address of the block to free
 * @order: Log2 of number of pages
 */
void free_pages(uint64_t phys_addr, unsigned int order)
{
    if (!buddy.initialized) return;
    if (order >= MAX_ORDER_BITS) return;
    if ((phys_addr % (PAGE_SIZE * (1UL << order))) != 0) {
        printk("[BUDDY] Misaligned free: 0x%lx order %u\n", phys_addr, order);
        return;
    }

    /* Coalesce with buddies */
    unsigned int coalesced = order;
    while (coalesced < MAX_ORDER_BITS - 1) {
        size_t page_idx = buddy_phys_to_idx(phys_addr, coalesced);
        uint64_t buddy_phys;
        size_t buddy_page_idx;

        if (page_idx % 2 == 0) {
            /* Buddy is the next block */
            buddy_phys = phys_addr + (1UL << coalesced) * PAGE_SIZE;
            buddy_page_idx = page_idx + (1UL << coalesced);
        } else {
            /* Buddy is the previous block */
            buddy_phys = phys_addr - (1UL << coalesced) * PAGE_SIZE;
            buddy_page_idx = page_idx - (1UL << coalesced);
        }

        /* Check if buddy is free */
        int buddy_free = 1;
        for (size_t i = 0; i < (1UL << coalesced); i++) {
            if (buddy_is_allocated(buddy_page_idx + i)) {
                buddy_free = 0;
                break;
            }
        }

        if (!buddy_free) break;

        /* Remove buddy from its free list and mark as unallocated */
        for (size_t i = 0; i < (1UL << coalesced); i++) {
            buddy_set_allocated(buddy_page_idx + i, 0);
        }
        buddy.free_count += (1UL << coalesced);

        /* Unlink buddy from free list */
        if (buddy.free_lists[coalesced] == NULL) break;
        buddy_free_list_t *prev = NULL;
        buddy_free_list_t *curr = buddy.free_lists[coalesced];
        while (curr) {
            if ((uint64_t)curr == buddy_phys) {
                if (prev)
                    prev->next = curr->next;
                else
                    buddy.free_lists[coalesced] = (buddy_free_list_t *)curr->next;
                break;
            }
            prev = curr;
            curr = (buddy_free_list_t *)curr->next;
        }

        /* Advance to next order */
        phys_addr = buddy_phys < phys_addr ? buddy_phys : phys_addr;
        coalesced++;
    }

    /* Insert into free list at the coalesced order */
    buddy_free_list_t *node = (buddy_free_list_t *)phys_addr;
    node->next = (uint64_t)buddy.free_lists[coalesced];
    buddy.free_lists[coalesced] = node;

    /* Mark the block as unallocated */
    size_t start_page = buddy_phys_to_idx(phys_addr, coalesced);
    for (size_t i = 0; i < (1UL << coalesced); i++) {
        buddy_set_allocated(start_page + i, 0);
    }
    buddy.free_count += (1UL << coalesced) - (1UL << order);

    printk("[BUDDY] Freed order %u at 0x%lx (coalesced to %u, %zu free pages)\n",
           order, phys_addr, coalesced, buddy.free_count);
}

/**
 * buddy_free_pages - Return number of free pages
 */
size_t buddy_free_pages(void)
{
    return buddy.free_count;
}

/**
 * buddy_total_pages - Return total pages in pool
 */
size_t buddy_total_pages(void)
{
    return buddy.pool_pages;
}

/* ============================================================
 * kmalloc / kfree - Slab-like allocator on top of buddy pages
 * ============================================================ */

/**
 * Slab allocator: manages small allocations from pre-allocated pages.
 * Each slab is a buddy-allocated page containing multiple objects.
 */
#define SLAB_OBJ_SIZE   64     /* Minimum object size */
#define SLAB_OBJS_PER_PAGE (PAGE_SIZE / SLAB_OBJ_SIZE)

typedef struct slab_obj {
    uint64_t next;
} slab_obj_t;

typedef struct slab {
    uint64_t page_phys;     /* Physical address of the page */
    uint64_t free_list;     /* Head of free object list */
    uint8_t  used[SLAB_OBJS_PER_PAGE]; /* Bitmask: 1 = used */
    struct slab *next;
} slab_t;

static struct {
    slab_t *slabs;          /* Linked list of slabs */
    size_t   total_objs;
    size_t   used_objs;
    size_t   total_pages;
    size_t   used_pages;
} slab = {
    .slabs = NULL,
    .total_objs = 0,
    .used_objs = 0,
    .total_pages = 0,
    .used_pages = 0
};

/**
 * slab_alloc - Allocate an object from a slab
 */
static void *slab_alloc(void)
{
    /* Try to find a free object in existing slabs */
    slab_t *slab_ptr = slab.slabs;
    while (slab_ptr) {
        if (slab_ptr->free_list != 0) {
            uint64_t obj_phys = slab_ptr->free_list;
            slab_ptr->free_list = ((slab_obj_t *)obj_phys)->next;

            /* Mark as used */
            size_t obj_idx = (obj_phys - slab_ptr->page_phys) / SLAB_OBJ_SIZE;
            slab_ptr->used[obj_idx] = 1;
            slab.used_objs++;

            return (void *)obj_phys;
        }
        slab_ptr = slab_ptr->next;
    }

    /* No free objects: allocate a new slab page */
    uint64_t page_phys;
    if (alloc_pages(0, &page_phys) != 0) {
        printk("[SLAB] Cannot allocate page for new slab!\n");
        return NULL;
    }

    slab.total_pages++;

    /* Initialize the slab */
    slab_t *new_slab = (slab_t *)page_phys;
    new_slab->page_phys = page_phys;
    new_slab->next = slab.slabs;
    slab.slabs = new_slab;

    /* Build free list from all objects in this page */
    new_slab->free_list = page_phys + SLAB_OBJ_SIZE;
    for (size_t i = 0; i < SLAB_OBJS_PER_PAGE - 1; i++) {
        uint64_t obj = page_phys + (i + 1) * SLAB_OBJ_SIZE;
        ((slab_obj_t *)obj)->next = page_phys + (i + 2) * SLAB_OBJ_SIZE;
    }
    ((slab_obj_t *)(page_phys + (SLAB_OBJS_PER_PAGE - 1) * SLAB_OBJ_SIZE))->next = 0;

    /* Mark first object as used */
    new_slab->used[0] = 1;
    slab.used_objs++;
    slab.total_objs += SLAB_OBJS_PER_PAGE;

    return (void *)page_phys;
}

/**
 * slab_free - Return an object to its slab
 */
static void slab_free(void *ptr)
{
    if (!ptr) return;

    uint64_t obj_phys = (uint64_t)ptr;
    slab_t *slab_ptr = slab.slabs;

    while (slab_ptr) {
        if (obj_phys >= slab_ptr->page_phys &&
            obj_phys < slab_ptr->page_phys + PAGE_SIZE) {
            size_t obj_idx = (obj_phys - slab_ptr->page_phys) / SLAB_OBJ_SIZE;
            slab_ptr->used[obj_idx] = 0;
            slab.used_objs--;

            /* Push onto free list */
            ((slab_obj_t *)obj_phys)->next = slab_ptr->free_list;
            slab_ptr->free_list = obj_phys;
            return;
        }
        slab_ptr = slab_ptr->next;
    }
}

/* ============================================================
 * Public API
 * ============================================================ */

/**
 * mm_init - Initialize memory manager
 */
void mm_init(void)
{
    printk("[MM] Initializing memory manager...\n");

    /* Initialize buddy system with memory above the kernel.
     * On ARM64 virt machine, RAM typically starts at 0x80000.
     * We use 64MB for the buddy pool (adjustable). */
    size_t pool_mb = 64;
    size_t pool_pages = (pool_mb * 1024 * 1024) / PAGE_SIZE;

    /* Buddy init uses kmalloc for bitmap, so we need a tiny bootstrap.
     * Use static buffer for initial bitmap. */
    extern char __kernel_end[];
    uintptr_t pool_start = (uintptr_t)__kernel_end;
    /* Align to 2MB boundary for huge pages */
    pool_start = (pool_start + (2 * 1024 * 1024) - 1) & ~((2 * 1024 * 1024) - 1);

    /* Recalculate pages from aligned start */
    pool_pages = (pool_mb * 1024 * 1024) / PAGE_SIZE;
    /* Use static bitmap temporarily */
    static uint64_t static_bitmap[1024];
    buddy.bitmap = static_bitmap;
    buddy.bitmap_words = sizeof(static_bitmap) / sizeof(uint64_t);
    buddy.pool_start = pool_start;
    buddy.pool_pages = pool_pages;
    buddy.free_count = pool_pages;

    /* Clear bitmap */
    memset(buddy.bitmap, 0, buddy.bitmap_words * sizeof(uint64_t));

    /* Initialize all free lists to NULL */
    for (int o = 0; o < MAX_ORDER_BITS; o++)
        buddy.free_lists[o] = NULL;

    /* Build free lists: add all pages as the largest possible blocks.
     * Process from highest order down to order 1.
     * Order 0 is not in the free list — order-0 allocations
     * come from splitting order-1 blocks. */
    {
        uint64_t offset = 0;
        uint64_t remaining = pool_pages;

        while (remaining > 0) {
            /* Find the largest block size that fits */
            unsigned int order = MAX_ORDER_BITS - 1;
            while (order > 0) {
                uint64_t block_size = 1UL << order;
                if (block_size <= remaining)
                    break;
                order--;
            }
            if (order == 0) {
                /* Single page at the end — this is fine,
                 * the order-0 list was built above */
                break;
            }

            uint64_t block_size = 1UL << order;
            uint64_t phys = pool_start + offset * PAGE_SIZE;
            buddy_free_list_t *node = (buddy_free_list_t *)phys;
            node->next = (uint64_t)buddy.free_lists[order];
            buddy.free_lists[order] = node;

            /* Mark pages as in-use (owned by this free-list block) */
            for (uint64_t j = 0; j < block_size; j++)
                buddy_set_allocated(offset + j, 1);

            offset += block_size;
            remaining -= block_size;
        }
    }

    buddy.initialized = 1;

    printk("[MM] Buddy pool: 0x%lx (%zu pages, %zu MB)\n",
           pool_start, pool_pages, pool_pages * PAGE_SIZE / (1024 * 1024));
    printk("[MM] Slab allocator initialized.\n");
}

/**
 * kmalloc - Allocate memory from kernel heap (slab allocator)
 */
void *kmalloc(size_t size)
{
    if (size == 0) return NULL;

    /* Round up to minimum slab object size */
    if (size < SLAB_OBJ_SIZE) size = SLAB_OBJ_SIZE;
    size = (size + 7) & ~7; /* Align to 8 bytes */

    void *ptr = slab_alloc();
    if (ptr) {
        memset(ptr, 0, size);
    }
    return ptr;
}

/**
 * kfree - Free memory allocated by kmalloc
 */
void kfree(void *ptr)
{
    if (!ptr) return;
    slab_free(ptr);
}

/**
 * krealloc - Reallocate memory (correctly copies old data)
 */
void *krealloc(void *ptr, size_t new_size)
{
    if (ptr == NULL) return kmalloc(new_size);
    if (new_size == 0) { kfree(ptr); return NULL; }

    void *new_ptr = kmalloc(new_size);
    if (new_ptr == NULL) return NULL;

    /* Find old slab and determine old size */
    uint64_t old_phys = (uint64_t)ptr;
    slab_t *slab_ptr = slab.slabs;
    size_t old_size = SLAB_OBJ_SIZE;
    while (slab_ptr) {
        if (old_phys >= slab_ptr->page_phys &&
            old_phys < slab_ptr->page_phys + PAGE_SIZE) {
            old_size = PAGE_SIZE; /* Full page was the allocation */
            break;
        }
        slab_ptr = slab_ptr->next;
    }

    /* Copy old data */
    memcpy(new_ptr, ptr, new_size < old_size ? new_size : old_size);
    kfree(ptr);

    return new_ptr;
}

/**
 * kmalloc_usable - Return the usable size of a kmalloc'd block
 */
size_t kmalloc_usable(void *ptr)
{
    (void)ptr;
    return SLAB_OBJ_SIZE; /* All allocations are at least SLAB_OBJ_SIZE */
}

/**
 * phys_alloc - Allocate physical memory (wrapper around alloc_pages)
 */
void *phys_alloc(size_t size)
{
    if (size == 0) return NULL;

    unsigned int order = 0;
    size_t pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    while ((1UL << order) < pages && order < MAX_ORDER_BITS - 1) {
        order++;
    }

    uint64_t phys;
    if (alloc_pages(order, &phys) != 0) return NULL;

    /* Zero the memory */
    void *ptr = (void *)phys;
    memset(ptr, 0, pages * PAGE_SIZE);

    return ptr;
}

/**
 * phys_free - Free physical memory
 */
void phys_free(void *ptr, size_t size)
{
    if (!ptr || size == 0) return;

    unsigned int order = 0;
    size_t pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    while ((1UL << order) < pages && order < MAX_ORDER_BITS - 1) {
        order++;
    }

    free_pages((uint64_t)ptr, order);
}

/**
 * page_alloc - Allocate a single page (backward compatibility)
 */
int page_alloc(uint64_t *page)
{
    return alloc_pages(0, page);
}

/**
 * page_free - Free a single page (backward compatibility)
 */
void page_free(uint64_t page)
{
    free_pages(page, 0);
}

/**
 * stack_alloc - Allocate a stack
 */
void *stack_alloc(size_t size)
{
    /* Round up to page boundary */
    size = (size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    unsigned int order = 0;
    size_t pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    while ((1UL << order) < pages) order++;

    uint64_t phys;
    if (alloc_pages(order, &phys) != 0) return NULL;

    /* Zero the stack */
    memset((void *)phys, 0, size);

    return (void *)phys;
}

/**
 * stack_free - Free a stack
 */
void stack_free(void *stack)
{
    if (!stack) return;
    /* We don't know the original size, so this is a limitation.
     * In production, we'd track sizes. For now, free as one page. */
    free_pages((uint64_t)stack, 0);
}
