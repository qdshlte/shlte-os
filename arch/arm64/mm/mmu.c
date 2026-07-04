/*
 * mmu.c - ARM64 MMU / Page Table Management
 */

#include <shlte/types.h>
#include <shlte/mm.h>
#include <shlte/printk.h>
#include <shlte/mmu.h>
#include <shlte/kernel.h>

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
#define BLOCK_SIZE_2MB  (2 * 1024 * 1024)
#define LARGE_BLOCK_1GB (1ULL << 30)

/* TTBR (Translation Table Base Register) */
#define TTBR_CNP        (1 << 11)

/* CR (Control Register) bits */
#define CR_MMU_ENABLE    (1 << 0)
#define CR_DCACHE_ENABLE (1 << 1)
#define CR_ICACHE_ENABLE (1 << 12)

/*
 * mmu_enable - Called from boot.S to initialize and enable the MMU.
 * This function sets up page tables, loads TTBR, enables the MMU,
 * and then calls kernel_main(). It does NOT loop forever — it
 * returns after kernel_main() exits (which should never happen).
 */
void mmu_enable(void)
{
    mmu_init(0);

    /* Print banner AFTER MMU is enabled so output uses virtual addresses */
    printk("[BOOT] MMU enabled, entering kernel_main...\n");
}

/*
 * Stage-1 translation table (4 levels, 4KB granule)
 * Level 0: 512 x 1GB slices  -> offset 0-8
 * Level 1: 512 x 512MB slices -> offset 9-16
 * Level 2: 512 x 1MB slices   -> offset 17-25
 * Level 3: 512 x 4KB pages    -> offset 26-32
 */

/* Translation tables (physically contiguous, aligned) */
static uint64_t __attribute__((aligned(0x400000))) pt_table_l0[512];
static uint64_t __attribute__((aligned(0x4000))) pt_table_l1[512];

/*
 * Map 2MB pages (level 1 block)
 * Format: [valid][attrs][NS][AF][NG][SH][TYPE=block]
 */
static uint64_t pt_block_2mb(uint64_t phys_addr, int mem_attr)
{
    uint64_t entry = phys_addr & ~0x1FFFFF;  /* Align to 2MB boundary */
    entry |= ((uint64_t)(mem_attr & 0xF)) << 2;
    entry |= (1ULL << 0) | (1ULL << 1) | (1ULL << 10) | (3ULL << 8); /* V|AF|IB|SH=inner*/
    return entry;
}

/*
 * mmu_init - Set up page tables and enable the MMU.
 * Called ONCE from mmu_enable() during early boot.
 * After this function returns, the MMU is enabled and all
 * memory accesses use virtual addresses.
 *
 * NOTE: kernel_main() must NOT call mmu_init() again — the MMU
 * is already enabled by the time kernel_main() runs.
 */
void mmu_init(uint64_t dtb_phys)
{
    (void)dtb_phys;
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
        uint64_t phys = (uint64_t)i * BLOCK_SIZE_2MB;
        pt_table_l1[i] = pt_block_2mb(phys, ATTR_NORMAL_WB);
    }

    /* Load TTBR0 (EL0/EL1 translation table base) */
    __asm__ volatile(
        "msr ttbr0_el1, %0\n\t"
        "isb\n\t"
        : : "r" ((uint64_t)&pt_table_l0)
        : "memory"
    );

    /* Invalidate TLB before enabling MMU to clear stale entries */
    __asm__ volatile(
        "tlbi vmalle1is\n\t"
        "dsb nsh\n\t"
        "isb\n\t"
        ::: "memory"
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

    /* Full cache maintenance by set/way after enabling caches */
    uint64_t clidr;
    __asm__ volatile("mrs %0, CLIDR_EL1" : "=r"(clidr));
    unsigned int level = (clidr >> 24) & 0x7;  /* Max cache level in CLIDR */
    for (unsigned int l = 0; l <= level; l++) {
        unsigned int ctype = (clidr >> (l * 3)) & 0x7;
        if (ctype == 0) continue;  /* No cache at this level */
        uint64_t csselr = l << 1;
        __asm__ volatile("msr CSSELR_EL1, %0" : : "r"(csselr));
        __asm__ volatile("isb");
        uint64_t ccsidr;
        __asm__ volatile("mrs %0, CCSIDR_EL1" : "=r"(ccsidr));
        unsigned int num_sets = ((ccsidr >> 13) & 0x7FFF) + 1;
        unsigned int num_ways = ((ccsidr >> 3) & 0x3FF) + 1;
        (void)(ccsidr & 0x7);  /* line size encoding - not needed for set/way op */
        for (unsigned int w = 0; w < num_ways; w++) {
            for (unsigned int s = 0; s < num_sets; s++) {
                uint64_t cisw = (w << 30) | (l << 1) | s;
                __asm__ volatile("dc cisw, %0" : : "r"(cisw));
            }
        }
    }
    __asm__ volatile("dsb sy");

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
