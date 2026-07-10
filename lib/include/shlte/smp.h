/*
 * shlte/smp.h - Symmetric Multi-Processing (SMP) Support
 *
 * ARM64 PSCI-based CPU bringup, spinlocks, per-CPU data,
 * and SMP-aware scheduling.
 */

#ifndef SHLTE_SMP_H
#define SHLTE_SMP_H

#include <shlte/types.h>

/* Maximum number of CPUs */
#define MAX_CPUS    8

/* ============================================================
 * PSCI function IDs
 * ============================================================ */

#define PSCI_CPU_ON         0xC4000003ULL
#define PSCI_CPU_OFF        0x84000002ULL
#define PSCI_AFFINITY_INFO  0xC4000004ULL

/* PSCI return codes */
#define PSCI_SUCCESS         0
#define PSCI_NOT_SUPPORTED  -1
#define PSCI_INVALID_PARAMS -2
#define PSCI_DENIED         -3
#define PSCI_ALREADY_ON     -4
#define PSCI_ON_PENDING     -5
#define PSCI_INTERNAL_FAILURE -6
#define PSCI_NOT_PRESENT    -7
#define PSCI_DISABLED       -8

/* ============================================================
 * Spinlock
 * ============================================================ */

typedef struct {
    volatile uint32_t lock;
} spinlock_t;

#define SPINLOCK_INIT {0}

static inline void spin_lock(spinlock_t *sl)
{
    while (__sync_lock_test_and_set(&sl->lock, 1)) {
        while (sl->lock) {
            __asm__ volatile("yield");
        }
    }
    __asm__ volatile("dmb sy" ::: "memory");
}

static inline void spin_unlock(spinlock_t *sl)
{
    __asm__ volatile("dmb sy" ::: "memory");
    __sync_lock_release(&sl->lock);
}

static inline int spin_trylock(spinlock_t *sl)
{
    if (__sync_lock_test_and_set(&sl->lock, 1))
        return 0;
    __asm__ volatile("dmb sy" ::: "memory");
    return 1;
}

/* ============================================================
 * Per-CPU data
 * ============================================================ */

typedef struct {
    uint32_t cpu_id;
    uint64_t mpidr;          /* Affinity value */
    int      online;
    void    *stack;          /* Per-CPU kernel stack */
    uint64_t ticks;          /* CPU-local tick count */
    spinlock_t sched_lock;   /* Per-CPU scheduler lock */
} percpu_t;

extern percpu_t g_percpu[MAX_CPUS];

/* ============================================================
 * API
 * ============================================================ */

/* Get current CPU ID (from MPIDR_EL1) */
static inline uint32_t smp_cpu_id(void)
{
    uint64_t mpidr;
    __asm__ volatile("mrs %0, mpidr_el1" : "=r"(mpidr));
    return (uint32_t)(mpidr & 0xFF);
}

/* PSCI call (must be at EL2 or EL1 with PSCI conduit) */
int64_t psci_call(uint64_t func_id, uint64_t arg0, uint64_t arg1, uint64_t arg2);

/* Initialize SMP */
int smp_init(void);

/* Start a secondary CPU */
int smp_boot_cpu(uint32_t cpu_id, uint64_t entry_addr);

/* Check if SMP is active */
int smp_active(void);
int smp_num_cpus(void);

/* Wait for all CPUs to be ready */
void smp_barrier(void);

#endif /* SHLTE_SMP_H */
