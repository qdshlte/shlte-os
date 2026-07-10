---
name: arm64-el0-userspace
description: ARM64 bare-metal kernel EL0 userspace infrastructure — exception frame design, SVC syscall routing, enter_el0 trampoline, page table AP bits for user access, and ARM64 syscall ABI convention (x8=num).
source: auto-skill
extracted_at: '2026-07-05T02:17:50.276Z'
---

# ARM64 EL0 Userspace Infrastructure for Bare-Metal Kernels

## When to use
Adding EL0 (userspace) support to an ARM64 bare-metal kernel: setting up the
syscall path from `svc #0` through the exception handler to a C dispatcher,
returning values to userspace, and entering EL0 for the first time via `eret`.

## Architecture overview

```
EL0 code:      svc #0 ─────────────────────────────┐
                                                    │ (trap to EL1)
EL1 exception: VBAR_EL1[EL0t_sync]                 │
                  → exception_handler (save regs)    │
                  → C: handle_exception(ESR,regs)   │
                  → C: syscall_handler(num,args)    │
                  → write ret to regs[0]            │
                  → eret (back to EL0)              │
```

## 1. Exception handler: save registers BEFORE reading system registers

**The critical bug pattern**: many ARM64 kernel tutorials read ESR_EL1/FAR_EL1
into x0/x1/x2 BEFORE saving the original x0/x1/x2 values. This loses the
userspace registers forever.

**Wrong pattern** (from many tutorials):
```asm
el0_sync:
    mrs x0, ESR_EL1        // clobbers userspace x0!
    mrs x1, FAR_EL1        // clobbers userspace x1!
    mrs x2, SP_EL0         // clobbers userspace x2!
    b   exception_handler
```

**Correct pattern**: vector entries just branch — all saving and sysreg reads
happen in the common handler AFTER preserving original registers:
```asm
el0_sync:
    b   exception_handler   // no mrs here!

exception_handler:
    sub sp, sp, #192        // 160 for regs + 32 for args
    // Save ALL original registers FIRST
    stp x0, x1,   [sp, #0]
    stp x2, x3,   [sp, #16]
    stp x4, x5,   [sp, #32]
    stp x6, x7,   [sp, #48]
    stp x8, x9,   [sp, #64]
    stp x10, x11, [sp, #80]
    stp x12, x13, [sp, #96]
    stp x14, x15, [sp, #112]
    stp x16, x17, [sp, #128]
    stp x18, x30, [sp, #144]

    // NOW safe to read system registers
    mrs x0, ESR_EL1
    mrs x1, FAR_EL1
    mrs x2, SP_EL0
    stp x0, x1, [sp, #160]   // pass ESR, FAR as C args
    stp x2, xzr, [sp, #176]

    // x3 = pointer to saved register frame (so C can modify x0 for return)
    mov x3, sp
    bl  handle_exception     // void handle_exception(esr, far, sp_el0, *regs)

    // Restore ALL registers (x0 may have been modified for syscall return)
    ldp x18, x30, [sp, #144]
    ldp x16, x17, [sp, #128]
    ldp x14, x15, [sp, #112]
    ldp x12, x13, [sp, #96]
    ldp x10, x11, [sp, #80]
    ldp x8, x9,   [sp, #64]
    ldp x6, x7,   [sp, #48]
    ldp x4, x5,   [sp, #32]
    ldp x2, x3,   [sp, #16]
    ldp x0, x1,   [sp, #0]
    add sp, sp, #192
    eret
```

**Register frame layout** (sp offset → register):
```
  +0  → x0      +8  → x1      +16 → x2      +24 → x3
 +32  → x4     +40  → x5      +48 → x6      +56 → x7
 +64  → x8     +72  → x9      +80 → x10     +88 → x11
 +96  → x12   +104  → x13    +112 → x14    +120 → x15
+128  → x16   +136  → x17    +144 → x18    +152 → x30 (LR)
+160  → ESR   +168  → FAR    +176 → SP_EL0
```

## 2. C exception handler: route SVC to syscall dispatcher

```c
void handle_exception(uint64_t esr, uint64_t far, uint64_t sp_el0, uint64_t *regs)
{
    uint64_t ec = (esr >> 26) & 0x3F;

    switch (ec) {
    case 0x15:  /* SVC from AArch64 */
        {
            // ARM64 syscall convention: x8 = number, x0-x5 = args
            uint64_t num = regs[8];
            int64_t ret = syscall_handler(num, regs[0], regs[1],
                                           regs[2], regs[3], regs[4], regs[5]);
            regs[0] = (uint64_t)ret;  // return value → userspace x0
        }
        break;

    case 0x24:  // Data Abort from EL0
    case 0x25:  // Data Abort from EL1
        // ... fault handling
        break;

    case 0x30:  // IRQ from EL0
    case 0x31:  // IRQ from EL1
        handle_irq();
        break;
    }
}
```

Key: `regs[0] = ret` writes the syscall return value into the saved x0 on the
stack. The assembly restore path picks it up and `eret` returns it to userspace.

## 3. syscall_handler signature

```c
int64_t syscall_handler(uint64_t syscall_num,
                        uint64_t arg1, uint64_t arg2, uint64_t arg3,
                        uint64_t arg4, uint64_t arg5, uint64_t arg6);
```

DO NOT use `void` return type — the exception handler writes the return value
to `regs[0]` and `eret` delivers it to userspace x0.

## 4. EL0 entry trampoline

```asm
/*
 * enter_el0 - Drop to EL0 userspace (never returns)
 *   x0 = entry point (physical/virtual address)
 *   x1 = user stack top
 */
enter_el0:
    msr  SPSel, #0      // Select SP_EL0 for user stack
    mov  sp, x1         // Set user stack pointer
    msr  ELR_EL1, x0    // Exception Link Register = entry point
    msr  SPSR_EL1, xzr  // EL0t, AArch64, all exceptions unmasked
    eret                // → EL0!
```

SPSR_EL1 = 0 means:
- M[3:0] = 0b0000 → EL0t (uses SP_EL0)
- M[4] = 0 → AArch64 execution state
- DAIF = 0b0000 → all exceptions/interrupts unmasked

## 5. Page table permissions for EL0 access

To allow EL0 to execute code and access data, page table entries must set
AP[2:1] bits:

| AP[2:1] | EL1 access | EL0 access |
|---------|-----------|-----------|
| 0b00    | read-write | no access |
| 0b01    | read-write | read-write |
| 0b10    | read-only  | no access |
| 0b11    | read-only  | read-only  |

Add `(1ULL << 6)` to block descriptor to set AP[1]=1 for EL0 access:
```c
entry |= (1ULL << 0)  |  // valid
         (1ULL << 10) |  // AF (access flag)
         (3ULL << 8)  |  // SH = inner shareable
         (1ULL << 6);    // AP[1] = EL0 access enabled
```

## 6. ARM64 syscall ABI convention

| Register | Role |
|----------|------|
| x8       | syscall number |
| x0-x5    | arguments (up to 6) |
| x0       | return value (written by kernel to regs[0]) |
| svc #0   | trigger exception |

Userspace example:
```asm
    mov x8, #3          // SYS_WRITE
    mov x0, #1          // fd = stdout
    adr x1, msg         // buffer
    mov x2, #13         // length
    svc #0              // → exception → syscall_handler(3, 1, buf, 13)
    // x0 now contains return value
```

## 7. Common pitfalls

- **SVC at EL1 instead of EL0**: if `eret` fails to lower exception level,
  `svc #0` triggers `el1_sync` instead of `el0_sync`. Check SPSR_EL1 value.
- **ELR_EL1 loaded with wrong value**: verify `ldr x0, =entry_point` loads
  the correct address from the literal pool (check with `objdump -s`).
- **Data abort on first userspace instruction**: page table AP bits not set
  for EL0 access, or MAIR_EL1 not configured (makes all memory Device).
- **syscall return value lost**: `regs[0] = ret` must happen BEFORE the
  assembly restore in `exception_handler`.
