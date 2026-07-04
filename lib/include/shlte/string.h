/*
 * shlte/string.h - String and memory operation declarations
 */

#ifndef SHLTE_STRING_H
#define SHLTE_STRING_H

#include <shlte/types.h>

/* String operations */
size_t strlen(const char *s);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);
char *strcpy(char *dst, const char *src);
char *strncpy(char *dst, const char *src, size_t n);
char *strcat(char *dst, const char *src);
char *strchr(const char *s, int c);
const char *strstr(const char *haystack, const char *needle);
int strncmp_ci(const char *s1, const char *s2, size_t n);

/* Memory operations */
void *memset(void *s, int c, size_t n);
void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
int memcmp(const void *s1, const void *s2, size_t n);

/* Number conversion */
int sprintf(char *str, const char *fmt, ...);
int snprintf(char *str, size_t size, const char *fmt, ...);
int vsprintf(char *str, const char *fmt, va_list ap);
int vsnprintf(char *str, size_t size, const char *fmt, va_list ap);
long atol(const char *nptr);

#endif /* SHLTE_STRING_H */
