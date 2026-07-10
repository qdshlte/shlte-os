/*
 * shlte/ext2.h - ext2 Filesystem Driver
 *
 * Second Extended Filesystem (ext2) implementation.
 * Supports reading directories, regular files, and basic writing.
 * Inherits the VFS interface from fs.h.
 */

#ifndef SHLTE_EXT2_H
#define SHLTE_EXT2_H

#include <shlte/types.h>

/* ============================================================
 * ext2 Superblock (at offset 1024 from start of disk)
 * ============================================================ */

#define EXT2_SUPER_MAGIC    0xEF53
#define EXT2_SUPER_OFFSET   1024

#define EXT2_S_IFSOCK  0xC000
#define EXT2_S_IFLNK   0xA000
#define EXT2_S_IFREG   0x8000
#define EXT2_S_IFBLK   0x6000
#define EXT2_S_IFDIR   0x4000
#define EXT2_S_IFCHR   0x2000
#define EXT2_S_IFIFO   0x1000
#define EXT2_S_IFMT    0xF000

typedef struct __attribute__((packed)) {
    uint32_t s_inodes_count;
    uint32_t s_blocks_count;
    uint32_t s_r_blocks_count;
    uint32_t s_free_blocks_count;
    uint32_t s_free_inodes_count;
    uint32_t s_first_data_block;
    uint32_t s_log_block_size;
    int32_t  s_log_frag_size;
    uint32_t s_blocks_per_group;
    uint32_t s_frags_per_group;
    uint32_t s_inodes_per_group;
    uint32_t s_mtime;
    uint32_t s_wtime;
    uint16_t s_mnt_count;
    int16_t  s_max_mnt_count;
    uint16_t s_magic;
    uint16_t s_state;
    uint16_t s_errors;
    uint16_t s_minor_rev_level;
    uint32_t s_lastcheck;
    uint32_t s_checkinterval;
    uint32_t s_creator_os;
    uint32_t s_rev_level;
    uint16_t s_def_resuid;
    uint16_t s_def_resgid;
    /* Extended superblock fields */
    uint32_t s_first_ino;
    uint16_t s_inode_size;
    uint16_t s_block_group_nr;
    uint32_t s_feature_compat;
    uint32_t s_feature_incompat;
    uint32_t s_feature_ro_compat;
    uint8_t  s_uuid[16];
    char     s_volume_name[16];
    char     s_last_mounted[64];
    uint32_t s_algo_bitmap;
} ext2_superblock_t;

/* ============================================================
 * Block Group Descriptor
 * ============================================================ */

typedef struct __attribute__((packed)) {
    uint32_t bg_block_bitmap;
    uint32_t bg_inode_bitmap;
    uint32_t bg_inode_table;
    uint16_t bg_free_blocks_count;
    uint16_t bg_free_inodes_count;
    uint16_t bg_used_dirs_count;
    uint16_t bg_pad;
    uint8_t  bg_reserved[12];
} ext2_bg_desc_t;

/* ============================================================
 * Inode (128 bytes for classic ext2)
 * ============================================================ */

typedef struct __attribute__((packed)) {
    uint16_t i_mode;
    uint16_t i_uid;
    uint32_t i_size;
    uint32_t i_atime;
    uint32_t i_ctime;
    uint32_t i_mtime;
    uint32_t i_dtime;
    uint16_t i_gid;
    uint16_t i_links_count;
    uint32_t i_blocks;
    uint32_t i_flags;
    uint32_t i_osd1;
    uint32_t i_block[15];    /* 12 direct + 1 indirect + 1 double + 1 triple */
    uint32_t i_generation;
    uint32_t i_file_acl;
    uint32_t i_dir_acl;
    uint32_t i_faddr;
    uint8_t  i_osd2[12];
} ext2_inode_t;

/* ============================================================
 * Directory Entry
 * ============================================================ */

typedef struct __attribute__((packed)) {
    uint32_t inode;
    uint16_t rec_len;
    uint8_t  name_len;
    uint8_t  file_type;
    char     name[];           /* Variable-length name */
} ext2_dir_entry_t;

/* File types in dir_entry.file_type */
#define EXT2_FT_UNKNOWN  0
#define EXT2_FT_REG_FILE 1
#define EXT2_FT_DIR      2
#define EXT2_FT_CHRDEV   3
#define EXT2_FT_BLKDEV   4
#define EXT2_FT_FIFO     5
#define EXT2_FT_SOCK     6
#define EXT2_FT_SYMLINK  7

/* ============================================================
 * Block I/O interface (must be provided by the caller)
 * ============================================================ */

typedef int (*ext2_read_block_fn)(void *ctx, uint64_t block, void *buf);
typedef int (*ext2_write_block_fn)(void *ctx, uint64_t block, const void *buf);

/* ============================================================
 * Ext2 filesystem state
 * ============================================================ */

typedef struct {
    ext2_superblock_t sb;
    uint32_t block_size;
    uint32_t blocks_per_group;
    uint32_t inodes_per_group;
    uint32_t bg_desc_blocks;

    void *io_ctx;
    ext2_read_block_fn  read_block;
    ext2_write_block_fn write_block;
    int mounted;
} ext2_fs_t;

/* ============================================================
 * Open file state
 * ============================================================ */

#define EXT2_MAX_OPEN_FILES  32

typedef struct {
    uint32_t inode_num;
    ext2_inode_t inode;
    uint32_t pos;            /* Current read/write position */
    int mode;                /* O_RDONLY / O_WRONLY / O_RDWR */
    int in_use;
} ext2_file_t;

/* ============================================================
 * API
 * ============================================================ */

int  ext2_mount(ext2_fs_t *fs, void *io_ctx,
                ext2_read_block_fn reader, ext2_write_block_fn writer);
int  ext2_umount(ext2_fs_t *fs);

int  ext2_open(ext2_fs_t *fs, const char *path, int mode, ext2_file_t *file);
int  ext2_close(ext2_fs_t *fs, ext2_file_t *file);
ssize_t ext2_read(ext2_fs_t *fs, ext2_file_t *file, void *buf, size_t count);
ssize_t ext2_write(ext2_fs_t *fs, ext2_file_t *file, const void *buf, size_t count);
int  ext2_list_dir(ext2_fs_t *fs, ext2_file_t *dir, char names[][128], int max);

/* Path to inode lookup */
int  ext2_lookup(ext2_fs_t *fs, const char *path, ext2_inode_t *inode_out, uint32_t *inum_out);

#endif /* SHLTE_EXT2_H */
