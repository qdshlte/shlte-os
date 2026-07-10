/*
 * mmu.c - x86_64 MMU / Page Table Management
 */

#include <shlte/types.h>
#include <shlte/mm.h>
#include <shlte/printk.h>
#include <shlte/mmu.h>
#include <shlte/string.h>

/* x86_64 4-level page tables (aligned to 4KB) */
static uint64_t __attribute__((aligned(0x1000))) pml4[512];
static uint64_t __attribute__((aligned(0x1000))) pdpt[512];
static uint64_t __attribute__((aligned(0x1000))) pd[4][512];  /* 4 PDPT entries → 4 PDs */

/*
 * mmu_enable - Called from boot.S to initialize and enable the MMU.
 * This function sets up page tables, loads CR3, and then calls
 * kernel_main(). It does NOT loop forever.
 */
void mmu_enable(void)
{
    mmu_init(0);

    printk("[BOOT] MMU enabled, entering kernel_main...\n");

    extern void kernel_main(uint64_t multiboot_info);
    kernel_main(0);

    printk("[FATAL] mmu_enable: kernel_main returned!\n");
    for (;;)
        __asm__ volatile("hlt");
}

/*
 * mmu_init - Set up x86_64 page tables and enable paging.
 *
 * On x86_64, GRUB already enables long mode with identity-mapped
 * paging. We build our own page tables with proper attributes
 * and switch to them.
 */
void mmu_init(uint64_t multiboot_info)
{
    (void)multiboot_info;
    int i, j;

    /* Clear page tables */
    memset(pml4, 0, sizeof(pml4));
    memset(pdpt, 0, sizeof(pdpt));
    memset(pd, 0, sizeof(pd));

    /*
     * Set up identity mapping for the first 8GB of physical memory
     * using 2MB huge pages.
     *
     * PML4[0] → PDPT
     * PDPT[0..3] → PD[0..3] (1GB each)
     * PD[i][j] → 2MB page
     */
    pml4[0] = (uint64_t)&pdpt | PTE_PRESENT | PTE_RW;

    for (i = 0; i < 4; i++) {
        pdpt[i] = (uint64_t)&pd[i] | PTE_PRESENT | PTE_RW;

        for (j = 0; j < 512; j++) {
            uint64_t phys = (uint64_t)(i * 512 + j) * HUGE_PAGE_2MB;
            pd[i][j] = phys | PTE_PRESENT | PTE_RW | PTE_HUGE | PTE_GLOBAL;
        }
    }

    /* Load CR3 with our PML4 */
    __asm__ volatile(
        "mov %0, %%cr3\n\t"
        :
        : "r" ((uint64_t)&pml4)
        : "memory"
    );

    /* Invalidate TLB by reloading CR3 */
    __asm__ volatile(
        "mov %%cr3, %%rax\n\t"
        "mov %%rax, %%cr3\n\t"
        :
        :
        : "rax", "memory"
    );

    printk("[MMU] x86_64 page tables initialized, PML4=%p\n", (void*)&pml4);
}

/*
 * kmalloc_simple - Simple bump allocator for early boot
 */
void *kmalloc_simple(unsigned long size)
{
    static unsigned long alloc_ptr = 0x20000000;  /* 512MB mark */
    void *ptr;

    size = (size + 0xFFF) & ~0xFFF;

    ptr = (void *)alloc_ptr;
    alloc_ptr += size;

    return ptr;
}
