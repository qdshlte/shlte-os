/*
 * kernel.c - Shlte OS Main Kernel Entry
 *
 * Initializes all subsystems including GPU and input,
 * creates the initial user process, and hands control to EL0.
 */

#include <shlte/types.h>
#include <shlte/printk.h>
#include <shlte/interrupt.h>
#include <shlte/timer.h>
#include <shlte/kernel.h>
#include <shlte/ipc.h>
#include <shlte/mm.h>
#include <shlte/fs.h>
#include <shlte/loader.h>
#include <shlte/process.h>
#include <shlte/string.h>
#include <shlte/virtio_gpu.h>
#include <shlte/virtio_input.h>
#include <shlte/gui.h>
#include <shlte/pcie.h>
#include <shlte/smp.h>
#include <shlte/net.h>

/* External PID set by syscall handler */
extern int current_pid;

void kernel_main(uint64_t dtb_ptr)
{
    (void)dtb_ptr;

    printk("\n==============================\n");
    printk("  Shlte OS v0.2  arm64 \n");
    printk("==============================\n\n");

    printk("[INIT] GIC... "); interrupt_init(); printk("ok\n");
    printk("[INIT] Timer... "); timer_init(); printk("ok\n");
    printk("[INIT] IPC... "); ipc_init(); printk("ok\n");
    printk("[INIT] MM... "); mm_init(); printk("ok\n");
    printk("[INIT] VFS... "); fs_init(); printk("ok\n");
    printk("[INIT] PCI... ");
    if (pci_init(0x3F000000ULL, 0, 0) > 0)
        printk("ok\n");
    else
        printk("none\n");
    printk("[INIT] SMP... ");
    smp_init();
    printk("ok (%d cpus)\n", smp_num_cpus());
    printk("[INIT] NET... ");
    if (net_init() == 0)
        printk("ok\n");
    else
        printk("none\n");

    /* Initialize display and input */
    printk("[INIT] GPU... ");
    if (virtio_gpu_init() == 0) {
        printk("ok (%ux%u)\n", g_gpu.width, g_gpu.height);
        printk("[INIT] GUI... ");
        gui_init();
        printk("ok\n");
    } else {
        printk("not found\n");
    }
    printk("[INIT] Input... ");
    if (virtio_input_init() == 0)
        printk("ok\n");
    else
        printk("not found\n");

    /* Register timer IRQ for preemptive scheduling */
    timer_register_irq();

    printk("[INIT] Subsystems ready.\n");

    /* Create the init process (shell) */
    process_t *init_proc = create_process(NULL, NULL, 0, "init");
    if (!init_proc) {
        printk("[FATAL] Cannot create init process!\n");
        for (;;) { __asm__ volatile("wfi"); }
    }
    init_proc->pid = 1;

    /* Load the init shell binary from embedded rootfs */
    uint64_t entry;
    void *stack;
    if (load_raw_binary("/mnt/init", &entry, &stack) == 0) {
        init_proc->entry = entry;
        init_proc->elr_el1 = entry;
        init_proc->spsr_el1 = 0;
        init_proc->user_space = stack;
        init_proc->user_space_size = USER_STACK_SIZE;
        init_proc->sp_el0 = (uint64_t)stack + USER_STACK_SIZE;
        init_proc->regs[0] = entry;
        printk("[PROC] Init loaded at 0x%lx, stack=0x%lx\n",
               entry, init_proc->sp_el0);
    } else {
        printk("[FATAL] Cannot load init binary!\n");
        for (;;) { __asm__ volatile("wfi"); }
    }

    current_process = init_proc;
    current_pid = 1;

    /* Enable W^X for user space: code=RO+X, data=RW, stack guard */
    mmu_setup_user_space(0x40200000ULL, 0x100000ULL,   /* Code: 1MB */
                          0x40300000ULL, 0x100000ULL);  /* Data: 1MB */

    /* Welcome message to GUI terminal */
    gui_term_write("Welcome to Shlte OS v0.2\n");
    gui_term_write("Type 'help' for available commands.\n\n");
    gui_term_write("> ");

    printk("[INIT] Launching init (pid 1)...\n");

    /* Jump to EL0 — never returns */
    resume_to_el0(init_proc->regs);

    /* Unreachable */
    for (;;) { __asm__ volatile("wfi"); }
}
