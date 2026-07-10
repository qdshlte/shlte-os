/*
 * syscall.c - System call interface
 */

#include <shlte/types.h>
#include <shlte/printk.h>
#include <shlte/mm.h>
#include <shlte/fs.h>
#include <shlte/loader.h>
#include <shlte/process.h>
#include <shlte/ipc.h>
#include <shlte/string.h>
#include <shlte/gui.h>
#include <shlte/virtio_input.h>

/* External declarations */
extern void uart_putc(char c);
extern char uart_getc(void);
extern void *kmalloc_simple(unsigned long size);
extern void early_putchar(char c);

/* Process counter — set by kernel_main during sequential launch */
int current_pid = 0;

/* Forward declaration for assembly entry */
int64_t syscall_handler(uint64_t syscall_num, uint64_t arg1, uint64_t arg2,
                        uint64_t arg3, uint64_t arg4, uint64_t arg5,
                        uint64_t arg6, uint64_t *regs);

/* System call numbers (ARM64 ABI: x8 = syscall number, x0-x5 = args) */
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
#define SYS_EXEC         17
#define SYS_LIST         18

/** Enter_el0 trampoline (defined in boot.S) */
extern void enter_el0(uint64_t entry, uint64_t stack);

/* Forward decl for vfs-read helper */
static ssize_t vfs_read_wrapper(int fd, void *buf, size_t count);

/**
 * syscall_handler - Handle a system call
 *
 * ARM64 syscall convention:
 *   x8 = syscall number
 *   x0-x5 = arguments (up to 6)
 *   Return value in x0 (written by exception handler to regs[0])
 */
int64_t syscall_handler(uint64_t syscall_num, uint64_t arg1, uint64_t arg2,
                        uint64_t arg3, uint64_t arg4, uint64_t arg5,
                        uint64_t arg6, uint64_t *regs)
{
    (void)arg4;
    (void)arg5;
    (void)arg6;
    (void)regs;
    int64_t ret = 0;

    switch (syscall_num) {
    case SYS_EXIT:
        printk("[SYSCALL] exit(%ld)\n", arg1);
        /* Return to kernel — boot continues to el0_return_here + kernel_main */
        ret = (int64_t)arg1;
        break;

    case SYS_FORK:
        printk("[SYSCALL] fork()\n");
        ret = -1;  /* Not implemented yet */
        break;

    case SYS_READ: {
        /* arg1 = fd, arg2 = buf ptr, arg3 = max count */
        uint64_t fd = arg1;
        char *buf = (char *)arg2;
        uint64_t max = arg3;

        if (fd <= 1) {
            /* UART read (stdin) */
            uint64_t i;
            for (i = 0; i < max; i++) {
                char c = uart_getc();
                if (c == '\r') c = '\n';
                buf[i] = c;
                early_putchar(c);   /* echo */
                if (c == '\n') break;
            }
            ret = (int64_t)(i + 1);
        } else {
            /* File read via VFS */
            ret = (int64_t)vfs_read_wrapper((int)(fd - 2), buf, (size_t)max);
        }
        break;
    }

    case SYS_WRITE: {
        /* arg1 = fd, arg2 = buf ptr, arg3 = len */
        uint64_t fd = arg1;
        const char *buf = (const char *)arg2;
        uint64_t len = arg3;

        if (fd <= 1) {
            /* UART write (stdout/stderr) */
            for (uint64_t i = 0; i < len && buf[i]; i++) {
                early_putchar(buf[i]);
            }
            /* Also echo to GUI terminal */
            if (g_gui.initialized) {
                for (uint64_t i = 0; i < len && buf[i]; i++) {
                    gui_term_putchar(buf[i]);
                }
            }
            ret = (int64_t)len;
        } else {
            /* File write via VFS */
            ssize_t written = vfs_write((int)(fd - 2), buf, (size_t)len);
            ret = (int64_t)written;
        }
        break;
    }

    case SYS_OPEN: {
        /* arg1 = pathname ptr (userspace), arg2 = flags */
        const char *path = (const char *)arg1;
        int vfs_fd = vfs_open(path, (int)arg2);
        if (vfs_fd < 0) {
            ret = -1;
        } else {
            /* Offset by 2 to avoid clashing with stdin/stdout */
            ret = (int64_t)(vfs_fd + 2);
        }
        break;
    }

    case SYS_CLOSE: {
        uint64_t fd = arg1;
        if (fd >= 2) {
            vfs_close((int)(fd - 2));
        }
        ret = 0;
        break;
    }

    case SYS_GETPID:
        ret = current_pid;
        break;

    case SYS_MALLOC:
        printk("[SYSCALL] malloc(%ld)\n", arg1);
        ret = (uint64_t)kmalloc_simple(arg1);
        break;

    case SYS_FREE:
        printk("[SYSCALL] free(%p)\n", (void *)arg1);
        ret = 0;
        break;

    case SYS_TIME:
        printk("[SYSCALL] time()\n");
        ret = 0;
        break;

    case SYS_IPC: {
        /* arg1 = subcall (0=send, 1=recv) */
        int subcall = (int)arg1;
        if (subcall == 0) {
            uint64_t target = arg2;
            ipc_message_t *msg = (ipc_message_t *)arg3;
            ret = ipc_send(target, msg);
        } else if (subcall == 1) {
            uint64_t *sender = (uint64_t *)arg2;
            ipc_message_t *msg = (ipc_message_t *)arg3;
            ret = ipc_recv(sender, msg);
        } else {
            ret = -1;
        }
        break;
    }

    case SYS_EXEC: {
        /* arg1 = pathname ptr (userspace) — e.g. "/mnt/program" */
        const char *path = (const char *)arg1;

        uint64_t entry;
        void *stack;
        int ok = load_raw_binary(path, &entry, &stack);
        if (ok != 0) {
            printk("[SYSCALL] exec(\"%s\") - load failed\n", path);
            ret = -1;
            break;
        }

        uint64_t stack_top = (uint64_t)stack + USER_STACK_SIZE;

        printk("[SYSCALL] exec(\"%s\") -> jumping to EL0 @ 0x%lx\n",
               path, entry);

        /* Redirect ERET to the new EL0 program via the exception frame.
         * regs[31]=ELR_EL1, regs[32]=SPSR_EL1, regs[33]=SP_EL0 */
        regs[31] = entry;       /* ELR_EL1 -> new entry point */
        regs[32] = 0;           /* SPSR_EL1 -> EL0t, unmasked */
        regs[33] = stack_top;   /* SP_EL0 -> new user stack */

        /* Also update PCB so future preemption saves correct state */
        if (current_process) {
            current_process->elr_el1 = entry;
            current_process->spsr_el1 = 0;
            current_process->sp_el0 = stack_top;
            current_process->entry = entry;
        }

        /* Unreachable — ERET happens in the assembly handler */
        ret = 0;
        break;
    }

    case SYS_LIST: {
        /* arg1 = buffer ptr (userspace), arg2 = max_count
         * Each entry is FS_MAX_NAME bytes.
         * Returns number of entries, or -1 on error. */
        char (*names)[FS_MAX_NAME] = (char (*)[FS_MAX_NAME])arg1;
        int max_count = (int)arg2;
        ret = (int64_t)vfs_list(names, max_count);
        break;
    }

    default:
        printk("[SYSCALL] Unknown syscall %ld\n", syscall_num);
        ret = -1;
        break;
    }

    return ret;
}

/**
 * vfs_read_wrapper — small helper so we can call vfs_read from the switch
 */
static ssize_t vfs_read_wrapper(int fd, void *buf, size_t count)
{
    return vfs_read(fd, buf, count);
}
