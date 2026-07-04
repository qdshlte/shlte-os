/*
 * syscall.c - System call interface
 */

#include <shlte/types.h>
#include <shlte/printk.h>
#include <shlte/mm.h>
#include <shlte/process.h>
#include <shlte/ipc.h>

/* External declarations */
extern void uart_putc(char c);
extern void *kmalloc_simple(unsigned long size);

/* Forward declaration for assembly entry */
void syscall_handler(uint64_t syscall_num, uint64_t arg1, uint64_t arg2,
                     uint64_t arg3, uint64_t arg4, uint64_t arg5,
                     uint64_t arg6);

/* System call numbers (ARM64 ABI: x0 = syscall number) */
#define SYS_EXIT         0
#define SYS_FORK         1
#define SYS_READ         2
#define SYS_WRITE        3
#define SYS_OPEN         4
#define SYS_CLOSE        5
#define SYS_WAITPID      6
#define SYS_KILL         7
#define SYS_GETPID       8
#define SYS_MOUNT        9
#define SYS_UNMOUNT      10
#define SYS_MALLOC       11
#define SYS_FREE         12
#define SYS_BRK          13
#define SYS_TIME         14
#define SYS_GETTIME      15
#define SYS_IPC          16

/**
 * syscall_handler - Handle a system call
 *
 * Called from assembly when user code executes SVC instruction.
 * x0 = syscall number
 * x1-x6 = arguments
 */
void syscall_handler(uint64_t syscall_num, uint64_t arg1, uint64_t arg2,
                     uint64_t arg3, uint64_t arg4, uint64_t arg5,
                     uint64_t arg6)
{
    (void)arg4;
    (void)arg5;
    (void)arg6;
    int ret = 0;

    switch (syscall_num) {
    case SYS_EXIT:
        if (current_process) {
            current_process->state = PROC_TERMINATED;
            printk("[SYSCALL] Process %d ('%s') exited with code %ld\n",
                   current_process->pid, current_process->name, arg1);
        }
        schedule();
        break;

    case SYS_FORK:
        printk("[SYSCALL] fork()\n");
        /* TODO: Clone current process */
        ret = -1;  /* Not implemented yet */
        break;

    case SYS_READ:
        printk("[SYSCALL] read(fd=%ld, buf=%p, count=%ld)\n", arg1, (void *)arg2, arg3);
        /* TODO: Read from file descriptor */
        ret = -1;
        break;

    case SYS_WRITE: {
        /* arg1 = fd, arg2 = buf ptr, arg3 = len */
        uint64_t fd = arg1;
        const char *buf = (const char *)arg2;
        uint64_t len = arg3;
        (void)fd;
        for (uint64_t i = 0; i < len && buf[i]; i++) {
            printk("%c", buf[i]);
        }
        ret = len;
        break;
    }

    case SYS_OPEN:
        printk("[SYSCALL] open(%p, 0x%lx)\n", (void *)arg1, arg2);
        ret = -1;
        break;

    case SYS_CLOSE:
        printk("[SYSCALL] close(%ld)\n", arg1);
        ret = 0;
        break;

    case SYS_GETPID:
        ret = 1;  /* Stub: always PID 1 */
        break;

    case SYS_MALLOC:
        printk("[SYSCALL] malloc(%ld)\n", arg1);
        ret = (uint64_t)kmalloc_simple(arg1);
        break;

    case SYS_FREE:
        printk("[SYSCALL] free(%p)\n", (void *)arg1);
        /* TODO: Free memory */
        ret = 0;
        break;

    case SYS_TIME:
        printk("[SYSCALL] time()\n");
        ret = 0;  /* TODO: Return current time */
        break;

    case SYS_IPC: {
        /* arg1 = subcall (0=send, 1=recv) */
        int subcall = (int)arg1;
        if (subcall == 0) {
            /* ipc_send(target_pid, msg_ptr) */
            uint64_t target = arg2;
            ipc_message_t *msg = (ipc_message_t *)arg3;
            ret = ipc_send(target, msg);
        } else if (subcall == 1) {
            /* ipc_recv(&sender, msg_ptr) */
            uint64_t *sender = (uint64_t *)arg2;
            ipc_message_t *msg = (ipc_message_t *)arg3;
            ret = ipc_recv(sender, msg);
        } else {
            ret = -1;
        }
        break;
    }

    default:
        printk("[SYSCALL] Unknown syscall %ld\n", syscall_num);
        ret = -1;
        break;
    }

    /* Return value in x0 */
    /* TODO: Set x0 = ret in the caller's register state */
    printk("[SYSCALL] syscall %ld returned %ld\n", syscall_num, ret);
}
