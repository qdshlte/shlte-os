/*
 * exception.c - x86_64 Exception Handler
 */

#include <shlte/types.h>
#include <shlte/printk.h>
#include <shlte/interrupt.h>

/* Exception vector names */
static const char *exception_names[] = {
    [0]  = "Division Error",
    [1]  = "Debug",
    [2]  = "NMI",
    [3]  = "Breakpoint",
    [4]  = "Overflow",
    [5]  = "Bound Range Exceeded",
    [6]  = "Invalid Opcode",
    [7]  = "Device Not Available",
    [8]  = "Double Fault",
    [9]  = "Coprocessor Segment Overrun",
    [10] = "Invalid TSS",
    [11] = "Segment Not Present",
    [12] = "Stack-Segment Fault",
    [13] = "General Protection Fault",
    [14] = "Page Fault",
    [16] = "x87 FPU Error",
    [17] = "Alignment Check",
    [18] = "Machine Check",
    [19] = "SIMD FP Exception",
    [20] = "Virtualization Exception",
};

/**
 * handle_exception - x86_64 exception handler
 *
 * Called from assembly when an exception occurs.
 * @vector: Exception vector number (0-31)
 * @error_code: Error code pushed by the CPU (0 if none)
 * @rip: Instruction pointer at time of exception
 */
void handle_exception(uint64_t vector, uint64_t error_code, uint64_t rip)
{
    const char *name = "Unknown";
    if (vector < sizeof(exception_names) / sizeof(exception_names[0])) {
        if (exception_names[vector])
            name = exception_names[vector];
    }

    printk("[EXC] #%s (vector %lu), code=0x%lx, RIP=0x%lx\n",
           name, (unsigned long)vector,
           (unsigned long)error_code, (unsigned long)rip);

    switch (vector) {
    case 0:   /* #DE */
    case 6:   /* #UD */
    case 13:  /* #GP */
        panic("Fatal exception");
        break;

    case 14:  /* #PF - Page Fault */
        {
            uint64_t cr2;
            __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
            printk("[EXC] Page fault at virtual address 0x%lx\n", (unsigned long)cr2);
            panic("Page fault exception");
        }
        break;

    case 8:   /* #DF - Double Fault */
        panic("Double fault exception");
        break;

    default:
        printk("[EXC] Unhandled exception %lu\n", (unsigned long)vector);
        break;
    }
}
