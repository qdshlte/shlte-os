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

    /* Set up a minimal stack early (before EL1 setup) */
    ldr     x1, =boot_stack_bottom
    msr     SPSEL, #0            /* SPSel: select SP_EL0 temporarily */

    /* Initialize UART BEFORE anything else */
    bl      early_uart_init
    ldr     x0, =early_msg1
    bl      early_putstr

    /* Save device tree pointer */
    mov     x20, x0              /* x20 = dtb pointer */

    /* Set up exception vectors */
    adr     x1, exception_vector_table
    msr     VBAR_EL1, x1

    /* Set up stack pointer for EL1 (use SP_EL1) */
    ldr     x1, =boot_stack_top
    msr     SPSEL, #1            /* SPSel: select SP_EL1 */

    /* Signal: EL1 stack ready */
    ldr     x0, =early_msg2
    bl      early_putstr

    /*
     * If we're in EL2, downgrade to EL1
     * Clear HCR_EL2.E2E (EL1 uses little-endian)
     * Set SCR_EL3.FIQ/IRQ if needed
     */
    mrs     x1, CurrentEL
    cmp     x1, #4               /* CurrentEL == EL2? */
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
     * Enable MMU and caches
     */
    bl      mmu_enable

    /*
     * Jump to C kernel entry
     */
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
 * ============================================================ */
    .align 8
exception_vector_table:
    b       el1_sync             /* Synchronous EL1t */
    b       el1_irq              /* IRQ EL1t */
    b       el1_fiq              /* FIQ EL1t */
    b       el1_error            /* Error EL1t */

    b       el0_sync             /* Synchronous EL064 */
    b       el0_irq              /* IRQ EL064 */
    b       el0_fiq              /* FIQ EL064 */
    b       el0_error            /* Error EL064 */

/* ============================================================
 * Exception Handlers
 * ============================================================ */
    .globl el1_sync
el1_sync:
    mrs     x0, ESR_EL1
    mrs     x1, FAR_EL1
    mrs     x2, SP_EL0
    b       exception_handler

    .globl el1_irq
el1_irq:
    mrs     x0, ESR_EL1
    mrs     x1, FAR_EL1
    mrs     x2, SP_EL0
    b       exception_handler

    .globl el0_sync
el0_sync:
    mrs     x0, ESR_EL1
    mrs     x1, FAR_EL1
    mrs     x2, SP_EL0
    b       exception_handler

    .globl el0_irq
el0_irq:
    mrs     x0, ESR_EL1
    mrs     x1, FAR_EL1
    mrs     x2, SP_EL0
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
 * ============================================================ */
    .globl exception_handler
exception_handler:
    sub     sp, sp, #256         /* Allocate stack space */
    stp     x0, x1, [sp, #0]     /* Save x0-x18 */
    stp     x2, x3, [sp, #16]
    stp     x4, x5, [sp, #32]
    stp     x6, x7, [sp, #48]
    stp     x8, x9, [sp, #64]
    stp     x10, x11, [sp, #80]
    stp     x12, x13, [sp, #96]
    stp     x14, x15, [sp, #112]
    stp     x16, x17, [sp, #128]
    mov     x18, sp
    stp     x18, xzr, [sp, #144]

    bl      handle_exception

    ldp     x18, xzr, [sp, #144]
    ldp     x16, x17, [sp, #128]
    ldp     x12, x13, [sp, #96]
    ldp     x14, x15, [sp, #112]
    ldp     x8, x9, [sp, #64]
    ldp     x4, x5, [sp, #32]
    ldp     x6, x7, [sp, #48]
    ldp     x2, x3, [sp, #16]
    ldp     x0, x1, [sp, #0]
    add     sp, sp, #256

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
    mov     x1, x0
1:
    ldrb    w2, [x1], #1
    cbz     w2, 2f
    mov     x0, x2
    bl      early_putchar
    b       1b
2:
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
