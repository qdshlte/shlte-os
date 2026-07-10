/*
 * boot/efi_stub.S - ARM64 UEFI Application Entry Point
 *
 * Provides the PE/COFF headers required by UEFI firmware,
 * the UEFI entry point that gathers system information,
 * and transitions to the native kernel entry (_start).
 *
 * Memory layout:
 *   +0x000:  PE/COFF headers
 *   +0x200:  efi_entry (UEFI entry point)
 *   ...      rest of kernel
 */

#include "efi.h"

    .section .head.text, "ax"
    .balign 4096

/* ============================================================
 * PE/COFF Header (for UEFI recognition)
 *
 * UEFI expects a PE32+ image with machine type ARM64 (0xAA64).
 * The DOS stub is minimal; the PE signature follows.
 * ============================================================ */

    /* DOS header */
    .ascii  "MZ"                    /* e_magic */
    .short  0                       /* e_cblp */
    .short  0                       /* e_cp */
    .short  0                       /* e_crlc */
    .short  0                       /* e_cparhdr */
    .short  0                       /* e_minalloc */
    .short  0                       /* e_maxalloc */
    .short  0                       /* e_ss */
    .short  0                       /* e_sp */
    .short  0                       /* e_csum */
    .short  0                       /* e_ip */
    .short  0                       /* e_cs */
    .short  0                       /* e_lfarlc */
    .short  0                       /* e_ovno */
    .skip   32, 0                   /* e_res */
    .long   pe_header - .           /* e_lfanew (offset to PE header) */

    /* DOS stub (just a branch to real entry) */
    .byte   0x0d, 0x0a              /* \r\n */
    .ascii  "This program cannot be run in DOS mode.\r\n"
    .byte   0x00

    .balign 8
pe_header:
    .ascii  "PE\0\0"                /* Signature */

    /* COFF Header */
    .short  0xAA64                  /* Machine: ARM64 */
    .short  1                       /* NumberOfSections */
    .long   0                       /* TimeDateStamp */
    .long   0                       /* PointerToSymbolTable */
    .long   0                       /* NumberOfSymbols */
    .short  section_table - optional_header  /* SizeOfOptionalHeader */
    .short  0x0202                  /* Characteristics: EXECUTABLE_IMAGE | LARGE_ADDRESS_AWARE */

optional_header:
    /* PE32+ Optional Header */
    .short  0x020B                  /* Magic: PE32+ */
    .byte   0                       /* MajorLinkerVersion */
    .byte   0                       /* MinorLinkerVersion */
    .long   __text_end - .head.text /* SizeOfCode */
    .long   0                       /* SizeOfInitializedData */
    .long   0                       /* SizeOfUninitializedData */
    .long   efi_entry - .head.text  /* AddressOfEntryPoint (RVA) */
    .long   .head.text - .head.text /* BaseOfCode (RVA) */

    /* PE32+ extra fields */
    .quad   0                       /* ImageBase (relocated by loader) */
    .long   4096                    /* SectionAlignment */
    .long   4096                    /* FileAlignment */
    .short  0                       /* MajorOSVersion */
    .short  0                       /* MinorOSVersion */
    .short  0                       /* MajorImageVersion */
    .short  0                       /* MinorImageVersion */
    .short  0                       /* MajorSubsystemVersion */
    .short  0                       /* MinorSubsystemVersion */
    .long   0                       /* Win32Version */
    .long   __text_end - .head.text /* SizeOfImage */
    .long   .head.text - .head.text /* SizeOfHeaders */
    .long   0                       /* CheckSum */
    .short  0x000A                  /* Subsystem: EFI_APPLICATION */
    .short  0                       /* DllCharacteristics */
    .quad   0                       /* SizeOfStackReserve */
    .quad   0                       /* SizeOfStackCommit */
    .quad   0                       /* SizeOfHeapReserve */
    .quad   0                       /* SizeOfHeapCommit */
    .long   0                       /* LoaderFlags */
    .long   2                       /* NumberOfRvaAndSizes */

    /* Data directories (none) */
    .skip   2*8, 0

section_table:
    .ascii  ".text\0\0\0"           /* Name */
    .long   __text_end - .head.text /* VirtualSize */
    .long   .head.text - .head.text /* VirtualAddress (RVA) */
    .long   __text_end - .head.text /* SizeOfRawData */
    .long   .head.text - .head.text /* PointerToRawData */
    .long   0                       /* PointerToRelocations */
    .long   0                       /* PointerToLinenumbers */
    .short  0                       /* NumberOfRelocations */
    .short  0                       /* NumberOfLinenumbers */
    .long   0xE0000020              /* Characteristics: CODE|EXECUTE|READ|CNT_CODE */

    .balign 4096

/* ============================================================
 * UEFI Entry Point
 *
 * Called by UEFI firmware with:
 *   x0 = ImageHandle
 *   x1 = SystemTable pointer
 *
 * We save the system table, gather hardware info, exit boot
 * services, relocate if needed, and jump to the real kernel.
 * ============================================================ */

    .globl efi_entry
    .type  efi_entry, %function
efi_entry:
    /* Save UEFI parameters */
    mov     x19, x0                 /* ImageHandle */
    mov     x20, x1                 /* SystemTable */

    /* Set up a temporary stack */
    adrp    x0, efi_stack_top
    add     x0, x0, :lo12:efi_stack_top
    mov     sp, x0

    /* Save link register */
    stp     x29, x30, [sp, #-16]!
    mov     x29, sp

    /* Call C EFI stub handler */
    mov     x0, x19                 /* ImageHandle */
    mov     x1, x20                 /* SystemTable */
    bl      efi_stub_main

    /* Should never return */
    b       .

/* ============================================================
 * EFI stub C code
 *
 * efi_stub_main runs in UEFI context.  It must:
 *   1. Get memory map
 *   2. Find ACPI RSDP
 *   3. Get GOP framebuffer
 *   4. Exit boot services
 *   5. Set up the real kernel environment
 *   6. Jump to _start
 * ============================================================ */

    .text
    .globl efi_stub_main
efi_stub_main:
    /* Just call the C implementation in efi_main.c */
    b       efi_stub_main_c

/* ============================================================
 * EFI stub data
 * ============================================================ */

    .data
    .balign 8
    .globl g_boot_info
g_boot_info:
    /* efi_boot_info_t structure: all zeros initially */
    .space  64, 0

    .bss
    .balign 4096
efi_stack_bottom:
    .space  65536
efi_stack_top:

/* ============================================================
 * Section end marker (for PE header SizeOfCode)
 * ============================================================ */

    .section .text.end, "ax"
    .globl __text_end
__text_end:
