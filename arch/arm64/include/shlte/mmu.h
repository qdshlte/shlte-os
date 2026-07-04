/*
 * shlte/mmu.h - Memory management unit declarations
 */

#ifndef SHLTE_MMU_H
#define SHLTE_MMU_H

#include <shlte/types.h>

/* MMU initialization */
void mmu_enable(void);
void mmu_init(uint64_t dtb_phys);

#endif /* SHLTE_MMU_H */
