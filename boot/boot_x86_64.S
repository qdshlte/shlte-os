/*
 * boot_x86_64.S - x86_64 Multiboot2 Entry Point
 *
 * GRUB loads the kernel as a multiboot2-compliant ELF and jumps to
 * _start in 64-bit long mode with identity-mapped paging enabled.
 *
 * On entry:
 *   rax = multiboot2 magic (0x36D76289)
 *   rbx = pointer to multiboot2 information structure
 *
 * Responsibilities:
 *   1. Set up kernel stack
 *   2. Clear BSS segment
 *   3. Initialize UART
 *   4. Initialize MMU (build our own page tables)
 *   5. Jump to kernel_main()
 */

.section .multiboot

/* Multiboot2 header */
.align 8
multiboot_header:
    .long 0xE85250D6                /* Magic */
    .long 0                         /* Architecture: i386 (protected mode) */
    .long multiboot_header_end - multiboot_header  /* Header length */
    .long -(0xE85250D6 + 0 + (multiboot_header_end - multiboot_header))  /* Checksum */

    /* Entry address tag */
    .align 8
    .short 3                        /* Type: entry address */
    .short 0                        /* Flags */
    .long 12                        /* Size */
    .long _start                    /* Entry address */
    .align 8

    /* End tag */
    .short 0                        /* Type */
    .short 0                        /* Flags */
    .long 8                         /* Size */
multiboot_header_end:

.section .text

.global _start
.type _start, @function

_start:
    /*
     * GRUB has placed us in 64-bit long mode.
     * Set up a proper kernel stack.
     */
    lea    stack_top(%rip), %rsp
    mov    %rsp, %rbp

    /* Save multiboot info pointer (rbx) for later use */
    push   %rbx

    /*
     * Clear BSS segment.
     * __bss_start and __bss_end are defined in the linker script.
     */
    lea    __bss_start(%rip), %rdi
    lea    __bss_end(%rip), %rcx
    sub    %rdi, %rcx              /* rcx = BSS size in bytes */
    shr    $3, %rcx                /* Convert to qwords */
    xor    %rax, %rax
    cld
    rep stosq

    /* Initialize serial port (early UART) */
    call   uart_init

    /* Print early boot message */
    lea    boot_msg(%rip), %rdi
    call   uart_puts_early

    /*
     * Initialize MMU and jump to kernel_main.
     * mmu_enable() builds our page tables, enables paging,
     * and calls kernel_main().
     */
    call   mmu_enable

    /* If we ever return, halt */
halt:
    hlt
    jmp    halt

.size _start, . - _start

/* ============================================================
 * Early UART (COM1 serial port) for boot messages
 * ============================================================ */

#define COM1_PORT  0x3F8
#define LSR_OFFSET 5
#define LSR_THRE   (1 << 5)

/*
 * uart_putc_early - Output a single character to COM1
 */
.type uart_putc_early, @function
uart_putc_early:
    push   %rbp
    mov    %rsp, %rbp

    /* Handle newline: send carriage return first */
    cmp    $'\n', %dil
    jne    1f
    mov    $'\r', %dil
    call   uart_putc_early
    mov    $'\n', %dil
1:
    /* Wait for transmitter to be ready */
    mov    $COM1_PORT + LSR_OFFSET, %dx
2:
    inb    %dx, %al
    test   $LSR_THRE, %al
    jz     2b

    /* Send character */
    mov    $COM1_PORT, %dx
    mov    %dil, %al
    outb   %al, %dx

    pop    %rbp
    ret

/*
 * uart_puts_early - Output a null-terminated string to COM1
 */
.type uart_puts_early, @function
uart_puts_early:
    push   %rbp
    mov    %rsp, %rbp
    push   %rbx
    mov    %rdi, %rbx

1:
    movzbl (%rbx), %edi
    test   %dil, %dil
    jz     2f
    call   uart_putc_early
    inc    %rbx
    jmp    1b

2:
    pop    %rbx
    pop    %rbp
    ret

.section .rodata
boot_msg:
    .asciz "[BOOT] x86_64 bootloader entry, setting up kernel...\n"

/* ============================================================
 * Kernel Stack (16KB)
 * ============================================================ */

.section .bss
.align 16
stack_bottom:
    .skip 16384
stack_top:
