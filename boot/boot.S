/* ============================================================
 * External function declarations
 * ============================================================ */
    .globl mmu_enable
    .globl handle_exception
    .globl kernel_main

/*
 * boot/boot.S - ARM64 Boot Sector / Kernel Entry Point
 *
 * This is the very first code that runs when the kernel loads.
 * It sets up basic CPU state before handing off to C code.
 */

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

    /* Save device tree pointer */
    mov     x20, x0              /* x20 = dtb pointer */

    /* Disable interrupts (SPSel=1 => SP_EL0, disable FIQ/IRQ) */
    mrs     x1, SPSR_EL1
    orr     x1, x1, #0xC0        /* Disable FIQ and IRQ */
    msr     SPSR_EL1, x1

    /* Set up exception vectors */
    adr     x1, exception_vector_table
    msr     VBAR_EL1, x1

    /* Set up stack pointer for EL1 (use SP_EL1) */
    ldr     x1, =boot_stack_top
    msr     SPSEL, x1            /* SPSel: select SP_EL1 */

    /*
     * If we're in EL2, downgrade to EL1
     * Clear HCR_EL2.E2E (EL1 uses little-endian)
     * Set SCR_EL3.FIQ/IRQ if needed
     */
    mrs     x1, CurrentEL
    cmp     x1, #4               /* CurrentEL == EL2? */
    bne     1f                   /* No, skip EL2 setup */

    /* EL2 -> EL1 transition */
    mrs     x0, SCR_EL3
    orr     x0, x0, #0x3         /* NS=1, RW=1 */
    msr     SCR_EL3, x0

    msr     HCR_EL2, xzr         /* Clear HCR */

    msr     SPSR_EL2, xzr        /* Return to EL1, AArch64 */
    eret                         /* Return to EL1 */

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
