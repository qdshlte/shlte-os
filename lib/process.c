/*
 * process.c - Process management with preemptive scheduling
 *
 * Provides process creation, round-robin scheduling, and context
 * switching.  The actual save/restore of registers at exception
 * boundaries is done by exc_save_context / exc_restore_context
 * in exception.c (for IRQ preemption) and resume_to_el0 in
 * boot.S (for first-time process launch).
 */

#include <shlte/types.h>
#include <shlte/printk.h>
#include <shlte/string.h>
#include <shlte/mm.h>
#include <shlte/process.h>
#include <shlte/ipc.h>

/* ============================================================
 * Global state
 * ============================================================ */

/* Global process table */
static process_t process_table[MAX_PROCESSES];
process_t *current_process = NULL;
static int next_pid = 1;
int num_processes = 0;

/* Preemption flag — set by timer_tick, checked in exception handler */
volatile int need_reschedule = 0;

/* ============================================================
 * Timer tick — called from timer interrupt context
 * ============================================================ */

/**
 * timer_tick - Called by the timer interrupt handler every 10 ms
 *
 * Increments the current process's tick counter and signals a
 * reschedule after 10 ticks (100 ms time slice).
 */
void timer_tick(void)
{
    if (current_process) {
        current_process->ticks++;
        if (current_process->ticks >= 10) {
            current_process->ticks = 0;
            need_reschedule = 1;
        }
    }
}

/* ============================================================
 * Process management functions
 * ============================================================ */

/**
 * create_process - Allocate and initialise a new process
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

    /* Zero the entire PCB, then set known fields */
    memset(p, 0, sizeof(process_t));

    p->pid = next_pid++;
    p->ppid = current_process ? current_process->pid : 0;
    p->state = PROC_RUNNING;
    p->entry = (uint64_t)entry;
    p->elr_el1 = (uint64_t)entry;       /* First ERET goes to entry */
    p->spsr_el1 = 0;                    /* EL0t, interrupts unmasked */
    p->user_space = stack;
    p->user_space_size = stack_size;
    p->exit_code = 0;
    p->ticks = 0;
    p->priority = 128;

    num_processes++;

    /* Register IPC queue for this process */
    ipc_register_queue(p->pid);

    /* Set the process name */
    if (name) {
        strncpy(p->name, name, sizeof(p->name) - 1);
        p->name[sizeof(p->name) - 1] = '\0';
    } else {
        /* Auto-name: "procN" */
        int len = 0;
        const char *prefix = "proc";
        for (int j = 0; j < 4 && len < 15; j++)
            p->name[len++] = prefix[j];
        /* Simple PID to string */
        int pid = p->pid;
        if (pid == 0) {
            p->name[len++] = '0';
        } else {
            char buf[16];
            int bi = 0;
            while (pid > 0 && bi < 15) {
                buf[bi++] = '0' + (pid % 10);
                pid /= 10;
            }
            for (int j = bi - 1; j >= 0 && len < 15; j--)
                p->name[len++] = buf[j];
        }
        p->name[len] = '\0';
    }

    /* Set up user stack pointer */
    if (stack && stack_size > 0) {
        p->sp_el0 = (uint64_t)stack + stack_size;
        p->sp_el0 &= ~7ULL;  /* 8-byte alignment */
    } else {
        p->sp_el0 = (uint64_t)&p->stack[1024];
    }

    /* Set x0 = entry point so the process receives it on first run */
    p->regs[0] = (uint64_t)entry;

    printk("[PROC] Created '%s' (pid=%d, entry=%p, sp=%lx)\n",
           p->name, p->pid, entry, p->sp_el0);
    return p;
}

/**
 * pick_next - Find the next runnable process (round-robin)
 *
 * Returns a pointer to the next process that should run.
 * Does NOT perform a context switch — the caller is responsible
 * for saving/restoring state.
 */
process_t *pick_next(void)
{
    if (num_processes <= 0)
        return NULL;

    static int last_idx = 0;

    for (int attempt = 0; attempt < MAX_PROCESSES; attempt++) {
        int idx = (last_idx + 1 + attempt) % MAX_PROCESSES;
        process_t *p = &process_table[idx];
        if (p->pid > 0 && p->state == PROC_RUNNING) {
            last_idx = idx;
            return p;
        }
    }

    /* If nothing runnable, return the current process (idle) */
    if (current_process && current_process->state == PROC_RUNNING)
        return current_process;

    return NULL;
}

/**
 * schedule - Pick and switch to the next runnable process
 *
 * Updates current_process to point to the next runnable process.
 * The actual register save/restore is done by the caller (exception
 * handler or resume_to_el0).
 */
void schedule(void)
{
    process_t *next = pick_next();

    if (next) {
        printk("[SCHED] switch %d('%s') -> %d('%s')\n",
               current_process ? current_process->pid : -1,
               current_process ? current_process->name : "none",
               next->pid, next->name);
        current_process = next;
    } else {
        printk("[SCHED] No runnable process!\n");
        current_process = NULL;
    }

    need_reschedule = 0;
}

/**
 * context_switch - Legacy wrapper (used for non-exception-path switches)
 */
void context_switch(process_t *prev, process_t *next)
{
    (void)prev;
    if (next) {
        current_process = next;
        resume_to_el0(next->regs);
    }
}

/**
 * get_process_by_index - Access process table by index (for scheduler loop)
 */
process_t *get_process_by_index(int idx)
{
    if (idx < 0 || idx >= MAX_PROCESSES)
        return NULL;
    if (process_table[idx].pid == 0)
        return NULL;
    return &process_table[idx];
}

/**
 * init_process - Legacy init (kept for compatibility)
 */
void init_process(void)
{
    printk("[INIT] Creating init process...\n");
    process_t *init = create_process(NULL, NULL, 0, "init");
    if (init) {
        init->pid = 1;
        current_process = init;
    }
}

/**
 * usr_init - Initialize user space
 */
void usr_init(void)
{
    printk("[USR] User space initialized.\n");
}
