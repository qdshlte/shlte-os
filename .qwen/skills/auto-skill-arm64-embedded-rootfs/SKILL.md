---
name: arm64-embedded-rootfs
description: Build an embedded rootfs for ARM64 bare-metal kernels — compile EL0 user programs as raw binaries, convert to ELF sections with objcopy -I binary, link into kernel, load into writable memory, and jump to EL0.
source: auto-skill
extracted_at: '2026-07-05T04:58:15.898Z'
---

# ARM64 Embedded Rootfs for Bare-Metal Kernels

## When to use
You need to ship user-space (EL0) programs with an ARM64 bare-metal kernel without a disk or filesystem driver. The programs are compiled as raw binaries and linked directly into the kernel image.

## Pipeline overview

```
user/prog.S  →  aarch64-linux-gnu-as  →  ELF .o
ELF .o       →  ld -Ttext=0 --oformat=binary  →  raw .bin (position-independent)
raw .bin     →  objcopy -I binary -O elf64-littleaarch64  →  embeddable .o with .data section
embeddable .o →  objcopy --rename-section .data=.rootfs  →  section renamed
.rootfs .o   →  linker script  →  linked into kernel at a known address
```

## Step 1: Compile user program as raw binary

```makefile
# Assemble to ELF relocatable
build/user_prog.o: user/prog.S
    aarch64-linux-gnu-as -march=armv8-a -o $@ $<

# Link as raw binary (no ELF headers, no relocations, entry at offset 0)
build/prog.bin: build/user_prog.o
    aarch64-linux-gnu-ld -Ttext=0 -o build/prog_temp.elf $<
    aarch64-linux-gnu-objcopy -O binary build/prog_temp.elf $@
    rm -f build/prog_temp.elf
```

The raw binary MUST be position-independent (use `adr` for all data references, no absolute addresses). The first byte of the binary is the entry point.

## Step 2: Embed binary into the kernel

Convert the raw binary to an ELF object with a custom section name:

```makefile
build/prog_embedded.o: build/prog.bin
    # First: convert binary to ELF .data section
    aarch64-linux-gnu-objcopy -I binary -O elf64-littleaarch64 -B aarch64 $< $@
    # Second: rename .data to .rootfs (IMPORTANT: two steps, not one --rename-section)
    aarch64-linux-gnu-objcopy --rename-section .data=.rootfs,contents,alloc,load,readonly,data $@
```

**Important**: Do NOT combine `-I binary` and `--rename-section` in a single objcopy invocation — it produces all-zeros in the output section. Always use two separate objcopy calls.

The output object defines symbols:
- `_binary_<path_with_slashes_replaced_by_underscores>_start`
- `_binary_<path_with_slashes_replaced_by_underscores>_end`
- `_binary_<path_with_slashes_replaced_by_underscores>_size`

## Step 3: Linker script section

```lds
SECTIONS {
    . = 0x40080000;
    /* ... other sections ... */

    .rodata : {
        *(.rodata*)
    } :rodata

    .rootfs : {
        __rootfs_start = .;
        *(.rootfs*)
        __rootfs_end = .;
    } :rodata

    .data : { ... } :data
}
```

## Step 4: Loader function

The loader copies the embedded binary from read-only kernel memory to a writable address (since EL0 may need to modify data, and .rodata is read-only at the page-table level):

```c
extern char _binary_build_prog_bin_start[];
extern char _binary_build_prog_bin_end[];

int load_embedded(const char *name, uint64_t *entry_out, void **stack_out) {
    size_t sz = (size_t)(_binary_build_prog_bin_end - _binary_build_prog_bin_start);
    if (sz == 0) return -1;

    // Allocate or use a fixed writable address
    uint64_t load_addr = 0x40200000UL;      // must be in Normal-WriteBack mapped RAM
    uint64_t stack_addr = load_addr + 0x10000UL;

    memcpy((void *)load_addr, _binary_build_prog_bin_start, sz);

    *entry_out = load_addr;
    *stack_out = (void *)stack_addr;
    return 0;
}
```

## Step 5: Jump to EL0

```c
extern void enter_el0(uint64_t entry, uint64_t stack_top);

uint64_t entry;
void *stack;
if (load_embedded("init", &entry, &stack) == 0) {
    enter_el0(entry, (uint64_t)stack + USER_STACK_SIZE);
}
```

The `enter_el0` trampoline (assembly):
```asm
enter_el0:
    msr  SPSel, #0
    mov  sp, x1           // user stack top
    msr  ELR_EL1, x0     // entry point
    msr  SPSR_EL1, xzr   // EL0t, all exceptions unmasked
    eret
```

## Gotchas

| Gotcha | Symptom | Fix |
|--------|---------|-----|
| `objcopy --rename-section` in one step | .rootfs section is all zeros | Split into two objcopy calls |
| Running from .rodata directly | Unknown exception (EC=0x00, undefined instruction) | Copy to writable memory first |
| Load address overlaps kernel/page tables | MMU breaks, unknown exceptions | Pick address in Normal memory above all kernel sections (e.g., 0x40200000 for a kernel at 0x40080000) |
| Binary not position-independent | Data access faults at different address | Use `adr` not absolute addresses; compile with `-Ttext=0` |
| `svc #0` causes "Unknown exception" not SVC | Loading garbage or PXN/UXN bit set | Verify binary content with `objdump -D -b binary -m aarch64` |
