/*
 * shlte/io.h - x86_64 I/O port access
 */

#ifndef SHLTE_IO_H
#define SHLTE_IO_H

#include <shlte/types.h>

static inline uint8_t inb(uint16_t port)
{
    uint8_t val;
    __asm__ volatile("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static inline void outb(uint16_t port, uint8_t val)
{
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

#endif /* SHLTE_IO_H */
