---
name: arm64-multiprocess-launcher
description: Launch multiple EL0 processes from an ARM64 bare-metal kernel by re-entering kernel_main via SYS_EXIT eret redirect — chaining processes one after another without a scheduler.
source: auto-skill
extracted_at: '2026-07-05T02:43:53.211Z'
---

# ARM64 Multi-Process EL0 Launcher (Scheduler-Free)

## When to use
You need to run multiple EL0 userspace processes sequentially from a bare-metal
ARM64 kernel, without implementing a round-robin scheduler or preemption.
Each process runs to completion (SYS_EXIT), then the kernel automatically
launches the next process.

## Architecture

Two approaches:

### Approach A: for-loop with early increment (original)

Used when the init is done in boot.S and kernel_main is pure launching:

```
boot.S: kernel_main()
  └─ for(current_prog=0; current_prog<N; )
       current_prog++
       enter_el0(entry, stack)   // eret → EL0
       // SYS_EXIT → eret to el0_return_here → kernel_main continues loop
       // (next iteration with updated current_prog)
```

### Approach B: switch/case fall-through (cleaner for init+launch)

Used when kernel_main does init AND launching:

```
kernel_main(process_seq):
  switch(process_seq):
    case 0: init(); process_seq++;  // fall through to case 1
    case 1: launch proc 1; process_seq++; enter_el0();
    case 2: launch proc 2; process_seq++; enter_el0();
    case 3: launch proc 3; process_seq++; enter_el0();
    default: halt

  After proc 1 exits → el0_return_here calls kernel_main again
  process_seq is now 1 → case 2 runs → launch proc 2
```

## 1. Boot.S: provide the re-entry point

```asm
/* Called once from boot.S for the initial kernel entry.
 * Then called again from el0_return_here after each process exits. */
    .globl el0_return_here
el0_return_here:
    mov x0, x20            /* Pass DTB pointer */
    bl  kernel_main
```

## 2a. kernel_main: for-loop approach

```c
static int current_prog = 0;
static uint8_t stacks[2][4096] __attribute__((aligned(16)));

void kernel_main(uint64_t dtb_ptr) {
    (void)dtb_ptr;
    printk("Process Launcher\n");

    for (;;) {
        if (current_prog >= 2) {
            printk("All done. Halting.\n");
            break;
        }

        /* Increment BEFORE enter_el0 — eret never returns */
        current_prog++;

        uint64_t entry, stack;
        if (current_prog == 1) {
            entry = (uint64_t)utest_start;
            stack = (uint64_t)&stacks[0][sizeof(stacks[0])];
        } else {
            entry = (uint64_t)el0_prog2_start;
            stack = (uint64_t)&stacks[1][sizeof(stacks[1])];
        }

        enter_el0(entry, stack);    // never returns
    }
    for (;;) { __asm__ volatile("wfi"); }
}
```

## 2b. kernel_main: switch/case fall-through approach

```c
static int process_seq = 0;        // persistent across re-entries
static uint8_t stacks[4][4096];
extern int current_pid;             // visible to SYS_GETPID

void kernel_main(uint64_t dtb_ptr) {
    (void)dtb_ptr;

    switch (process_seq) {

    case 0:                         // Run once: init subsystems
        interrupt_init();
        ipc_init();
        process_seq++;
        /* fall through */

    case 1:                         // Launch process 1
        current_pid = process_seq;
        process_seq++;
        enter_el0((uint64_t)utest_start, (uint64_t)&stacks[0][4096]);
        break;                      // never reached

    case 2:
        current_pid = process_seq;
        process_seq++;
        enter_el0((uint64_t)el0_prog2_start, (uint64_t)&stacks[1][4096]);
        break;

    case 3:
        current_pid = process_seq;
        process_seq++;
        enter_el0((uint64_t)el0_shell_start, (uint64_t)&stacks[2][4096]);
        break;

    default:
        printk("All processes done. Halting.\n");
        for (;;) wfi;
    }
}
```

**Key insight**: case 0 does NOT use `break` after process_seq++. It falls
through to case 1, which launches the first process immediately on the first
call. `break` after case 0 would cause kernel_main to return without ever
entering EL0.

**PID management**: set `current_pid` (a global extern in syscall.c) before
each `enter_el0` call so SYS_GETPID returns the correct value. Increment
process_seq AFTER setting current_pid, so the PID matches the current process.

**Key design decision**: `current_prog` is incremented BEFORE `enter_el0`.
Since `enter_el0` does `eret` and never returns, the increment after the call
would be dead code. The SYS_EXIT handler calls `kernel_main` again via the
`el0_return_here` redirect, and the for-loop continues with the updated
`current_prog` value.

## 3. SYS_EXIT: redirect to kernel continuation

In the exception handler's SVC dispatch (exception.c):

```c
case 0x15:  /* SVC from AArch64 — system call */
    {
        uint64_t num = regs[8];
        int64_t ret = syscall_handler(num, regs[0], ...);
        regs[0] = (uint64_t)ret;

        /* SYS_EXIT(0): eret to kernel boot continuation */
        if (num == 0) {
            uint64_t tmp;
            __asm__ volatile(
                "msr elr_el1, %1\n\t"       // return to el0_return_here
                "mov %0, #0x3c5\n\t"         // EL1h, DAIF masked
                "msr spsr_el1, %0\n\t"
                : "=r"(tmp)
                : "r"(el0_return_here)
            );
        }
    }
    break;
```

- `0x3c5` = EL1h (AArch64), IRQ+FIQ masked — ensures the kernel returns to
  the boot continuation at EL1h with interrupts off
- `el0_return_here` is the label in boot.S after the inline EL0 entry

## 4. In boot.S, embed EL0 programs in a separate section

```asm
.section .rodata.utest, "a"
.align 2
.globl utest_start
utest_start:
    mov x8, #3          // SYS_WRITE
    mov x0, #1
    adr x1, 1f
    mov x2, #17
    svc #0
    mov x8, #0          // SYS_EXIT
    mov x0, #0
    svc #0
    b .
1:  .ascii "Hello from EL0!\n"
.globl utest_end
utest_end:

/* Second process */
.globl el0_prog2_start
el0_prog2_start:
    mov x8, #3
    mov x0, #1
    adr x1, 2f
    mov x2, #19
    svc #0
    mov x8, #0
    mov x0, #0
    svc #0
    b .
2:  .ascii "Process 2 is alive!\n"
.globl el0_prog2_end
el0_prog2_end:
```

## 5. Static variable ordering to prevent stack pollution

**Critical bug pattern**: The user stack top (`SP_EL0`) is set to
`&stacks[N][4096]`. If `current_prog` is declared AFTER `stacks` in C source,
`SP_EL0` for the last process points directly at `current_prog` — the first
stack frame overwrites the variable.

**Fix**: declare `current_prog` BEFORE the stack arrays:

```c
/* SAFE: current_prog sits below stacks in memory */
static int current_prog = 0;
static uint8_t stacks[2][4096];

/* UNSAFE: &stacks[1][4096] may equal &current_prog */
static uint8_t stacks[2][4096];
static int current_prog = 0;
```

Alternatively, add a padding buffer between the counter and the stacks:
```c
static int current_prog = 0;
static uint8_t pad[64];          // keeps SP_EL0 away from next variable
static uint8_t stacks[2][4096];
```

## 6. ARM64 EL0 program requirements

Each EL0 program:
- Must end with `SYS_EXIT` (or `b .` infinite loop)
- Cannot return from `svc #0` without the syscall being handled
- Uses x8 for syscall number, x0-x5 for arguments, returns value in x0
- Cannot use SP_EL0 stack without ensuring it doesn't overflow

The character count in SYS_WRITE includes the newline `\n` at the end.

## 7. Caveats

- This is a **cooperative, sequential** launcher — no preemption, no
  concurrency. Processes run one at a time, each to completion.
- The kernel stack (SP_EL1) persists across re-entries, so deep call chains
  may eventually overflow. Boot with more stack space or limit re-entries.
- `current_prog` is static — it survives across re-entrant calls to
  `kernel_main` because BSS is only cleared once at the very first boot.
