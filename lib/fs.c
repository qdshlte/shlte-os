/*
 * fs.c - Virtual filesystem backed by spfs
 */

#include <shlte/types.h>
#include <shlte/fs.h>
#include <shlte/spfs.h>
#include <shlte/printk.h>
#include <shlte/string.h>

/* Simple in-memory file table backed by spfs */
#define MAX_OPEN_FILES 16

typedef struct {
    int   used;
    int   spfs_fd;         /* spfs file descriptor */
    char  path[128];
    off_t pos;
} file_entry_t;

static file_entry_t file_table[MAX_OPEN_FILES];
static int fs_initialized = 0;

/**
 * fs_init - Initialize filesystem
 */
void fs_init(void)
{
    fs_initialized = 1;
    for (int i = 0; i < MAX_OPEN_FILES; i++)
        file_table[i].used = 0;

    printk("[FS] VFS initialized (spfs backend)\n");
}

/**
 * vfs_open - Open a file from spfs
 *
 * Files with path starting with "/mnt/" are routed to the spfs
 * persistent filesystem. All other paths return -1 (no filesystem).
 */
int vfs_open(const char *pathname, int flags)
{
    (void)flags;
    if (!fs_initialized) fs_init();

    /* Route /mnt/ paths to spfs */
    if (strncmp(pathname, "/mnt/", 5) == 0) {
        const char *spfs_path = pathname + 5;  /* strip "/mnt/" prefix */

        /* Find a free slot */
        for (int i = 0; i < MAX_OPEN_FILES; i++) {
            if (!file_table[i].used) {
                int spfd = spfs_open(spfs_path);
                if (spfd < 0) {
                    printk("[FS] open(\"%s\") - not found in spfs\n", pathname);
                    return -1;
                }

                size_t len = strlen(pathname);
                if (len >= sizeof(file_table[i].path))
                    len = sizeof(file_table[i].path) - 1;
                memcpy(file_table[i].path, pathname, len);
                file_table[i].path[len] = '\0';
                file_table[i].spfs_fd = spfd;
                file_table[i].pos = 0;
                file_table[i].used = 1;
                return i;
            }
        }
        printk("[FS] open(\"%s\") - file table full\n", pathname);
        return -1;
    }

    /* No filesystem mounted for non-/mnt paths */
    printk("[FS] open(\"%s\") - no filesystem\n", pathname);
    return -1;
}

/**
 * vfs_read - Read from open file
 */
ssize_t vfs_read(int fd, void *buf, size_t count)
{
    if (fd < 0 || fd >= MAX_OPEN_FILES || !file_table[fd].used)
        return -1;

    file_entry_t *f = &file_table[fd];

    int ret = spfs_read(f->spfs_fd, (uint8_t *)buf,
                        (uint32_t)count, (uint32_t)f->pos);
    if (ret < 0)
        return -1;
    f->pos += ret;
    return (ssize_t)ret;
}

/**
 * vfs_write - Write to file (not yet implemented via VFS)
 */
ssize_t vfs_write(int fd, const void *buf, size_t count)
{
    (void)fd; (void)buf; (void)count;
    printk("[FS] write() - not implemented\n");
    return -1;
}

/**
 * vfs_close - Close a file descriptor
 */
int vfs_close(int fd)
{
    if (fd < 0 || fd >= MAX_OPEN_FILES || !file_table[fd].used)
        return -1;
    file_table[fd].used = 0;
    return 0;
}

/**
 * vfs_lseek - Seek in open file
 */
off_t vfs_lseek(int fd, off_t offset, int whence)
{
    if (fd < 0 || fd >= MAX_OPEN_FILES || !file_table[fd].used)
        return -1;

    file_entry_t *f = &file_table[fd];
    off_t new_pos;

    /* spfs handles bounds internally; use SIZE_MAX as upper bound */
    switch (whence) {
        case SEEK_SET: new_pos = offset; break;
        case SEEK_CUR: new_pos = f->pos + offset; break;
        case SEEK_END: new_pos = (off_t)SIZE_MAX + offset; break;
        default: return -1;
    }

    if (new_pos < 0)
        return -1;

    f->pos = (size_t)new_pos;
    return new_pos;
}

/**
 * vfs_list - List files in the root directory
 */
int vfs_list(char names[][FS_MAX_NAME], int max_count)
{
    return spfs_list(names, max_count);
}

/**
 * vfs_mount - Mount a filesystem (stub)
 */
int vfs_mount(const char *device, const char *mountpoint, const char *fstype)
{
    printk("[FS] Mount %s -> %s (%s) - not implemented\n", device, mountpoint, fstype);
    return -1;
}
