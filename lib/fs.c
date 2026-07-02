/*
 * fs.c - Virtual filesystem stubs
 */

#include <shlte/types.h>
#include <shlte/fs.h>
#include <shlte/printk.h>

/**
 * vfs_mount - Mount a filesystem (stub)
 */
int vfs_mount(const char *device, const char *mountpoint, const char *fstype)
{
    printk("[FS] Mount %s -> %s (%s) - not implemented\n", device, mountpoint, fstype);
    return -1;
}

/**
 * vfs_read - Read from file descriptor (stub)
 */
ssize_t vfs_read(int fd, void *buf, size_t count)
{
    printk("[FS] read(fd=%d, count=%zu) - not implemented\n", fd, count);
    return -1;
}

/**
 * vfs_write - Write to file descriptor (stub)
 */
ssize_t vfs_write(int fd, const void *buf, size_t count)
{
    printk("[FS] write(fd=%d, count=%zu) - not implemented\n", fd, count);
    return -1;
}

/**
 * vfs_open - Open a file (stub)
 */
int vfs_open(const char *pathname, int flags)
{
    printk("[FS] open(\"%s\", 0x%x) - not implemented\n", pathname, flags);
    return -1;
}

/**
 * vfs_close - Close a file descriptor (stub)
 */
int vfs_close(int fd)
{
    printk("[FS] close(%d)\n", fd);
    return 0;
}

/**
 * vfs_lseek - Seek in file (stub)
 */
off_t vfs_lseek(int fd, off_t offset, int whence)
{
    printk("[FS] lseek(fd=%d, offset=%ld, whence=%d) - not implemented\n", fd, offset, whence);
    return -1;
}

/**
 * fs_init - Initialize filesystem (stub)
 */
void fs_init(void)
{
    printk("[FS] Filesystem initialized (stub)\n");
}
