/*
 * shlte/printk.h - Kernel print/printf interface
 */

#ifndef SHLTE_PRINTK_H
#define SHLTE_PRINTK_H

#include <shlte/types.h>

/* UART base address */
#if defined(__aarch64__)
#define UART_BASE  0x09000000      /* ARM64 virt PL011 */
#elif defined(__x86_64__)
#define UART_BASE  0x3F8           /* x86_64 COM1 */
#else
#define UART_BASE  0x09000000
#endif

/* UART registers (PL011) */
#define UART_DR     0x000   /* Data Register */
#define UART_RSR    0x004   /* Receive Status Register / Error Clear Register */
#define UART_FR     0x018   /* Flag Register */
#define UART_CR     0x030   /* Control Register */
#define UART_IMSC   0x038   /* Interrupt Mask Set/Clear Register */
#define UART_RIS    0x03C   /* Raw Interrupt Status Register */
#define UART_MIS    0x040   /* Masked Interrupt Status Register */
#define UART_ICR    0x044   /* Interrupt Clear Register */

/* UART Flags Register (FR) flags */
#define UART_FR_TXFE    (1 << 7)
#define UART_FR_RXFF    (1 << 6)
#define UART_FR_TXFF    (1 << 5)
#define UART_FR_RXFE    (1 << 4)
#define UART_FR_BUSY    (1 << 3)

/* UART Control Register (CR) flags */
#define UART_CR_UARTEN  (1 << 0)
#define UART_CR_TXE     (1 << 9)
#define UART_CR_RXE     (1 << 8)
#define UART_CR_CTSEn   (1 << 10)
#define UART_CR_RTSDEn  (1 << 9)

/* Function declarations */
int printk(const char *fmt, ...);
void uart_putc(char c);
void uart_puts(const char *s);
void uart_init(void);
char uart_getc(void);
int uart_getc_nonblock(void);
void panic(const char *msg);

/* Debug macros */
#ifdef DEBUG
#define dprintk(fmt, ...) printk("[DEBUG] " fmt, ##__VA_ARGS__)
#else
#define dprintk(fmt, ...) ((void)0)
#endif

#endif /* SHLTE_PRINTK_H */
