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
    (void)sp;
    uint64_t ec = (esr >> 26) & 0x3F;

    printk("[EXC] ESR=0x%lx, FAR=0x%lx\n", esr, far);

    switch (ec) {
    case 0x00:  /* Unknown */
        printk("[EXC] Unknown exception\n");
        break;
    case 0x15:  /* SVC from AArch64 */
        printk("[EXC] SVC from AArch64\n");
        break;
    case 0x20:  /* Instruction Abort from EL1 */
    case 0x21:  /* Instruction Abort from Lower EL */
        printk("[EXC] Instruction abort at 0x%lx\n", far);
        panic("Instruction abort exception");
        break;
    case 0x24:  /* Data Abort from Lower EL */
    case 0x25:  /* Data Abort from EL1 */
        printk("[EXC] Data abort at 0x%lx\n", far);
        panic("Data abort exception");
        break;
    case 0x30:  /* IRQ from Lower EL */
    case 0x31:  /* IRQ from EL1 */
        printk("[EXC] IRQ\n");
        handle_irq();
        break;
    default:
        printk("[EXC] Exception class 0x%x\n", (unsigned int)ec);
        break;
    }

    /* For fatal exceptions (abort), panic() never returns.
     * For recoverable exceptions (IRQ, FIQ, unknown), just return. */
}
