---
name: arm64-preemptive-scheduling
description: Convert an ARM64 bare-metal cooperative EL0 launcher to preemptive multitasking — exception-frame context switching, full 31-GPR save, timer IRQ preemption, resume_to_el0 trampoline, and scheduler integration.
source: auto-skill
extracted_at: '2026-07-10T10:47:10.781Z'
---

# ARM64 Preemptive Scheduling via Exception-Frame Context Switching

## When to use
Converting an ARM64 bare-metal microkernel from cooperative/sequential process
launching to true preemptive multitasking. The kernel already has basic EL0
entry, SVC syscall handling, and a timer; you need to wire them together so
the timer interrupt preempts the running process and switches to another.

## Architecture

```
kernel_main:
  init() → create_processes() → resume_to_el0(first_process)
    │
    ▼ (never returns — everything happens via exceptions)
    
Timer IRQ (100 Hz) during EL0 execution:
  CPU saves EL0 state → EL1 exception handler
    → handle_exception(IRQ)
      → handle_irq() → timer_handler() → timer_tick() → need_reschedule=1
      → exc_save_context(current_process, regs)   // save to PCB
      → schedule()                                  // pick next
      → exc_restore_context(next_process, regs)    // load into exception frame
    → eret → next process resumes at EL0

SYS_EXIT:
  CPU saves EL0 state → EL1 exception handler
    → handle_exception(SVC)
      → syscall_handler(SYS_EXIT)
      → mark process TERMINATED
      → schedule()
      → exc_restore_context(next, regs) or halt
    → eret → next process
```

## 1. Exception frame: save ALL 31 GPRs + sysregs

The original frame only saved x0-x18 — insufficient for preemption because
x19-x29 (callee-saved in AAPCS64) hold critical EL0 process state.

**Expanded frame** (boot.S, 296 bytes):

```
sp offset → register
  +0:   x0        +8:   x1       +16:  x2       +24:  x3
 +32:   x4       +40:   x5       +48:  x6       +56:  x7
 +64:   x8       +72:   x9       +80:  x10      +88:  x11
 +96:   x12     +104:   x13     +112:  x14     +120:  x15
+128:   x16     +136:   x17     +144:  x18     +152:  x19
+160:   x20     +168:   x21     +176:  x22     +184:  x23
+192:   x24     +200:   x25     +208:  x26     +216:  x27
+224:   x28     +232:   x29     +240:  x30 (LR)
+248:   ELR_EL1  +256: SPSR_EL1  +264: SP_EL0
+272:   ESR_EL1  +280: FAR_EL1
```

Registers x19-x29 are callee-saved — handle_exception (a C function) preserves
them, but the exception frame must save them because `exc_restore_context`
overwrites the CPU registers directly. Without saving x19-x29 in the frame,
the ERET at the end of exception_handler would restore the WRONG x19-x29
values (whatever handle_exception left in them).

**Assembly pattern** (boot.S):
```asm
exception_handler:
    sub  sp, sp, #296

    /* Save all 31 GPRs BEFORE any sysreg reads */
    stp  x0, x1,   [sp, #0]
    stp  x2, x3,   [sp, #16]
    /* ... x4-x29 ... */
    stp  x28, x29, [sp, #224]
    str  x30,      [sp, #240]

    /* Now safe to read system registers */
    mrs  x0, ELR_EL1
    mrs  x1, SPSR_EL1
    mrs  x2, SP_EL0
    mrs  x3, ESR_EL1
    mrs  x4, FAR_EL1
    stp  x0, x1, [sp, #248]   /* ELR_EL1, SPSR_EL1 */
    stp  x2, x3, [sp, #264]   /* SP_EL0, ESR_EL1 */
    str  x4,      [sp, #280]   /* FAR_EL1 */

    mov  x3, sp                /* regs frame pointer = sp */
    bl   handle_exception      /* handle_exception(esr, far, sp_el0, regs) */

    /* Restore sysregs FIRST (may have been modified by context switch) */
    ldp  x0, x1, [sp, #248]
    msr  ELR_EL1, x0
    msr  SPSR_EL1, x1
    ldr  x2,      [sp, #264]
    msr  SP_EL0, x2

    /* Restore all GPRs */
    ldp  x28, x29, [sp, #224]
    /* ... restore x26-x0 ... */
    ldp  x0, x1,   [sp, #0]
    add  sp, sp, #296
    eret
```

## 2. Context save/restore functions (exception.c)

These operate on the exception frame (regs array) and the PCB:

```c
/* regs[0..30] = x0..x30, regs[31]=ELR_EL1, regs[32]=SPSR_EL1, regs[33]=SP_EL0 */

static void exc_save_context(process_t *p, uint64_t *regs) {
    for (int i = 0; i < 31; i++) p->regs[i] = regs[i];
    p->elr_el1  = regs[31];
    p->spsr_el1 = regs[32];
    p->sp_el0   = regs[33];
}

static void exc_restore_context(process_t *p, uint64_t *regs) {
    for (int i = 0; i < 31; i++) regs[i] = p->regs[i];
    regs[31] = p->elr_el1;
    regs[32] = p->spsr_el1;
    regs[33] = p->sp_el0;
}
```

## 3. IRQ preemption path in handle_exception

```c
case 0x30:  /* IRQ from Lower EL */
case 0x31:  /* IRQ from EL1 */
    handle_irq();           // dispatches to timer_handler → timer_tick

    if (need_reschedule && current_process) {
        exc_save_context(current_process, regs);
        schedule();         // updates current_process
        if (current_process)
            exc_restore_context(current_process, regs);
        need_reschedule = 0;
    }
    break;
```

**Why it works**: `handle_irq()` acknowledges the GIC interrupt. Then we save
the current process's context into its PCB, call schedule() which picks the
next runnable process and sets `current_process`, then load the next process's
context into the exception frame. When `eret` fires, the CPU jumps to whatever
`regs[31]` (ELR_EL1) points to — which is now the NEXT process's saved
instruction pointer.

## 4. SYS_EXIT: full context switch, not kernel return

Old approach (cooperative): SYS_EXIT sets ELR_EL1 to a kernel label and returns
to the kernel loop. New approach (preemptive): SYS_EXIT marks the process dead,
calls schedule(), and loads the next process directly into the exception frame:

```c
case 0x15:  /* SVC */
    {
        uint64_t num = regs[8];
        int64_t ret = syscall_handler(num, regs[0], ... regs[5], regs);
        regs[0] = (uint64_t)ret;

        if (num == 0) {  /* SYS_EXIT */
            current_process->state = PROC_TERMINATED;
            schedule();
            if (current_process)
                exc_restore_context(current_process, regs);
            else
                panic("no runnable process");
        }
    }
    break;
```

No more `el0_return_here` redirect — the exception handler directly switches
to the next process.

## 5. resume_to_el0: first-time process launch from PCB

When the kernel launches the very first process, it must restore the full
context from the PCB and ERET to EL0. This is NOT done through enter_el0
(which only sets ELR_EL1 and SP_EL0) — it must restore x0-x30 too.

```asm
/*
 * resume_to_el0 - Full context restore from PCB, then ERET to EL0
 *   x0 = pointer to regs[0] (within process_t)
 * Context layout (x0 points to regs[0]):
 *   +0:    x0  ... +240:  x30
 *   +248:  ELR_EL1   +256:  SPSR_EL1   +264:  SP_EL0
 */
resume_to_el0:
    /* Restore SP_EL0 */
    ldr  x1, [x0, #264]
    msr  SPSel, #0
    mov  sp, x1

    /* Restore ELR_EL1, SPSR_EL1 */
    ldr  x1, [x0, #248]
    msr  ELR_EL1, x1
    ldr  x1, [x0, #256]
    msr  SPSR_EL1, x1

    /* Restore x1-x30 from regs[] using ldp pairs */
    ldp  x1, x2,   [x0, #8]
    ldp  x3, x4,   [x0, #24]
    /* ... x5-x30 ... */
    ldp  x29, x30, [x0, #232]

    /* x0 last (may contain entry/return value) */
    ldr  x0, [x0, #0]
    eret
```

**PCB field ordering matters**: `elr_el1`, `spsr_el1`, `sp_el0` MUST immediately
follow the `regs[31]` array in the struct, with no padding, so that
`resume_to_el0(&process->regs[0])` can index `regs[31]=ELR_EL1` at offset 248.

## 6. Process PCB additions

```c
typedef struct process {
    /* ... existing fields ... */
    uint64_t regs[31];     /* x0-x30 */
    uint64_t elr_el1;      /* MUST follow regs — resume_to_el0 depends on this */
    uint64_t spsr_el1;
    uint64_t sp_el0;
    /* ... */
} process_t;
```

## 7. SYS_EXEC: update both exception frame AND PCB

When `exec()` redirects to a new binary, it must update BOTH the exception
frame (for immediate ERET) AND the PCB (so future preemption saves the correct
state):

```c
case SYS_EXEC:
    load_raw_binary(path, &entry, &stack);
    /* Exception frame: takes effect immediately on ERET */
    regs[31] = entry;       /* ELR_EL1 */
    regs[32] = 0;           /* SPSR_EL1: EL0t */
    regs[33] = stack_top;   /* SP_EL0 */

    /* PCB: needed so future timer IRQ saves correct state */
    current_process->elr_el1  = entry;
    current_process->spsr_el1 = 0;
    current_process->sp_el0   = stack_top;
    current_process->entry    = entry;
    break;
```

## 8. Timer wiring

```c
void timer_handler(int irq_num) {
    jiffies++;
    timer_tick();              // increments current_process->ticks
    /* Reset CNTP_TVAL_EL0 for next 100Hz tick */
    __asm__ volatile("msr CNTP_TVAL_EL0, %0" : : "r"(timer_freq / 100));
}

void timer_tick(void) {
    if (current_process) {
        current_process->ticks++;
        if (current_process->ticks >= 10) {   // 100ms time slice
            current_process->ticks = 0;
            need_reschedule = 1;
        }
    }
}
```

## 9. kernel_main: launch-and-forget

```c
void kernel_main(uint64_t dtb_ptr) {
    /* Init all subsystems */
    interrupt_init(); timer_init(); ipc_init(); mm_init(); fs_init();
    timer_register_irq();    // register timer_handler for INTID 30

    /* Create first process */
    process_t *init = create_process(...);
    init->elr_el1  = init_entry;
    init->spsr_el1 = 0;     /* EL0t, interrupts unmasked */
    init->sp_el0   = stack_top;
    init->regs[0]  = init_entry;

    current_process = init;

    /* Jump to EL0 — never returns */
    resume_to_el0(init->regs);
    /* unreachable */
}
```

No more loop, no more `el0_return_here` — everything happens via exceptions.

## 10. GIC: enable Group 1 for timer PPI

The ARM Generic Timer uses PPI INTID 30, which is typically Group 1. The GIC
CPU interface must enable BOTH groups:

```c
/* Enable GIC CPU interface for both Group 0 and Group 1 */
gic_write(gicc + (GICC_CTLR / 4), 0x03);   // EnableGrp0 | EnableGrp1
```

## Common pitfalls

| Pitfall | Symptom | Fix |
|---------|---------|-----|
| x19-x29 not in exception frame | Resume after preemption uses wrong x19-x29 | Save all 31 GPRs in frame |
| ELR_EL1 not in frame | Can't redirect ERET | Add ELR_EL1 save/restore at offset 248 |
| SYS_EXIT returns to kernel loop | Kernel context is exception handler's, not scheduler's | Switch directly to next process in exception handler |
| SYS_EXEC updates only regs[], not PCB | After preemption, resumes OLD binary | Update both regs[] and PCB fields |
| PCB fields not contiguous after regs[] | resume_to_el0 reads wrong ELR/SPSR/SP | Place elr_el1/spsr_el1/sp_el0 immediately after regs[31] |
| Timer PPI doesn't fire | GICC_CTLR only enables Group 0 | Set GICC_CTLR = 0x03 (both groups) |
| DAIF masked at EL1 entry | Interrupts never fire | DAIF masking at EL1 is correct — interrupts unmask on ERET to EL0 via SPSR_EL1 |
