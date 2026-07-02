/*
 * linker.lds - Shlte OS ARM64 Kernel Linker Script
 *
 * Layout:
 *   0x80000  - Entry point (boot.S)
 *   0x80000+ - Kernel code & data
 */

OUTPUT_ARCH("aarch64")
ENTRY(_start)

SECTIONS
{
    . = 0x80000;

    /* Text section: code */
    .text : {
        *(.text._start)
        *(.text*)
    }

    /* Read-only data */
    .rodata : {
        *(.rodata*)
        *(.got*)
    }

    /* Initialized data */
    .data : {
        *(.data*)
    }

    /* Uninitialized data (BSS) */
    .bss : {
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        __bss_end = .;
    }

    /* Discard everything from libc */
    /DISCARD/ : {
        *(.comment)
        *(.gnu*)
        *(.note*)
        *(.eh_frame*)
    }

    . = ALIGN(4K);
    __kernel_end = .;
}
