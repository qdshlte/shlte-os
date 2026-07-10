/* ============================================================
 * init.S — Shlte OS init shell (raw binary for SPFS rootfs)
 *
 * Built-in commands: help, ls, cat <file>, exec <file>
 * Compiled as a raw binary with aarch64-linux-gnu-as + ld --oformat=binary.
 * Must be position-independent (all ADR, no absolute addresses).
 * Entry point is the first byte of the binary.
 * ============================================================ */

    .section .text
    .globl _start

_start:
    /* Main loop */
1:
    /* Print prompt */
    adr     x0, prompt_str
    bl      print_str

    /* Read a line from UART into cmd_buf */
    adr     x1, cmd_buf         /* buf */
    mov     x2, #254            /* max count */
    mov     x8, #2              /* SYS_READ */
    mov     x0, #0              /* fd = stdin */
    svc     #0
    /* x0 now has bytes read. Replace trailing newline with null. */
    adr     x1, cmd_buf
    /* Find newline or null in the buffer and terminate there */
    mov     x2, #0
    adr     x3, cmd_buf
0:  ldrb    w4, [x3, x2]
    cmp     w4, #'\n'
    beq     1f
    cmp     w4, #0
    beq     1f
    add     x2, x2, #1
    cmp     x2, #254
    blt     0b
1:  strb    wzr, [x3, x2]      /* null-terminate at newline */

    /* If empty line, just loop */
    ldrb    w4, [x3]
    cmp     w4, #0
    beq     main_loop

    /* --- Parse first word (command) --- */
    /* cmd_end = cmd_buf; skip leading spaces */
    adr     x19, cmd_buf        /* x19 = cmd_buf */
    adr     x20, arg_buf        /* x20 = arg_buf */
    mov     x21, #0             /* x21 = arg_index */

    /* Skip spaces */
    mov     x22, #0             /* x22 = offset in cmd_buf */
    adr     x23, cmd_buf
2:  ldrb    w0, [x23, x22]
    cmp     w0, #' '
    bne     3f
    add     x22, x22, #1
    b       2b
3:  /* Found start of command */
    mov     x24, x22            /* x24 = cmd_start offset */

    /* Find end of first word (space or null) */
4:  ldrb    w0, [x23, x22]
    cmp     w0, #0
    beq     5f                  /* no argument */
    cmp     w0, #' '
    beq     5f
    add     x22, x22, #1
    b       4b
5:
    /* null-terminate command at space */
    strb    wzr, [x23, x22]
    add     x22, x22, #1        /* move past space to argument */

    /* Copy argument after the space to arg_buf */
    mov     x25, #0
6:  ldrb    w0, [x23, x22]
    cmp     w0, #0
    beq     7f
    strb    w0, [x20, x25]
    add     x22, x22, #1
    add     x25, x25, #1
    cmp     x25, #127
    blt     6b
7:  strb    wzr, [x20, x25]    /* null-terminate arg */

    /* Command pointer in x19, arg pointer in x20 */
    /* Compare command */
    adr     x0, help_cmd
    mov     x1, x19
    bl      str_eq
    cmp     x0, #1
    beq     cmd_help

    adr     x0, ls_cmd
    mov     x1, x19
    bl      str_eq
    cmp     x0, #1
    beq     cmd_ls

    adr     x0, cat_cmd
    mov     x1, x19
    bl      str_eq
    cmp     x0, #1
    beq     cmd_cat

    adr     x0, exec_cmd
    mov     x1, x19
    bl      str_eq
    cmp     x0, #1
    beq     cmd_exec

    /* Unknown command */
    adr     x0, unknown_msg
    bl      print_str
    b       main_loop

/* ============================================================
 * Main loop branch target
 * ============================================================ */
main_loop:
    b       1b

/* ============================================================
 * Command: help
 * ============================================================ */
cmd_help:
    adr     x0, help_text
    bl      print_str
    b       main_loop

/* ============================================================
 * Command: ls
 * SYS_LIST(buf, max) — buf is FS_MAX_NAME * max_count bytes
 * ============================================================ */
cmd_ls:
    /* Use the ls_buf area (after the main buffers) */
    adr     x0, ls_buf
    mov     x1, #32             /* max 32 entries */
    mov     x8, #18             /* SYS_LIST */
    svc     #0
    mov     x19, x0             /* x19 = count */

    cmp     x19, #0
    ble     ls_done

    mov     x20, #0             /* index */
    adr     x21, ls_buf
    mov     x22, #128           /* FS_MAX_NAME stride */
ls_loop:
    mul     x23, x20, x22       /* offset = index * 128 */
    add     x0, x21, x23
    bl      print_str
    /* Print newline */
    adr     x0, newline_str
    bl      print_str

    add     x20, x20, #1
    cmp     x20, x19
    blt     ls_loop

ls_done:
    b       main_loop

/* ============================================================
 * Command: cat <file>
 * SYS_OPEN(path, 0) → read and print content → SYS_CLOSE
 * ============================================================ */
cmd_cat:
    /* Check if arg is non-empty */
    ldrb    w0, [x20]
    cmp     w0, #0
    bne     cat_open

    adr     x0, usage_cat
    bl      print_str
    b       main_loop

cat_open:
    mov     x0, x20             /* path */
    mov     x1, #0              /* flags */
    mov     x8, #4              /* SYS_OPEN */
    svc     #0
    mov     x19, x0             /* x19 = fd */

    cmp     x19, #0
    bge     cat_read_loop

    adr     x0, not_found_msg
    bl      print_str
    b       main_loop

cat_read_loop:
    /* Read a chunk */
    adr     x1, cat_buf         /* buf */
    mov     x2, #64             /* count */
    mov     x0, x19             /* fd */
    mov     x8, #2              /* SYS_READ */
    svc     #0
    mov     x20, x0             /* x20 = bytes read */

    cmp     x20, #0
    ble     cat_close           /* EOF or error */

    /* Write to UART */
    adr     x1, cat_buf
    mov     x2, x20             /* len */
    mov     x0, #1              /* fd = stdout */
    mov     x8, #3              /* SYS_WRITE */
    svc     #0

    b       cat_read_loop

cat_close:
    mov     x0, x19             /* fd */
    mov     x8, #5              /* SYS_CLOSE */
    svc     #0
    b       main_loop

/* ============================================================
 * Command: exec <file>
 * SYS_EXEC(path) — replaces this process with the new binary
 * ============================================================ */
cmd_exec:
    ldrb    w0, [x20]
    cmp     w0, #0
    bne     exec_go

    adr     x0, usage_exec
    bl      print_str
    b       main_loop

exec_go:
    mov     x0, x20             /* path */
    mov     x8, #17             /* SYS_EXEC */
    svc     #0

    /* If exec fails, we come back here */
    adr     x0, exec_fail_msg
    bl      print_str
    b       main_loop

/* ============================================================
 * print_str — print null-terminated string to UART
 * x0 = string pointer
 * Clobbers x0-x2, x8
 * ============================================================ */
print_str:
    mov     x2, #0              /* length */
    mov     x3, x0              /* save pointer */
1:  ldrb    w4, [x3, x2]
    cmp     w4, #0
    beq     2f
    add     x2, x2, #1
    b       1b
2:  mov     x1, x3              /* buf = saved pointer */
    mov     x0, #1              /* fd = stdout */
    mov     x8, #3              /* SYS_WRITE */
    svc     #0
    ret

/* ============================================================
 * str_eq — compare two null-terminated strings
 * x0 = str1, x1 = str2
 * Returns x0 = 1 if equal, 0 if not
 * ============================================================ */
str_eq:
    mov     x2, #0
1:  ldrb    w3, [x0, x2]
    ldrb    w4, [x1, x2]
    cmp     w3, w4
    bne     2f                  /* not equal */
    cmp     w3, #0
    beq     3f                  /* both ended = equal */
    add     x2, x2, #1
    b       1b
2:  mov     x0, #0
    ret
3:  mov     x0, #1
    ret

/* ============================================================
 * String constants (read-only data)
 * ============================================================ */
prompt_str:
    .ascii  "shlte> "
newline_str:
    .ascii  "\n"
help_cmd:
    .asciz  "help"
ls_cmd:
    .asciz  "ls"
cat_cmd:
    .asciz  "cat"
exec_cmd:
    .asciz  "exec"

help_text:
    .ascii  "Shlte OS Shell\n"
    .ascii  "Commands:\n"
    .ascii  "  help          - Show this help\n"
    .ascii  "  ls            - List files\n"
    .ascii  "  cat <file>    - Print file content\n"
    .ascii  "  exec <file>   - Run a binary\n"
    .asciz  "\n"

unknown_msg:
    .asciz  "Unknown command. Try 'help'.\n"
not_found_msg:
    .asciz  "File not found.\n"
usage_cat:
    .asciz  "Usage: cat <filename>\n"
usage_exec:
    .asciz  "Usage: exec <filename>\n"
exec_fail_msg:
    .asciz  "exec failed.\n"

/* ============================================================
 * Writable data (placed after code in the raw binary)
 * ============================================================ */
    .align 4
    .space  4                   /* padding */

/* Command buffer: 256 bytes */
    .globl cmd_buf
cmd_buf:
    .space  256

/* Argument buffer: 128 bytes */
    .globl arg_buf
arg_buf:
    .space  128

/* Cat read buffer: 64 bytes */
    .globl cat_buf
cat_buf:
    .space  64

/* Ls output buffer: 32 entries * 128 bytes each = 4096 bytes */
    .globl ls_buf
ls_buf:
    .space  4096

/* ============================================================
 * End marker for the assembler
 * ============================================================ */
    .end
