/*
 * stdio.c - Standard I/O stubs
 */

#include <shlte/types.h>
#include <shlte/stdio.h>
#include <shlte/printk.h>
#include <shlte/string.h>
#include <shlte/fs.h>

/* Standard streams */
FILE *stdin  = NULL;
FILE *stdout = NULL;
FILE *stderr = NULL;

/**
 * printf - Print formatted output to stdout
 */
int printf(const char *fmt, ...)
{
    va_list ap;
    char buf[256];
    int ret;
    VA_START(ap, fmt);
    ret = vsprintf(buf, fmt, ap);
    VA_END(ap);
    /* Write to stdout */
    if (stdout) {
        vfs_write(STDOUT_FILENO, buf, ret);
    } else {
        /* Fallback: just print via uart */
        for (int i = 0; i < ret; i++) {
            uart_putc(buf[i]);
        }
    }
    return ret;
}

/**
 * fprintf - Print formatted output to file stream (stub)
 */
int fprintf(FILE *stream, const char *fmt, ...)
{
    (void)stream;
    (void)fmt;
    printk("[STDIO] fprintf not fully implemented\n");
    return -1;
}

/**
 * fgets - Read string from stream (stub)
 */
char *fgets(char *s, int size, FILE *stream)
{
    (void)s;
    (void)size;
    (void)stream;
    printk("[STDIO] fgets not implemented\n");
    return NULL;
}

/**
 * getchar - Read a character from stdin (blocking)
 */
int getchar(void)
{
    return uart_getc();
}

/**
 * putchar - Write a character to stdout
 */
int putchar(int c)
{
    uart_putc((char)c);
    return c;
}
