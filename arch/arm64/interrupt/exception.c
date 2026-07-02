/*
 * exception.c - ARM64 Exception Handler
 */

#include <shlte/types.h>
#include <shlte/printk.h>
#include <shlte/interrupt.h>

/**
 * handle_exception - Common exception handler
 *
 * Called from assembly when any exception occurs.
 * x0 = ESR_EL1 (Exception Syndrome Register)
 * x1 = FAR_EL1 (Fault Address Register)
 * x2 = SP_EL0 (User stack pointer at time of exception)
 */
void handle_exception(uint64_t esr, uint64_t far, uint64_t sp)
{
    uint64_t ec = (esr >> 26) & 0x3F;

    printk("[EXC] ESR=0x%lx, FAR=0x%lx\n", esr, far);

    switch (ec) {
    case 0x00:  /* Unknown */
        printk("[EXC] Unknown exception\n");
        break;
    case 0x15:  /* IL - Instruction Abort (EL1) */
    case 0x16:  /* IABT lower */
        printk("[EXC] Instruction abort\n");
        break;
    case 0x22:  /* DABT lower (Data Abort) */
    case 0x23:  /* DABT EL1 */
        printk("[EXC] Data abort at 0x%lx\n", far);
        break;
    case 0x24:  /* IRQ */
        printk("[EXC] IRQ\n");
        handle_irq();
        break;
    case 0x25:  /* FIQ */
        printk("[EXC] FIQ (ignored)\n");
        break;
    default:
        printk("[EXC] Exception class 0x%x\n", (unsigned int)ec);
        break;
    }

    /* For now, just print and continue.
     * In production, this would panic or kill the process. */
    printk("[EXC] Continuing after exception...\n");
}
