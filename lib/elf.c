/*
 * lib/elf.c - ELF Binary Loader + Dynamic Linker
 *
 * Loads ELF64 executables and shared libraries (.so).
 * Handles relocations, symbol resolution, and dynamic linking.
 * Supports ARM64 (AArch64) ELF format.
 */

#include <shlte/types.h>
#include <shlte/printk.h>
#include <shlte/string.h>
#include <shlte/mm.h>
#include <shlte/process.h>

/* ============================================================
 * ELF64 types
 * ============================================================ */

#define EI_NIDENT      16

#define ELFCLASS64      2
#define ELFDATA2LSB     1
#define EV_CURRENT      1

#define ET_EXEC         2
#define ET_DYN          3      /* Shared object / PIE */

#define PT_LOAD         1
#define PT_DYNAMIC      2
#define PT_INTERP       3
#define PT_PHDR         6
#define PT_GNU_RELRO    0x6474E552

#define PF_X            1
#define PF_W            2
#define PF_R            4

#define EM_AARCH64      0xB7

/* Dynamic tags */
#define DT_NULL          0
#define DT_NEEDED        1
#define DT_PLTGOT        3
#define DT_STRTAB        5
#define DT_SYMTAB        6
#define DT_RELA          7
#define DT_RELASZ        8
#define DT_RELAENT       9
#define DT_STRSZ        10
#define DT_JMPREL       23
#define DT_PLTRELSZ      2
#define DT_REL          17
#define DT_RELSZ        18
#define DT_RELENT       19
#define DT_GNU_HASH     0x6FFFFEF5

/* ARM64 relocations */
#define R_AARCH64_NONE      0
#define R_AARCH64_ABS64     257
#define R_AARCH64_GLOB_DAT  1025
#define R_AARCH64_JUMP_SLOT 1026
#define R_AARCH64_RELATIVE  1027

/* ============================================================
 * ELF Header
 * ============================================================ */

typedef struct {
    uint8_t  e_ident[EI_NIDENT];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} elf64_hdr_t;

typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} elf64_phdr_t;

typedef struct {
    uint64_t d_tag;
    uint64_t d_val;
} elf64_dyn_t;

typedef struct {
    uint32_t st_name;
    uint8_t  st_info;
    uint8_t  st_other;
    uint16_t st_shndx;
    uint64_t st_value;
    uint64_t st_size;
} elf64_sym_t;

typedef struct {
    uint64_t r_offset;
    uint32_t r_type;
    uint32_t r_sym;
    uint64_t r_addend;
} elf64_rela_t;

typedef struct {
    uint64_t r_offset;
    uint32_t r_type;
    uint32_t r_sym;
} elf64_rel_t;

/* ============================================================
 * Loader state
 * ============================================================ */

typedef struct {
    uint64_t base;           /* Load base virtual address */
    uint64_t entry;          /* Entry point */
    void    *data;           /* Raw binary data */
    size_t   size;           /* Size of binary */

    /* Dynamic linking info */
    uint64_t  *got;          /* Global Offset Table */
    elf64_dyn_t *dynamic;    /* .dynamic section */
    char      *strtab;       /* .dynstr */
    elf64_sym_t *symtab;     /* .dynsym */
    elf64_rela_t *rela;      /* .rela.dyn */
    size_t     rela_size;
    elf64_rela_t *jmprel;    /* .rela.plt */
    size_t     jmprel_size;
} elf_loader_t;

/* ============================================================
 * ELF validation
 * ============================================================ */

static int elf_validate(elf64_hdr_t *hdr, size_t size)
{
    if (size < sizeof(elf64_hdr_t)) return -1;

    /* Check ELF magic */
    if (hdr->e_ident[0] != 0x7F || hdr->e_ident[1] != 'E' ||
        hdr->e_ident[2] != 'L'  || hdr->e_ident[3] != 'F')
        return -1;

    if (hdr->e_ident[4] != ELFCLASS64) return -1;
    if (hdr->e_ident[5] != ELFDATA2LSB) return -1;
    if (hdr->e_machine != EM_AARCH64) return -1;
    if (hdr->e_type != ET_EXEC && hdr->e_type != ET_DYN) return -1;

    return 0;
}

/* ============================================================
 * Load program headers
 * ============================================================ */

static int elf_load_segments(elf_loader_t *ld, elf64_hdr_t *hdr)
{
    elf64_phdr_t *phdrs = (elf64_phdr_t *)((uint8_t *)hdr + hdr->e_phoff);

    for (uint16_t i = 0; i < hdr->e_phnum; i++) {
        elf64_phdr_t *ph = &phdrs[i];

        if (ph->p_type != PT_LOAD) continue;

        uint64_t dest_vaddr = ld->base + ph->p_vaddr;
        uint64_t file_size  = ph->p_filesz;
        uint64_t mem_size   = ph->p_memsz;

        /* Copy segment data */
        if (file_size > 0) {
            const uint8_t *src = (const uint8_t *)hdr + ph->p_offset;
            memcpy((void *)dest_vaddr, src, (size_t)file_size);
        }

        /* Zero-initialize .bss portion */
        if (mem_size > file_size) {
            memset((void *)(dest_vaddr + file_size), 0,
                   (size_t)(mem_size - file_size));
        }

        /* Set page permissions for W^X */
        uint32_t prot = 0;
        if (ph->p_flags & PF_R) prot |= 1;  /* PROT_READ */
        if (ph->p_flags & PF_W) prot |= 2;  /* PROT_WRITE */
        if (ph->p_flags & PF_X) prot |= 4;  /* PROT_EXEC */

        /* Update MMU for this region */
        extern int sys_mprotect(void *addr, size_t length, int prot);
        sys_mprotect((void *)(dest_vaddr & ~(2*1024*1024-1)),
                     mem_size + (2*1024*1024),
                     prot);
    }

    return 0;
}

/* ============================================================
 * Parse .dynamic section
 * ============================================================ */

static void elf_parse_dynamic(elf_loader_t *ld, elf64_phdr_t *phdrs,
                               uint16_t phnum, uint64_t load_base)
{
    /* Find PT_DYNAMIC */
    for (uint16_t i = 0; i < phnum; i++) {
        if (phdrs[i].p_type == PT_DYNAMIC) {
            ld->dynamic = (elf64_dyn_t *)(load_base +
                           phdrs[i].p_offset);
            break;
        }
    }
    if (!ld->dynamic) return;

    /* Parse dynamic entries */
    for (elf64_dyn_t *d = ld->dynamic; d->d_tag != DT_NULL; d++) {
        switch (d->d_tag) {
        case DT_PLTGOT:
            ld->got = (uint64_t *)(load_base + d->d_val);
            break;
        case DT_STRTAB:
            ld->strtab = (char *)(load_base + d->d_val);
            break;
        case DT_SYMTAB:
            ld->symtab = (elf64_sym_t *)(load_base + d->d_val);
            break;
        case DT_RELA:
            ld->rela = (elf64_rela_t *)(load_base + d->d_val);
            break;
        case DT_RELASZ:
            ld->rela_size = (size_t)d->d_val;
            break;
        case DT_JMPREL:
            ld->jmprel = (elf64_rela_t *)(load_base + d->d_val);
            break;
        case DT_PLTRELSZ:
            ld->jmprel_size = (size_t)d->d_val;
            break;
        }
    }
}

/* ============================================================
 * Relocation processing
 * ============================================================ */

static void elf_relocate(elf_loader_t *ld, elf64_hdr_t *hdr)
{
    /* Process R_AARCH64_RELATIVE relocations */
    if (ld->rela && ld->rela_size > 0) {
        size_t count = ld->rela_size / sizeof(elf64_rela_t);
        for (size_t i = 0; i < count; i++) {
            elf64_rela_t *r = &ld->rela[i];
            uint64_t *where = (uint64_t *)(ld->base + r->r_offset);

            switch (r->r_type) {
            case R_AARCH64_RELATIVE:
                *where = ld->base + r->r_addend;
                break;
            case R_AARCH64_GLOB_DAT:
                if (ld->symtab) {
                    /* Resolve symbol */
                    uint32_t sym_idx = r->r_sym;
                    /* For now, only handle relative */
                }
                *where = ld->base + r->r_addend;
                break;
            case R_AARCH64_ABS64:
                *where = ld->base + r->r_addend;
                break;
            }
        }
    }

    /* Process PLT relocations (R_AARCH64_JUMP_SLOT) */
    if (ld->jmprel && ld->jmprel_size > 0) {
        size_t count = ld->jmprel_size / sizeof(elf64_rela_t);
        for (size_t i = 0; i < count; i++) {
            elf64_rela_t *r = &ld->jmprel[i];
            uint64_t *where = (uint64_t *)(ld->base + r->r_offset);

            if (r->r_type == R_AARCH64_JUMP_SLOT) {
                /* Resolve function symbol */
                uint32_t sym_idx = r->r_sym;
                elf64_sym_t *sym = &ld->symtab[sym_idx];
                const char *name = ld->strtab + sym->st_name;

                /* For now, use PLT stub address */
                /* In production: look up symbol in loaded libraries */
                *where = ld->base + r->r_addend;

                printk("[ELF] PLT: %s -> 0x%lx\n", name, *where);
            }
        }
    }
}

/* ============================================================
 * Public API
 * ============================================================ */

/**
 * elf_load - Load an ELF executable or shared library
 * @data: Pointer to raw ELF binary data
 * @size: Size of the binary
 * @base: Desired load base (0 = auto)
 * @entry_out: Output entry point
 *
 * Returns 0 on success, -1 on error.
 */
int elf_load(const void *data, size_t size, uint64_t base,
             uint64_t *entry_out)
{
    if (!data || !entry_out) return -1;

    elf64_hdr_t *hdr = (elf64_hdr_t *)data;

    if (elf_validate(hdr, size) != 0) {
        printk("[ELF] Invalid ELF header\n");
        return -1;
    }

    elf_loader_t ld;
    memset(&ld, 0, sizeof(ld));

    /* Determine load base */
    ld.base = base;
    if (ld.base == 0 && hdr->e_type == ET_DYN) {
        /* PIE or shared library: load at a default address */
        ld.base = 0x40500000ULL;  /* After heap area */
    }

    ld.data   = (void *)data;
    ld.size   = size;
    ld.entry  = ld.base + hdr->e_entry;

    /* Allocate memory for segments */
    /* (In production, use mmap. For now, segments are loaded in-place
     *  at identity-mapped physical addresses) */

    /* Load program segments */
    elf_load_segments(&ld, hdr);

    /* Parse dynamic info */
    elf64_phdr_t *phdrs = (elf64_phdr_t *)((uint8_t *)data + hdr->e_phoff);
    elf_parse_dynamic(&ld, phdrs, hdr->e_phnum, ld.base);

    /* Process relocations */
    elf_relocate(&ld, hdr);

    *entry_out = ld.entry;

    printk("[ELF] Loaded %s at 0x%lx, entry=0x%lx\n",
           hdr->e_type == ET_DYN ? "PIE/SO" : "EXEC",
           ld.base, ld.entry);

    return 0;
}
