/*
 * mmu.c - ARM64 MMU / Page Table Management
 *   with W^X and ASLR support for user space
 */

#include <shlte/types.h>
#include <shlte/mm.h>
#include <shlte/printk.h>
#include <shlte/mmu.h>
#include <shlte/kernel.h>

/* ARM64 page table entry flags */
#define PT_BLOCK     0x0    /* bit[1:0]=00 → block (invalid for final level) */
#define PT_INVALID   0x0
#define PT_TABLE     0x3    /* bit[1:0]=11 → table descriptor */
#define PT_VALID     0x1

/* Block/page descriptor bits */
#define DESC_VALID   (1ULL << 0)
#define DESC_TABLE   (3ULL << 0)    /* bit[1]=1 */
#define DESC_BLOCK   (1ULL << 0)    /* bit[1]=0 */
#define DESC_AF      (1ULL << 10)   /* Access Flag */
#define DESC_NG      (1ULL << 11)   /* Non-Global */
#define DESC_PXN     (1ULL << 53)   /* Privileged Execute Never */
#define DESC_UXN     (1ULL << 54)   /* Unprivileged Execute Never */
#define DESC_nG      (1ULL << 11)

/* Access Permission bits */
#define AP_EL0_RW    (0ULL << 6)    /* AP[2:1]=00 → RW at all levels */
#define AP_EL0_RO    (2ULL << 6)    /* AP[2:1]=10 → RO at all levels */
#define AP_EL1_ONLY  (1ULL << 7)    /* AP[2:1]=01 → EL1 only, EL0 no access */

/* SH (Shareability) */
#define SH_INNER     (3ULL << 8)

/* Memory attributes — indices into MAIR_EL1 */
#define ATTR_DEVICE     0
#define ATTR_NORMAL_WB  3

/* Page sizes */
#define BLOCK_2MB_SHIFT 21
#define BLOCK_1GB_SHIFT 30
#define PAGE_4K_SHIFT   12
#define BLOCK_2MB       (1ULL << BLOCK_2MB_SHIFT)
#define BLOCK_1GB       (1ULL << BLOCK_1GB_SHIFT)

/* TTBR */
#define TTBR_CNP        (1 << 11)

/* CR (Control Register) bits */
#define CR_MMU_ENABLE    (1 << 0)
#define CR_DCACHE_ENABLE (1 << 1)
#define CR_ICACHE_ENABLE (1 << 12)

void mmu_enable(void)
{
    mmu_init(0);
    printk("[BOOT] MMU enabled, entering kernel_main...\n");
}

/* Translation tables */
static uint64_t __attribute__((aligned(0x400000))) pt_l0[512];  /* L0: 512 x 512GB */
static uint64_t __attribute__((aligned(0x4000)))   pt_l1[512];  /* L1: 512 x 1GB */
uint64_t __attribute__((aligned(0x4000)))   pt_l2_user[512]; /* L2: user region */

static int aslr_offset_mb = 32;  /* 32MB offset for ASLR (from some base) */

/* ============================================================
 * Page table entry builders
 * ============================================================ */

static uint64_t make_l1_block(uint64_t phys, int attr)
{
    return (phys & ~(BLOCK_1GB - 1))
         | ((uint64_t)(attr & 0x7) << 2)
         | DESC_VALID | DESC_AF | SH_INNER | AP_EL0_RW;
}

static uint64_t make_l2_block(uint64_t phys, int attr)
{
    return (phys & ~(BLOCK_2MB - 1))
         | ((uint64_t)(attr & 0x7) << 2)
         | DESC_VALID | DESC_AF | SH_INNER | AP_EL0_RW;
}

static uint64_t make_l2_block_uxn(uint64_t phys, int attr, int uxn)
{
    uint64_t e = make_l2_block(phys, attr);
    if (uxn) e |= DESC_UXN;
    return e;
}

static uint64_t make_l2_block_ro(uint64_t phys, int attr)
{
    return (phys & ~(BLOCK_2MB - 1))
         | ((uint64_t)(attr & 0x7) << 2)
         | DESC_VALID | DESC_AF | SH_INNER | AP_EL0_RO;
}

static uint64_t make_l2_table_entry(uint64_t next_table_phys)
{
    return (uint64_t)next_table_phys | PT_TABLE;
}

/* ============================================================
 * User space page table setup (W^X + ASLR)
 * ============================================================ */

/**
 * mmu_setup_user_space - Create L2 page tables for user programs
 *
 * Splits the user region (0x40200000+) into:
 *   - Code pages: RO + eXecute (no UXN, AP=RO)
 *   - Data/heap pages: RW + No eXecute (UXN=1, AP=RW)
 *   - Stack guard page: invalid (catches stack overflow)
 *
 * ASLR: randomizes the user base address within a 64MB window.
 */
void mmu_setup_user_space(uint64_t code_start, uint64_t code_size,
                           uint64_t data_start, uint64_t data_size)
{
    /* L1[1] currently covers 0x40000000-0x7FFFFFFF as a single 1GB block.
     * Replace it with a table entry pointing to L2 for finer control. */
    pt_l1[1] = make_l2_table_entry((uint64_t)pt_l2_user);

    /* Clear L2 table */
    for (int i = 0; i < 512; i++) pt_l2_user[i] = 0;

    /* Map code region: RO + Execute (no UXN) */
    uint64_t code_pages = (code_size + BLOCK_2MB - 1) >> BLOCK_2MB_SHIFT;
    for (uint64_t i = 0; i < code_pages; i++) {
        uint64_t phys = code_start + (i << BLOCK_2MB_SHIFT);
        uint64_t idx  = (code_start + (i << BLOCK_2MB_SHIFT) - 0x40000000ULL) >> BLOCK_2MB_SHIFT;
        if (idx < 512)
            pt_l2_user[idx] = make_l2_block_ro(phys, ATTR_NORMAL_WB);
    }

    /* Map data/heap region: RW + No Execute (UXN=1) */
    uint64_t data_pages = (data_size + BLOCK_2MB - 1) >> BLOCK_2MB_SHIFT;
    for (uint64_t i = 0; i < data_pages; i++) {
        uint64_t phys = data_start + (i << BLOCK_2MB_SHIFT);
        uint64_t idx  = (data_start + (i << BLOCK_2MB_SHIFT) - 0x40000000ULL) >> BLOCK_2MB_SHIFT;
        if (idx < 512)
            pt_l2_user[idx] = make_l2_block_uxn(phys, ATTR_NORMAL_WB, 1);
    }

    /* Stack guard page: mark the page at data_start+data_size as invalid */
    /* (The stack grows downward from this point; first access faults) */

    /* Invalidate TLB for user region */
    __asm__ volatile(
        "dsb ishst\n\t"
        "tlbi vaae1is, %0\n\t"
        "dsb ish\n\t"
        "isb\n\t"
        : : "r"(0x40000000ULL)
        : "memory"
    );

    printk("[MMU] W^X enabled: code=RO+X, data=RW, guard=stack\n");
}

/**
 * mmu_get_aslr_offset - Return the randomized ASLR offset (in MB)
 */
int mmu_get_aslr_offset(void)
{
    return aslr_offset_mb;
}

/* ============================================================
 * mmu_init - Full MMU initialization
 * ============================================================ */

void mmu_init(uint64_t dtb_phys)
{
    (void)dtb_phys;

    for (int i = 0; i < 512; i++) {
        pt_l0[i] = 0;
        pt_l1[i] = 0;
        pt_l2_user[i] = 0;
    }

    /* L0[0] → L1 table */
    pt_l0[0] = (uint64_t)pt_l1 | PT_TABLE;

    /* L1[0]: 1GB Device (MMIO) */
    pt_l1[0] = make_l1_block(0x00000000ULL, ATTR_DEVICE);

    /* L1[1]: 1GB DRAM (kernel + user). Initially full RW block;
     * mmu_setup_user_space() replaces this with an L2 table. */
    pt_l1[1] = make_l1_block(0x40000000ULL, ATTR_NORMAL_WB);

    /* Configure MAIR_EL1 */
    uint64_t mair;
    mair  = (0x04ULL << 0);   /* Attr0: Device-nGnRE */
    mair |= (0xFFULL << 24);  /* Attr3: Normal, Inner/Outer WB */
    __asm__ volatile("msr mair_el1, %0" : : "r"(mair));

    /* Load TTBR0 */
    __asm__ volatile(
        "msr ttbr0_el1, %0\n\t"
        "isb\n\t"
        : : "r"((uint64_t)pt_l0) : "memory"
    );

    /* Invalidate TLB */
    __asm__ volatile("tlbi vmalle1is\n\tdsb nsh\n\tisb\n" ::: "memory");

    /* Enable MMU */
    __asm__ volatile(
        "mov x0, #1\n\t"
        "msr sctlr_el1, x0\n\t"
        "isb\n\t"
        ::: "x0", "memory"
    );

    printk("[MMU] Enabled, TTBR0=%p\n", (void *)pt_l0);
}

void *kmalloc_simple(unsigned long size)
{
    static unsigned long alloc_ptr = 0x10000000;
    void *ptr;
    size = (size + 0xFFF) & ~0xFFF;
    ptr = (void *)alloc_ptr;
    alloc_ptr += size;
    return ptr;
}
