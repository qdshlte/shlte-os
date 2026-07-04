/*
 * shlte/spfs.h - Simple Persistent File System declarations
 */

#ifndef SHLTE_SPFS_H
#define SHLTE_SPFS_H

#include <shlte/types.h>

#define SPFS_MAGIC      0x53504653   /* "SPFS" */
#define SPFS_VERSION    1
#define SPFS_BLOCK_SIZE 4096
#define SPFS_MAX_NAME   128
#define SPFS_MAX_FILES  128

/* File types */
#define SPFS_FILE       0
#define SPFS_DIR        1

/* File entry in the directory table */
typedef struct {
    char     name[SPFS_MAX_NAME];
    uint8_t  type;           /* SPFS_FILE or SPFS_DIR */
    uint8_t  _pad[3];
    uint32_t size;           /* file size in bytes */
    uint32_t block_start;    /* first data block number */
    uint32_t block_count;    /* number of blocks allocated */
} __attribute__((packed)) spfs_dirent_t;

/* Superblock (block 0) */
typedef struct {
    uint32_t magic;              /* SPFS_MAGIC */
    uint32_t version;            /* SPFS_VERSION */
    uint32_t total_blocks;       /* total blocks on device */
    uint32_t block_size;         /* must be 4096 */
    uint32_t dir_count;          /* number of valid dir entries */
    uint32_t dir_max;            /* max number of dir entries */
    uint32_t data_block_start;   /* first block used for file data */
    uint32_t _reserved[505];     /* pad to 1 block */
} __attribute__((packed)) spfs_superblock_t;

/* Initialize or mount spfs on the virtio-blk device. */
int spfs_init(void);

/* Format the disk (create empty filesystem). WARNING: destroys all data. */
int spfs_format(void);

/* List files in root directory. Returns number of entries. */
int spfs_list(char names[][SPFS_MAX_NAME], int max_count);

/* Open a file by name. Returns fd (0-based index) or -1. */
int spfs_open(const char *name);

/* Read from an open file. Returns bytes read. */
int spfs_read(int fd, uint8_t *buffer, uint32_t count, uint32_t offset);

/* Write to a file. Creates file if it doesn't exist. */
int spfs_write(const char *name, const uint8_t *data, uint32_t size);

/* Delete a file. */
int spfs_delete(const char *name);

/* Return number of files. */
int spfs_file_count(void);

#endif /* SHLTE_SPFS_H */
