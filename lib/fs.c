/*
 * fs.c - Virtual filesystem with embedded rootfs support
 */

#include <shlte/types.h>
#include <shlte/fs.h>
#include <shlte/spfs.h>
#include <shlte/printk.h>
#include <shlte/rootfs.h>
#include <string.h>

/* SEEK_* constants (not provided by freestanding headers) */
#ifndef SEEK_SET
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#endif

/* Simple in-memory file table backed by embedded rootfs */
#define MAX_OPEN_FILES 16

typedef struct {
    int   used;
    int   is_spfs;         /* non-zero if backed by spfs */
    int   spfs_fd;         /* spfs file descriptor */
    char  path[128];
    const uint8_t *data;   /* rootfs data pointer (NULL for spfs) */
    size_t size;
    off_t pos;
} file_entry_t;

static file_entry_t file_table[MAX_OPEN_FILES];
static int fs_initialized = 0;

/**
 * fs_init - Initialize filesystem with embedded rootfs
 */
void fs_init(void)
{
    fs_initialized = 1;
    for (int i = 0; i < MAX_OPEN_FILES; i++)
        file_table[i].used = 0;

    int count = rootfs_get_count();
    printk("[FS] Embedded rootfs initialized: %d files\n", count);
}

/**
 * vfs_open - Open a file from the embedded rootfs or spfs
 *
 * Files with path starting with "/mnt/" are routed to the spfs
 * persistent filesystem. All other paths go to the embedded rootfs.
 */
int vfs_open(const char *pathname, int flags)
{
    (void)flags;
    if (!fs_initialized) fs_init();

    /* Route /mnt/ paths to spfs */
    if (strncmp(pathname, "/mnt/", 5) == 0) {
        const char *spfs_path = pathname + 5;  /* strip "/mnt/" prefix */

        printk("[FS] open(\"%s\") -> spfs(\"%s\")\n", pathname, spfs_path);

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
                file_table[i].data = NULL;
                file_table[i].size = 0;   /* actual size from spfs_read */
                file_table[i].pos = 0;
                file_table[i].is_spfs = 1;
                file_table[i].spfs_fd = spfd;
                file_table[i].used = 1;
                return i;
            }
        }
        printk("[FS] open(\"%s\") - file table full\n", pathname);
        return -1;
    }

    /* Default: embedded rootfs */
    size_t fsize = 0;
    int fmode = 0;
    const uint8_t *data = rootfs_get_file(pathname, &fsize, &fmode);

    if (!data) {
        printk("[FS] open(\"%s\") - not found in rootfs\n", pathname);
        return -1;
    }

    /* Find a free slot */
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (!file_table[i].used) {
            size_t len = strlen(pathname);
            if (len >= sizeof(file_table[i].path))
                len = sizeof(file_table[i].path) - 1;
            memcpy(file_table[i].path, pathname, len);
            file_table[i].path[len] = '\0';
            file_table[i].data = data;
            file_table[i].size = fsize;
            file_table[i].pos = 0;
            file_table[i].is_spfs = 0;
            file_table[i].spfs_fd = -1;
            file_table[i].used = 1;
            return i;  /* fd = index */
        }
    }
    printk("[FS] open(\"%s\") - file table full\n", pathname);
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

    /* Route spfs-backed files */
    if (f->is_spfs) {
        int ret = spfs_read(f->spfs_fd, (uint8_t *)buf,
                            (uint32_t)count, (uint32_t)f->pos);
        if (ret < 0)
            return -1;
        f->pos += ret;
        return (ssize_t)ret;
    }

    /* Default: rootfs-backed file */
    size_t avail = f->size - f->pos;
    size_t to_read = (count < avail) ? count : avail;

    if (to_read == 0) return 0;

    memcpy(buf, f->data + f->pos, to_read);
    f->pos += to_read;
    return (ssize_t)to_read;
}

/**
 * vfs_write - Write to file (stub - rootfs is read-only)
 */
ssize_t vfs_write(int fd, const void *buf, size_t count)
{
    (void)buf;
    printk("[FS] write(fd=%d) - rootfs is read-only\n", fd);
    (void)count;
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

    /* For spfs files, we don't track the exact size in the VFS;
     * spfs_read will handle bounds internally. */
    size_t max_size = f->is_spfs ? SIZE_MAX : f->size;

    switch (whence) {
        case SEEK_SET: new_pos = offset; break;
        case SEEK_CUR: new_pos = f->pos + offset; break;
        case SEEK_END: new_pos = (off_t)max_size + offset; break;
        default: return -1;
    }

    if (new_pos < 0 || (size_t)new_pos > max_size)
        return -1;

    f->pos = (size_t)new_pos;
    return new_pos;
}

/**
 * vfs_mount - Mount a filesystem (stub)
 */
int vfs_mount(const char *device, const char *mountpoint, const char *fstype)
{
    printk("[FS] Mount %s -> %s (%s) - not implemented\n", device, mountpoint, fstype);
    return -1;
}
