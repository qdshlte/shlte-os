/*
 * spfs.c - Simple Persistent File System
 *
 * A flat (no subdirectories) filesystem stored on a virtio-blk device.
 * Block 0: superblock
 * Blocks 1..N: directory entries
 * Remaining blocks: file data (contiguous per file)
 */

#include <shlte/types.h>
#include <shlte/spfs.h>
#include <shlte/virtio_blk.h>
#include <shlte/printk.h>
#include <shlte/string.h>

/* ------------------------------------------------------------------ */
/* Internal constants                                                 */
/* ------------------------------------------------------------------ */
#define SPFS_MAX_OPEN       8
#define ENTRIES_PER_BLOCK   (SPFS_BLOCK_SIZE / sizeof(spfs_dirent_t))

/* ------------------------------------------------------------------ */
/* Open file table                                                    */
/* ------------------------------------------------------------------ */
typedef struct {
    int          used;
    spfs_dirent_t dirent;          /* cached copy of the directory entry */
} spfs_file_t;

static spfs_file_t open_files[SPFS_MAX_OPEN];

/* Cached superblock data */
static spfs_superblock_t sb;

/* Scratch buffer for block I/O */
static uint8_t block_buf[SPFS_BLOCK_SIZE] __attribute__((aligned(4096)));

/* ------------------------------------------------------------------ */
/* Forward declarations                                               */
/* ------------------------------------------------------------------ */
static int read_dirent(int index, spfs_dirent_t *ent);
static int write_dirent(int index, const spfs_dirent_t *ent);
static int find_free_dirent_slot(void);
static int find_file(const char *name);
static int alloc_blocks(uint32_t count, uint32_t *start_out);

/* ------------------------------------------------------------------ */
/* Read a directory entry by its index in the flat table              */
/* ------------------------------------------------------------------ */
static int read_dirent(int index, spfs_dirent_t *ent)
{
    uint32_t block = 1 + (uint32_t)index / ENTRIES_PER_BLOCK;
    uint32_t offset = ((uint32_t)index % ENTRIES_PER_BLOCK) * sizeof(spfs_dirent_t);

    if (virtio_blk_read_block(block, block_buf) != 0)
        return -1;

    memcpy(ent, block_buf + offset, sizeof(spfs_dirent_t));
    return 0;
}

/* ------------------------------------------------------------------ */
/* Write a directory entry by its index                               */
/* ------------------------------------------------------------------ */
static int write_dirent(int index, const spfs_dirent_t *ent)
{
    uint32_t block = 1 + (uint32_t)index / ENTRIES_PER_BLOCK;
    uint32_t offset = ((uint32_t)index % ENTRIES_PER_BLOCK) * sizeof(spfs_dirent_t);

    /* Read-modify-write: load the whole block, patch the entry */
    if (virtio_blk_read_block(block, block_buf) != 0)
        return -1;

    memcpy(block_buf + offset, ent, sizeof(spfs_dirent_t));
    return virtio_blk_write_block(block, block_buf);
}

/* ------------------------------------------------------------------ */
/* Find a free slot in the directory table                            */
/* ------------------------------------------------------------------ */
static int find_free_dirent_slot(void)
{
    spfs_dirent_t ent;

    for (uint32_t i = 0; i < sb.dir_max; i++) {
        if (read_dirent((int)i, &ent) != 0)
            return -1;
        if (ent.name[0] == '\0')
            return (int)i;
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/* Find a file by name in the directory table                         */
/* ------------------------------------------------------------------ */
static int find_file(const char *name)
{
    spfs_dirent_t ent;

    for (uint32_t i = 0; i < sb.dir_count; i++) {
        if (read_dirent((int)i, &ent) != 0)
            continue;
        if (ent.name[0] != '\0' && strcmp(ent.name, name) == 0)
            return (int)i;
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/* Simple block allocator: first-fit scan of directory entries        */
/* Finds a range of 'count' contiguous free blocks.                   */
/* ------------------------------------------------------------------ */
static int alloc_blocks(uint32_t count, uint32_t *start_out)
{
    uint32_t data_end = sb.total_blocks;
    uint32_t candidate = sb.data_block_start;
    uint32_t found = 0;

    while (candidate + count <= data_end) {
        int collision = 0;

        /* Check if candidate overlaps any existing file */
        for (uint32_t i = 0; i < sb.dir_count; i++) {
            spfs_dirent_t ent;
            if (read_dirent((int)i, &ent) != 0)
                continue;
            if (ent.name[0] == '\0')
                continue;
            if (ent.block_count == 0)
                continue;

            uint32_t f_start = ent.block_start;
            uint32_t f_end   = f_start + ent.block_count;

            /* Overlap check */
            if (candidate < f_end && (candidate + count) > f_start) {
                /* Collision: move candidate past this file */
                candidate = f_end;
                collision = 1;
                break;
            }
        }

        if (!collision) {
            found = 1;
            break;
        }
    }

    if (!found)
        return -1;

    *start_out = candidate;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Public API                                                         */
/* ------------------------------------------------------------------ */

/**
 * spfs_init - Mount or create the filesystem
 */
int spfs_init(void)
{
    printk("[SPFS] Initializing...\n");

    /* Clear open file table */
    for (int i = 0; i < SPFS_MAX_OPEN; i++)
        open_files[i].used = 0;

    /* Read superblock */
    if (virtio_blk_read_block(0, (uint8_t *)&sb) != 0) {
        printk("[SPFS] Failed to read superblock\n");
        return -1;
    }

    /* Validate superblock */
    if (sb.magic != SPFS_MAGIC || sb.version != SPFS_VERSION) {
        printk("[SPFS] No valid filesystem found (magic=0x%08x, version=%u)\n",
               sb.magic, sb.version);
        printk("[SPFS] Formatting new filesystem...\n");
        if (spfs_format() != 0) {
            printk("[SPFS] Format failed!\n");
            return -1;
        }
    }

    printk("[SPFS] Mounted: %u files, %u/%u blocks, %u data blocks available\n",
           sb.dir_count, sb.data_block_start, sb.total_blocks,
           sb.total_blocks - sb.data_block_start);

    return 0;
}

/**
 * spfs_format - Create a fresh filesystem (destroys all data)
 */
int spfs_format(void)
{
    /* Build superblock */
    memset(&sb, 0, sizeof(sb));
    sb.magic           = SPFS_MAGIC;
    sb.version         = SPFS_VERSION;
    sb.total_blocks    = (uint32_t)virtio_blk_get_block_count();
    sb.block_size      = SPFS_BLOCK_SIZE;
    sb.dir_count       = 0;
    sb.dir_max         = ENTRIES_PER_BLOCK;  /* one block of directory entries */
    sb.data_block_start = 2;                  /* block 0 = super, block 1 = dir */

    if (sb.total_blocks < sb.data_block_start + 2) {
        printk("[SPFS] Disk too small (%u blocks)\n", sb.total_blocks);
        return -1;
    }

    /* Write superblock */
    if (virtio_blk_write_block(0, (const uint8_t *)&sb) != 0) {
        printk("[SPFS] Failed to write superblock\n");
        return -1;
    }

    /* Clear directory block(s) */
    memset(block_buf, 0, SPFS_BLOCK_SIZE);
    {
        uint32_t dir_blocks = sb.dir_max / ENTRIES_PER_BLOCK;
        if (sb.dir_max % ENTRIES_PER_BLOCK != 0)
            dir_blocks++;

        for (uint32_t b = 1; b < 1 + dir_blocks; b++) {
            if (virtio_blk_write_block(b, block_buf) != 0) {
                printk("[SPFS] Failed to clear dir block %u\n", b);
                return -1;
            }
        }
    }

    printk("[SPFS] Formatted: %u blocks, %u dir slots, data starts at block %u\n",
           sb.total_blocks, sb.dir_max, sb.data_block_start);

    return 0;
}

/**
 * spfs_list - List files in root directory
 */
int spfs_list(char names[][SPFS_MAX_NAME], int max_count)
{
    int count = 0;
    spfs_dirent_t ent;

    for (uint32_t i = 0; i < sb.dir_count && count < max_count; i++) {
        if (read_dirent((int)i, &ent) != 0)
            continue;
        if (ent.name[0] != '\0') {
            size_t len = strlen(ent.name);
            if (len >= SPFS_MAX_NAME)
                len = SPFS_MAX_NAME - 1;
            memcpy(names[count], ent.name, len);
            names[count][len] = '\0';
            count++;
        }
    }
    return count;
}

/**
 * spfs_open - Open a file by name
 */
int spfs_open(const char *name)
{
    int idx = find_file(name);
    if (idx < 0) {
        printk("[SPFS] open(\"%s\") - not found\n", name);
        return -1;
    }

    /* Find free slot in open file table */
    for (int i = 0; i < SPFS_MAX_OPEN; i++) {
        if (!open_files[i].used) {
            if (read_dirent(idx, &open_files[i].dirent) != 0)
                return -1;
            open_files[i].used = 1;
            return i;  /* fd */
        }
    }

    printk("[SPFS] open(\"%s\") - file table full\n", name);
    return -1;
}

/**
 * spfs_read - Read from an open file
 */
int spfs_read(int fd, uint8_t *buffer, uint32_t count, uint32_t offset)
{
    if (fd < 0 || fd >= SPFS_MAX_OPEN || !open_files[fd].used)
        return -1;

    spfs_dirent_t *ent = &open_files[fd].dirent;

    if (offset >= ent->size)
        return 0;

    /* Clamp count to remaining bytes */
    uint32_t remaining = ent->size - offset;
    if (count > remaining)
        count = remaining;

    if (count == 0)
        return 0;

    uint32_t bytes_read = 0;
    uint32_t pos = offset;

    while (bytes_read < count) {
        /* Which block are we in? */
        uint32_t block_idx = pos / SPFS_BLOCK_SIZE;
        uint32_t block_off = pos % SPFS_BLOCK_SIZE;
        uint32_t block_num = ent->block_start + block_idx;

        /* How many bytes to read from this block */
        uint32_t to_read = SPFS_BLOCK_SIZE - block_off;
        uint32_t need = count - bytes_read;
        if (to_read > need)
            to_read = need;

        /* Read the block */
        if (virtio_blk_read_block(block_num, block_buf) != 0)
            return (int)bytes_read > 0 ? (int)bytes_read : -1;

        memcpy(buffer + bytes_read, block_buf + block_off, to_read);
        bytes_read += to_read;
        pos += to_read;
    }

    return (int)bytes_read;
}

/**
 * spfs_write - Write data to a file (create if not exists)
 */
int spfs_write(const char *name, const uint8_t *data, uint32_t size)
{
    if (name == NULL || name[0] == '\0')
        return -1;

    int idx = find_file(name);

    /* Calculate required blocks */
    uint32_t need_blocks = size / SPFS_BLOCK_SIZE;
    if (size % SPFS_BLOCK_SIZE != 0)
        need_blocks++;

    if (need_blocks == 0)
        need_blocks = 1;  /* always allocate at least one block */

    if (idx >= 0) {
        /* File exists -- check if we can reuse */
        spfs_dirent_t ent;
        if (read_dirent(idx, &ent) != 0)
            return -1;

        if (ent.block_count >= need_blocks) {
            /* Reuse existing blocks */
            /* (no change to block_start / block_count) */
        } else {
            /* Need more blocks -- allocate new range */
            uint32_t new_start;
            if (alloc_blocks(need_blocks, &new_start) != 0) {
                printk("[SPFS] write(\"%s\") - out of space\n", name);
                return -1;
            }
            ent.block_start = new_start;
            ent.block_count = need_blocks;
        }

        ent.size = size;
        if (write_dirent(idx, &ent) != 0)
            return -1;
    } else {
        /* Create new file */
        idx = find_free_dirent_slot();
        if (idx < 0) {
            printk("[SPFS] write(\"%s\") - directory full\n", name);
            return -1;
        }

        spfs_dirent_t ent;
        memset(&ent, 0, sizeof(ent));
        size_t nlen = strlen(name);
        if (nlen >= SPFS_MAX_NAME) nlen = SPFS_MAX_NAME - 1;
        memcpy(ent.name, name, nlen);
        ent.name[nlen] = '\0';
        ent.type = SPFS_FILE;
        ent.size = size;
        ent.block_count = need_blocks;

        {
            uint32_t new_start;
            if (alloc_blocks(need_blocks, &new_start) != 0) {
                printk("[SPFS] write(\"%s\") - out of space\n", name);
                return -1;
            }
            ent.block_start = new_start;
        }

        if (write_dirent(idx, &ent) != 0)
            return -1;

        /* Update superblock dir_count if this is a new slot at the end */
        if ((uint32_t)idx >= sb.dir_count) {
            sb.dir_count = (uint32_t)idx + 1;
            /* Re-read: update in-memory copy; persist on next superblock write */
            /* The superblock will be updated when the dir_count is persisted */
        }
    }

    /* Write data blocks */
    {
        uint32_t remaining = size;
        uint32_t block_idx = 0;
        uint32_t data_offset = 0;

        /* Read the directory entry again (may have been updated) */
        spfs_dirent_t ent;
        if (read_dirent(idx, &ent) != 0)
            return -1;

        while (remaining > 0) {
            uint32_t to_write = (remaining < SPFS_BLOCK_SIZE) ? remaining : SPFS_BLOCK_SIZE;
            uint32_t block_num = ent.block_start + block_idx;

            memset(block_buf, 0, SPFS_BLOCK_SIZE);
            memcpy(block_buf, data + data_offset, to_write);

            if (virtio_blk_write_block(block_num, block_buf) != 0)
                return -1;

            remaining -= to_write;
            data_offset += to_write;
            block_idx++;
        }
    }

    /* Update superblock to persist dir_count */
    if (virtio_blk_write_block(0, (const uint8_t *)&sb) != 0)
        return -1;

    printk("[SPFS] write(\"%s\") - %u bytes, %u blocks\n", name, size, need_blocks);
    return (int)size;
}

/**
 * spfs_delete - Delete a file
 */
int spfs_delete(const char *name)
{
    int idx = find_file(name);
    if (idx < 0) {
        printk("[SPFS] delete(\"%s\") - not found\n", name);
        return -1;
    }

    /* Clear the directory entry */
    spfs_dirent_t ent;
    memset(&ent, 0, sizeof(ent));
    if (write_dirent(idx, &ent) != 0)
        return -1;

    printk("[SPFS] delete(\"%s\") - deleted\n", name);
    return 0;
}

/**
 * spfs_file_count - Return number of files
 */
int spfs_file_count(void)
{
    return (int)sb.dir_count;
}
