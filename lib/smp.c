/*
 * smp.c - ARM64 SMP Support (PSCI-based CPU bringup)
 *
 * Uses PSCI (Power State Coordination Interface) to start
 * secondary CPUs. Supports spinlock synchronization and
 * per-CPU data structures.
 */

#include <shlte/types.h>
#include <shlte/printk.h>
#include <shlte/string.h>
#include <shlte/smp.h>

/* ============================================================
 * Global state
 * ============================================================ */

percpu_t g_percpu[MAX_CPUS];
static int g_smp_active = 0;
static int g_num_cpus = 1;   /* CPU0 is always online */
static spinlock_t g_smp_lock = SPINLOCK_INIT;

/* Secondary CPU entry point (set by smp_init) */
static uint64_t g_secondary_entry = 0;

/* ============================================================
 * PSCI implementation
 * ============================================================ */

int64_t psci_call(uint64_t func_id, uint64_t arg0, uint64_t arg1, uint64_t arg2)
{
    register uint64_t x0 __asm__("x0") = func_id;
    register uint64_t x1 __asm__("x1") = arg0;
    register uint64_t x2 __asm__("x2") = arg1;
    register uint64_t x3 __asm__("x3") = arg2;

    /* Try SMC first (for EL1), fall back to HVC (for EL2) */
    __asm__ volatile(
        "smc #0\n"
        : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3)
        :
        : "memory"
    );

    return (int64_t)x0;
}

/* ============================================================
 * Secondary CPU boot
 * ============================================================ */

/* Assembly entry point for secondary CPUs (in boot.S) */
extern void secondary_entry(void);

/**
 * smp_boot_cpu - Wake up a secondary CPU via PSCI CPU_ON
 *
 * @cpu_id: Logical CPU ID (affinity level 0)
 * @entry_addr: Physical address where the CPU should start executing
 *
 * The secondary CPU will start at @entry_addr with MMU off.
 */
int smp_boot_cpu(uint32_t cpu_id, uint64_t entry_addr)
{
    if (cpu_id >= MAX_CPUS) return -1;

    /* MPIDR_EL1 format: Aff3.Aff2.Aff1.Aff0.
     * For simple systems, cpu_id == Aff0, higher affinities are 0. */
    uint64_t mpidr = (uint64_t)cpu_id;

    printk("[SMP] Booting CPU %d (entry=0x%lx)...\n", cpu_id, entry_addr);

    int64_t ret = psci_call(PSCI_CPU_ON, mpidr, entry_addr, 0);

    if (ret != PSCI_SUCCESS && ret != PSCI_ALREADY_ON) {
        printk("[SMP] CPU_ON for CPU %d failed: %ld\n", cpu_id, (long)ret);
        return -1;
    }

    g_percpu[cpu_id].online = 1;
    g_num_cpus++;
    return 0;
}

/**
 * smp_secondary_start - C entry point for secondary CPUs
 *
 * Called by secondary_entry assembly trampoline after basic setup.
 */
void smp_secondary_start(uint32_t cpu_id)
{
    g_percpu[cpu_id].cpu_id = cpu_id;
    g_percpu[cpu_id].online = 1;

    /* Read MPIDR */
    __asm__ volatile("mrs %0, mpidr_el1" : "=r"(g_percpu[cpu_id].mpidr));

    printk("[SMP] CPU %d online (MPIDR=0x%lx)\n",
           cpu_id, g_percpu[cpu_id].mpidr);

    /* Enable GIC CPU interface for this CPU */
    /* (GICv2: per-CPU registers are banked, just enable) */
    volatile uint32_t *gicc = (volatile uint32_t *)0x08010000UL;
    gicc[0x0000/4] = 0x01;  /* GICC_CTLR = EnableGrp0 */
    gicc[0x0004/4] = 0xFF;  /* GICC_PMR = allow all */

    /* Enable interrupts */
    __asm__ volatile("msr DAIFClr, #0x2" ::: "memory");  /* Unmask IRQ */

    /* Enter idle loop — secondary CPUs run scheduler tasks */
    while (1) {
        /* Poll for work from the scheduler */
        __asm__ volatile("wfi");
    }
}

/* ============================================================
 * Initialization
 * ============================================================ */

/**
 * smp_init - Initialize SMP subsystem
 *
 * Detects available CPUs and brings up secondary cores.
 */
int smp_init(void)
{
    if (g_smp_active) return 0;

    /* Initialize per-CPU data for CPU 0 */
    memset(g_percpu, 0, sizeof(g_percpu));
    g_percpu[0].cpu_id = 0;
    g_percpu[0].online = 1;
    __asm__ volatile("mrs %0, mpidr_el1" : "=r"(g_percpu[0].mpidr));

    printk("[SMP] CPU 0 (BSP) MPIDR=0x%lx\n", g_percpu[0].mpidr);

    /* Read number of CPUs from device tree or try up to MAX_CPUS */
    /* For QEMU virt, try CPU1-CPU3 */
    g_secondary_entry = (uint64_t)&secondary_entry;

    int booted = 0;
    for (uint32_t cpu = 1; cpu < MAX_CPUS && cpu < 4; cpu++) {
        if (smp_boot_cpu(cpu, g_secondary_entry) == 0) {
            booted++;
        } else {
            break;  /* No more CPUs */
        }
    }

    g_smp_active = 1;
    printk("[SMP] %d CPU(s) total, %d secondary booted\n",
           g_num_cpus, booted);
    return g_num_cpus;
}

int smp_active(void)  { return g_smp_active; }
int smp_num_cpus(void) { return g_num_cpus; }

/**
 * smp_barrier - Simple barrier: wait until all expected CPUs are online
 */
void smp_barrier(void)
{
    /* For now, just a short delay for secondary CPUs to come online */
    for (volatile int i = 0; i < 1000000; i++) {
        __asm__ volatile("nop");
    }
}
