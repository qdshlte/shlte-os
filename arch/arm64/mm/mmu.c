/*
 * mmu.c - ARM64 MMU / Page Table Management
 */

#include <shlte/types.h>
#include <shlte/mm.h>
#include <shlte/printk.h>
#include <shlte/mmu.h>

/* ARM64 page table entry flags */
#define PT_BLOCK     0x0
#define PT_INVALID   0x0
#define PT_TABLE     0x3
#define PT_VALID     0x1

/* Memory attributes */
#define MT_DEVICE    0x00  /* Device type 0 (nGnRE) */
#define MT_NORMAL_NC 0x04  /* Normal, Non-Cacheable */
#define MT_NORMAL_WT 0x08  /* Normal, Write-Through */
#define MT_NORMAL_WB 0x0C  /* Normal, Write-Back */

#define ATTR_DEVICE     ((MT_DEVICE << 2) | (1 << 0))
#define ATTR_NORMAL_NC  ((MT_NORMAL_NC << 2) | (1 << 0))
#define ATTR_NORMAL_WB  ((MT_NORMAL_WB << 2) | (1 << 0))

#define PF_VALID   (1 << 0)
#define PF_AF      (1 << 1)
#define PF_NG      (1 << 11)

/* Page sizes */
#define PAGE_SIZE      4096
#define BLOCK_SIZE     (512 * 1024)   /* 512KB block */
#define LARGE_BLOCK    (1 << 30)      /* 1GB large block */

/* TTBR (Translation Table Base Register) */
#define TTBR_CNP      (1 << 11)

/* CR (Control Register) bits */
#define CR_MMU_ENABLE    (1 << 0)
#define CR_DCACHE_ENABLE (1 << 1)
#define CR_ICACHE_ENABLE (1 << 12)

/* Level 1 block mapping (1GB) - kept for future use */
static uint64_t pt_block_1gb(void)
{
    uint64_t attr = ATTR_NORMAL_WB;
    return (attr << 2) | PF_VALID | PF_AF | PF_NG | (1ULL << 54) | (3ULL << 8);
}

/* Level 3 table entry (4KB page) - kept for future use */
static uint64_t pt_page_4k(uint64_t phys, int attrs)
{
    uint64_t entry = phys | PF_VALID | PF_AF;
    entry |= ((uint64_t)(attrs & 0xF)) << 2;
    entry |= (1ULL << 54);  /* Non-shareable */
    return entry;
}

/*
 * Stage-1 translation table (4 levels, 4KB granule)
 * Level 0: 512 x 1GB slices  -> offset 0-8
 * Level 1: 512 x 512MB slices -> offset 9-16
 * Level 2: 512 x 1MB slices   -> offset 17-25
 * Level 3: 512 x 4KB pages    -> offset 26-32
 */

/*
 * Simple 2-level page table:
 * - Level 0: 512 entries (each points to a level-1 table)
 * - Level 1 block: 512KB blocks (for simplicity)
 *
 * For a minimal kernel, we use a 1GB block mapping at level 1.
 */

/*
 * Map 2MB pages (level 1 block)
 * Format: [valid][attrs][NS][AF][NG][SH][TYPE=block]
 */
static uint64_t pt_block_2mb(uint64_t phys_addr, int mem_attr)
{
    uint64_t entry = phys_addr & ~0xFFF;  /* Align to 2MB */
    entry |= ((uint64_t)(mem_attr & 0xF)) << 2;
    entry |= (1ULL << 0) | (1ULL << 1) | (1ULL << 10) | (3ULL << 8); /* V|AF|IB|SH=inner*/
    return entry;
}

/*
 * mmu_enable - Wrapper called from boot.S
 * Initializes MMU and jumps to kernel_main after setup.
 */
void mmu_enable(void)
{
    mmu_init(0);  /* DTB pointer not available yet, pass 0 */
    
    /* Jump to kernel entry */
    extern void kernel_main(uint64_t dtb);
    kernel_main(0);
    
    /* Should never return */
    for (;;);
}

/* Translation tables (physically contiguous) */
static uint64_t __attribute__((aligned(0x400000))) pt_table_l0[512];
static uint64_t __attribute__((aligned(0x4000))) pt_table_l1[512];

void mmu_init(uint64_t dtb_phys)
{
    (void)dtb_phys; /* DTB pointer not yet needed */
    int i;

    /* Clear page tables */
    for (i = 0; i < 512; i++) {
        pt_table_l0[i] = 0;
        pt_table_l1[i] = 0;
    }

    /*
     * Build L1 block mappings (2MB pages).
     * Map first 1GB of physical memory.
     *
     * L0[0] -> L1 table
     */
    pt_table_l0[0] = (uint64_t)&pt_table_l1 | PT_TABLE;

    /* Map 0x0 - 0x40000000 (1GB) with 2MB blocks */
    for (i = 0; i < 512; i++) {
        uint64_t phys = (uint64_t)i * (2 * 1024 * 1024);
        pt_table_l1[i] = pt_block_2mb(phys, ATTR_NORMAL_WB);
    }

    /* Map device memory at 0x30000000 (GIC, UART, etc.) */
    /* This is at L1 index 0x18000000 / 0x200000 = 384 */
    /* For simplicity, map a region as device memory */

    /* Load TTBR0 (EL0/EL1 translation table base) */
    __asm__ volatile(
        "msr ttbr0_el1, %0\n\t"
        "isb\n\t"
        : : "r" ((uint64_t)&pt_table_l0)
        : "memory"
    );

    /* Enable MMU and caches */
    uint64_t sctlr;
    __asm__ volatile(
        "mrs %0, sctlr_el1\n\t"
        "orr %0, %0, #(1 << 0)\n\t"   /* MMU enable */
        "orr %0, %0, #(1 << 1)\n\t"   /* Data cache enable */
        "orr %0, %0, #(1 << 12)\n\t"  /* Instruction cache enable */
        "msr sctlr_el1, %0\n\t"
        "isb\n\t"
        : "=r" (sctlr)
        :
        : "memory"
    );

    printk("[MMU] Enabled, TTBR0=%p\n", (void*)&pt_table_l0);
}

void *kmalloc_simple(unsigned long size)
{
    static unsigned long alloc_ptr = 0x10000000;  /* Simple bump allocator */
    void *ptr;

    size = (size + 0xFFF) & ~0xFFF;  /* Align to 4KB */

    ptr = (void *)alloc_ptr;
    alloc_ptr += size;

    return ptr;
}
