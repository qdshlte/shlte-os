/*
 * linker.lds - Shlte OS ARM64 Kernel Linker Script
 *
 * Layout:
 *   0x80000  - Entry point (boot.S)
 *   0x80000+ - Kernel code & data
 */

OUTPUT_ARCH("aarch64")
ENTRY(_start)

PHDRS
{
  text   PT_LOAD FLAGS(5);   /* PF_R | PF_X */
  rodata PT_LOAD FLAGS(4);   /* PF_R */
  data   PT_LOAD FLAGS(6);   /* PF_R | PF_W */
}

SECTIONS
{
    . = 0x80000;

    /* Text section: code */
    .text : {
        *(.text._start)
        *(.text*)
    } :text

    . = ALIGN(4096);

    /* Read-only data */
    .rodata : {
        *(.rodata*)
        *(.got*)
    } :rodata

    . = ALIGN(4096);

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
