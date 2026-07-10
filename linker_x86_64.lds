/*
 * linker_x86_64.lds - Shlte OS x86_64 Kernel Linker Script
 *
 * Layout:
 *   0x100000  - Multiboot2 entry point (_start)
 *   0x100000+ - Kernel code & data
 */

OUTPUT_FORMAT("elf64-x86-64")
OUTPUT_ARCH("i386:x86-64")
ENTRY(_start)

PHDRS
{
    text   PT_LOAD FLAGS(5);   /* Read + Execute */
    data   PT_LOAD FLAGS(6);   /* Read + Write */
}

SECTIONS
{
    . = 0x100000;

    /* Multiboot2 header (must be in first 8KB) */
    .multiboot : {
        KEEP(*(.multiboot))
    } :text

    /* Text section: code */
    .text : {
        *(.text*)
    } :text

    /* Read-only data */
    .rodata : {
        *(.rodata*)
        *(.got*)
    } :text

    . = ALIGN(4K);

    /* Initialized data */
    .data : {
        *(.data*)
    } :data

    /* Uninitialized data (BSS) */
    .bss : {
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        __bss_end = .;
    } :data

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
