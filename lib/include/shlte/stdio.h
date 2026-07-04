/*
 * shlte/stdio.h - Standard I/O declarations
 */

#ifndef SHLTE_STDIO_H
#define SHLTE_STDIO_H

#include <shlte/types.h>

/* File operations */
typedef struct {
    char *buf;
    int fd;
    int pos;
    int size;
    int flags;
} FILE;

/* Standard streams */
extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

/* Buffered I/O */
int fprintf(FILE *stream, const char *fmt, ...);
int printf(const char *fmt, ...);
int sprintf(char *str, const char *fmt, ...);
int snprintf(char *str, size_t size, const char *fmt, ...);
char *fgets(char *s, int size, FILE *stream);
int getchar(void);
int putchar(int c);

#endif /* SHLTE_STDIO_H */
