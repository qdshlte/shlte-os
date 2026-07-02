/*
 * syscall.c - System call interface
 */

#include <shlte/types.h>
#include <shlte/printk.h>
#include <shlte/mm.h>

/* External declarations */
extern void uart_putc(char c);
extern void *kmalloc_simple(unsigned long size);

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
    int ret = 0;

    switch (syscall_num) {
    case SYS_EXIT:
        printk("[SYSCALL] exit(%ld)\n", arg1);
        /* TODO: Terminate process */
        for (;;) {
            __asm__ volatile("wfi");
        }

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

    case SYS_WRITE:
        printk("[SYSCALL] write(fd=%ld, buf=%p, count=%ld)\n", arg1, (void *)arg2, arg3);
        /* For now, just print to kernel log */
        if (arg1 == 1 || arg1 == 2) {
            /* stdout or stderr - write buffer contents */
            char *buf = (char *)arg2;
            for (uint64_t i = 0; i < arg3; i++) {
                uart_putc(buf[i]);
            }
            ret = arg3;
        }
        break;

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

    default:
        printk("[SYSCALL] Unknown syscall %ld\n", syscall_num);
        ret = -1;
        break;
    }

    /* Return value in x0 */
    /* TODO: Set x0 = ret in the caller's register state */
    printk("[SYSCALL] syscall %ld returned %ld\n", syscall_num, ret);
}
