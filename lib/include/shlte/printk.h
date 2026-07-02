/*
 * shlte/printk.h - Kernel print/printf interface
 */

#ifndef SHLTE_PRINTK_H
#define SHLTE_PRINTK_H

#include <shlte/types.h>

/* UART base address for ARM64 virt machine */
#define UART_BASE  0x09000000

/* UART registers */
#define UART_DR     0x00
#define UART_SR     0x18
#define UART_CR     0x30
#define UART_IMSC   0x3C
#define UART_FR     0x18

/* UART Status Register flags */
#define UART_SR_TXFE    (1 << 4)
#define UART_SR_RXFF    (1 << 5)
#define UART_SR_TXFF    (1 << 5)
#define UART_SR_RXFE    (1 << 4)

/* UART Control Register flags */
#define UART_CR_UARTEN  (1 << 0)
#define UART_CR_TXE     (1 << 9)
#define UART_CR_RXE     (1 << 8)

/* Function declarations */
int printk(const char *fmt, ...);
void uart_putc(char c);
void uart_puts(const char *s);
void uart_init(void);
char uart_getc(void);
int uart_getc_nonblock(void);

/* Debug macros */
#ifdef DEBUG
#define dprintk(fmt, ...) printk("[DEBUG] " fmt, ##__VA_ARGS__)
#else
#define dprintk(fmt, ...) ((void)0)
#endif

#endif /* SHLTE_PRINTK_H */
