/*
 * kernel.c - Shlte OS Main Kernel Entry (C code)
 */

#include <shlte/types.h>
#include <shlte/printk.h>
#include <shlte/string.h>
#include <shlte/mm.h>
#include <shlte/interrupt.h>
#include <shlte/fs.h>
#include <shlte/process.h>

/* Forward declarations */
extern void mmu_init(uint64_t dtb_phys);
extern void interrupt_init(void);
extern void fs_init(void);

/**
 * kernel_main - Main kernel entry point (called from boot.S)
 * @dtb_ptr: Device tree blob physical address
 *
 * This is the first C function called after boot.S sets up
 * basic CPU state, clears BSS, and enables the MMU.
 */
void kernel_main(uint64_t dtb_ptr)
{
    /* Print kernel banner */
    printk("\n");
    printk("========================================\n");
    printk("   Shlte OS v0.1 - ARM64 Microkernel   \n");
    printk("========================================\n");
    printk("\n");

    /*
     * NOTE: MMU is already enabled by mmu_enable() → mmu_init()
     * which was called from boot.S. Do NOT call mmu_init() again.
     */

    /* Initialize interrupt controller (GIC) */
    printk("[INIT] Initializing interrupt subsystem...\n");
    interrupt_init();
    printk("[INIT] Interrupt subsystem initialized.\n");

    /* Initialize filesystem */
    printk("[INIT] Initializing filesystem...\n");
    fs_init();
    printk("[INIT] Filesystem initialized.\n");

    /* Print system info */
    printk("\n");
    printk("[SYS] Kernel loaded at physical address 0x%lx\n", (unsigned long)kernel_main);
    printk("[SYS] Device tree at 0x%lx\n", dtb_ptr);
    printk("[SYS] BSS: __bss_start=%p, __bss_end=%p\n", &__bss_start, &__bss_end);
    printk("\n");

    /* Initialize user space */
    printk("[INIT] Starting user space initialization...\n");
    usr_init();
    printk("[INIT] User space initialized.\n");

    /* Start the init process */
    printk("[INIT] Starting init process...\n");
    init_process();

    /*
     * If init returns (shouldn't happen), halt the system.
     */
    printk("[FATAL] Init process exited! Halting...\n");
    for (;;) {
        __asm__ volatile("wfi");
    }
}

/* External symbols from linker script */
extern char __bss_start[], __bss_end[];
