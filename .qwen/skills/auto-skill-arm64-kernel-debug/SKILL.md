---
name: arm64-kernel-debug
description: Systematically debug ARM64 bare-metal kernel boot failures using QEMU -d flags, monitor, and trace analysis — no GDB required. Covers boot.S bugs, MMU page tables (MAIR, block descriptors, memory types), linker addresses, and register clobbering.
source: auto-skill
extracted_at: '2026-07-05T02:17:50.276Z'
---

# ARM64 Bare-Metal Kernel Debugging with QEMU (No GDB)

## When to use
When an ARM64 kernel crashes, hangs, or produces no/wrong output during QEMU boot, and GDB is unavailable or impractical.

## The debugging stack

### 1. Get verbose execution trace
```bash
qemu-system-aarch64 -M virt -cpu cortex-a53 -m 512M \
  -kernel build/kernel.bin \
  -display none -serial file:/tmp/serial.out \
  -D qemu.log -d cpu,in_asm
```
- `-d cpu,in_asm`: logs every instruction + full CPU state (all 31 GPRs, SP, PSTATE)
- `-D qemu.log`: writes debug log to file (stderr → /dev/null if needed)
- `-serial file:`: captures serial output separately from debug noise
- `-display none`: headless mode

### 2. Check for exception patterns
```bash
grep 'Taking exception' qemu.log | head -5
```
Exception classes (from ESR_EL1 EC field):
- **0x00** (Unknown): undefined instruction — check CurrentEL checks, privileged register access
- **0x07** (FP/SIMD trap): compiler generated SIMD instructions — add `-mgeneral-regs-only` + set CPACR_EL1
- **0x21** (Prefetch Abort): instruction fetch from unmapped memory — MMU page tables don't cover kernel
- **0x25** (Data Abort): bad memory access — check stack pointer, literal pool addresses

### 3. Read exception context
```bash
grep -A5 'Taking exception' qemu.log | head -20
```
Key fields:
- **ELR**: address of the faulting instruction
- **FAR**: address that caused the abort
- **ESR**: exception class + syndrome
- **to EL1 PC**: exception handler address (if also faulting → handler page not mapped)

### 4. Check memory contents with QEMU monitor
```bash
echo 'x /16xb 0x40086000' | timeout 4 qemu-system-aarch64 \
  -M virt -cpu cortex-a53 -m 512M \
  -kernel build/kernel.elf \
  -monitor stdio -display none -S
```
- `-S`: freeze CPU at start (memory is pristine)
- `-monitor stdio`: interact via stdin
- Compare with `objdump -s -j .rodata` to verify data was loaded correctly

### 5. Verify disassembly at fault address
```bash
aarch64-linux-gnu-objdump -d build/kernel.elf | grep -A10 '<faulting-addr>:'
```

## Common ARM64 kernel boot bugs (checklist)

### boot.S
- **CurrentEL check**: EL2 = 8, EL1 = 4. `cmp x1, #8` for EL2 detection
- **Stack pointer never set**: `msr SPSEL, #1` only selects SP_EL1; need `mov sp, x1` after
- **Register clobbering across `bl`**: ARM64 caller-saved: x0-x18. If a helper (e.g. early_putchar) modifies x1-x3, the caller must use x4+ or callee-saved x19-x28 for loop variables
- **String output loop**: common pattern — string pointer in x1 gets overwritten by UART base address inside putchar

### linker.lds / Makefile
- **Link address must match QEMU load address**: aarch64 virt loads at `0x40080000` (DRAM start + 0x80000). Linking at `0x80000` creates a `0x40000000` offset mismatch
- **QEMU 8.2+ breaks `-bios none`**: newer QEMU treats "none" as a file path. Simply omit `-bios` entirely

### mmu.c
- **L1 page table entries are 1GB blocks, not 2MB**: the `pt_block_2mb()` function creates entries with bits[1:0]=01, which at L1 means 1GB granularity. Using i*2MB for phys addresses wastes entries and misses the kernel region
- **Identity-map at least 2GB**: L1[0] for 0x0-0x40000000, L1[1] for 0x40000000-0x80000000 to cover both peripherals and DRAM
- **MMIO should be Device memory (nGnRE)**, not Write-Back — works on QEMU but fails on real hardware
- **MAIR_EL1 must be configured** before MMU enable. Reset value is 0, which maps all AttrIndx to Device-nGnRnE. Without MAIR setup, even Normal WB pages are treated as strongly-ordered device memory — code fetch fails after MMU on
- **Block descriptor type bit (bit 1)**: must be 0 for block entries (0b01), NOT 0b11 (which means "table descriptor pointing to next level"). Setting bit1=1 silently breaks translation
- **AttrIndx encoding**: page table bits[4:2] select MAIR_EL1 attribute. Use indices 0-7. Typical: 0=Device-nGnRE (0x04), 3=Normal-WB-WA (0xFF)

```c
/* Configure MAIR_EL1 before loading TTBR0 */
uint64_t mair;
mair  = (0x04ULL << 0);   /* Attr0: Device-nGnRE */
mair |= (0xFFULL << 24);  /* Attr3: Normal WB-WA, Inner Shareable */
__asm__ volatile("msr mair_el1, %0" : : "r"(mair));
```

**Symptom**: kernel executes boot.S messages (pre-MMU, physical addresses via early UART) but produces no output after MMU enable — C code `printk` output never reaches serial. Root cause: MAIR_EL1=0 makes DRAM pages Device memory, preventing instruction fetch.

### GAS conditional assembly in .S files
**`#if 0` does NOT work in `.S` files**. GAS treats `#` as a comment character
(ARM assembly syntax). The assembler ignores `#if 0` and assembles the code
anyway.

**Wrong** (silently assembles the disabled block):
```asm
#if 0
    // This WILL be assembled!
    ldr x0, =utest_start
    eret
#endif
```

**Correct** (GAS directives):
```asm
    .if 0
    ldr x0, =utest_start
    eret
    .endif
```

Alternatively, use C preprocessor by running `.S` through gcc (`gcc -c` instead of `as`), but the Makefile usually uses `$(AS)` which is `as` directly.

### Makefile
- **`-mgeneral-regs-only`**: prevents compiler from using SIMD/FP registers in kernel code. Critical for -O2 or higher where the compiler inlines memcpy/memset with SIMD
- **Enable FP/SIMD in CPACR_EL1**: `mov x0, #(3<<20); msr CPACR_EL1, x0` — safety net if SIMD slips through

### boot.S
- **Switch statement with `break` after init**: If kernel_main uses a switch-case for sequential process launch, `break` after the init case causes the function to return without entering EL0. Use **fall-through** from case 0 → case 1:
  ```c
  switch (process_seq) {
  case 0:
      init(); process_seq++;
      /* fall through */
  case 1:
      launch_process();  // enter_el0 never returns
      break;
  ...
  }
  ```

## Trace interpretation flow
1. Find first exception → note ELR and FAR
2. Check if ELR is in `.text` (should be) and FAR is within mapped memory
3. If FAR == exception handler PC (0x40080300) → handler page not executable (MMU issue)
4. If exception repeats infinitely → handler itself faults → stack/MMU/memory issue
5. Check SP value: 0x0 or 0xFFFFFFFFXXXXXXXX → stack never initialized
6. Check X20-X28 values: if they contain ASCII chars (0x5B='[') instead of pointers → register clobbered by earlier call

### Debug: user stack corrupting kernel static variables
When EL0 processes use stack buffers declared as kernel static variables, the
stack top address (SP_EL0) may point directly AT the next static variable.

**How it happens**: If `static int counter;` is declared BEFORE `static uint8_t
stack[4096];` in C source, the compiler places `counter` FIRST, then `stack`.
The user stack starts at `&stack[4096]` — which is the address right after the
array. If `counter` is the next variable in memory, the first user stack frame
(or the exception handler's SP_EL0 use) overwrites `counter`.

**Fix**: declare the counter AFTER the stacks, ensuring SP_EL0 points past it,
or pad the arrays:
```c
/* BAD: counter may sit at &stack[4096], which is SP_EL0 for process N */
static int counter;
static uint8_t stack[2][4096];

/* GOOD: counter is before stacks, safe from SP_EL0 overflow */
static int counter;
/* pad ensures SP_EL0 points into padding, not at the next variable */
static uint8_t pad[64];
static uint8_t stack[2][4096];
```

Alternatively, always increment the process counter BEFORE eret to EL0, so
the counter update is durable even if eret never returns.

## QEMU version quirks
- **QEMU 8.2+**: `-bios none` is interpreted as a file path and fails with "Could not find ROM image 'none'". Simply omit `-bios` — the built-in aarch64 stub at 0x40000000 loads the kernel at 0x40080000.
