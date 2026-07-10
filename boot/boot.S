/*
 * boot/boot.S - ARM64 Boot Sector / Kernel Entry Point
 *
 * This is the very first code that runs when the kernel loads.
 * It sets up basic CPU state before handing off to C code.
 */

/* ============================================================
 * External function declarations
 * ============================================================ */
    .globl mmu_enable
    .globl handle_exception
    .globl kernel_main
    .globl early_uart_init
    .globl early_putchar
    .globl early_putstr

    .section .text._start
    .globl _start
    .align 2

_start:
    /*
     * ARM64 boot entry conventions:
     * - x0 = device tree pointer (physical address)
     * - All registers in unknown state
     * - MMU off, D-cache off, I-cache unknown
     * - EL2 or EL1 (depending on firmware)
     */

    /* Save DTB pointer immediately, before any calls clobber x0 */
    mov     x20, x0              /* x20 = dtb pointer */

    /* Set up a minimal stack early (before EL1 setup) */
    ldr     x1, =boot_stack_top
    msr     SPSEL, #0            /* SPSel: select SP_EL0 temporarily */
    mov     sp, x1

    /* Initialize UART BEFORE anything else */
    bl      early_uart_init
    ldr     x0, =early_msg1
    bl      early_putstr

    /* Set up exception vectors */
    adr     x1, exception_vector_table
    msr     VBAR_EL1, x1

    /* Set up stack pointer for EL1 (use SP_EL1) */
    ldr     x1, =boot_stack_top
    msr     SPSEL, #1            /* SPSel: select SP_EL1 */
    mov     sp, x1

    /* Signal: EL1 stack ready */
    ldr     x0, =early_msg2
    bl      early_putstr

    /*
     * If we're in EL2, downgrade to EL1
     * Clear HCR_EL2.E2E (EL1 uses little-endian)
     * Set SCR_EL3.FIQ/IRQ if needed
     */
    mrs     x1, CurrentEL
    cmp     x1, #8               /* CurrentEL == EL2? */
    bne     1f                   /* No, skip EL2 setup */

    /* Signal: EL2 detected */
    ldr     x0, =early_msg3
    bl      early_putstr

    /* EL2 -> EL1 transition */
    msr     HCR_EL2, xzr         /* EL2 -> EL1, AArch64 */
    adr     x1, el1_entry        /* save return address */
    msr     ELR_EL2, x1          /* ERET jumps here */
    msr     SPSR_EL2, xzr        /* EL1t (EL1 with SP_EL0), AArch64, no exceptions masked */
    eret

el1_entry:
    /* Now in EL1 - set up SPSR_EL1 and mask interrupts */
    msr     SPSR_EL1, xzr
    msr     DAIFSet, #0x3        /* mask IRQ + FIQ via DAIF */

    /* Enable FP/SIMD at EL1 and EL0 */
    mov     x0, #(3 << 20)       /* FPEN = 3 */
    msr     CPACR_EL1, x0

    /* Signal: EL1 entered */
    ldr     x0, =early_msg4
    bl      early_putstr

1:
    /*
     * Clear BSS
     */
    ldr     x1, =__bss_start
    ldr     x2, =__bss_end
    subs    x2, x2, x1
    b       2f
3:
    str     xzr, [x1], #8
    subs    x2, x2, #8
    b.gt    3b
2:
    bgt     3b

    /* Signal: BSS cleared */
    ldr     x0, =early_msg5
    bl      early_putstr

    /*
     * Jump to C kernel entry.
     * el0_return_here is the return point for SYS_EXIT — the
     * exception handler sets ELR_EL1 here after EL0 programs exit.
     */
    .globl el0_return_here
el0_return_here:
    mov     x0, x20              /* Pass DTB pointer */
    bl      kernel_main

    /*
     * Should never reach here
     */
    b       halt

halt:
    wfi
    b       halt

/* ============================================================
 * Exception Vector Table (AArch64)
 *
 * ARM64 requires 16 entries, each spaced 0x80 bytes apart.
 * Entries we don't handle branch to a panic-like infinite loop.
 * ============================================================ */
    .balign 0x800
exception_vector_table:

    /* 0x000: Synchronous EL1t */
    b       el1_sync
    .balign 0x80
    /* 0x080: IRQ EL1t */
    b       el1_irq
    .balign 0x80
    /* 0x100: FIQ EL1t */
    b       el1_fiq
    .balign 0x80
    /* 0x180: Error EL1t */
    b       el1_error
    .balign 0x80

    /* 0x200: Synchronous EL1h */
    b       el1_sync
    .balign 0x80
    /* 0x280: IRQ EL1h */
    b       el1_irq
    .balign 0x80
    /* 0x300: FIQ EL1h */
    b       el1_fiq
    .balign 0x80
    /* 0x380: Error EL1h */
    b       el1_error
    .balign 0x80

    /* 0x400: Synchronous EL0t (AArch64) */
    b       el0_sync
    .balign 0x80
    /* 0x480: IRQ EL0t */
    b       el0_irq
    .balign 0x80
    /* 0x500: FIQ EL0t */
    b       el0_fiq
    .balign 0x80
    /* 0x580: Error EL0t */
    b       el0_error
    .balign 0x80

    /* 0x600: Synchronous EL0h (AArch32) — unused, just hang */
    b       .
    .balign 0x80
    /* 0x680: IRQ EL0h */
    b       .
    .balign 0x80
    /* 0x700: FIQ EL0h */
    b       .
    .balign 0x80
    /* 0x780: Error EL0h */
    b       .
    .balign 0x80

/* ============================================================
 * Exception Handlers
 *
 * Each vector entry saves nothing — just jumps to the common
 * handler which saves all original registers before reading
 * system registers (ESR_EL1, FAR_EL1, SP_EL0).
 * ============================================================ */
    .globl el1_sync
el1_sync:
    b       exception_handler

    .globl el1_irq
el1_irq:
    b       exception_handler

    .globl el0_sync
el0_sync:
    b       exception_handler

    .globl el0_irq
el0_irq:
    b       exception_handler

    .globl el1_fiq
el1_fiq:
    b       exception_handler

    .globl el0_fiq
el0_fiq:
    b       exception_handler

    .globl el1_error
el1_error:
    b       exception_handler

    .globl el0_error
el0_error:
    b       exception_handler

/* ============================================================
 * Common exception handler
 *
 * Stack frame layout (sp offset):
 *   +0   : x0  (original, may be modified by handle_exception)
 *   +8   : x1
 *   +16  : x2
 *   +24  : x3
 *   +32  : x4
 *   +40  : x5
 *   +48  : x6
 *   +56  : x7
 *   +64  : x8
 *   +72  : x9
 *   +80  : x10
 *   +88  : x11
 *   +96  : x12
 *   +104 : x13
 *   +112 : x14
 *   +120 : x15
 *   +128 : x16
 *   +136 : x17
 *   +144 : x18
 *   +152 : x19
 *   +160 : x20
 *   +168 : x21
 *   +176 : x22
 *   +184 : x23
 *   +192 : x24
 *   +200 : x25
 *   +208 : x26
 *   +216 : x27
 *   +224 : x28
 *   +232 : x29
 *   +240 : x30 (LR at time of exception)
 *   +248 : ELR_EL1
 *   +256 : SPSR_EL1
 *   +264 : SP_EL0
 *   +272 : ESR_EL1
 *   +280 : FAR_EL1
 *
 * handle_exception(esr, far, sp_el0, regs_frame)
 *   regs_frame points to saved x0 on the stack.
 *   handle_exception may modify registers + ELR/SPSR for
 *   syscall return values or preemption context switches.
 * ============================================================ */
    .globl exception_handler
exception_handler:
    sub     sp, sp, #296         /* 288 for regs + 8 alignment */

    /* Save ALL general-purpose registers BEFORE touching sysregs */
    stp     x0, x1, [sp, #0]
    stp     x2, x3, [sp, #16]
    stp     x4, x5, [sp, #32]
    stp     x6, x7, [sp, #48]
    stp     x8, x9, [sp, #64]
    stp     x10, x11, [sp, #80]
    stp     x12, x13, [sp, #96]
    stp     x14, x15, [sp, #112]
    stp     x16, x17, [sp, #128]
    stp     x18, x19, [sp, #144]
    stp     x20, x21, [sp, #160]
    stp     x22, x23, [sp, #176]
    stp     x24, x25, [sp, #192]
    stp     x26, x27, [sp, #208]
    stp     x28, x29, [sp, #224]
    str     x30, [sp, #240]

    /* Now safe to read system registers */
    mrs     x0, ELR_EL1
    mrs     x1, SPSR_EL1
    mrs     x2, SP_EL0
    mrs     x3, ESR_EL1
    mrs     x4, FAR_EL1
    stp     x0, x1, [sp, #248]   /* ELR_EL1, SPSR_EL1 */
    stp     x2, x3, [sp, #264]   /* SP_EL0, ESR_EL1 */
    str     x4, [sp, #280]       /* FAR_EL1 */

    /* x3 = pointer to saved register frame (for syscall/preemption) */
    mov     x3, sp

    bl      handle_exception

    /* Restore system registers first (may have been modified for context switch) */
    ldp     x0, x1, [sp, #248]
    msr     ELR_EL1, x0
    msr     SPSR_EL1, x1
    ldr     x2, [sp, #264]
    msr     SP_EL0, x2

    /* Restore all GPRs (x0 may have been modified by syscall) */
    ldp     x28, x29, [sp, #224]
    ldp     x26, x27, [sp, #208]
    ldp     x24, x25, [sp, #192]
    ldp     x22, x23, [sp, #176]
    ldp     x20, x21, [sp, #160]
    ldp     x18, x19, [sp, #144]
    ldp     x16, x17, [sp, #128]
    ldp     x14, x15, [sp, #112]
    ldp     x12, x13, [sp, #96]
    ldp     x10, x11, [sp, #80]
    ldp     x8, x9, [sp, #64]
    ldp     x6, x7, [sp, #48]
    ldp     x4, x5, [sp, #32]
    ldp     x2, x3, [sp, #16]
    ldp     x0, x1, [sp, #0]
    add     sp, sp, #296

    eret

/* ============================================================
 * Stack
 * ============================================================ */
    .section .bss.stack
    .align 12
    .space  65536                /* 64KB boot stack */
boot_stack_bottom:

    .globl boot_stack_top
boot_stack_top:

/* ============================================================
 * EL0 user stack (4KB, before MMU — physical addresses)
 * ============================================================ */
    .section .bss.ustack
    .align 12
    .space  4096
    .globl utest_stack_top
utest_stack_top:

/* ============================================================
 * Early UART helpers (before MMU setup)
 * ============================================================ */
    .section .text.early_uart
    .align 2

/*
 * early_uart_init - Initialize PL011 UART at 0x09000000
 */
    .globl early_uart_init
early_uart_init:
    mov     x0, #0x09000000

    /* Disable UART before config (no BUSY wait — undefined before UARTEN) */
    str     wzr, [x0, #0x30]

    /* Clear interrupts */
    str     wzr, [x0, #0x3C]
    str     xzr, [x0, #0x4C]

    /* Enable UART, TX, RX */
    mov     w1, #(1 << 0) | (1 << 8) | (1 << 9)  /* UARTEN | RXE | TXE */
    str     w1, [x0, #0x30]

    ret

/*
 * early_putchar - Output a single character to UART
 * x0 = character to print
 */
    .globl early_putchar
early_putchar:
    mov     x1, #0x09000000
    /* Wait for TX FIFO not full */
1:
    ldr     w2, [x1, #0x18]
    and     w2, w2, #(1 << 5)  /* FR_TXFF */
    cbnz    w2, 1b

    /* Handle newline: send CR (0x0d) before LF (0x0a) */
    cmp     w0, #'\n'
    bne     2f

    /* Wait again, then send CR */
    mov     w3, #0x0d
    str     w3, [x1, #0x00]
    /* Wait for TX FIFO not full again */
1:
    ldr     w2, [x1, #0x18]
    and     w2, w2, #(1 << 5)
    cbnz    w2, 1b

    /* Fall through to send the actual LF character */

2:
    /* Send character */
    str     w0, [x1, #0x00]
    ret

/*
 * early_putstr - Output a null-terminated string
 * x0 = string pointer (physical address, before MMU)
 */
    .globl early_putstr
early_putstr:
    stp     x29, x30, [sp, #-16]!
    mov     x29, sp
    mov     x4, x0               /* x4 = string pointer (early_putchar clobbers x0-x3) */
1:
    ldrb    w0, [x4], #1
    cbz     w0, 2f
    bl      early_putchar
    b       1b
2:
    ldp     x29, x30, [sp], #16
    ret

/* ============================================================
 * Boot messages (in .rodata, accessible before MMU)
 * ============================================================ */
    .section .rodata
    .align 2

    .globl early_msg1
early_msg1:
    .ascii  "[BOOT] Initializing...\n"

    .globl early_msg2
early_msg2:
    .ascii  "[BOOT] Stack setup done\n"

    .globl early_msg3
early_msg3:
    .ascii  "[BOOT] EL2 detected, downgrading...\n"

    .globl early_msg4
early_msg4:
    .ascii  "[BOOT] Running at EL1\n"

    .globl early_msg5
early_msg5:
    .ascii  "[BOOT] BSS cleared, about to enable MMU\n"

/* ============================================================
 * EL0 entry trampoline
 * ============================================================ */
    .section .text.enter_el0
    .align 2

/*
 * enter_el0 - Drop to EL0 userspace
 *   x0 = entry point (physical address)
 *   x1 = user stack top
 *
 * Sets up SP_EL0, ELR_EL1, SPSR_EL1 and executes eret.
 * Does NOT return — execution continues at EL0.
 */
    .globl enter_el0
enter_el0:
    msr     SPSel, #0           /* Select SP_EL0 */
    mov     sp, x1              /* User stack */
    msr     ELR_EL1, x0         /* Return address = entry point */

    /* SPSR_EL1 for EL0t: M[3:0]=0, all exceptions unmasked (DAIF=0000) */
    msr     SPSR_EL1, xzr

    eret                        /* -> EL0! */

/*
 * resume_to_el0 - Restore full process context and ERET to EL0
 *   x0 = pointer to regs[0] array (within process_t PCB)
 *
 * Restores x0-x30 from the regs array, then ELR_EL1, SPSR_EL1,
 * and SP_EL0 from the fields that follow in the PCB. Finally erets
 * to resume process execution.
 *
 * Memory layout (x0 must point to regs[0]):
 *   +0:    x0
 *   +8:    x1
 *   ...
 *   +240:  x30
 *   +248:  ELR_EL1
 *   +256:  SPSR_EL1
 *   +264:  SP_EL0
 */
    .globl resume_to_el0
resume_to_el0:
    /* x0 = pointer to regs[0] */

    /* Restore SP_EL0 first */
    ldr     x1, [x0, #264]
    msr     SPSel, #0
    mov     sp, x1

    /* Restore ELR_EL1 and SPSR_EL1 */
    ldr     x1, [x0, #248]
    msr     ELR_EL1, x1
    ldr     x1, [x0, #256]
    msr     SPSR_EL1, x1

    /* Restore x1-x30 from regs[] */
    ldp     x1, x2, [x0, #8]      /* x1, x2 */
    ldp     x3, x4, [x0, #24]     /* x3, x4 */
    ldp     x5, x6, [x0, #40]     /* x5, x6 */
    ldp     x7, x8, [x0, #56]     /* x7, x8 */
    ldp     x9, x10, [x0, #72]    /* x9, x10 */
    ldp     x11, x12, [x0, #88]   /* x11, x12 */
    ldp     x13, x14, [x0, #104]  /* x13, x14 */
    ldp     x15, x16, [x0, #120]  /* x15, x16 */
    ldp     x17, x18, [x0, #136]  /* x17, x18 */
    ldp     x19, x20, [x0, #152]  /* x19, x20 */
    ldp     x21, x22, [x0, #168]  /* x21, x22 */
    ldp     x23, x24, [x0, #184]  /* x23, x24 */
    ldp     x25, x26, [x0, #200]  /* x25, x26 */
    ldp     x27, x28, [x0, #216]  /* x27, x28 */
    ldp     x29, x30, [x0, #232]  /* x29, x30 */

    /* Restore x0 last (first arg / return value) */
    ldr     x0, [x0, #0]

    eret                        /* -> EL0! */

/* ============================================================
 * Embedded EL0 test program
 *
 * Prints "Hello from EL0!\n" via SYS_WRITE, then SYS_EXIT.
 * ============================================================ */
    .section .rodata.utest, "a"
    .align 2
    .globl utest_start
utest_start:
    /* SYS_WRITE(1, msg, 16) — x8=3, x0=fd, x1=buf, x2=len */
    mov     x8, #3
    mov     x0, #1
    adr     x1, 1f
    mov     x2, #17
    svc     #0

    /* SYS_EXIT(0) — x8=0, x0=0 */
    mov     x8, #0
    mov     x0, #0
    svc     #0

    b       .
1:
    .ascii  "Hello from EL0!\n"
    .globl utest_end
utest_end:

/* ============================================================
 * Embedded EL0 process 2
 * ============================================================ */
    .section .rodata.utest, "a"
    .align 2
    .globl el0_prog2_start
el0_prog2_start:
    /* SYS_WRITE(1, msg, 17) */
    mov     x8, #3
    mov     x0, #1
    adr     x1, 2f
    mov     x2, #19
    svc     #0

    /* SYS_EXIT(0) */
    mov     x8, #0
    mov     x0, #0
    svc     #0

    b       .
2:
    .ascii  "Process 2 is alive!\n"
    .globl el0_prog2_end
el0_prog2_end:

