/*
 * process.c - Minimal process management with full context switch
 */

#include <shlte/types.h>
#include <shlte/printk.h>
#include <shlte/string.h>
#include <shlte/mm.h>
#include <shlte/process.h>

/* ============================================================
 * Global state
 * ============================================================ */

/* Global process table */
static process_t process_table[MAX_PROCESSES];
static process_t *current_process = NULL;
static int next_pid = 1;

/* ============================================================
 * ARM64 context switch implementation
 * ============================================================ */

/**
 * ARM64 register save/restore for context_switch.
 *
 * On ARM64, the exception frame when entering EL1 from EL0 looks like:
 * - ELR_EL1: Return address (where to ERET to)
 * - SPSR_EL1: Saved processor state
 * - x0-x30: General purpose registers
 *
 * Our PCB stores registers in p->regs[30]:
 *   regs[0]  .. regs[29]  = x0 .. x29
 *   regs[30] = x30 (LR)
 *
 * The stack pointer for the process is stored in p->user_stack.
 */

/**
 * save_context - Save current CPU state to process PCB
 * @p: The process whose state should be saved
 *
 * Uses inline assembly to save all 30 general-purpose registers
 * (x0-x29) plus the link register (x30/LR) to the PCB.
 */
static void save_context(process_t *p)
{
    if (!p) return;

    /* Save general purpose registers x0-x29 and LR (x30) */
    __asm__ volatile (
        /* Save x0-x7 */
        "str x0, [%0, #0]\n"
        "str x1, [%0, #8]\n"
        "str x2, [%0, #16]\n"
        "str x3, [%0, #24]\n"
        "str x4, [%0, #32]\n"
        "str x5, [%0, #40]\n"
        "str x6, [%0, #48]\n"
        "str x7, [%0, #56]\n"
        /* Save x8-x15 */
        "str x8, [%0, #64]\n"
        "str x9, [%0, #72]\n"
        "str x10, [%0, #80]\n"
        "str x11, [%0, #88]\n"
        "str x12, [%0, #96]\n"
        "str x13, [%0, #104]\n"
        "str x14, [%0, #112]\n"
        "str x15, [%0, #120]\n"
        /* Save x16-x23 */
        "str x16, [%0, #128]\n"
        "str x17, [%0, #136]\n"
        "str x18, [%0, #144]\n"
        "str x19, [%0, #152]\n"
        "str x20, [%0, #160]\n"
        "str x21, [%0, #168]\n"
        "str x22, [%0, #176]\n"
        "str x23, [%0, #184]\n"
        /* Save x24-x29 and LR(x30) */
        "str x24, [%0, #192]\n"
        "str x25, [%0, #200]\n"
        "str x26, [%0, #208]\n"
        "str x27, [%0, #216]\n"
        "str x28, [%0, #224]\n"
        "str x29, [%0, #232]\n"
        "str x30, [%0, #240]\n"
        /* Save current SP_EL0 to user_stack */
        "mrs x1, sp_el0\n"
        "str x1, [%0, #248]\n"
        :
        : "r"(p->regs)
        : "x1", "memory"
    );
}

/**
 * restore_context - Restore CPU state from process PCB
 * @p: The process whose state should be restored
 *
 * Uses inline assembly to restore all 30 general-purpose registers
 * (x0-x29) plus the link register (x30/LR) from the PCB,
 * and sets up the stack pointer.
 */
static void restore_context(process_t *p)
{
    if (!p) return;

    __asm__ volatile (
        "mov x22, %0\n"
        /* Restore x0-x7 using post-increment addressing */
        "ldr x0, [x22], #8\n"
        "ldr x1, [x22], #8\n"
        "ldr x2, [x22], #8\n"
        "ldr x3, [x22], #8\n"
        "ldr x4, [x22], #8\n"
        "ldr x5, [x22], #8\n"
        "ldr x6, [x22], #8\n"
        "ldr x7, [x22], #8\n"
        /* Restore x8-x15 */
        "ldr x8, [x22], #8\n"
        "ldr x9, [x22], #8\n"
        "ldr x10, [x22], #8\n"
        "ldr x11, [x22], #8\n"
        "ldr x12, [x22], #8\n"
        "ldr x13, [x22], #8\n"
        "ldr x14, [x22], #8\n"
        "ldr x15, [x22], #8\n"
        /* Restore x16-x18 */
        "ldr x16, [x22], #8\n"
        "ldr x17, [x22], #8\n"
        "ldr x18, [x22], #8\n"
        /* Restore x19 */
        "ldr x19, [x22], #8\n"
        /* Restore x20 */
        "ldr x20, [x22], #8\n"
        /* Restore x21 via temp register x23 to avoid clobbering base pointer */
        "ldr x23, [x22], #8\n"
        "mov x21, x23\n"
        /* Restore x22-x23 via temp register x23 */
        "ldr x23, [x22], #8\n"
        "mov x22, x23\n"
        "ldr x23, [x22], #8\n"
        /* Restore x24-x29 and LR(x30) */
        "ldr x24, [x22], #8\n"
        "ldr x25, [x22], #8\n"
        "ldr x26, [x22], #8\n"
        "ldr x27, [x22], #8\n"
        "ldr x28, [x22], #8\n"
        "ldr x29, [x22], #8\n"
        "ldr x30, [x22], #8\n"
        /* Restore user_stack (SP_EL0) from regs[30] slot */
        "ldr x1, [x22], #8\n"
        "msr sp_el0, x1\n"
        :
        : "r"(p->regs)
        : "x1", "x19", "x20", "x21", "x22", "x23", "memory"
    );
}

/**
 * setup_context - Set up initial register state for a new process
 * @p: The process to set up
 *
 * Initializes the PCB register array with the entry point and
 * stack pointer so that when restore_context is called, the
 * process will start executing from the entry point.
 */
static void setup_context(process_t *p)
{
    if (!p) return;

    /* Clear all saved registers */
    memset(p->regs, 0, sizeof(p->regs));

    /* x0 = entry point (first argument to process) */
    p->regs[0] = (uint64_t)p->entry;
    
    /* x30 = LR = entry point (return address when process starts) */
    p->regs[30] = (uint64_t)p->entry;

    /* Set up user stack pointer */
    if (p->user_space) {
        /* Allocate stack at top of user space */
        p->user_stack = (uint64_t *)((uint64_t)p->user_space + p->user_space_size);
        /* Align stack down to 8 bytes */
        p->user_stack = (uint64_t *)((uint64_t)p->user_stack & ~7ULL);
        /* Reserve space for initial stack frame (x30, x0) */
        p->user_stack -= 2;

        /* Store entry in x0 and LR on the stack */
        *(p->user_stack + 1) = 0;          /* x30 (LR) placeholder */
        *(p->user_stack) = (uint64_t)p->entry;  /* x0 = entry point */
    } else {
        /* Use kernel stack as fallback */
        p->user_stack = (uint64_t *)(p->stack + 1024);
    }

    printk("[CTX] Setup context for pid %d ('%s'), entry=%p, stack=%p\n",
           p->pid, p->name, (void *)p->entry, p->user_stack);
}

/**
 * switch_stack - Actual assembly context switch
 * @prev: Pointer to previous process pointer (address of current_process)
 * @next: Next process to switch to
 *
 * This is the core context switch that:
 * 1. Saves current registers to prev->regs[]
 * 2. Restores registers from next->regs[]
 * 3. Switches stack pointers
 * 4. Returns to the caller
 */
__attribute__((noreturn))
static void switch_stack(process_t **prev_ptr, process_t *next)
{
    process_t *prev = *prev_ptr;
    
    /* Save current process state */
    if (prev) {
        save_context(prev);
        prev->state = PROC_SLEEPING;
        prev->user_stack = (uint64_t *)prev->regs[31];
    }

    /* Switch to next process */
    if (next) {
        /* Restore next process state */
        restore_context(next);
        next->state = PROC_RUNNING;
        *prev_ptr = next;
        current_process = next;
        
        printk("[CTX] Switched to pid %d ('%s')\n", next->pid, next->name);
    }
}

/* ============================================================
 * Process management functions
 * ============================================================ */

/**
 * create_process - Create a new process
 */
process_t *create_process(void *entry, void *stack, size_t stack_size, const char *name)
{
    process_t *p = NULL;

    /* Find free slot */
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].pid == 0) {
            p = &process_table[i];
            break;
        }
    }

    if (!p) {
        printk("[PROC] Cannot create process: table full\n");
        return NULL;
    }

    /* Initialize process */
    p->pid = next_pid++;
    p->ppid = current_process ? current_process->pid : 0;
    p->state = PROC_RUNNING;
    p->entry = (uint64_t)entry;
    p->user_space = stack;
    p->user_space_size = stack_size;
    p->exit_code = 0;

    if (name) {
        strncpy(p->name, name, sizeof(p->name) - 1);
        p->name[sizeof(p->name) - 1] = '\0';
    } else {
        char tmp[16];
        int len = 0;
        int pid = p->pid;
        /* Simple int-to-string for pid */
        if (pid == 0) { tmp[0] = '0'; tmp[1] = '\0'; }
        else {
            char buf[16];
            int bi = 0;
            while (pid > 0 && bi < 15) { buf[bi++] = '0' + (pid % 10); pid /= 10; }
            buf[bi] = '\0';
            for (int j = bi - 1; j >= 0; j--) tmp[len++] = buf[j];
        }
        const char *prefix = "proc";
        for (int j = 0; j < 4 && len < 15; j++) tmp[len++] = prefix[j];
        tmp[len] = '\0';
        strncpy(p->name, tmp, sizeof(p->name) - 1);
        p->name[sizeof(p->name) - 1] = '\0';
    }

    /* Setup initial stack frame on user stack */
    if (stack && stack_size > 0) {
        uint64_t *stk = (uint64_t *)((uint64_t)stack + stack_size);
        stk -= 32;  /* Reserve space for x0-x30 */
        *stk = (uint64_t)entry;  /* x0 - first argument / entry point */
        stk--;
        *stk = 0;          /* x30 (LR) - return address */
        for (int i = 1; i < 30; i++) {
            stk--;
            *stk = 0;      /* x1-x29 */
        }
        p->user_stack = (uint64_t *)((uint64_t)stack + stack_size);
        p->regs[0] = (uint64_t)entry;
        p->regs[30] = 0;
    }

    printk("[PROC] Created process '%s' (pid=%d, entry=%p)\n", p->name, p->pid, entry);
    return p;
}

/**
 * schedule - Context switch to next process (round-robin)
 *
 * Finds the next runnable process and performs a context switch.
 */
void schedule(void)
{
    if (!current_process) {
        printk("[SCHED] No current process, skipping schedule\n");
        return;
    }

    /* Find next runnable process */
    process_t *next = NULL;
    static int last_idx = 0;
    
    for (int i = 1; i <= MAX_PROCESSES; i++) {
        int idx = (last_idx + i) % MAX_PROCESSES;
        if (process_table[idx].pid != 0 && 
            process_table[idx].state == PROC_RUNNING) {
            next = &process_table[idx];
            last_idx = idx;
            break;
        }
    }

    if (!next) {
        printk("[SCHED] No runnable processes!\n");
        return;
    }

    if (next == current_process) {
        /* Same process, just yield */
        return;
    }

    printk("[SCHED] Switching from pid %d ('%s') to pid %d ('%s')\n",
           current_process->pid, current_process->name,
           next->pid, next->name);

    context_switch(current_process, next);
}

/**
 * context_switch - Full ARM64 context switch between processes
 * @prev: The process being preempted (may be NULL for first switch)
 * @next: The process being switched to
 *
 * This implements a full context switch that saves and restores:
 * - All 31 general-purpose registers (x0-x30)
 * - Stack pointer (SP_EL0 for EL0 execution)
 * - Program counter (via entry point in x0/x30)
 *
 * The saved state is stored in the PCB (process_t::regs[] array).
 */
void context_switch(process_t *prev, process_t *next)
{
    if (!prev && !next) {
        printk("[CTX] Both prev and next are NULL!\n");
        return;
    }

    /* Save current process state */
    if (prev) {
        save_context(prev);
        prev->state = PROC_SLEEPING;
        printk("[CTX] Saved context for pid %d ('%s')\n", prev->pid, prev->name);
    }

    /* Restore next process state */
    if (next) {
        if (next->state != PROC_RUNNING) {
            /* First time running this process, set up initial context */
            setup_context(next);
        }
        next->state = PROC_RUNNING;
        restore_context(next);
        printk("[CTX] Restored context for pid %d ('%s')\n", next->pid, next->name);
    }

    /* Update current process pointer */
    current_process = next;
}

/**
 * init_process - Initialize the init process (pid=1)
 */
void init_process(void)
{
    printk("[INIT] Creating init process...\n");

    /* Create init process */
    process_t *init = &process_table[0];
    init->pid = 1;
    init->ppid = 0;
    init->state = PROC_RUNNING;
    init->entry = (uint64_t)init_process;
    init->user_space = NULL;
    init->user_space_size = 0;
    init->exit_code = 0;
    strncpy(init->name, "init", sizeof(init->name) - 1);

    /* Set up initial stack context */
    setup_context(init);

    current_process = init;

    printk("[INIT] Init process started (pid=1, '%s')\n", init->name);
    printk("[INIT] Waiting for init to complete...\n");

    /* Run init ticks */
    for (int i = 0; i < 10; i++) {
        printk("[INIT] Init tick %d\n", i);
        
        /* Simulate context switch to self */
        schedule();
        
        /* TODO: Add timer-based sleep */
    }

    printk("[INIT] Init completed\n");
}

/**
 * usr_init - Initialize user space
 */
void usr_init(void)
{
    printk("[USR] User space initialized.\n");
}
