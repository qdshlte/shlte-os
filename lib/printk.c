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
 * UART Driver
 * ============================================================ */

/**
 * uart_wait_tx_empty - Wait until UART transmit holding register is empty
 */
static void uart_wait_tx_empty(void)
{
    volatile uint32_t *uart_fr = (volatile uint32_t *)(UART_BASE + UART_FR);
    while (*uart_fr & 0x20) {  /* TXFE bit */
        /* Busy wait */
    }
}

/**
 * uart_wait_tx_fifo_empty - Wait until UART transmit FIFO is empty
 */
static void uart_wait_tx_fifo_empty(void)
{
    volatile uint32_t *uart_fr = (volatile uint32_t *)(UART_BASE + UART_FR);
    while (*uart_fr & 0x30) {  /* TXFE | TXFF */
        /* Busy wait */
    }
}

/**
 * uart_init - Initialize UART for output
 */
void uart_init(void)
{
    volatile uint32_t *uart_cr = (volatile uint32_t *)(UART_BASE + UART_CR);

    /* Disable UART first */
    *uart_cr = 0;

    /* Set baud rate divisor (115200 bps default) */
    /* For 1MHz UART clock, DIV = UARTCLK / (16 * BAUD) = 1000000 / (16 * 115200) ≈ 0.54 */
    /* Use default divisor of 0 for simplicity */
    volatile uint32_t *uart_ibrd = (volatile uint32_t *)(UART_BASE + 0x24);
    volatile uint32_t *uart_fbrd = (volatile uint32_t *)(UART_BASE + 0x28);
    *uart_ibrd = 0;
    *uart_fbrd = 0;

    /* Enable UART, TX, RX */
    *uart_cr = UART_CR_UARTEN | UART_CR_TXE | UART_CR_RXE;
}

/**
 * uart_putc - Output a single character to UART
 */
void uart_putc(char c)
{
    if (c == '\n') {
        uart_wait_tx_empty();
        volatile uint32_t *uart_dr = (volatile uint32_t *)(UART_BASE + UART_DR);
        *uart_dr = '\r';
    }

    uart_wait_tx_empty();
    volatile uint32_t *uart_dr = (volatile uint32_t *)(UART_BASE + UART_DR);
    *uart_dr = c;
}

/**
 * uart_puts - Output a string to UART
 */
void uart_puts(const char *s)
{
    while (*s) {
        uart_putc(*s);
        s++;
    }
}

/**
 * uart_getc - Read a character from UART (blocking)
 */
char uart_getc(void)
{
    volatile uint32_t *uart_fr = (volatile uint32_t *)(UART_BASE + UART_FR);
    while (*uart_fr & 0x10) {  /* RXFE */
        /* Wait */
    }
    volatile uint32_t *uart_dr = (volatile uint32_t *)(UART_BASE + UART_DR);
    return (char)(*uart_dr & 0xFF);
}

/**
 * uart_getc_nonblock - Try to read a character (non-blocking)
 * Returns -1 if no character available
 */
int uart_getc_nonblock(void)
{
    volatile uint32_t *uart_fr = (volatile uint32_t *)(UART_BASE + UART_FR);
    if (*uart_fr & 0x10) {  /* RXFE */
        return -1;
    }
    volatile uint32_t *uart_dr = (volatile uint32_t *)(UART_BASE + UART_DR);
    return (int)(*uart_dr & 0xFF);
}

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
    int need_prefix = 0;
    char prefix[2] = {0};
    int digit_count = 0;

    /* Handle 0 */
    if (value == 0 && precision <= 0) {
        if (is_signed && sign) {
            prefix[0] = (sign == '-') ? '-' : '+';
        } else if (flags & F_HASH && base == 16) {
            prefix[0] = '0';
            prefix[1] = 'x';
        }
        *p++ = '0';
        goto done;
    }

    /* Generate digits */
    while (value > 0) {
        *p++ = digits[value % base];
        value /= base;
        digit_count++;
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
