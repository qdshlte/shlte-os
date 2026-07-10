/*
 * user/syscalls.h - System call wrappers for EL0 user programs
 *
 * ARM64 syscall convention: x8 = number, x0-x5 = args, return in x0.
 * These wrappers use inline assembly to invoke the kernel.
 */

#ifndef USER_SYSCALLS_H
#define USER_SYSCALLS_H

typedef unsigned long long uint64_t;
typedef unsigned int       uint32_t;
typedef unsigned short     uint16_t;
typedef unsigned char      uint8_t;
typedef long long int64_t;
typedef int                int32_t;
typedef short              int16_t;
typedef signed char        int8_t;
typedef unsigned long size_t;
typedef long ssize_t;

#ifndef NULL
#define NULL ((void *)0)
#endif

/* Syscall numbers */
#define SYS_EXIT      0
#define SYS_FORK      1
#define SYS_READ      2
#define SYS_WRITE     3
#define SYS_OPEN      4
#define SYS_CLOSE     5
#define SYS_WAITPID   6
#define SYS_KILL      7
#define SYS_GETPID    8
#define SYS_MOUNT     9
#define SYS_UNMOUNT   10
#define SYS_MALLOC    11
#define SYS_FREE      12
#define SYS_BRK       13
#define SYS_TIME      14
#define SYS_GETTIME   15
#define SYS_IPC       16
#define SYS_EXEC      17
#define SYS_LIST      18

/* File descriptor constants */
#define STDIN_FILENO   0
#define STDOUT_FILENO  1
#define STDERR_FILENO  2

/* Open flags */
#define O_RDONLY  0
#define O_WRONLY  1
#define O_RDWR    2
#define O_CREAT   0x40
#define O_TRUNC   0x200

#define FS_MAX_NAME  128

/* Inline syscall wrappers */
static int64_t syscall6(uint64_t num, uint64_t a0, uint64_t a1,
                                uint64_t a2, uint64_t a3, uint64_t a4,
                                uint64_t a5)
{
    register uint64_t x8 __asm__("x8") = num;
    register uint64_t x0 __asm__("x0") = a0;
    register uint64_t x1 __asm__("x1") = a1;
    register uint64_t x2 __asm__("x2") = a2;
    register uint64_t x3 __asm__("x3") = a3;
    register uint64_t x4 __asm__("x4") = a4;
    register uint64_t x5 __asm__("x5") = a5;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2),
                     "r"(x3), "r"(x4), "r"(x5) : "memory");
    return (int64_t)x0;
}

static int64_t syscall3(uint64_t num, uint64_t a0, uint64_t a1,
                                uint64_t a2)
{
    return syscall6(num, a0, a1, a2, 0, 0, 0);
}

static int64_t syscall2(uint64_t num, uint64_t a0, uint64_t a1)
{
    return syscall6(num, a0, a1, 0, 0, 0, 0);
}

static int64_t syscall1(uint64_t num, uint64_t a0)
{
    return syscall6(num, a0, 0, 0, 0, 0, 0);
}

static int64_t syscall0(uint64_t num)
{
    return syscall6(num, 0, 0, 0, 0, 0, 0);
}

/* Convenience wrappers */

static void exit(int code) {
    syscall1(SYS_EXIT, (uint64_t)code);
    __builtin_unreachable();
}

static ssize_t read(int fd, void *buf, size_t count) {
    return (ssize_t)syscall3(SYS_READ, (uint64_t)fd, (uint64_t)buf, (uint64_t)count);
}

static ssize_t write(int fd, const void *buf, size_t count) {
    return (ssize_t)syscall3(SYS_WRITE, (uint64_t)fd, (uint64_t)buf, (uint64_t)count);
}

static int open(const char *path, int flags) {
    return (int)syscall2(SYS_OPEN, (uint64_t)path, (uint64_t)flags);
}

static int close(int fd) {
    return (int)syscall1(SYS_CLOSE, (uint64_t)fd);
}

static int getpid(void) {
    return (int)syscall0(SYS_GETPID);
}

static void *malloc(size_t size) {
    return (void *)syscall1(SYS_MALLOC, (uint64_t)size);
}

static void free(void *ptr) {
    syscall1(SYS_FREE, (uint64_t)ptr);
}

static int exec(const char *path) {
    return (int)syscall1(SYS_EXEC, (uint64_t)path);
}

static int listdir(char names[][128], int max_count) {
    return (int)syscall2(SYS_LIST, (uint64_t)names, (uint64_t)max_count);
}

#endif /* USER_SYSCALLS_H */
