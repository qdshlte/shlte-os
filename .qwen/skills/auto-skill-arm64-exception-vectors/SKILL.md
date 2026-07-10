---
name: arm64-exception-vectors
description: ARM64 exception vector table construction — the 16-entry spacing requirement, .balign 0x80 pattern, SVC routing by source EL (EL1t vs EL1h), and conditional assembly for .S files (gas .if vs C #if).
source: auto-skill
extracted_at: '2026-07-05T02:37:12.279Z'
---

# ARM64 Exception Vector Table Construction

## When to use
Setting up VBAR_EL1 and the 16-entry exception vector table on ARM64. The
vector table has strict layout requirements that are easy to get wrong.

## The critical rule: 16 entries, 0x80 bytes apart

ARM64 requires 16 vector entries, each spaced exactly 0x80 (128) bytes apart.
The entire table must be aligned to 0x800 (2048) bytes:

| Offset | Exception type |
|--------|---------------|
| +0x000 | Synchronous EL1t (EL1 with SP_EL0) |
| +0x080 | IRQ EL1t |
| +0x100 | FIQ EL1t |
| +0x180 | Error EL1t |
| +0x200 | Synchronous EL1h (EL1 with SP_EL1) |
| +0x280 | IRQ EL1h |
| +0x300 | FIQ EL1h |
| +0x380 | Error EL1h |
| +0x400 | Synchronous EL0t (AArch64) |
| +0x480 | IRQ EL0t |
| +0x500 | FIQ EL0t |
| +0x580 | Error EL0t |
| +0x600 | Synchronous EL0h (AArch32) |
| +0x680 | IRQ EL0h |
| +0x700 | FIQ EL0h |
| +0x780 | Error EL0h |

### Wrong pattern (packed branches — EASY TO MISS)
```asm
.align 8
exception_vector_table:
    b   el1_sync       // +0x00 → branch here
    b   el1_irq        // +0x04 → NOT at +0x80!
    b   el1_fiq        // +0x08 → NOT at +0x100!
    b   el1_error      // +0x0C
    b   el0_sync       // +0x10 — at the EL1h sync slot!
    b   el0_irq        // +0x14
    b   el0_fiq        // +0x18
    b   el0_error      // +0x1C
```

This packs 8 branches at 4-byte intervals. The hardware reads VBAR+0x200 (the
EL1h sync slot) and finds whatever code happens to be at that address — NOT a
valid branch instruction. SVC from EL1 jumps to random code.

### Correct pattern (aligned + spaced with .balign)
```asm
    .balign 0x800          // <--- REQUIRED: tables must be 2KB-aligned
exception_vector_table:

    /* 0x000: Synchronous EL1t */
    b       el1_sync
    .balign 0x80
    /* 0x080: IRQ EL1t */
    b       el1_irq
    .balign 0x80
    /* 0x100: FIQ EL1t */
    b       el1_fiq
    .balign 0x80
    /* 0x180: Error EL1t */
    b       el1_error
    .balign 0x80

    /* 0x200: Synchronous EL1h */
    b       el1_sync          // same handler as EL1t sync
    .balign 0x80
    /* 0x280: IRQ EL1h */
    b       el1_irq
    .balign 0x80
    /* 0x300: FIQ EL1h */
    b       el1_fiq
    .balign 0x80
    /* 0x380: Error EL1h */
    b       el1_error
    .balign 0x80

    /* 0x400: Synchronous EL0t (AArch64) */
    b       el0_sync
    .balign 0x80
    /* 0x480: IRQ EL0t */
    b       el0_irq
    .balign 0x80
    /* 0x500: FIQ EL0t */
    b       el0_fiq
    .balign 0x80
    /* 0x580: Error EL0t */
    b       el0_error
    .balign 0x80

    /* 0x600-0x780: AArch32 slots — unused, infinite loop */
    b       .
    .balign 0x80
    b       .
    .balign 0x80
    b       .
    .balign 0x80
    b       .
    .balign 0x80
```

## Why EL1h slots matter

When an exception is taken from EL1 (regardless of EL1t or EL1h), the target
mode is always **EL1h** (using SP_EL1). The vector entry used depends on the
**target** stack pointer, not the source.

A `svc #0` executed at EL1 (any mode) uses **VBAR + 0x200** (EL1h sync).
If that slot contains garbage, the CPU jumps to arbitrary code.

This is why SVC at EL1 was silently failing in our kernel — the EL1h slots
weren't populated because we only had 8 entries.

## Where the slot goes — verifying with objdump

```bash
aarch64-linux-gnu-objdump -t build/kernel.elf | grep exception_vector
# → 0000000040080800  exception_vector_table

# VBAR + 0x200 = 0x40080A00 — check what's there:
aarch64-linux-gnu-objdump -d build/kernel.elf | grep -A1 '40080a00:'
```

With `.balign 0x80`, each branch sits exactly at its expected offset relative
to the table base.

## Gas conditional assembly for .S files

**`.S` files are assembled with `as`, NOT `gcc`**. C preprocessor directives
like `#if 0` are treated as comments (since `#` starts a comment in ARM assembly).

Use gas directives instead:
```asm
    .if 1        // enable
    ldr x0, =utest_start
    ...
    eret
    .endif

    .if 0        // disable
    ...
    .endif
```

## SYS_EXIT: redirecting eret back to kernel

When userspace calls `SYS_EXIT`, the eret should return to kernel code, not
back to userspace. In the C exception handler, after syscall_handler returns:

```c
case 0x15:  /* SVC */
    {
        uint64_t num = regs[8];
        int64_t ret = syscall_handler(num, regs[0], ...);
        regs[0] = (uint64_t)ret;

        /* SYS_EXIT(0): redirect eret to kernel boot continuation */
        if (num == 0) {
            __asm__ volatile(
                "msr elr_el1, %1\n\t"
                "mov %0, #0x3c5\n\t"       // EL1h, DAIF masked
                "msr spsr_el1, %0\n\t"
                : "=r"(tmp)
                : "r"(el0_return_here)
            );
        }
    }
    break;
```

The boot.S must provide the label:
```asm
    eret                    /* enter EL0 */
    .globl el0_return_here
el0_return_here:            /* kernel resumes here after SYS_EXIT */
    bl mmu_enable
    bl kernel_main
```
