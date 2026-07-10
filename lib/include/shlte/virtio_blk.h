/*
 * shlte/virtio_blk.h - Virtio-blk device driver declarations
 */

#ifndef SHLTE_VIRTIO_BLK_H
#define SHLTE_VIRTIO_BLK_H

#include <shlte/types.h>

#define VIRTIO_BLK_MMIO_BASE  0x0A000000  /* QEMU virt 8.2+: first virtio-mmio at 0x0A000000 */
#define VIRTIO_BLK_IRQ        48          /* First virtio-mmio IRQ (32 + 16) */

#define SECTOR_SIZE          512
#define BLOCK_SIZE           4096        /* spfs uses 4K blocks */
#define SECTORS_PER_BLOCK    (BLOCK_SIZE / SECTOR_SIZE)

/* Status codes */
#define VIRTIO_BLK_OK        0
#define VIRTIO_BLK_ERR       -1

/* Initialize the virtio-blk device. Returns 0 on success. */
int virtio_blk_init(void);

/* Read one block (BLOCK_SIZE bytes) from the disk. */
int virtio_blk_read_block(uint64_t block_num, uint8_t *buffer);

/* Write one block (BLOCK_SIZE bytes) to the disk. */
int virtio_blk_write_block(uint64_t block_num, const uint8_t *buffer);

/* Return total number of blocks. */
uint64_t virtio_blk_get_block_count(void);

#endif /* SHLTE_VIRTIO_BLK_H */
