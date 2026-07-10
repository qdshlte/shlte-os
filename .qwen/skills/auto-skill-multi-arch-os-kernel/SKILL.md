---
name: multi-arch-os-kernel
description: Port an OS kernel to a new architecture — analyze boundaries, create arch tree, refactor shared code with #ifdef, write multi-arch Makefile
source: auto-skill
extracted_at: '2026-07-10T13:30:02.687Z'
---

# Multi-Architecture OS Kernel Porting

## When to apply
When adding a new CPU architecture to a freestanding OS kernel (e.g., adding x86_64 support to an ARM64 kernel).

## Approach

### Phase 1: Audit arch-specific boundaries
1. Catalog ALL files in the existing arch tree (`arch/<arch>/`)
2. Scan every "shared" file (lib/, headers, boot/) for:
   - Inline assembly (`__asm__ volatile`) — these are arch-specific
   - Hardcoded arch constants (memory addresses, register names)
   - Arch-specific instructions (`wfi`, `hlt`, `eret`)
   - Arch-specific comments naming the architecture
3. Classify each file as: **fully portable**, **has minor arch-specific bits**, or **must be rewritten**

**Why:** The biggest risk in porting is missing an arch-specific dependency buried in a "shared" file. A single `wfi` instruction in a panic handler will compile but crash at runtime on a different arch.

### Phase 2: Create parallel arch tree
Mirror the existing arch structure exactly:
```
arch/<new_arch>/
├── include/shlte/   # Arch-specific headers (mmu.h, io.h)
├── mm/              # MMU/page table setup
├── interrupt/       # Interrupt controller + exception handler
└── kernel.c         # Arch-specific entry point (kernel_main)
```

**Key files:**
- `mm/mmu.c` — page table format, register names, TLB/cache ops differ completely
- `interrupt/interrupt.c` — interrupt controller (GIC→PIC, or GIC→APIC)
- `interrupt/exception.c` — exception dispatch (ESR_EL1→vector+error_code, different EC values)
- `kernel.c` — idle loop instruction (`wfi`→`hlt`)

### Phase 3: Refactor shared code with #ifdef guards
Use `#if defined(__aarch64__)` / `#elif defined(__x86_64__)` for:

1. **Context switch assembly** — save/restore_context in process.c
2. **UART/serial driver** — memory-mapped (PL011) vs I/O port (16550) with completely different register layouts
3. **Halt/panic/syscall-exit loops** — `wfi` vs `hlt` vs empty spin
4. **UART_BASE address** — in headers, not only the value but the access method differs

**Pattern for UART when register layouts differ:**
```c
#if defined(__aarch64__)
// Full PL011 MMIO driver with uart_read/uart_write helpers
#elif defined(__x86_64__)
// Full 16550 I/O port driver with inb/outb helpers
#endif
```
Do NOT try to share code between radically different UART models; wrap the entire driver.

### Phase 4: Multi-arch Makefile
```makefile
ARCH ?= arm64
ifeq ($(ARCH), arm64)
    CROSS_COMPILE ?= aarch64-linux-gnu-
    QEMU := qemu-system-aarch64
    QEMU_FLAGS := -M virt -cpu cortex-a53 -m 512M -bios none
    LD_SCRIPT := linker.lds
    CFLAGS_ARCH := -march=armv8-a -mtune=cortex-a53
    KERNEL_LOAD := 0x80000
else ifeq ($(ARCH), x86_64)
    CROSS_COMPILE ?=
    QEMU := qemu-system-x86_64
    QEMU_FLAGS := -m 512M -serial stdio -nographic
    LD_SCRIPT := linker_x86_64.lds
    CFLAGS_ARCH := -mno-red-zone -mcmodel=kernel
    KERNEL_LOAD := 0x100000
endif
```

**Critical Makefile details:**
- Use `$(CC)` (not `$(AS)`) for `.S` files — native `as` may lack the C preprocessor that `.S` files need for `#define` directives
- Place objects in arch-prefixed subdirs (`build/arch/`, `build/lib/`, `build/boot/`) to avoid collisions
- Use wildcards for C source discovery but explicit file names for the single arch-specific `.S` boot file

### Phase 5: Debug build failures
Common cross-arch build failures and fixes:
- **`inb`/`outb` implicit declaration** — add `arch/<new_arch>/include/shlte/io.h` with static inline I/O helpers
- **Conflicting function signatures** — when the header declares `handle_irq(void)` but the new arch needs `handle_irq(uint64_t vector)`, update the header and add `(void)vector` to the old arch's implementation
- **Redefined macros** — `PAGE_SIZE`, UART register names: remove stutter definitions, keep only the canonical one in the shared header
- **Assembler `#define` not working** — `.S` files must go through `$(CC)` not `$(AS)` for C preprocessing

### Phase 6: Verify
```bash
make clean && make kernel                  # default arch, 0 warnings
make clean && make kernel ARCH=x86_64      # alternate arch, 0 warnings
```
Check for grep `warning\|error` — target literal zero on both.
