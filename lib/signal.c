/*
 * lib/signal.c - POSIX Signal Handling
 *
 * Implements signal delivery, signal masks, and default actions.
 */

#include <shlte/types.h>
#include <shlte/printk.h>
#include <shlte/string.h>
#include <shlte/process.h>

#define NSIG             32
#define SIGKILL           9
#define SIGSTOP          19
#define SIGCONT          18
#define SIGTERM          15
#define SIGINT            2
#define SIGCHLD          17
#define SIGSEGV          11

#define SIG_DFL  ((void *)0)
#define SIG_IGN  ((void *)1)

typedef void (*sig_handler_t)(int);

typedef struct {
    sig_handler_t handlers[NSIG];
    uint32_t      pending;
    uint32_t      blocked;
    int           exit_code;
    int           signo;
} signal_state_t;

/* Per-process signal state */
static signal_state_t *sig_get_state(process_t *p)
{
    if (!p) return NULL;
    /* Use the top of the kernel stack for signal state */
    return (signal_state_t *)(p->stack + 1024 - sizeof(signal_state_t) / 8);
}

int sys_kill(int pid, int sig)
{
    if (sig < 0 || sig >= NSIG) return -1;
    if (pid <= 0) return -1;

    process_t *target = NULL;
    extern process_t *get_process_by_index(int idx);
    for (int i = 0; i < 64; i++) {
        process_t *p = get_process_by_index(i);
        if (p && p->pid == pid) { target = p; break; }
    }
    if (!target) return -1;

    signal_state_t *st = sig_get_state(target);
    if (!st) return -1;

    st->pending |= (1U << sig);
    st->signo = sig;

    if (sig == SIGKILL || sig == SIGSTOP) {
        target->state = (sig == SIGKILL) ? PROC_TERMINATED : PROC_STOPPED;
    }

    return 0;
}

int sys_signal(int sig, sig_handler_t handler)
{
    if (sig < 0 || sig >= NSIG || sig == SIGKILL || sig == SIGSTOP)
        return -1;

    process_t *p = current_process;
    if (!p) return -1;

    signal_state_t *st = sig_get_state(p);
    if (!st) return -1;

    sig_handler_t old = st->handlers[sig];
    st->handlers[sig] = handler;
    return (int)(uint64_t)old;
}

int sys_sigreturn(void)
{
    return 0;
}

void signal_deliver(process_t *p)
{
    if (!p) return;
    signal_state_t *st = sig_get_state(p);
    if (!st) return;

    uint32_t pending = st->pending & ~st->blocked;
    if (!pending) return;

    /* Find highest priority pending signal */
    for (int sig = 1; sig < NSIG; sig++) {
        if (pending & (1U << sig)) {
            st->pending &= ~(1U << sig);

            sig_handler_t handler = st->handlers[sig];
            if (handler == SIG_DFL) {
                /* Default action */
                if (sig == SIGTERM || sig == SIGINT || sig == SIGSEGV) {
                    p->state = PROC_TERMINATED;
                    p->exit_code = sig;
                } else if (sig == SIGCHLD) {
                    /* Ignore by default */
                }
            } else if (handler == SIG_IGN) {
                /* Ignore */
            } else {
                /* Call user handler — set up signal trampoline */
                /* For now, just ignore (needs trampoline in exception handler) */
            }
            break;
        }
    }
}

void signal_init_process(process_t *p)
{
    signal_state_t *st = sig_get_state(p);
    if (st) {
        memset(st, 0, sizeof(*st));
        for (int i = 1; i < NSIG; i++)
            st->handlers[i] = SIG_DFL;
        st->handlers[SIGCHLD] = SIG_IGN;
    }
}
