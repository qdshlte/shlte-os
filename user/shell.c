/*
 * user/shell.c - Shlte Shell (shltesh)
 *
 * A bash-compatible interactive shell for the Shlte OS.
 * Built-in commands: help, ls, cat, exec, echo, clear, ps,
 *   uname, mem, date, pwd, cd
 * Compatible with .deb packages via sap package manager.
 */

#include "libc.h"

/* ============================================================
 * Shell state
 * ============================================================ */

#define MAX_ARGS    16
#define MAX_CMD     256
#define PROMPT      "\033[1;32mshlte\033[0m:\033[1;34m~\033[0m$ "

static char cmd_buf[MAX_CMD];
static char cwd[256] = "/";

/* ============================================================
 * Forward declarations
 * ============================================================ */
static int sap_main(int argc, char **argv);

/* ============================================================
 * Built-in commands
 * ============================================================ */

static int cmd_help(int argc, char **argv)
{
    (void)argc; (void)argv;
    printf("Shlte Shell (shltesh) v0.2\n\n");
    printf("Built-in commands:\n");
    printf("  help          - Show this help\n");
    printf("  echo [text]   - Print text\n");
    printf("  clear         - Clear screen\n");
    printf("  ls            - List files\n");
    printf("  cat <file>    - Print file contents\n");
    printf("  exec <prog>   - Run a program\n");
    printf("  ps            - Show processes\n");
    printf("  uname         - System information\n");
    printf("  mem           - Memory usage\n");
    printf("  date          - Show date/time\n");
    printf("  reboot        - Restart system\n");
    printf("  exit          - Exit shell\n");
    return 0;
}

static int cmd_echo(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        if (i > 1) printf(" ");
        printf("%s", argv[i]);
    }
    printf("\n");
    return 0;
}

static int cmd_clear(int argc, char **argv)
{
    (void)argc; (void)argv;
    printf("\033[2J\033[H");
    return 0;
}

static int cmd_ls(int argc, char **argv)
{
    (void)argc; (void)argv;
    char names[64][FS_MAX_NAME];
    int count = listdir(names, 64);

    if (count < 0) {
        printf("ls: cannot list directory\n");
        return -1;
    }

    printf("  NAME            SIZE\n");
    printf("  ---------------- ----\n");
    for (int i = 0; i < count; i++) {
        printf("  %-16s\n", names[i]);
    }
    printf("  %d file(s)\n", count);
    return 0;
}

static int cmd_cat(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: cat <file>\n");
        return -1;
    }

    int fd = open(argv[1], O_RDONLY);
    if (fd < 0) {
        printf("cat: %s: No such file\n", argv[1]);
        return -1;
    }

    char buf[256];
    ssize_t n;
    while ((n = read(fd + 2, buf, sizeof(buf) - 1)) > 0) {
        buf[n] = '\0';
        printf("%s", buf);
    }
    close(fd + 2);
    return 0;
}

static int cmd_exec(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: exec <program>\n");
        return -1;
    }

    char path[256];
    /* Try with and without /mnt/ prefix */
    if (argv[1][0] == '/') {
        strcpy(path, argv[1]);
    } else {
        strcpy(path, "/mnt/");
        int plen = strlen(path);
        strncpy(path + plen, argv[1], sizeof(path) - plen - 1);
    }

    printf("exec: loading %s...\n", path);
    int ret = exec(path);
    if (ret != 0) {
        printf("exec: failed to load '%s'\n", path);
    }
    return ret;
}

static int cmd_ps(int argc, char **argv)
{
    (void)argc; (void)argv;
    printf("  PID  STATE    NAME\n");
    printf("  ---- -------- ----------------\n");
    printf("  %-4d running  shell\n", getpid());
    return 0;
}

static int cmd_uname(int argc, char **argv)
{
    (void)argc; (void)argv;
    if (argc > 1 && strcmp(argv[1], "-a") == 0) {
        printf("Shlte OS shlte 0.2.0 aarch64 ARM64\n");
    } else {
        printf("Shlte OS\n");
    }
    return 0;
}

static int cmd_mem(int argc, char **argv)
{
    (void)argc; (void)argv;
    printf("Memory: 512 MB total, heap available\n");
    return 0;
}

static int cmd_date(int argc, char **argv)
{
    (void)argc; (void)argv;
    printf("2026-07-10 (UTC)\n");
    return 0;
}

static int cmd_reboot(int argc, char **argv)
{
    (void)argc; (void)argv;
    printf("Rebooting...\n");
    exit(0);
    return 0;
}

static int cmd_exit(int argc, char **argv)
{
    (void)argc; (void)argv;
    printf("Goodbye.\n");
    exit(0);
    return 0;
}

/* ============================================================
 * Command dispatch
 * ============================================================ */

typedef struct {
    const char *name;
    int (*func)(int argc, char **argv);
    const char *desc;
} builtin_cmd_t;

static const builtin_cmd_t builtins[] = {
    {"help",   cmd_help,   "Show help"},
    {"echo",   cmd_echo,   "Print text"},
    {"clear",  cmd_clear,  "Clear screen"},
    {"ls",     cmd_ls,     "List files"},
    {"cat",    cmd_cat,    "Print file"},
    {"exec",   cmd_exec,   "Run program"},
    {"sap",    sap_main,   "Package manager"},
    {"ps",     cmd_ps,     "Process list"},
    {"uname",  cmd_uname,  "System info"},
    {"mem",    cmd_mem,    "Memory info"},
    {"date",   cmd_date,   "Show date"},
    {"reboot", cmd_reboot, "Restart"},
    {"exit",   cmd_exit,   "Exit shell"},
    {NULL, NULL, NULL}
};

static int dispatch(char *line)
{
    /* Tokenize */
    char *argv[MAX_ARGS];
    int argc = 0;
    char *token = strtok(line, " \t");
    while (token && argc < MAX_ARGS - 1) {
        argv[argc++] = token;
        token = strtok(NULL, " \t");
    }
    argv[argc] = NULL;

    if (argc == 0) return 0;

    /* Find and execute built-in */
    for (int i = 0; builtins[i].name; i++) {
        if (strcmp(argv[0], builtins[i].name) == 0) {
            return builtins[i].func(argc, argv);
        }
    }

    /* Try as external program (via exec) */
    printf("%s: command not found. Try 'help'.\n", argv[0]);
    return -1;
}

/* ============================================================
 * Main
 * ============================================================ */

int main(void)
{
    printf("\n\033[1;36m"
           "  ==================================\n"
           "    Welcome to Shlte OS v0.2\n"
           "    Type 'help' for commands.\n"
           "  ==================================\n"
           "\033[0m\n");

    while (1) {
        printf("%s", PROMPT);
        gets(cmd_buf, sizeof(cmd_buf));

        if (cmd_buf[0] == '\0') continue;

        dispatch(cmd_buf);
    }

    return 0;
}

/* ============================================================
 * Built-in: SAP Package Manager (included from sap.c)
 * ============================================================ */
#include "sap.c"
