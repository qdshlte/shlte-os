/*
 * printk.c - Kernel print implementation with UART output
 *
 * Implements a minimal printf for the kernel that outputs
 * characters through the UART serial port.
 */

#include <shlte/types.h>
#include <shlte/printk.h>
#include <stdarg.h>
#include <shlte/string.h>

/* ============================================================
 * UART Driver - ARM PL011 (aarch64) / 16550 COM1 (x86_64)
 * ============================================================ */

#if defined(__aarch64__)

/* Global UART base pointer (set by uart_init) */
static volatile uint32_t *uart_base = NULL;

/**
 * uart_read - Read a value from a UART register
 */
static inline uint32_t uart_read(volatile uint32_t *uart, int reg)
{
    return uart[reg >> 2];
}

/**
 * uart_write - Write a value to a UART register
 */
static inline void uart_write(volatile uint32_t *uart, int reg, uint32_t val)
{
    uart[reg >> 2] = val;
}

/* PL011 UART register offsets (defined in printk.h) */

void uart_init(void)
{
    uart_base = (volatile uint32_t *)UART_BASE;

    while (uart_read(uart_base, UART_FR) & UART_FR_BUSY) {
        /* Busy wait */
    }

    uart_write(uart_base, UART_CR, 0);
    uart_write(uart_base, UART_IMSC, 0);
    uart_write(uart_base, UART_ICR, 0x7FF);
    uart_write(uart_base, UART_CR,
               UART_CR_UARTEN | UART_CR_TXE | UART_CR_RXE);
}

void uart_putc(char c)
{
    if (!uart_base) {
        uart_init();
    }

    while (uart_read(uart_base, UART_FR) & UART_FR_TXFF) {
        /* Busy wait */
    }

    if (c == '\n') {
        while (uart_read(uart_base, UART_FR) & UART_FR_TXFF) {
            /* Busy wait */
        }
        uart_write(uart_base, UART_DR, '\r');
    }

    uart_write(uart_base, UART_DR, (uint32_t)c);
}

void uart_puts(const char *s)
{
    while (*s) {
        uart_putc(*s);
        s++;
    }
}

char uart_getc(void)
{
    if (!uart_base) {
        uart_init();
    }

    while (uart_read(uart_base, UART_FR) & UART_FR_RXFE) {
        /* Busy wait */
    }

    return (char)uart_read(uart_base, UART_DR);
}

int uart_getc_nonblock(void)
{
    if (!uart_base) {
        uart_init();
    }

    if (uart_read(uart_base, UART_FR) & UART_FR_RXFE) {
        return -1;
    }

    return (int)uart_read(uart_base, UART_DR);
}

#elif defined(__x86_64__)

/* ============================================================
 * x86_64 16550 UART (COM1 Serial Port at I/O 0x3F8)
 * ============================================================ */

/* COM1 I/O ports */
#define COM1_PORT  UART_BASE

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

void uart_init(void)
{
    /* Disable interrupts */
    outb(COM1_PORT + 1, 0x00);

    /* Set DLAB to configure baud rate */
    outb(COM1_PORT + 3, 0x80);

    /* 115200 baud: divisor = 1 */
    outb(COM1_PORT + 0, 0x01);
    outb(COM1_PORT + 1, 0x00);

    /* 8N1, clear DLAB */
    outb(COM1_PORT + 3, 0x03);

    /* Enable FIFO, clear, 14-byte threshold */
    outb(COM1_PORT + 2, 0xC7);

    /* RTS/DSR set */
    outb(COM1_PORT + 4, 0x0B);
}

void uart_putc(char c)
{
    /* Wait for transmitter holding register empty */
    while (!(inb(COM1_PORT + 5) & 0x20)) {
        /* Busy wait */
    }

    if (c == '\n') {
        while (!(inb(COM1_PORT + 5) & 0x20)) {
            /* Busy wait */
        }
        outb(COM1_PORT, '\r');
    }

    outb(COM1_PORT, (uint8_t)c);
}

void uart_puts(const char *s)
{
    while (*s) {
        uart_putc(*s);
        s++;
    }
}

char uart_getc(void)
{
    /* Wait for data ready */
    while (!(inb(COM1_PORT + 5) & 0x01)) {
        /* Busy wait */
    }

    return (char)inb(COM1_PORT);
}

int uart_getc_nonblock(void)
{
    if (!(inb(COM1_PORT + 5) & 0x01)) {
        return -1;
    }

    return (int)inb(COM1_PORT);
}

#endif /* __x86_64__ */

/* ============================================================
 * Printf Implementation
 * ============================================================ */

/* Format specifier flags */
#define F_NONE     0x00
#define F_PLUS     0x01
#define F_SPACE    0x02
#define F_HASH     0x04
#define F_ZERO     0x08
#define F_LEFT     0x10

/* Length modifiers */
#define LEN_NONE   0
#define LEN_SHORT  1
#define LEN_LONG   2
#define LEN_LONGLONG 3
#define LEN_PTR    4

/**
 * format_number - Format an integer number
 */
static void format_number(char *buf, size_t bufsize, uint64_t value, int base, int is_signed, int sign,
                          int flags, int width, int precision)
{
    char digits[] = "0123456789abcdef";
    char *p = buf;
    char *start = buf;

    (void)bufsize;
    (void)width;

    /* Handle 0 */
    if (value == 0 && precision <= 0) {
        if (is_signed && sign) {
            buf[0] = (sign == '-') ? '-' : '+';
            buf[1] = '0';
            buf[2] = '\0';
            return;
        } else if (flags & F_HASH && base == 16) {
            buf[0] = '0';
            buf[1] = 'x';
            buf[2] = '0';
            buf[3] = '\0';
            return;
        }
        *p++ = '0';
        goto done;
    }

    /* Generate digits */
    while (value > 0) {
        *p++ = digits[value % base];
        value /= base;
    }

    /* Reverse digits */
    size_t len = p - start;
    for (size_t i = 0; i < len / 2; i++) {
        char tmp = start[i];
        start[i] = start[len - 1 - i];
        start[len - 1 - i] = tmp;
    }

done:
    /* Null terminate */
    *p = '\0';
}

/**
 * vsprintf - Format a string with variable arguments
 */
static int vsprintf_impl(char *str, size_t size, const char *fmt, va_list ap)
{
    char *start = str;
    char num_buf[32];
    int written = 0;

    while (*fmt) {
        if (*fmt != '%') {
            if (str - start < (int)size - 1) {
                *str++ = *fmt;
                written++;
            }
            fmt++;
            continue;
        }

        fmt++;  /* Skip '%' */

        /* Parse flags */
        int flags = F_NONE;
        while (*fmt) {
            switch (*fmt) {
            case '+': flags |= F_PLUS; fmt++; break;
            case ' ': flags |= F_SPACE; fmt++; break;
            case '#': flags |= F_HASH;  fmt++; break;
            case '0': flags |= F_ZERO;  fmt++; break;
            case '-': flags |= F_LEFT;  fmt++; break;
            default: goto parse_width;
            }
        }

parse_width:
        /* Parse width */
        int width = 0;
        if (*fmt >= '1' && *fmt <= '9') {
            width = *fmt - '0';
            fmt++;
            while (*fmt >= '0' && *fmt <= '9') {
                width = width * 10 + (*fmt - '0');
                fmt++;
            }
        }

        /* Parse precision */
        int precision = -1;
        if (*fmt == '.') {
            fmt++;
            precision = 0;
            if (*fmt >= '0' && *fmt <= '9') {
                precision = *fmt - '0';
                fmt++;
                while (*fmt >= '0' && *fmt <= '9') {
                    precision = precision * 10 + (*fmt - '0');
                    fmt++;
                }
            }
        }

        /* Parse length modifier */
        int len_mod = LEN_NONE;
        if (*fmt == 'l') {
            len_mod = LEN_LONG;
            fmt++;
            if (*fmt == 'l') {
                len_mod = LEN_LONGLONG;
                fmt++;
            }
        } else if (*fmt == 'h') {
            len_mod = LEN_SHORT;
            fmt++;
        }

        /* Parse format specifier */
        int is_signed = 0;
        int sign = 0;
        uint64_t value = 0;

        switch (*fmt) {
        case 'd':
        case 'i':
            is_signed = 1;
            if (len_mod == LEN_LONGLONG)
                value = (int64_t)va_arg(ap, int64_t);
            else if (len_mod == LEN_LONG)
                value = (int32_t)va_arg(ap, int);
            else
                value = (int32_t)va_arg(ap, int);
            if ((int64_t)value < 0) {
                value = -(int64_t)value;
                sign = '-';
            } else if (((int64_t)value) >= 0 && (flags & F_PLUS)) {
                sign = '+';
            } else if ((flags & F_SPACE) && !sign) {
                sign = ' ';
            }
            format_number(num_buf, sizeof(num_buf), value, 10, is_signed, sign, flags, width, precision);
            break;

        case 'u':
            if (len_mod == LEN_LONGLONG)
                value = va_arg(ap, uint64_t);
            else if (len_mod == LEN_LONG)
                value = (uint32_t)va_arg(ap, int);
            else
                value = (uint32_t)va_arg(ap, int);
            format_number(num_buf, sizeof(num_buf), value, 10, 0, 0, flags, width, precision);
            break;

        case 'x':
        case 'X':
            if (len_mod == LEN_LONGLONG)
                value = va_arg(ap, uint64_t);
            else if (len_mod == LEN_LONG)
                value = (uint32_t)va_arg(ap, int);
            else
                value = (uint32_t)va_arg(ap, int);
            format_number(num_buf, sizeof(num_buf), value, 16, 0, 0, flags, width, precision);
            break;

        case 'p':
            value = (uintptr_t)va_arg(ap, void *);
            snprintf(num_buf, sizeof(num_buf), "0x%lx", (unsigned long)value);
            break;

        case 's':
            {
                const char *s = va_arg(ap, const char *);
                if (s == NULL) s = "(null)";
                size_t slen = strlen(s);
                if (precision >= 0 && (int)slen > precision)
                    slen = precision;
                int pad = width - (int)slen;
                for (int i = 0; i < pad && !(flags & F_LEFT); i++) {
                    if (str - start < (int)size - 1) *str++ = ' ';
                    written++;
                }
                for (size_t i = 0; i < slen; i++) {
                    if (str - start < (int)size - 1) *str++ = s[i];
                    written++;
                }
                for (int i = 0; i < pad && (flags & F_LEFT); i++) {
                    if (str - start < (int)size - 1) *str++ = ' ';
                    written++;
                }
                fmt++;
                continue;
            }

        case 'c':
            {
                char c = (char)va_arg(ap, int);
                *str++ = c;
                written++;
                fmt++;
                continue;
            }

        case '%':
            if (str - start < (int)size - 1) *str++ = '%';
            written++;
            fmt++;
            continue;

        default:
            if (str - start < (int)size - 1) *str++ = *fmt;
            written++;
            fmt++;
            continue;
        }

        /* Output formatted number with padding */
        int num_len = strlen(num_buf);
        int pad = width - num_len;
        if (sign) pad--;

        for (int i = 0; i < pad && !(flags & F_LEFT); i++) {
            if (str - start < (int)size - 1) *str++ = '0';
            written++;
        }
        if (sign && str - start < (int)size - 1) {
            *str++ = sign;
            written++;
        }
        for (char *p = num_buf; *p && str - start < (int)size - 1; p++, str++, written++);

        for (int i = 0; i < pad && (flags & F_LEFT); i++) {
            if (str - start < (int)size - 1) *str++ = ' ';
            written++;
        }

        fmt++;
    }

    if (str - start < (int)size - 1) *str = '\0';
    else if (size > 0) str[size - 1] = '\0';

    return written;
}

/**
 * printk - Kernel print with formatting (like printf)
 */
int printk(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    int ret;

    va_start(ap, fmt);
    ret = vsprintf_impl(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    uart_puts(buf);
    return ret;
}

/**
 * panic - Kernel panic with halt
 * @msg: Panic message
 *
 * Prints a panic message and halts the system. Never returns.
 */
void panic(const char *msg)
{
    printk("\n");
    printk("************************ PANIC ************************\n");
    printk("PANIC: %s\n", msg);
    printk("************************************************************\n");
    printk("System halted.\n\n");

    for (;;) {
#if defined(__aarch64__)
        __asm__ volatile("wfi");
#elif defined(__x86_64__)
        __asm__ volatile("hlt");
#endif
    }
}
