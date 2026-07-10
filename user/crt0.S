/*
 * user/crt0.S - C Runtime Startup for Shlte OS EL0 programs
 *
 * Sets up a minimal C environment and calls main().
 * When main() returns, calls exit().
 */
    .text
    .globl _start
    .align 2

_start:
    /* Stack pointer (SP_EL0) is already set by the kernel loader.
     * Frame pointer = 0 (mark end of stack trace) */
    mov     x29, #0

    /* Call main() */
    bl      main

    /* If main returns, call exit(0) */
    mov     x0, #0
    mov     x8, #0          /* SYS_EXIT */
    svc     #0

    /* Should never reach here */
    b       .
