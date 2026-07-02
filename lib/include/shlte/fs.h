/*
 * shlte/fs.h - Filesystem declarations
 */

#ifndef SHLTE_FS_H
#define SHLTE_FS_H

#include <shlte/types.h>

/* File descriptors */
#define STDIN_FILENO   0
#define STDOUT_FILENO  1
#define STDERR_FILENO  2

/* File operations */
int vfs_mount(const char *device, const char *mountpoint, const char *fstype);
ssize_t vfs_read(int fd, void *buf, size_t count);
ssize_t vfs_write(int fd, const void *buf, size_t count);
int vfs_open(const char *pathname, int flags);
int vfs_close(int fd);
off_t vfs_lseek(int fd, off_t offset, int whence);

/* Initialize filesystem */
void fs_init(void);

#endif /* SHLTE_FS_H */
