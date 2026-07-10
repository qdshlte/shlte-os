/*
 * user/libc.h - Minimal C library for EL0 user programs
 *
 * Provides: string functions, memory operations, printf,
 * and a simple command-line parser.
 */

#ifndef USER_LIBC_H
#define USER_LIBC_H

#include "syscalls.h"

/* ============================================================
 * Memory operations
 * ============================================================ */

static void *memset(void *s, int c, size_t n)
{
    unsigned char *p = (unsigned char *)s;
    while (n--) *p++ = (unsigned char)c;
    return s;
}

static void *memcpy(void *dest, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    while (n--) *d++ = *s++;
    return dest;
}

static int memcmp(const void *s1, const void *s2, size_t n)
{
    const unsigned char *a = (const unsigned char *)s1;
    const unsigned char *b = (const unsigned char *)s2;
    while (n--) {
        if (*a != *b) return (int)*a - (int)*b;
        a++; b++;
    }
    return 0;
}

/* ============================================================
 * String operations
 * ============================================================ */

static size_t strlen(const char *s)
{
    size_t n = 0;
    while (*s++) n++;
    return n;
}

static int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

static int strncmp(const char *a, const char *b, size_t n)
{
    while (n && *a && *a == *b) { a++; b++; n--; }
    if (n == 0) return 0;
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

static char *strcpy(char *dest, const char *src)
{
    char *d = dest;
    while ((*d++ = *src++));
    return dest;
}

static char *strncpy(char *dest, const char *src, size_t n)
{
    size_t i;
    for (i = 0; i < n && src[i]; i++)
        dest[i] = src[i];
    for (; i < n; i++)
        dest[i] = '\0';
    return dest;
}

static char *strchr(const char *s, int c)
{
    while (*s) {
        if (*s == (char)c) return (char *)s;
        s++;
    }
    return NULL;
}

static char *strtok(char *str, const char *delim)
{
    static char *save;
    if (str) save = str;
    if (!save) return NULL;

    /* Skip leading delimiters */
    while (*save && strchr(delim, *save)) save++;
    if (!*save) return NULL;

    char *token = save;
    while (*save && !strchr(delim, *save)) save++;

    if (*save) {
        *save = '\0';
        save++;
    }
    return token;
}

/* ============================================================
 * Character classification
 * ============================================================ */

static int isspace(int c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static int isdigit(int c) {
    return c >= '0' && c <= '9';
}

static int isalpha(int c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

/* ============================================================
 * Number conversion
 * ============================================================ */

static int atoi(const char *s)
{
    int n = 0, sign = 1;
    while (isspace(*s)) s++;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') s++;
    while (isdigit(*s)) { n = n * 10 + (*s - '0'); s++; }
    return n * sign;
}

/* ============================================================
 * Minimal printf
 * ============================================================ */

static void print_uint(unsigned long long n, int base)
{
    char buf[32];
    int i = 0;
    const char *digits = "0123456789abcdef";
    if (n == 0) { buf[i++] = '0'; }
    while (n > 0) { buf[i++] = digits[n % base]; n /= base; }
    while (i > 0) { char c = buf[--i]; write(1, &c, 1); }
}

static void print_int(long long n)
{
    if (n < 0) { write(1, "-", 1); n = -n; }
    print_uint((unsigned long long)n, 10);
}

static void printf(const char *fmt, ...)
{
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);

    while (*fmt) {
        if (*fmt != '%') {
            write(1, fmt, 1);
            fmt++;
            continue;
        }
        fmt++;
        switch (*fmt) {
        case 's': {
            const char *s = __builtin_va_arg(ap, const char *);
            if (s) write(1, s, strlen(s));
            else write(1, "(null)", 6);
            break;
        }
        case 'd':
            print_int(__builtin_va_arg(ap, int));
            break;
        case 'u':
            print_uint(__builtin_va_arg(ap, unsigned int), 10);
            break;
        case 'x':
        case 'p':
            print_uint(__builtin_va_arg(ap, unsigned long long), 16);
            break;
        case 'c': {
            char c = (char)__builtin_va_arg(ap, int);
            write(1, &c, 1);
            break;
        }
        case '%':
            write(1, "%", 1);
            break;
        default:
            write(1, fmt - 1, 2);
            break;
        }
        fmt++;
    }
    __builtin_va_end(ap);
}

/* ============================================================
 * Input helpers
 * ============================================================ */

static int getchar(void)
{
    char c;
    if (read(0, &c, 1) == 1) return (int)(unsigned char)c;
    return -1;
}

static char *gets(char *buf, int size)
{
    int i = 0;
    while (i < size - 1) {
        char c;
        if (read(0, &c, 1) != 1) break;
        if (c == '\n' || c == '\r') break;
        if (c == '\b' || c == 0x7f) {
            if (i > 0) i--;
            continue;
        }
        buf[i++] = c;
    }
    buf[i] = '\0';
    write(1, "\n", 1);
    return buf;
}

#endif /* USER_LIBC_H */
