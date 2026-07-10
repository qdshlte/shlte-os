/*
 * kernel.c - x86_64 Kernel Entry Point
 */

#include <shlte/types.h>
#include <shlte/printk.h>
#include <shlte/mm.h>
#include <shlte/process.h>
#include <shlte/fs.h>
#include <shlte/interrupt.h>

/* External declarations */
extern void mmu_init(uint64_t multiboot_info);
extern void interrupt_init(void);
extern void fs_init(void);
extern void usr_init(void);
extern void init_process(void);

/**
 * kernel_main - Main kernel function (x86_64 entry)
 *
 * Called from boot.S after MMU is enabled and page tables are set up.
 * @multiboot_info: Pointer to multiboot2 information structure (from GRUB),
 *                   or 0 if not available.
 */
void kernel_main(uint64_t multiboot_info)
{
    (void)multiboot_info;

    printk("\n");
    printk("=====================================================\n");
    printk("  Shlte OS Kernel (x86_64)\n");
    printk("  Built: %s %s\n", __DATE__, __TIME__);
    printk("=====================================================\n\n");

    /* Initialize memory manager */
    printk("[KERN] Initializing memory manager...\n");
    mm_init();

    /* Initialize interrupt controller */
    printk("[KERN] Initializing interrupts...\n");
    interrupt_init();

    /* Initialize filesystem */
    printk("[KERN] Initializing filesystem...\n");
    fs_init();

    /* Initialize user space */
    printk("[KERN] Initializing user space...\n");
    usr_init();

    /* Start init process */
    printk("[KERN] Starting init process...\n");
    init_process();

    printk("[KERN] Kernel initialized successfully.\n");

    /* Idle loop */
    for (;;) {
        __asm__ volatile("hlt");
    }
}
