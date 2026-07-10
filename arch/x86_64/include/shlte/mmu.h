/*
 * shlte/mmu.h - x86_64 MMU declarations
 */

#ifndef SHLTE_MMU_ARCH_H
#define SHLTE_MMU_ARCH_H

#include <shlte/types.h>

/* Page table entry flags */
#define PTE_PRESENT    (1ULL << 0)
#define PTE_RW         (1ULL << 1)
#define PTE_USER       (1ULL << 2)
#define PTE_PWT        (1ULL << 3)
#define PTE_PCD        (1ULL << 4)
#define PTE_ACCESSED   (1ULL << 5)
#define PTE_DIRTY      (1ULL << 6)
#define PTE_HUGE       (1ULL << 7)
#define PTE_GLOBAL     (1ULL << 8)
#define PTE_NX         (1ULL << 63)

/* x86_64 page table levels */
#define PML4_SHIFT     39
#define PDPT_SHIFT     30
#define PD_SHIFT       21
#define PT_SHIFT       12

#define HUGE_PAGE_2MB   (2 * 1024 * 1024)
#define HUGE_PAGE_1GB   (1ULL << 30)

void mmu_enable(void);
void mmu_init(uint64_t multiboot_info);

#endif /* SHLTE_MMU_ARCH_H */
