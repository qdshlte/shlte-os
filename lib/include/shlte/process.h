/*
 * shlte/process.h - Process management declarations
 */

#ifndef SHLTE_PROCESS_H
#define SHLTE_PROCESS_H

#include <shlte/types.h>

/* Process states */
#define PROC_RUNNING   0
#define PROC_SLEEPING  1
#define PROC_ZOMBIE    2
#define PROC_STOPPED   3

/* Maximum number of processes */
#define MAX_PROCESSES  64

/* Process control block */
typedef struct process {
    int pid;
    int ppid;
    int state;
    char name[16];
    uint64_t stack[1024];  /* Kernel stack */
    uint64_t *user_stack;
    uint64_t regs[31];     /* Saved registers x0-x30 */
    uint64_t entry;        /* Entry point */
    void *user_space;      /* User space memory */
    size_t user_space_size;
    int exit_code;
    struct process *next;
} process_t;

/* Process management functions */
int fork(void);
int execve(const char *pathname, char *argv[], char *envp[]);
int waitpid(int pid, int *status, int options);
void exit(int code);
void init_process(void);
void usr_init(void);
process_t *create_process(void *entry, void *stack, size_t stack_size, const char *name);
void schedule(void);
void context_switch(process_t *prev, process_t *next);

#endif /* SHLTE_PROCESS_H */
