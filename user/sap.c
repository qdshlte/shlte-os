/*
 * user/sap.c - SAP Package Manager (Shlte Advanced Packager)
 *
 * Can be compiled standalone (SAP_STANDALONE) or included as
 * a built-in command in the shell.
 *
 * Supports:
 *   - SAP native format (.sap) — fast, simple
 *   - .deb package format — basic ar extraction (uncompressed)
 *
 * Usage: sap <command> [args...]
 *   sap install <package>    Install a .sap or .deb package
 *   sap remove  <name>       Remove an installed package
 *   sap list                 List installed packages
 *   sap info   <package>     Show package information
 *   sap help                 Show help
 */

#include "libc.h"

#ifndef SAP_STANDALONE
#define static
#endif

/* ============================================================
 * Constants
 * ============================================================ */

#define SAP_MAGIC       0x21504153   /* "SAP!" */
#define SAP_VERSION     1
#define MAX_PKG_NAME    64
#define MAX_PKG_FILES   128
#define MAX_PKG_DESC    256
#define PKG_DB_PATH     "/mnt/sap_db"
#define PKG_DB_MAX      64

/* ============================================================
 * Package entry (database)
 * ============================================================ */

typedef struct {
    char name[MAX_PKG_NAME];
    char version[32];
    char desc[MAX_PKG_DESC];
    int file_count;
} pkg_entry_t;

static pkg_entry_t pkg_db[PKG_DB_MAX];
static int pkg_db_count = 0;

/* ============================================================
 * SAP native package header
 * ============================================================ */

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t version;
    uint32_t file_count;
    char     pkg_name[32];
    char     pkg_ver[16];
    char     pkg_desc[128];
} sap_header_t;

typedef struct __attribute__((packed)) {
    char     name[64];
    uint32_t size;
    uint32_t mode;
} sap_file_entry_t;

/* ============================================================
 * Utility functions
 * ============================================================ */

static int file_exists(const char *path)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) return 0;
    close(fd + 2);
    return 1;
}

static int make_path(const char *base, const char *name, char *out, int out_size)
{
    int blen = strlen(base);
    int nlen = strlen(name);
    if (blen + nlen + 2 > out_size) return -1;

    /* Use /mnt/ prefix for SPFS compatibility */
    if (base[0] != '/') {
        memcpy(out, "/mnt/", 5);
        memcpy(out + 5, name, nlen);
        out[5 + nlen] = '\0';
    } else {
        memcpy(out, name, nlen);
        out[nlen] = '\0';
    }
    return 0;
}

/* ============================================================
 * Package database
 * ============================================================ */

static void pkg_db_load(void)
{
    pkg_db_count = 0;

    /* Try to load the SAP database file */
    /* For now, start empty (no persistent DB yet) */
}

static void pkg_db_save(void)
{
    /* Save package database to /mnt/sap_db */
    /* Format: one entry per line: name|version|desc|files */
    /* Not yet implemented for SPFS flat filesystem */
}

static int pkg_db_find(const char *name)
{
    for (int i = 0; i < pkg_db_count; i++) {
        if (strcmp(pkg_db[i].name, name) == 0) return i;
    }
    return -1;
}

static void pkg_db_add(const char *name, const char *ver, const char *desc, int files)
{
    if (pkg_db_count >= PKG_DB_MAX) {
        printf("sap: package database full\n");
        return;
    }
    pkg_entry_t *e = &pkg_db[pkg_db_count++];
    strncpy(e->name, name, MAX_PKG_NAME - 1);
    strncpy(e->version, ver, 31);
    strncpy(e->desc, desc, MAX_PKG_DESC - 1);
    e->file_count = files;
}

static void pkg_db_remove(int idx)
{
    if (idx < 0 || idx >= pkg_db_count) return;
    for (int i = idx; i < pkg_db_count - 1; i++) {
        pkg_db[i] = pkg_db[i + 1];
    }
    pkg_db_count--;
}

/* ============================================================
 * .deb / AR archive parser
 * ============================================================ */

#define AR_MAGIC    "!<arch>\n"
#define AR_HDR_SIZE 60

typedef struct __attribute__((packed)) {
    char name[16];
    char date[12];
    char uid[6];
    char gid[6];
    char mode[8];
    char size[10];
    char magic[2];   /* "`\n" */
} ar_hdr_t;

static int ar_parse_size(const char *field, int len)
{
    int val = 0;
    for (int i = 0; i < len; i++) {
        if (field[i] >= '0' && field[i] <= '9')
            val = val * 10 + (field[i] - '0');
    }
    return val;
}

/* ============================================================
 * SAP native install
 * ============================================================ */

static int sap_install_native(const char *path)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        printf("sap: cannot open '%s'\n", path);
        return -1;
    }

    sap_header_t hdr;
    ssize_t n = read(fd + 2, &hdr, sizeof(hdr));
    if (n < (ssize_t)sizeof(hdr)) {
        printf("sap: failed to read header\n");
        close(fd + 2);
        return -1;
    }

    if (hdr.magic != SAP_MAGIC) {
        printf("sap: invalid package format\n");
        close(fd + 2);
        return -1;
    }

    printf("Installing: %s v%s\n", hdr.pkg_name, hdr.pkg_ver);
    printf("Description: %s\n", hdr.pkg_desc);

    int files_installed = 0;
    for (uint32_t i = 0; i < hdr.file_count; i++) {
        sap_file_entry_t entry;
        if (read(fd + 2, &entry, sizeof(entry)) != sizeof(entry)) break;

        char outpath[128];
        make_path("/mnt/", entry.name, outpath, sizeof(outpath));

        printf("  -> %s (%u bytes)\n", entry.name, entry.size);

        /* Create the file */
        int out_fd = open(outpath, O_WRONLY | O_CREAT | O_TRUNC);
        if (out_fd < 0) {
            printf("sap: cannot create '%s'\n", outpath);
            continue;
        }

        /* Copy data in chunks */
        char buf[256];
        uint32_t remaining = entry.size;
        while (remaining > 0) {
            uint32_t chunk = remaining > sizeof(buf) ? sizeof(buf) : remaining;
            ssize_t rn = read(fd + 2, buf, chunk);
            if (rn <= 0) break;
            write(out_fd + 2, buf, (size_t)rn);
            remaining -= (uint32_t)rn;
        }
        close(out_fd + 2);
        files_installed++;
    }
    close(fd + 2);

    /* Add to package DB */
    pkg_db_add(hdr.pkg_name, hdr.pkg_ver, hdr.pkg_desc, files_installed);
    pkg_db_save();

    printf("sap: installed %s v%s (%d files)\n", hdr.pkg_name, hdr.pkg_ver, files_installed);
    return 0;
}

/* ============================================================
 * .deb install (basic AR extraction)
 * ============================================================ */

static int sap_install_deb(const char *path)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        printf("sap: cannot open '%s'\n", path);
        return -1;
    }

    /* Read AR magic */
    char magic[8];
    if (read(fd + 2, magic, 8) != 8 || memcmp(magic, AR_MAGIC, 8) != 0) {
        printf("sap: not a valid .deb package\n");
        close(fd + 2);
        return -1;
    }

    printf("Parsing .deb package...\n");

    char pkg_name[64] = "unknown";
    char pkg_ver[32] = "0.0";
    char pkg_desc[256] = "";
    int files_installed = 0;

    /* Parse AR entries */
    for (int entry_num = 0; entry_num < 10; entry_num++) {
        ar_hdr_t ar_hdr;
        ssize_t n = read(fd + 2, &ar_hdr, sizeof(ar_hdr));
        if (n != sizeof(ar_hdr)) break;

        int size = ar_parse_size(ar_hdr.size, 10);
        if (size <= 0) break;

        /* Extract name (trim trailing spaces/slashes) */
        char ename[64];
        int ename_len = 0;
        for (int i = 0; i < 16 && ar_hdr.name[i] != ' ' && ar_hdr.name[i] != '/'; i++) {
            ename[ename_len++] = ar_hdr.name[i];
        }
        ename[ename_len] = '\0';

        printf("  AR entry: %s (%d bytes)\n", ename, size);

        if (strcmp(ename, "debian-binary") == 0) {
            /* Skip version file */
            char buf[16];
            read(fd + 2, buf, size < 16 ? size : 16);
        } else if (strncmp(ename, "control.", 8) == 0) {
            /* Read control file for package info */
            char *buf = (char *)malloc(size + 1);
            if (buf) {
                read(fd + 2, buf, size);
                buf[size] = '\0';

                /* Parse key fields from control */
                char *line = buf;
                while (*line) {
                    char *end = strchr(line, '\n');
                    if (end) *end = '\0';

                    if (strncmp(line, "Package:", 8) == 0)
                        strncpy(pkg_name, line + 9, 63);
                    else if (strncmp(line, "Version:", 8) == 0)
                        strncpy(pkg_ver, line + 9, 31);
                    else if (strncmp(line, "Description:", 12) == 0)
                        strncpy(pkg_desc, line + 13, 255);

                    if (end) line = end + 1;
                    else break;
                }
                free(buf); /* Note: free is a no-op in current kernel */
            }
        } else if (strncmp(ename, "data.", 5) == 0) {
            /* Data archive - copy as-is for now */
            printf("sap: .deb data payload (%d bytes) — extracting...\n", size);

            /* For now, save data as a single blob since SPFS is flat */
            char outpath[128];
            make_path("/mnt/", "deb_data.tar", outpath, sizeof(outpath));
            int out_fd = open(outpath, O_WRONLY | O_CREAT | O_TRUNC);
            if (out_fd >= 0) {
                char buf[256];
                int remaining = size;
                while (remaining > 0) {
                    int chunk = remaining > 256 ? 256 : remaining;
                    ssize_t rn = read(fd + 2, buf, chunk);
                    if (rn <= 0) break;
                    write(out_fd + 2, buf, (size_t)rn);
                    remaining -= rn;
                }
                close(out_fd + 2);
                files_installed++;
            }
        } else {
            /* Skip unknown entries */
            char buf[256];
            int remaining = size;
            while (remaining > 0) {
                int chunk = remaining > 256 ? 256 : remaining;
                read(fd + 2, buf, chunk);
                remaining -= chunk;
            }
        }

        /* AR entries are padded to even bytes */
        if (size & 1) {
            char pad;
            read(fd + 2, &pad, 1);
        }
    }

    close(fd + 2);

    if (strcmp(pkg_name, "unknown") != 0) {
        pkg_db_add(pkg_name, pkg_ver, pkg_desc, files_installed);
        pkg_db_save();
    }

    printf("sap: installed %s v%s (%d files)\n", pkg_name, pkg_ver, files_installed);
    return 0;
}

/* ============================================================
 * Commands
 * ============================================================ */

static int cmd_install(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: sap install <package>\n");
        return -1;
    }

    const char *path = argv[1];

    /* Detect package type by extension */
    int len = strlen(path);
    if (len > 4 && strcmp(path + len - 4, ".deb") == 0) {
        return sap_install_deb(path);
    } else {
        return sap_install_native(path);
    }
}

static int cmd_remove(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: sap remove <name>\n");
        return -1;
    }

    int idx = pkg_db_find(argv[1]);
    if (idx < 0) {
        printf("sap: package '%s' not installed\n", argv[1]);
        return -1;
    }

    printf("Removing %s v%s...\n", pkg_db[idx].name, pkg_db[idx].version);
    pkg_db_remove(idx);
    pkg_db_save();
    printf("sap: removed '%s'\n", argv[1]);
    return 0;
}

static int cmd_list(int argc, char **argv)
{
    (void)argc; (void)argv;

    if (pkg_db_count == 0) {
        printf("No packages installed.\n");
        return 0;
    }

    printf("Installed packages:\n");
    printf("  %-20s %-10s %s\n", "NAME", "VERSION", "DESCRIPTION");
    printf("  -------------------- ---------- ---------------------\n");
    for (int i = 0; i < pkg_db_count; i++) {
        printf("  %-20s %-10s %s\n",
               pkg_db[i].name, pkg_db[i].version, pkg_db[i].desc);
    }
    printf("  %d package(s)\n", pkg_db_count);
    return 0;
}

static int cmd_info(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: sap info <file>\n");
        return -1;
    }

    const char *path = argv[1];
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        printf("sap: cannot open '%s'\n", path);
        return -1;
    }

    /* Check magic */
    char magic[8];
    read(fd + 2, magic, 8);
    close(fd + 2);

    if (memcmp(magic, AR_MAGIC, 8) == 0) {
        printf("Format: Debian package (.deb)\n");
    } else if (*(uint32_t *)magic == SAP_MAGIC) {
        /* Read more for SAP info */
        printf("Format: SAP native package (.sap)\n");
    } else {
        printf("Format: Unknown\n");
    }
    return 0;
}

static int sap_cmd_help(int argc, char **argv)
{
    (void)argc; (void)argv;
    printf("SAP - Shlte Advanced Packager v1.0\n\n");
    printf("Usage: sap <command> [args...]\n\n");
    printf("Commands:\n");
    printf("  install <pkg>   Install a .sap or .deb package\n");
    printf("  remove  <name>  Remove an installed package\n");
    printf("  list            List installed packages\n");
    printf("  info   <file>   Show package info\n");
    printf("  help            Show this help\n");
    return 0;
}

/* ============================================================
 * Entry point
 * ============================================================ */

int sap_main(int argc, char **argv)
{
    (void)argc;

    pkg_db_load();

    if (argc < 2) {
        sap_cmd_help(0, NULL);
        return 0;
    }

    const char *cmd = argv[1];

    if (strcmp(cmd, "install") == 0)      return cmd_install(argc - 1, argv + 1);
    if (strcmp(cmd, "remove") == 0)       return cmd_remove(argc - 1, argv + 1);
    if (strcmp(cmd, "list") == 0)         return cmd_list(argc - 1, argv + 1);
    if (strcmp(cmd, "info") == 0)         return cmd_info(argc - 1, argv + 1);
    if (strcmp(cmd, "help") == 0)         return sap_cmd_help(argc - 1, argv + 1);

    printf("sap: unknown command '%s'. Try 'sap help'.\n", cmd);
    return -1;
}

#ifdef SAP_STANDALONE
int main(int argc, char **argv)
{
    return sap_main(argc, argv);
}
#endif
