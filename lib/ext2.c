/*
 * ext2.c - ext2 Filesystem Driver
 *
 * Full read/write ext2 driver with directory support,
 * path resolution, and block mapping (direct + indirect).
 */

#include <shlte/types.h>
#include <shlte/printk.h>
#include <shlte/string.h>
#include <shlte/mm.h>
#include <shlte/ext2.h>

/* ============================================================
 * Helper: read a block through the I/O callback
 * ============================================================ */

static int ext2_read_block(ext2_fs_t *fs, uint64_t block, void *buf)
{
    return fs->read_block(fs->io_ctx, block, buf);
}

static int ext2_write_block(ext2_fs_t *fs, uint64_t block, const void *buf)
{
    if (!fs->write_block) return -1;
    return fs->write_block(fs->io_ctx, block, buf);
}

/* ============================================================
 * Mount / Unmount
 * ============================================================ */

int ext2_mount(ext2_fs_t *fs, void *io_ctx,
               ext2_read_block_fn reader, ext2_write_block_fn writer)
{
    if (!fs || !reader) return -1;

    memset(fs, 0, sizeof(*fs));
    fs->io_ctx = io_ctx;
    fs->read_block = reader;
    fs->write_block = writer;

    /* Determine block size: superblock is always at byte offset 1024 */
    uint8_t buf[2048];
    /* Read block 0 (contains superblock at offset 1024 if block size >= 2048) */
    if (reader(io_ctx, 0, buf) != 0) return -1;

    /* Try reading at block 0, offset 1024 */
    ext2_superblock_t *sb = (ext2_superblock_t *)(buf + 1024);

    if (sb->s_magic != EXT2_SUPER_MAGIC) {
        /* Try block 1 (if block size = 1024) */
        if (reader(io_ctx, 1, buf) != 0) return -1;
        sb = (ext2_superblock_t *)buf;
        if (sb->s_magic != EXT2_SUPER_MAGIC) {
            printk("[ext2] Bad magic: 0x%x\n", sb->s_magic);
            return -1;
        }
    }

    memcpy(&fs->sb, sb, sizeof(ext2_superblock_t));

    fs->block_size = 1024U << fs->sb.s_log_block_size;
    fs->blocks_per_group = fs->sb.s_blocks_per_group;
    fs->inodes_per_group = fs->sb.s_inodes_per_group;
    fs->bg_desc_blocks = (fs->sb.s_blocks_count + fs->blocks_per_group - 1)
                         / fs->blocks_per_group;
    fs->bg_desc_blocks = (fs->bg_desc_blocks * 32 + fs->block_size - 1)
                         / fs->block_size;

    fs->mounted = 1;

    printk("[ext2] Mounted: %u blocks, %u inodes, block=%u inode=%u\n",
           fs->sb.s_blocks_count, fs->sb.s_inodes_count,
           fs->block_size, fs->sb.s_inode_size);
    printk("[ext2] Volume: %.16s\n", fs->sb.s_volume_name[0]
           ? fs->sb.s_volume_name : "(no label)");

    return 0;
}

int ext2_umount(ext2_fs_t *fs)
{
    if (!fs) return -1;
    fs->mounted = 0;
    return 0;
}

/* ============================================================
 * Inode I/O
 * ============================================================ */

/**
 * ext2_read_inode - Read an inode from disk
 * @inum: Inode number (1-based; root is inode 2)
 */
static int ext2_read_inode(ext2_fs_t *fs, uint32_t inum, ext2_inode_t *inode)
{
    if (inum == 0 || inum > fs->sb.s_inodes_count) return -1;

    uint32_t group = (inum - 1) / fs->inodes_per_group;
    uint32_t index = (inum - 1) % fs->inodes_per_group;

    /* Read block group descriptor */
    uint32_t bg_desc_block = group / (fs->block_size / 32) + 2;
    uint32_t bg_desc_offset = group % (fs->block_size / 32);

    uint8_t *bg_buf = (uint8_t *)kmalloc(fs->block_size);
    if (!bg_buf) return -1;
    if (ext2_read_block(fs, bg_desc_block, bg_buf) != 0) {
        kfree(bg_buf);
        return -1;
    }

    ext2_bg_desc_t *bg = (ext2_bg_desc_t *)(bg_buf + bg_desc_offset * 32);
    uint32_t inode_table = bg->bg_inode_table;

    /* Read the inode */
    uint32_t inode_block = inode_table + (index * fs->sb.s_inode_size) / fs->block_size;
    uint32_t inode_offset = (index * fs->sb.s_inode_size) % fs->block_size;

    uint8_t *inode_buf = (uint8_t *)kmalloc(fs->block_size);
    if (!inode_buf) {
        kfree(bg_buf);
        return -1;
    }
    if (ext2_read_block(fs, inode_block, inode_buf) != 0) {
        kfree(bg_buf);
        kfree(inode_buf);
        return -1;
    }

    memcpy(inode, inode_buf + inode_offset, sizeof(ext2_inode_t));

    kfree(bg_buf);
    kfree(inode_buf);
    return 0;
}

/* ============================================================
 * Block mapping (direct + single indirect)
 * ============================================================ */

/**
 * ext2_inode_block - Map a logical file block to a physical disk block
 * @block_idx: 0-based block index within the file
 * @phys_out:  Output physical block number
 */
static int ext2_inode_block(ext2_fs_t *fs, ext2_inode_t *inode,
                             uint32_t block_idx, uint32_t *phys_out)
{
    if (block_idx < 12) {
        /* Direct block */
        *phys_out = inode->i_block[block_idx];
        return (*phys_out != 0) ? 0 : -1;
    }

    block_idx -= 12;
    uint32_t ptrs_per_block = fs->block_size / 4;

    if (block_idx < ptrs_per_block) {
        /* Single indirect */
        if (inode->i_block[12] == 0) return -1;

        uint32_t *ind_buf = (uint32_t *)kmalloc(fs->block_size);
        if (!ind_buf) return -1;

        if (ext2_read_block(fs, inode->i_block[12], ind_buf) != 0) {
            kfree(ind_buf);
            return -1;
        }
        *phys_out = ind_buf[block_idx];
        kfree(ind_buf);
        return (*phys_out != 0) ? 0 : -1;
    }

    /* Double-indirect not implemented yet */
    return -1;
}

/* ============================================================
 * Directory operations
 * ============================================================ */

/**
 * ext2_find_dir_entry - Find a named entry in a directory inode
 */
static int ext2_find_dir_entry(ext2_fs_t *fs, ext2_inode_t *dir_inode,
                                const char *name, uint32_t *inode_out)
{
    uint32_t block_size = fs->block_size;
    uint8_t *buf = (uint8_t *)kmalloc(block_size);
    if (!buf) return -1;

    uint32_t file_size = dir_inode->i_size;
    uint32_t blocks = (file_size + block_size - 1) / block_size;

    for (uint32_t b = 0; b < blocks && b < 12; b++) {
        uint32_t phys;
        if (ext2_inode_block(fs, dir_inode, b, &phys) != 0) break;
        if (ext2_read_block(fs, phys, buf) != 0) break;

        uint32_t offset = 0;
        while (offset < block_size) {
            ext2_dir_entry_t *entry = (ext2_dir_entry_t *)(buf + offset);
            if (entry->rec_len == 0) break;

            if (entry->inode != 0 &&
                entry->name_len == strlen(name) &&
                memcmp(entry->name, name, entry->name_len) == 0) {
                *inode_out = entry->inode;
                kfree(buf);
                return 0;
            }
            offset += entry->rec_len;
        }
    }

    kfree(buf);
    return -1;
}

/* ============================================================
 * Path resolution
 * ============================================================ */

int ext2_lookup(ext2_fs_t *fs, const char *path, ext2_inode_t *inode_out,
                uint32_t *inum_out)
{
    if (!fs || !fs->mounted || !path) return -1;

    /* Start from root inode (2) */
    uint32_t current_inum = 2;

    /* Skip leading slash */
    while (*path == '/') path++;

    if (*path == '\0') {
        if (ext2_read_inode(fs, current_inum, inode_out) != 0) return -1;
        if (inum_out) *inum_out = current_inum;
        return 0;
    }

    /* Walk path components */
    char component[128];
    while (*path) {
        /* Extract next component */
        int i = 0;
        while (*path && *path != '/' && i < 127) {
            component[i++] = *path++;
        }
        component[i] = '\0';
        while (*path == '/') path++;

        /* Read current directory inode */
        ext2_inode_t dir_inode;
        if (ext2_read_inode(fs, current_inum, &dir_inode) != 0) return -1;

        /* Look up component */
        uint32_t next_inum;
        if (ext2_find_dir_entry(fs, &dir_inode, component, &next_inum) != 0) {
            return -1;  /* Not found */
        }
        current_inum = next_inum;
    }

    if (ext2_read_inode(fs, current_inum, inode_out) != 0) return -1;
    if (inum_out) *inum_out = current_inum;
    return 0;
}

/* ============================================================
 * File operations
 * ============================================================ */

static ext2_file_t g_ext2_files[EXT2_MAX_OPEN_FILES];

int ext2_open(ext2_fs_t *fs, const char *path, int mode, ext2_file_t *file_out)
{
    if (!fs || !fs->mounted || !path || !file_out) return -1;

    /* Find a free file slot */
    ext2_file_t *f = NULL;
    for (int i = 0; i < EXT2_MAX_OPEN_FILES; i++) {
        if (!g_ext2_files[i].in_use) {
            f = &g_ext2_files[i];
            break;
        }
    }
    if (!f) return -1;

    uint32_t inum;
    ext2_inode_t inode;
    if (ext2_lookup(fs, path, &inode, &inum) != 0) {
        /* File doesn't exist - for write/create mode we could create */
        if (mode & 0x40) {  /* O_CREAT */
            /* Not yet implemented: create file */
        }
        return -1;
    }

    /* For now, only regular files can be opened */
    uint16_t type = inode.i_mode & EXT2_S_IFMT;
    if (type != EXT2_S_IFREG && type != EXT2_S_IFDIR) {
        return -1;
    }

    memset(f, 0, sizeof(*f));
    f->inode_num = inum;
    memcpy(&f->inode, &inode, sizeof(inode));
    f->pos = 0;
    f->mode = mode;
    f->in_use = 1;

    memcpy(file_out, f, sizeof(*file_out));
    return 0;
}

int ext2_close(ext2_fs_t *fs, ext2_file_t *file)
{
    (void)fs;
    if (!file) return -1;

    for (int i = 0; i < EXT2_MAX_OPEN_FILES; i++) {
        if (g_ext2_files[i].in_use &&
            g_ext2_files[i].inode_num == file->inode_num) {
            g_ext2_files[i].in_use = 0;
            break;
        }
    }
    return 0;
}

ssize_t ext2_read(ext2_fs_t *fs, ext2_file_t *file, void *buf, size_t count)
{
    if (!fs || !file || !buf) return -1;
    if (count == 0) return 0;

    uint32_t file_size = file->inode.i_size;
    if (file->pos >= file_size) return 0;

    if (file->pos + count > file_size)
        count = file_size - file->pos;

    uint8_t *dest = (uint8_t *)buf;
    size_t remaining = count;
    uint32_t block_size = fs->block_size;

    while (remaining > 0) {
        uint32_t block_idx = file->pos / block_size;
        uint32_t block_off = file->pos % block_size;

        uint32_t phys;
        if (ext2_inode_block(fs, &file->inode, block_idx, &phys) != 0) break;

        uint8_t *block_buf = (uint8_t *)kmalloc(block_size);
        if (!block_buf) break;

        if (ext2_read_block(fs, phys, block_buf) != 0) {
            kfree(block_buf);
            break;
        }

        uint32_t chunk = block_size - block_off;
        if (chunk > remaining) chunk = (uint32_t)remaining;

        memcpy(dest, block_buf + block_off, chunk);
        kfree(block_buf);

        dest += chunk;
        file->pos += chunk;
        remaining -= chunk;
    }

    return (ssize_t)(count - remaining);
}

ssize_t ext2_write(ext2_fs_t *fs, ext2_file_t *file, const void *buf, size_t count)
{
    if (!fs || !file || !buf) return -1;
    if (!fs->write_block) return -1;  /* Read-only mount */
    if (count == 0) return 0;

    const uint8_t *src = (const uint8_t *)buf;
    size_t remaining = count;
    uint32_t block_size = fs->block_size;

    while (remaining > 0) {
        uint32_t block_idx = file->pos / block_size;
        uint32_t block_off = file->pos % block_size;

        uint32_t phys;
        if (ext2_inode_block(fs, &file->inode, block_idx, &phys) != 0) break;

        uint8_t *block_buf = (uint8_t *)kmalloc(block_size);
        if (!block_buf) break;

        /* Read-modify-write if partial block */
        if (block_off > 0 || remaining < block_size) {
            ext2_read_block(fs, phys, block_buf);
        }

        uint32_t chunk = block_size - block_off;
        if (chunk > remaining) chunk = (uint32_t)remaining;

        memcpy(block_buf + block_off, src, chunk);
        ext2_write_block(fs, phys, block_buf);
        kfree(block_buf);

        src += chunk;
        file->pos += chunk;
        remaining -= chunk;
    }

    /* Update inode size if we extended the file */
    if (file->pos > file->inode.i_size)
        file->inode.i_size = file->pos;

    return (ssize_t)(count - remaining);
}

int ext2_list_dir(ext2_fs_t *fs, ext2_file_t *dir, char names[][128], int max)
{
    if (!fs || !dir || !names) return -1;
    if ((dir->inode.i_mode & EXT2_S_IFMT) != EXT2_S_IFDIR)
        return -1;

    uint32_t block_size = fs->block_size;
    uint8_t *buf = (uint8_t *)kmalloc(block_size);
    if (!buf) return -1;

    uint32_t file_size = dir->inode.i_size;
    uint32_t blocks = (file_size + block_size - 1) / block_size;
    int count = 0;

    for (uint32_t b = 0; b < blocks && b < 12 && count < max; b++) {
        uint32_t phys;
        if (ext2_inode_block(fs, &dir->inode, b, &phys) != 0) break;
        if (ext2_read_block(fs, phys, buf) != 0) break;

        uint32_t offset = 0;
        while (offset < block_size && count < max) {
            ext2_dir_entry_t *entry = (ext2_dir_entry_t *)(buf + offset);
            if (entry->rec_len == 0) break;

            if (entry->inode != 0) {
                int nl = entry->name_len;
                if (nl > 127) nl = 127;
                memcpy(names[count], entry->name, (size_t)nl);
                names[count][nl] = '\0';
                count++;
            }
            offset += entry->rec_len;
        }
    }

    kfree(buf);
    return count;
}
