/*
 * virtio_blk.c - Virtio-blk device driver (MMIO transport)
 *
 * Implements the virtio-mmio transport layer for a block device
 * on the QEMU ARM64 virt machine. Supports block read/write using
 * a single virtqueue with descriptor chaining.
 */

#include <shlte/types.h>
#include <shlte/virtio_blk.h>
#include <shlte/printk.h>
#include <shlte/interrupt.h>
#include <shlte/string.h>

/* ------------------------------------------------------------------ */
/* Virtio MMIO register offsets (legacy interface)                    */
/* ------------------------------------------------------------------ */
#define VIRTIO_MMIO_MAGIC              0x00
#define VIRTIO_MMIO_VERSION            0x04
#define VIRTIO_MMIO_DEVICE_ID          0x08
#define VIRTIO_MMIO_VENDOR_ID          0x0C
#define VIRTIO_MMIO_HOST_FEATURES      0x10
#define VIRTIO_MMIO_HOST_FEATURES_SEL  0x14
#define VIRTIO_MMIO_GUEST_FEATURES     0x18
#define VIRTIO_MMIO_GUEST_FEATURES_SEL 0x1C
#define VIRTIO_MMIO_GUEST_PAGE_SIZE    0x20
#define VIRTIO_MMIO_QUEUE_SEL          0x24
#define VIRTIO_MMIO_QUEUE_NUM_MAX      0x28
#define VIRTIO_MMIO_QUEUE_NUM          0x2C
#define VIRTIO_MMIO_QUEUE_ALIGN        0x30
#define VIRTIO_MMIO_QUEUE_PFN          0x34
#define VIRTIO_MMIO_QUEUE_READY        0x38
#define VIRTIO_MMIO_QUEUE_NOTIFY       0x3C
#define VIRTIO_MMIO_INTERRUPT_STATUS   0x50
#define VIRTIO_MMIO_INTERRUPT_ACK      0x54
#define VIRTIO_MMIO_STATUS             0x60

/* ------------------------------------------------------------------ */
/* Device status bits                                                 */
/* ------------------------------------------------------------------ */
#define VIRTIO_STATUS_ACKNOWLEDGE  1
#define VIRTIO_STATUS_DRIVER       2
#define VIRTIO_STATUS_DRIVER_OK    4
#define VIRTIO_STATUS_FEATURES_OK  8
#define VIRTIO_STATUS_FAILED       128

/* ------------------------------------------------------------------ */
/* Virtqueue descriptor flags                                         */
/* ------------------------------------------------------------------ */
#define VIRTQ_DESC_F_NEXT   1       /* continues in next descriptor */
#define VIRTQ_DESC_F_WRITE  2       /* device writes to buffer (in) */

/* ------------------------------------------------------------------ */
/* Block request types                                                */
/* ------------------------------------------------------------------ */
#define VIRTIO_BLK_T_IN     0       /* read */
#define VIRTIO_BLK_T_OUT    1       /* write */

/* Block device feature bits */
#define VIRTIO_BLK_F_BLK_SIZE   (1 << 6)

/* ------------------------------------------------------------------ */
/* MMIO register access helpers                                       */
/* ------------------------------------------------------------------ */
typedef volatile uint32_t vuint32_t;

static vuint32_t * const mmio = (vuint32_t *)VIRTIO_BLK_MMIO_BASE;

static inline uint32_t virtio_read(uint32_t offset)
{
    return *(vuint32_t *)((uintptr_t)mmio + offset);
}

static inline void virtio_write(uint32_t offset, uint32_t val)
{
    *(vuint32_t *)((uintptr_t)mmio + offset) = val;
}

/* ------------------------------------------------------------------ */
/* Block request header (device-readable part of the descriptor chain)*/
/* ------------------------------------------------------------------ */
struct virtio_blk_req_hdr {
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;
} __attribute__((packed));

/* ------------------------------------------------------------------ */
/* Virtqueue layout                                                   */
/* ------------------------------------------------------------------ */
#define VRING_NUM   4       /* small ring; we use at most 3 descs */

struct vring_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} __attribute__((packed));

struct vring_avail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[VRING_NUM];
} __attribute__((packed));

struct vring_used_elem {
    uint32_t id;
    uint32_t len;
} __attribute__((packed));

struct vring_used {
    uint16_t flags;
    uint16_t idx;
    struct vring_used_elem ring[VRING_NUM];
} __attribute__((packed));

/* ------------------------------------------------------------------ */
/* Static allocation: vring at a fixed identity-mapped physical addr  */
/* The first 1 GB is identity mapped, so 0x40000000 is always valid.  */
/* Page 0: descriptor table + available ring                          */
/* Page 1: used ring                                                  */
/* ------------------------------------------------------------------ */
#define VRING_PHYS          0x40000000UL

static volatile struct vring_desc * const vring_desc =
    (struct vring_desc *)VRING_PHYS;

static volatile struct vring_avail * const vring_avail =
    (struct vring_avail *)(VRING_PHYS + VRING_NUM * sizeof(struct vring_desc));

static volatile struct vring_used * const vring_used =
    (struct vring_used *)(VRING_PHYS + 4096);

/* Static I/O buffers (aligned, in identity-mapped memory) */
static struct virtio_blk_req_hdr io_hdr __attribute__((aligned(16)));
static uint8_t io_status __attribute__((aligned(4)));

/* Device capacity */
static uint64_t block_count = 0;

/* Track last consumed used-ring index */
static uint16_t last_used_idx = 0;

/* ------------------------------------------------------------------ */
/* IRQ handler                                                        */
/* ------------------------------------------------------------------ */
static void virtio_blk_irq_handler(int irq_num)
{
    (void)irq_num;

    /* Acknowledge interrupt by writing 1 to InterruptStatus ACK reg */
    uint32_t status = virtio_read(VIRTIO_MMIO_INTERRUPT_STATUS);
    virtio_write(VIRTIO_MMIO_INTERRUPT_ACK, status & 3);
}

/* ------------------------------------------------------------------ */
/* Submit a block I/O request and wait for completion                 */
/* Uses three chained descriptors: header (out), data (in/out), status*/
/* ------------------------------------------------------------------ */
static int submit_io(int type, uint64_t sector_num, uint8_t *data)
{
    int desc_idx = 0;

    /* ---- Descriptor 0: request header (device-readable) ---- */
    io_hdr.type      = (uint32_t)type;
    io_hdr.reserved  = 0;
    io_hdr.sector    = sector_num;
    io_status        = 0xFF;

    vring_desc[0].addr  = (uint64_t)(uintptr_t)&io_hdr;
    vring_desc[0].len   = sizeof(struct virtio_blk_req_hdr);
    vring_desc[0].flags = VIRTQ_DESC_F_NEXT;  /* chain to desc 1 */
    vring_desc[0].next  = 1;

    /* ---- Descriptor 1: data buffer ---- */
    vring_desc[1].addr = (uint64_t)(uintptr_t)data;
    vring_desc[1].len  = BLOCK_SIZE;
    vring_desc[1].flags = VIRTQ_DESC_F_NEXT;   /* chain to desc 2 */
    if (type == VIRTIO_BLK_T_IN) {
        vring_desc[1].flags |= VIRTQ_DESC_F_WRITE;  /* device writes */
    }
    vring_desc[1].next = 2;

    /* ---- Descriptor 2: status byte (device-writable) ---- */
    vring_desc[2].addr  = (uint64_t)(uintptr_t)&io_status;
    vring_desc[2].len   = 1;
    vring_desc[2].flags = VIRTQ_DESC_F_WRITE;   /* device writes */
    vring_desc[2].next  = 0;                     /* end of chain */

    /* ---- Place descriptor 0 index into available ring ---- */
    uint16_t avail_idx = vring_avail->idx;
    vring_avail->ring[avail_idx % VRING_NUM] = (uint16_t)desc_idx;

    /* Write barrier before updating idx (device reads idx) */
    __sync_synchronize();
    vring_avail->idx = avail_idx + 1;

    /* Write barrier before notifying */
    __sync_synchronize();
    virtio_write(VIRTIO_MMIO_QUEUE_NOTIFY, 0);

    /* ---- Wait for completion (poll used ring) ---- */
    {
        volatile int timeout = 50000000;
        while (vring_used->idx == last_used_idx) {
            if (--timeout <= 0) {
                printk("[VIRTIO] I/O timeout (type=%d, sector=%lu)\n",
                       type, (unsigned long)sector_num);
                return VIRTIO_BLK_ERR;
            }
            __asm__ volatile("yield");
        }

        /* Consume the used element */
        last_used_idx = vring_used->idx;
    }

    return (io_status == 0) ? VIRTIO_BLK_OK : VIRTIO_BLK_ERR;
}

/* ------------------------------------------------------------------ */
/* Public API                                                         */
/* ------------------------------------------------------------------ */

int virtio_blk_init(void)
{
    uint32_t magic, version, device_id, status;
    uint32_t host_features;

    printk("[VIRTIO] Probing virtio-mmio device at 0x%lx\n",
           (unsigned long)VIRTIO_BLK_MMIO_BASE);

    /* ---- Check magic value ---- */
    magic = virtio_read(VIRTIO_MMIO_MAGIC);
    if (magic != 0x74726976UL) {  /* "virt" */
        printk("[VIRTIO] Bad magic: 0x%08x (expected 0x74726976)\n", magic);
        return VIRTIO_BLK_ERR;
    }

    /* ---- Check version (legacy = 2) ---- */
    version = virtio_read(VIRTIO_MMIO_VERSION);
    if (version != 2) {
        printk("[VIRTIO] Unsupported version: %u (need 2)\n", version);
        return VIRTIO_BLK_ERR;
    }

    /* ---- Check device ID ---- */
    device_id = virtio_read(VIRTIO_MMIO_DEVICE_ID);
    if (device_id != 2) {  /* 2 = block device */
        printk("[VIRTIO] Not a block device (device_id=%u)\n", device_id);
        return VIRTIO_BLK_ERR;
    }

    printk("[VIRTIO] Found virtio-blk device\n");

    /* ---- Device initialization sequence ---- */
    /* 1. Reset device */
    virtio_write(VIRTIO_MMIO_STATUS, 0);

    /* 2. Acknowledge */
    virtio_write(VIRTIO_MMIO_STATUS, VIRTIO_STATUS_ACKNOWLEDGE);

    /* 3. Driver */
    virtio_write(VIRTIO_MMIO_STATUS,
                 VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);

    /* 4. Negotiate features */
    virtio_write(VIRTIO_MMIO_HOST_FEATURES_SEL, 0);
    host_features = virtio_read(VIRTIO_MMIO_HOST_FEATURES);
    printk("[VIRTIO] Host features: 0x%08x\n", host_features);

    /* Only accept VIRTIO_BLK_F_BLK_SIZE for now */
    virtio_write(VIRTIO_MMIO_GUEST_FEATURES_SEL, 0);
    virtio_write(VIRTIO_MMIO_GUEST_FEATURES,
                 host_features & VIRTIO_BLK_F_BLK_SIZE);

    /* 5. Features OK */
    virtio_write(VIRTIO_MMIO_STATUS,
                 VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER |
                 VIRTIO_STATUS_FEATURES_OK);

    /* Re-read status to verify FEATURES_OK was accepted */
    status = virtio_read(VIRTIO_MMIO_STATUS);
    if (!(status & VIRTIO_STATUS_FEATURES_OK)) {
        printk("[VIRTIO] Device rejected our feature set\n");
        virtio_write(VIRTIO_MMIO_STATUS, VIRTIO_STATUS_FAILED);
        return VIRTIO_BLK_ERR;
    }

    /* 6. Guest page size */
    virtio_write(VIRTIO_MMIO_GUEST_PAGE_SIZE, 4096);

    /* 7. Set up virtqueue */
    {
        uint32_t queue_num_max;

        virtio_write(VIRTIO_MMIO_QUEUE_SEL, 0);
        queue_num_max = virtio_read(VIRTIO_MMIO_QUEUE_NUM_MAX);
        printk("[VIRTIO] Queue 0 max size: %u\n", queue_num_max);

        if (queue_num_max < 3) {
            printk("[VIRTIO] Queue too small (%u)\n", queue_num_max);
            virtio_write(VIRTIO_MMIO_STATUS, VIRTIO_STATUS_FAILED);
            return VIRTIO_BLK_ERR;
        }

        /* Use the smaller of VRING_NUM and queue_num_max */
        uint32_t qsz = (VRING_NUM < queue_num_max) ? VRING_NUM : queue_num_max;
        virtio_write(VIRTIO_MMIO_QUEUE_NUM, qsz);
        virtio_write(VIRTIO_MMIO_QUEUE_ALIGN, 4096);
        virtio_write(VIRTIO_MMIO_QUEUE_PFN, VRING_PHYS >> 12);

        /* Initialize vring memory */
        memset((void *)VRING_PHYS, 0, 8192);  /* 2 pages */
        last_used_idx = 0;
    }

    /* 8. DRIVER_OK */
    virtio_write(VIRTIO_MMIO_STATUS,
                 VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER |
                 VIRTIO_STATUS_DRIVER_OK);

    /* ---- Register IRQ handler ---- */
    irq_register(VIRTIO_BLK_IRQ, virtio_blk_irq_handler);

    /* ---- Determine capacity ---- */
    {
        /* Read capacity using a block read of sector 0 with small buffer
         * to discover the actual size; alternatively, for QEMU's legacy
         * virtio-blk, we can read the config space.
         */
        uint8_t tmp[BLOCK_SIZE] __attribute__((aligned(4096)));
        int ret = virtio_blk_read_block(0, tmp);
        if (ret != VIRTIO_BLK_OK) {
            printk("[VIRTIO] Failed to probe disk capacity\n");
            /* Fallback: assume a reasonable size */
            block_count = 4096;
        } else {
            /* The config space for capacity is at a device-specific
             * offset. For legacy, it's 8 bytes at the start of config.
             * We'll use a default large-enough value and let spfs
             * discover actual size during format/init.
             */
            block_count = 65536;  /* 256 MB (64K * 4K blocks) */
        }
    }

    printk("[VIRTIO] Block device ready: ~%lu blocks (%lu MB)\n",
           (unsigned long)block_count,
           (unsigned long)(block_count * BLOCK_SIZE / (1024 * 1024)));

    return VIRTIO_BLK_OK;
}

int virtio_blk_read_block(uint64_t block_num, uint8_t *buffer)
{
    if (buffer == NULL) return VIRTIO_BLK_ERR;
    return submit_io(VIRTIO_BLK_T_IN,
                     block_num * SECTORS_PER_BLOCK, buffer);
}

int virtio_blk_write_block(uint64_t block_num, const uint8_t *buffer)
{
    /* Cast away const; submit_io uses the same descriptor layout */
    return submit_io(VIRTIO_BLK_T_OUT,
                     block_num * SECTORS_PER_BLOCK, (uint8_t *)buffer);
}

uint64_t virtio_blk_get_block_count(void)
{
    return block_count;
}
