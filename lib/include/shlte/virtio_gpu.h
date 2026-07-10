/*
 * shlte/virtio_gpu.h - VirtIO-GPU driver declarations
 *
 * Provides framebuffer access via virtio-gpu on QEMU virt machine.
 * The GPU is detected at the virtio MMIO base (0x0A000000) by scanning
 * for device ID 16 (VIRTIO_ID_GPU).
 */

#ifndef SHLTE_VIRTIO_GPU_H
#define SHLTE_VIRTIO_GPU_H

#include <shlte/types.h>

/* Virtio protocol constants */
#define VIRTIO_GPU_DEVICE_ID        16

/* Virtio MMIO transport offsets (32-bit registers at stride 0x200) */
#define VIRTIO_MMIO_MAGIC_VALUE     0x000
#define VIRTIO_MMIO_VERSION         0x004
#define VIRTIO_MMIO_DEVICE_ID       0x008
#define VIRTIO_MMIO_VENDOR_ID       0x00C
#define VIRTIO_MMIO_DEVICE_FEATURES 0x010
#define VIRTIO_MMIO_DEVICE_FEATURES_SEL 0x014
#define VIRTIO_MMIO_DRIVER_FEATURES 0x020
#define VIRTIO_MMIO_DRIVER_FEATURES_SEL 0x024
#define VIRTIO_MMIO_QUEUE_SEL       0x030
#define VIRTIO_MMIO_QUEUE_NUM_MAX   0x034
#define VIRTIO_MMIO_QUEUE_NUM       0x038
#define VIRTIO_MMIO_QUEUE_READY     0x044
#define VIRTIO_MMIO_QUEUE_NOTIFY    0x050
#define VIRTIO_MMIO_INTERRUPT_STATUS 0x060
#define VIRTIO_MMIO_INTERRUPT_ACK   0x064
#define VIRTIO_MMIO_STATUS          0x070
#define VIRTIO_MMIO_QUEUE_DESC_LOW  0x080
#define VIRTIO_MMIO_QUEUE_DESC_HIGH 0x084
#define VIRTIO_MMIO_QUEUE_DRIVER_LOW 0x090
#define VIRTIO_MMIO_QUEUE_DRIVER_HIGH 0x094
#define VIRTIO_MMIO_QUEUE_DEVICE_LOW 0x0A0
#define VIRTIO_MMIO_QUEUE_DEVICE_HIGH 0x0A4
#define VIRTIO_MMIO_CONFIG_GENERATION 0x0FC
#define VIRTIO_MMIO_CONFIG          0x100

/* Virtio device status bits */
#define VIRTIO_STATUS_ACKNOWLEDGE   1
#define VIRTIO_STATUS_DRIVER        2
#define VIRTIO_STATUS_FAILED        128
#define VIRTIO_STATUS_FEATURES_OK   8
#define VIRTIO_STATUS_DRIVER_OK     4
#define VIRTIO_STATUS_NEEDS_RESET   64

/* Virtqueue descriptor flags */
#define VIRTQ_DESC_F_NEXT           1
#define VIRTQ_DESC_F_WRITE          2

/* Virtio-gpu config space offsets (from Config base at 0x100) */
#define VIRTIO_GPU_CONFIG_EVENTS_READ    0x00
#define VIRTIO_GPU_CONFIG_EVENTS_CLEAR   0x04
#define VIRTIO_GPU_CONFIG_NUM_SCANOUTS   0x08
#define VIRTIO_GPU_CONFIG_NUM_CAPSETS    0x0C

/* Virtio-gpu command types */
#define VIRTIO_GPU_CMD_GET_DISPLAY_INFO       0x0100
#define VIRTIO_GPU_CMD_RESOURCE_CREATE_2D     0x0101
#define VIRTIO_GPU_CMD_RESOURCE_UNREF         0x0102
#define VIRTIO_GPU_CMD_SET_SCANOUT            0x0103
#define VIRTIO_GPU_CMD_RESOURCE_FLUSH         0x0104
#define VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D    0x0105
#define VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING 0x0106
#define VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING 0x0107

/* Virtio-gpu response types */
#define VIRTIO_GPU_RESP_OK_NODATA        0x1100
#define VIRTIO_GPU_RESP_OK_DISPLAY_INFO  0x1101

/* Virtio-gpu pixel formats */
#define VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM  0x01
#define VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM  0x02
#define VIRTIO_GPU_FORMAT_R8G8B8X8_UNORM  0x03
#define VIRTIO_GPU_FORMAT_R8G8B8A8_UNORM  0x04
#define VIRTIO_GPU_FORMAT_X8R8G8B8_UNORM  0x05
#define VIRTIO_GPU_FORMAT_A8R8G8B8_UNORM  0x06

/* Virtio-gpu control header (common to all commands/responses) */
typedef struct __attribute__((packed)) {
    uint32_t type;       /* Command or response type */
    uint32_t flags;      /* FENCE flag */
    uint64_t fence_id;   /* Fence ID for fencing */
    uint32_t ctx_id;     /* Rendering context ID */
    uint32_t padding;
} virtio_gpu_ctrl_hdr_t;

/* CMD: GET_DISPLAY_INFO */
typedef struct __attribute__((packed)) {
    virtio_gpu_ctrl_hdr_t hdr;
} virtio_gpu_cmd_get_display_info_t;

/* RESP: OK_DISPLAY_INFO */
typedef struct __attribute__((packed)) {
    virtio_gpu_ctrl_hdr_t hdr;
    struct __attribute__((packed)) {
        uint32_t x;
        uint32_t y;
        uint32_t width;
        uint32_t height;
        uint32_t enabled;
        uint32_t flags;
    } pmodes[4];  /* Up to 4 scanouts */
} virtio_gpu_resp_display_info_t;

/* CMD: RESOURCE_CREATE_2D */
typedef struct __attribute__((packed)) {
    virtio_gpu_ctrl_hdr_t hdr;
    uint32_t resource_id;
    uint32_t format;
    uint32_t width;
    uint32_t height;
} virtio_gpu_cmd_resource_create_2d_t;

/* CMD: RESOURCE_ATTACH_BACKING */
typedef struct __attribute__((packed)) {
    virtio_gpu_ctrl_hdr_t hdr;
    uint32_t resource_id;
    uint32_t nr_entries;
} virtio_gpu_cmd_resource_attach_backing_t;

/* Memory entry for attach_backing */
typedef struct __attribute__((packed)) {
    uint64_t addr;
    uint32_t length;
    uint32_t padding;
} virtio_gpu_mem_entry_t;

/* CMD: SET_SCANOUT */
typedef struct __attribute__((packed)) {
    virtio_gpu_ctrl_hdr_t hdr;
    uint32_t scanout_id;
    uint32_t resource_id;
    uint32_t r_x;
    uint32_t r_y;
    uint32_t r_width;
    uint32_t r_height;
} virtio_gpu_cmd_set_scanout_t;

/* CMD: RESOURCE_FLUSH */
typedef struct __attribute__((packed)) {
    virtio_gpu_ctrl_hdr_t hdr;
    uint32_t resource_id;
    uint32_t r_x;
    uint32_t r_y;
    uint32_t r_width;
    uint32_t r_height;
    uint32_t padding;
} virtio_gpu_cmd_resource_flush_t;

/* CMD: TRANSFER_TO_HOST_2D */
typedef struct __attribute__((packed)) {
    virtio_gpu_ctrl_hdr_t hdr;
    uint32_t resource_id;
    uint32_t r_x;
    uint32_t r_y;
    uint32_t r_width;
    uint32_t r_height;
    uint64_t offset;
    uint32_t padding;
} virtio_gpu_cmd_transfer_to_host_2d_t;

/* Virtqueue descriptor */
typedef struct __attribute__((packed)) {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} virtq_desc_t;

/* Virtqueue available ring */
typedef struct __attribute__((packed)) {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[256];
    uint16_t used_event;
} virtq_avail_t;

/* Virtqueue used ring element */
typedef struct __attribute__((packed)) {
    uint32_t id;
    uint32_t len;
} virtq_used_elem_t;

/* Virtqueue used ring */
typedef struct __attribute__((packed)) {
    uint16_t flags;
    uint16_t idx;
    virtq_used_elem_t ring[256];
    uint16_t avail_event;
} virtq_used_t;

/* Maximum dimensions */
#define MAX_FB_WIDTH    1920
#define MAX_FB_HEIGHT   1080
#define MAX_FB_SIZE     (MAX_FB_WIDTH * MAX_FB_HEIGHT * 4)  /* 32-bit RGBA */

/*
 * GPU state — populated by virtio_gpu_init()
 */
typedef struct {
    int present;               /* 1 if GPU was found */
    uint32_t width;            /* Display width (pixels) */
    uint32_t height;           /* Display height (pixels) */
    uint32_t stride;           /* Bytes per row */
    uint8_t *fb;               /* Framebuffer (physically contiguous) */
    uint32_t resource_id;      /* Virtio-gpu resource ID */
    uint64_t mmio_base;        /* MMIO base address */
    uint32_t num_scanouts;
} gpu_state_t;

extern gpu_state_t g_gpu;

/* API */
int  virtio_gpu_init(void);
int  virtio_gpu_flush(void);
void virtio_gpu_fill_rect(uint32_t x, uint32_t y, uint32_t w,
                          uint32_t h, uint32_t color);
void virtio_gpu_draw_char(uint32_t x, uint32_t y, char c,
                          uint32_t fg, uint32_t bg);

#endif /* SHLTE_VIRTIO_GPU_H */
