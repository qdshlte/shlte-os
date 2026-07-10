---
name: arm64-freestanding-printf
description: Porting printf/snprintf to freestanding AArch64 — va_arg width pitfalls (%lx, %zu, %lu read 64-bit values), handling 'z' length modifier, and avoiding internal snprintf recursion.
source: auto-skill
extracted_at: '2026-07-05T04:58:15.898Z'
---

# ARM64 Freestanding Printf Format Specifiers

## When to use
Writing a custom `printf`/`printk` implementation for a bare-metal ARM64 kernel in C (freestanding, no libc). The symptom is garbled output, missing values, or incorrect va_list alignment when using `%lx`, `%zu`, `%lu`, `%d`, etc.

## Root cause: va_arg width on AArch64

On AArch64 (AAPCS64):
- `int` and `unsigned int` are 32-bit
- `long` and `unsigned long` are 64-bit
- `size_t` is `unsigned long` (64-bit)
- `uint64_t` is 64-bit

The va_list stores all variadic arguments in 8-byte aligned slots. Using `va_arg(ap, int)` reads 4 bytes but advances the pointer by **8 bytes** (the register slot size). This works correctly for `%d`/`%u` (reads lower 4 bytes, skips upper 4).

But if you use `va_arg(ap, int)` for a `%lx` argument (which was passed as 64-bit), you read the correct lower 32 bits but the pointer advances correctly to the next slot. **This is actually fine for reading the value** (lower 32 bits of a 0-padded 64-bit value), but the aliasing confusion often leads to the wrong code pattern.

**The real bug**: using `va_arg(ap, int)` instead of `va_arg(ap, unsigned long)` when the format specifier `l` modifier is present.

## Fixed patterns for each format specifier

### `%lx` / `%lu` / `%ld` (len_mod == LEN_LONG)

```c
// WRONG — reads 32 bits, truncates 64-bit values
value = (uint32_t)va_arg(ap, int);

// RIGHT — reads full 64-bit slot
value = va_arg(ap, unsigned long);   // for unsigned
value = (int64_t)va_arg(ap, long);   // for signed
```

### `%llx` / `%llu` (len_mod == LEN_LONGLONG)

```c
value = va_arg(ap, uint64_t);   // always correct
```

### `%zu` / `%zd` (size_t / ssize_t)

The `z` length modifier is NOT `l` or `h` — it must be parsed explicitly:

```c
// Add 'z' to the length modifier parser:
} else if (*fmt == 'z') {
    len_mod = LEN_LONG;   /* size_t = unsigned long on AArch64 */
    fmt++;
}
```

### `%d` / `%i` / `%u` / `%x` (no modifier)

```c
value = (int32_t)va_arg(ap, int);   // correct: 32-bit read
```

### `%p` (pointer)

Do NOT call `snprintf()` recursively — your vsnprintf may call vsprintf_impl:

```c
// WRONG — recursion or infinite loop
case 'p':
    snprintf(num_buf, sizeof(num_buf), "0x%lx", ...);
    break;

// RIGHT — format manually or use format_number directly
case 'p':
    num_buf[0] = '0';
    num_buf[1] = 'x';
    format_number(num_buf + 2, sizeof(num_buf) - 2, (uint64_t)va_arg(ap, void *), 16, 0, 0, 0, 0, -1);
    break;
```

## The `##` (hash) flag for `%#x`

The `%#x` flag outputs `0x` prefix. In a freestanding implementation, handle it explicitly in `format_number`:

```c
if (flags & F_HASH && base == 16) {
    buf[0] = '0';
    buf[1] = 'x';
    buf += 2;
}
```

## Verifying the implementation

Test with a variety of calls from your kernel:

```c
printk("Testing: %lx %zu %u %s\n", large_ptr, count, val, "str");
```

If the output is garbled or shows raw format characters like `zu`, the `z` parser is missing. If hex values are truncated to 32 bits, the `l` handler is using `va_arg(ap, int)`.
