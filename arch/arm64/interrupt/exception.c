/*
 * exception.c - ARM64 Exception Handler
 *
 * Dispatches synchronous (SVC) and asynchronous (IRQ) exceptions.
 * Supports preemptive multitasking via timer IRQ and context switching
 * through the exception frame.
 */

#include <shlte/types.h>
#include <shlte/printk.h>
#include <shlte/interrupt.h>
#include <shlte/process.h>
#include <shlte/gui.h>

/* Assembly helpers (boot.S) */
extern void early_putchar(char c);
extern void early_putstr(const char *s);

/* Forward declaration */
extern int64_t syscall_handler(uint64_t num, uint64_t a0, uint64_t a1,
                                uint64_t a2, uint64_t a3, uint64_t a4,
                                uint64_t a5, uint64_t *regs);

/**
 * exc_save_context - Save exception frame into process PCB
 * @p: Target process
 * @regs: Exception frame on the kernel stack
 *
 * The exception frame layout (set by boot.S):
 *   regs[0..30]  = x0..x30 (31 GPRs)
 *   regs[31]     = ELR_EL1
 *   regs[32]     = SPSR_EL1
 *   regs[33]     = SP_EL0
 */
static void exc_save_context(process_t *p, uint64_t *regs)
{
    if (!p || !regs) return;
    for (int i = 0; i < 31; i++)
        p->regs[i] = regs[i];
    p->elr_el1 = regs[31];
    p->spsr_el1 = regs[32];
    p->sp_el0 = regs[33];
}

/**
 * exc_restore_context - Load process PCB into exception frame
 * @p: Source process
 * @regs: Exception frame to populate
 *
 * After this call, ERET will resume the process at p->elr_el1.
 */
static void exc_restore_context(process_t *p, uint64_t *regs)
{
    if (!p || !regs) return;
    for (int i = 0; i < 31; i++)
        regs[i] = p->regs[i];
    regs[31] = p->elr_el1;
    regs[32] = p->spsr_el1;
    regs[33] = p->sp_el0;
}

/**
 * handle_exception - Common exception handler
 *
 * Called from assembly with the full exception frame on the kernel stack.
 * May modify regs[0] (x0 return value), regs[31] (ELR_EL1), regs[32] (SPSR_EL1),
 * and regs[33] (SP_EL0) to redirect ERET for syscalls or preemption.
 *
 * @esr:  ESR_EL1 value
 * @far:  FAR_EL1 value
 * @sp:   SP_EL0 value at time of exception
 * @regs: Pointer to exception frame on kernel stack:
 *        regs[0..30]=x0..x30, regs[31]=ELR_EL1, regs[32]=SPSR_EL1, regs[33]=SP_EL0
 */
void handle_exception(uint64_t esr, uint64_t far, uint64_t sp, uint64_t *regs)
{
    (void)sp;
    uint64_t ec = (esr >> 26) & 0x3F;

    switch (ec) {
    case 0x00:  /* Unknown */
        printk("[EXC] Unknown exception: ESR=0x%lx\n", esr);
        break;

    case 0x15:  /* SVC from AArch64 — system call */
        {
            /* ARM64 syscall convention: x8 = number, x0-x5 = args, ret in x0 */
            uint64_t num = regs[8];
            int64_t ret = syscall_handler(num, regs[0], regs[1],
                                           regs[2], regs[3], regs[4],
                                           regs[5], regs);
            regs[0] = (uint64_t)ret;  /* Return value in x0 */

            /* SYS_EXIT — mark process dead, switch to next */
            if (num == 0) {
                if (current_process) {
                    current_process->state = PROC_TERMINATED;
                    printk("[EXC] pid %d terminated\n", current_process->pid);
                }
                schedule();
                if (current_process) {
                    exc_restore_context(current_process, regs);
                } else {
                    /* No runnable process — halt the system */
                    printk("[EXC] No runnable processes. Halting.\n");
                    for (;;) { __asm__ volatile("wfi"); }
                }
            }
        }
        break;

    case 0x20:  /* Instruction Abort from Lower EL */
    case 0x21:  /* Instruction Abort from EL1 */
        printk("[EXC] Instruction abort at 0x%lx, ESR=0x%lx\n", far, esr);
        panic("Instruction abort exception");
        break;

    case 0x24:  /* Data Abort from Lower EL */
    case 0x25:  /* Data Abort from EL1 */
        printk("[EXC] Data abort at 0x%lx, ESR=0x%lx\n", far, esr);
        panic("Data abort exception");
        break;

    case 0x30:  /* IRQ from Lower EL — preemption point */
    case 0x31:  /* IRQ from EL1 */
        handle_irq();

        /* Update GUI (poll input, redraw if dirty) */
        if (g_gui.initialized) {
            gui_update();
        }

        /* Preemption: save current process, schedule, restore next */
        if (need_reschedule && current_process) {
            exc_save_context(current_process, regs);
            schedule();
            if (current_process) {
                exc_restore_context(current_process, regs);
            }
            need_reschedule = 0;
        }
        break;

    default:
        printk("[EXC] Unhandled exception class 0x%lx, ESR=0x%lx\n", ec, esr);
        break;
    }
}
