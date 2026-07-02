/*
 * process.c - Minimal process management
 */

#include <shlte/types.h>
#include <shlte/printk.h>
#include <shlte/string.h>
#include <shlte/mm.h>
#include <shlte/process.h>

/* Global process table */
static process_t process_table[MAX_PROCESSES];
static process_t *current_process = NULL;
static int next_pid = 1;

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

    /* Setup initial stack frame */
    uint64_t *stk = (uint64_t *)((uint64_t)stack + stack_size);
    *--stk = 0;          /* x30 (LR) - return address */
    *--stk = (uint64_t)entry;  /* x0 - first argument */
    *--stk = 0;          /* x1-x29 */
    *--stk = 0;
    *--stk = 0;
    *--stk = 0;
    *--stk = 0;
    *--stk = 0;
    *--stk = 0;
    *--stk = 0;
    *--stk = 0;
    *--stk = 0;
    *--stk = 0;
    *--stk = 0;
    *--stk = 0;
    *--stk = 0;
    *--stk = 0;
    *--stk = 0;
    *--stk = 0;
    *--stk = 0;
    *--stk = 0;
    *--stk = 0;
    *--stk = 0;
    *--stk = 0;
    *--stk = 0;
    *--stk = 0;
    *--stk = 0;
    *--stk = 0;
    *--stk = 0;
    *--stk = 0;
    *--stk = 0;
    p->stack[0] = (uint64_t)stk;  /* SP */

    printk("[PROC] Created process '%s' (pid=%d, entry=%p)\n", p->name, p->pid, entry);
    return p;
}

/**
 * schedule - Context switch to next process (stub)
 */
void schedule(void)
{
    /* Simple round-robin for now */
    printk("[SCHED] Round-robin scheduling (stub)\n");
}

/**
 * context_switch - Switch between processes (stub)
 */
void context_switch(process_t *prev, process_t *next)
{
    printk("[CTX] Context switch: pid %d -> pid %d\n", prev ? prev->pid : 0, next->pid);
}

/**
 * init_process - Initialize the init process
 */
void init_process(void)
{
    printk("[INIT] Creating init process...\n");

    /* Create init process that runs /sbin/init */
    /* For now, just loop */
    current_process = &process_table[0];
    current_process->pid = 1;
    current_process->state = PROC_RUNNING;
    strncpy(current_process->name, "init", sizeof(current_process->name));

    printk("[INIT] Init process started (pid=1)\n");
    printk("[INIT] Waiting for init to complete...\n");

    /* In a real OS, this would exec /sbin/init */
    /* For now, just run a simple loop */
    for (int i = 0; i < 10; i++) {
        printk("[INIT] Init tick %d\n", i);
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
