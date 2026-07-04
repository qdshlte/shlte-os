#ifndef SHLTE_ROOTFS_H
#define SHLTE_ROOTFS_H

/**
 * rootfs_get_file — Look up a file by path in the embedded rootfs.
 * @param path     Absolute path, e.g. "/etc/init.sh" or "etc/init.sh"
 * @param size_out Receives file size
 * @param mode_out Receives file mode (optional, can be NULL)
 * @return Pointer to file data, or NULL if not found.
 */
const unsigned char *rootfs_get_file(const char *path, unsigned long *size_out, int *mode_out);

/** Return the total number of embedded rootfs files. */
int rootfs_get_count(void);

/** Return the path of the file at the given index. */
const char *rootfs_get_name(int index);

#endif /* SHLTE_ROOTFS_H */
