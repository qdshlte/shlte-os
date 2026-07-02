/*
 * string.c - Basic string and memory operations
 */

#include <shlte/types.h>
#include <shlte/string.h>
#include <shlte/printk.h>

/**
 * strlen - Calculate string length
 */
size_t strlen(const char *s)
{
    const char *p = s;
    while (*p) p++;
    return p - s;
}

/**
 * strcmp - Compare two strings
 */
int strcmp(const char *s1, const char *s2)
{
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

/**
 * strncmp - Compare up to n characters of two strings
 */
int strncmp(const char *s1, const char *s2, size_t n)
{
    if (n == 0) return 0;
    while (--n > 0 && *s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

/**
 * strcpy - Copy string (destination must be large enough)
 */
char *strcpy(char *dst, const char *src)
{
    char *save = dst;
    while ((*dst++ = *src++));
    return save;
}

/**
 * strncpy - Copy at most n characters
 */
char *strncpy(char *dst, const char *src, size_t n)
{
    char *save = dst;
    while (n > 0 && (*dst++ = *src++)) n--;
    while (n > 0) { *dst++ = '\0'; n--; }
    return save;
}

/**
 * strcat - Concatenate strings
 */
char *strcat(char *dst, const char *src)
{
    char *save = dst;
    while (*dst) dst++;
    while ((*dst++ = *src++));
    return save;
}

/**
 * strchr - Find first occurrence of character in string
 */
char *strchr(const char *s, int c)
{
    while (*s) {
        if (*s == (char)c) return (char *)s;
        s++;
    }
    if (c == '\0') return (char *)s;
    return NULL;
}

/**
 * strstr - Find substring
 */
const char *strstr(const char *haystack, const char *needle)
{
    if (!*needle) return haystack;
    while (*haystack) {
        const char *h = haystack;
        const char *n = needle;
        while (*h && *n && *h == *n) { h++; n++; }
        if (!*n) return haystack;
        haystack++;
    }
    return NULL;
}

/**
 * memset - Fill memory with a byte value
 */
void *memset(void *s, int c, size_t n)
{
    unsigned char *p = (unsigned char *)s;
    while (n--) *p++ = (unsigned char)c;
    return s;
}

/**
 * memcpy - Copy memory
 */
void *memcpy(void *dst, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while (n--) *d++ = *s++;
    return dst;
}

/**
 * memmove - Copy memory (handles overlap)
 */
void *memmove(void *dst, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    if (d < s) {
        while (n--) *d++ = *s++;
    } else {
        d += n;
        s += n;
        while (n--) *--d = *--s;
    }
    return dst;
}

/**
 * memcmp - Compare memory regions
 */
int memcmp(const void *s1, const void *s2, size_t n)
{
    const unsigned char *p1 = s1;
    const unsigned char *p2 = s2;
    while (n--) {
        if (*p1 != *p2) return *p1 - *p2;
        p1++;
        p2++;
    }
    return 0;
}

/**
 * atol - Convert string to long
 */
long atol(const char *nptr)
{
    long result = 0;
    int sign = 1;

    while (*nptr == ' ' || *nptr == '\t' || *nptr == '\n') nptr++;

    if (*nptr == '-') { sign = -1; nptr++; }
    else if (*nptr == '+') nptr++;

    while (*nptr >= '0' && *nptr <= '9') {
        result = result * 10 + (*nptr - '0');
        nptr++;
    }

    return sign * result;
}

/**
 * sprintf - Format string to buffer
 */
int sprintf(char *str, const char *fmt, ...)
{
    va_list ap;
    int ret;
    VA_START(ap, fmt);
    ret = vsprintf(str, fmt, ap);
    VA_END(ap);
    return ret;
}

/**
 * snprintf - Format string to buffer with size limit
 */
int snprintf(char *str, size_t size, const char *fmt, ...)
{
    va_list ap;
    int ret;
    VA_START(ap, fmt);
    ret = vsnprintf(str, size, fmt, ap);
    VA_END(ap);
    return ret;
}

/**
 * vsprintf - Format string with va_list
 */
int vsprintf(char *str, const char *fmt, va_list ap)
{
    size_t i = 0;
    const char *p = fmt;

    while (*p && i < 1023) {
        if (*p == '%') {
            p++;
            switch (*p) {
            case 's': {
                const char *s = (const char *)VA_ARG(ap, void *);
                size_t len;
                if (!s) s = "(null)";
                len = strlen(s);
                if (i + len > 1023) len = 1023 - i;
                memcpy(str + i, s, len);
                i += len;
                break;
            }
            case 'd':
            case 'i': {
                long val = VA_ARG(ap, long);
                char buf[32];
                int neg = 0;
                if (val < 0) { neg = 1; val = -val; }
                char *bp = buf + sizeof(buf) - 1;
                *bp = '\0';
                do { *--bp = '0' + (val % 10); val /= 10; } while (val > 0 && bp > buf);
                if (neg) *--bp = '-';
                size_t blen = (buf + sizeof(buf) - 1) - bp;
                if (i + blen > 1023) blen = 1023 - i;
                memcpy(str + i, bp, blen);
                i += blen;
                break;
            }
            case 'u': {
                unsigned long val = VA_ARG(ap, unsigned long);
                char buf[32];
                char *bp = buf + sizeof(buf) - 1;
                *bp = '\0';
                do { *--bp = '0' + (val % 10); val /= 10; } while (val > 0 && bp > buf);
                size_t blen = (buf + sizeof(buf) - 1) - bp;
                if (i + blen > 1023) blen = 1023 - i;
                memcpy(str + i, bp, blen);
                i += blen;
                break;
            }
            case 'x':
            case 'X': {
                unsigned long val = VA_ARG(ap, unsigned long);
                char buf[32];
                char *bp = buf + sizeof(buf) - 1;
                *bp = '\0';
                do {
                    char digit = (val & 0xF);
                    *--bp = (digit < 10) ? ('0' + digit) : ('a' + digit - 10);
                    val >>= 4;
                } while (val > 0 && bp > buf);
                size_t blen = (buf + sizeof(buf) - 1) - bp;
                if (i + blen > 1023) blen = 1023 - i;
                memcpy(str + i, bp, blen);
                i += blen;
                break;
            }
            case 'p': {
                unsigned long val = (unsigned long)VA_ARG(ap, void *);
                const char *hex = "0123456789abcdef";
                if (i + 2 <= 1023) { str[i++] = '0'; str[i++] = 'x'; }
                int shift = (28 >= 28) ? 28 : 28;
                for (; shift >= 0; shift -= 4) {
                    if (i >= 1023) break;
                    char digit = (val >> shift) & 0xF;
                    str[i++] = hex[digit];
                }
                break;
            }
            case 'c': {
                char ch = (char)VA_ARG(ap, int);
                str[i++] = ch;
                break;
            }
            case '%':
                str[i++] = '%';
                break;
            default:
                str[i++] = '%';
                str[i++] = *p;
                break;
            }
        } else {
            str[i++] = *p;
        }
        p++;
    }
    if (i > 1023) i = 1023;
    str[i] = '\0';
    return (int)i;
}

/**
 * vsnprintf - Format string with va_list and size limit
 */
int vsnprintf(char *str, size_t size, const char *fmt, va_list ap)
{
    if (size == 0) return 0;
    int ret = vsprintf(str, fmt, ap);
    if ((size_t)ret >= size) {
        str[size - 1] = '\0';
        ret = (int)size - 1;
    }
    return ret;
}
