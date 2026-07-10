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

/* Memory attributes — indices into MAIR_EL1 */
#define ATTR_DEVICE     0   /* MAIR[7:0]   = Device-nGnRE */
#define ATTR_NORMAL_NC  1   /* MAIR[15:8]  = Normal Non-Cacheable */
#define ATTR_NORMAL_WT  2   /* MAIR[23:16] = Normal Write-Through */
#define ATTR_NORMAL_WB  3   /* MAIR[31:24] = Normal Write-Back */

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
static uint64_t __attribute__((aligned(0x4000)))   pt_table_l1[512];
static uint64_t __attribute__((aligned(0x4000)))   pt_table_l2[512];  /* L2 for 2MB blocks */

/*
 * Map 2MB pages (level 1 block)
 * Format: [valid][attrs][NS][AF][NG][SH][TYPE=block]
 */
static uint64_t pt_block_2mb(uint64_t phys_addr, int mem_attr)
{
    uint64_t entry = phys_addr & ~0x1FFFFF;  /* Align to 2MB boundary */
    /* AttrIndx = mem_attr (0..7), placed in bits[4:2] */
    entry |= ((uint64_t)(mem_attr & 0x7)) << 2;
    /* Block descriptor: V=1(bit0), type=block(bit1=0), AF(bit10), SH=inner(9:8), AP=EL0-RW(bit6) */
    entry |= (1ULL << 0) | (1ULL << 10) | (3ULL << 8) | (1ULL << 6);
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
     * L0[0] -> L1 table.
     * L1 entries are 1GB blocks (simplest possible mapping).
     */
    pt_table_l0[0] = (uint64_t)&pt_table_l1 | PT_TABLE;

    /*
     * L1[0]: 1GB Device block at phys 0x0 (UART, GIC, virtio MMIO)
     * L1[1]: 1GB Normal WB block at phys 0x40000000 (DRAM, kernel)
     *
     * For 1GB L1 block descriptor:
     *   bits[1:0] = 01 (block)
     *   bits[47:30] = output address[47:30]
     *   bit[10] = AF
     *   bits[9:8] = SH (0b11 = inner shareable)
     *   bits[7:6] = AP (0b01 = EL0-RW)
     *   bits[4:2] = AttrIndx
     */
    pt_table_l1[0] = (0x00000000ULL & ~((1ULL << 30) - 1))
                   | ((uint64_t)ATTR_DEVICE << 2)
                   | (1ULL << 10) | (3ULL << 8) | (1ULL << 6)
                   | (1ULL << 0);  /* bit1=0 → block */
    pt_table_l1[1] = (0x40000000ULL & ~((1ULL << 30) - 1))
                   | ((uint64_t)ATTR_NORMAL_WB << 2)
                   | (1ULL << 10) | (3ULL << 8) | (1ULL << 6)
                   | (1ULL << 0);  /* bit1=0 → block */

    /*
     * Configure MAIR_EL1. Attr0=Device, Attr3=Normal-WB.
     */
    {
        uint64_t mair;
        mair  = (0x04ULL << 0);   /* Attr0: Device-nGnRE */
        mair |= (0xFFULL << 24);  /* Attr3: Normal, Inner/Outer Write-Back */
        __asm__ volatile("msr mair_el1, %0" : : "r"(mair));
    }

    /* TCR_EL1 left at reset — T0SZ=0 is fine for our 48-bit VAs */

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

    /* Enable MMU only — write exact value, not read-modify-write */
    __asm__ volatile(
        "mov x0, #1\n\t"
        "msr sctlr_el1, x0\n\t"
        "isb\n\t"
        ::: "x0", "memory"
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
