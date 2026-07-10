/*
 * virtio_input.c - VirtIO-Input Driver (Keyboard + Mouse)
 *
 * Scans for virtio-input devices on MMIO, sets up event queues,
 * and provides poll-based event reading.
 */

#include <shlte/types.h>
#include <shlte/printk.h>
#include <shlte/string.h>
#include <shlte/virtio_input.h>
#include <shlte/virtio_gpu.h>   /* for VIRTIO_MMIO_* constants */

typedef volatile uint32_t vu32;

/* ============================================================
 * MMIO Helpers
 * ============================================================ */

static inline uint32_t mmio_read(vu32 *addr) { return *addr; }
static inline void mmio_write(vu32 *addr, uint32_t val) { *addr = val; }

/* ============================================================
 * Virtqueue layout
 * ============================================================ */

#define IN_VQ_BASE      0x41200000UL
#define IN_VQ_DESC      0x41200000UL
#define IN_VQ_AVAIL     0x41201000UL
#define IN_VQ_USED      0x41202000UL
#define IN_VQ_NUM       32

/* Event buffers for polling */
#define EVENT_BUF_COUNT 8
static virtio_input_event_t __attribute__((aligned(16))) kbd_event_buf[EVENT_BUF_COUNT];
static virtio_input_event_t __attribute__((aligned(16))) mouse_event_buf[EVENT_BUF_COUNT];

/* ============================================================
 * Global state
 * ============================================================ */

input_state_t g_input;

/* ============================================================
 * Internal: scan and initialize one input device
 * ============================================================ */

typedef struct __attribute__((packed)) {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} vq_desc_t;

typedef struct __attribute__((packed)) {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[IN_VQ_NUM];
    uint16_t used_event;
} vq_avail_t;

typedef struct __attribute__((packed)) {
    uint32_t id;
    uint32_t len;
} vq_used_elem_t;

typedef struct __attribute__((packed)) {
    uint16_t flags;
    uint16_t idx;
    vq_used_elem_t ring[IN_VQ_NUM];
    uint16_t avail_event;
} vq_used_t;

static vq_desc_t  *invq_desc  = (vq_desc_t *)IN_VQ_DESC;
static vq_avail_t *invq_avail = (vq_avail_t *)IN_VQ_AVAIL;
static vq_used_t  *invq_used  = (vq_used_t *)IN_VQ_USED;

static uint16_t invq_next = 0;
static uint16_t invq_last_used = 0;

/**
 * init_input_device - Initialize a single virtio-input device
 * @mmio: MMIO base for this device
 * @events: Buffer for received events
 *
 * Returns 0 on success.
 */
static int init_input_device(vu32 *mmio, virtio_input_event_t *events)
{
    /* Reset */
    mmio_write(mmio + (VIRTIO_MMIO_STATUS / 4), 0);
    mmio_write(mmio + (VIRTIO_MMIO_STATUS / 4), VIRTIO_STATUS_ACKNOWLEDGE);
    mmio_write(mmio + (VIRTIO_MMIO_STATUS / 4),
               VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);

    /* No special features */
    mmio_write(mmio + (VIRTIO_MMIO_DRIVER_FEATURES_SEL / 4), 0);
    mmio_write(mmio + (VIRTIO_MMIO_DRIVER_FEATURES / 4), 0);

    mmio_write(mmio + (VIRTIO_MMIO_STATUS / 4),
               VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER |
               VIRTIO_STATUS_FEATURES_OK);

    /* Set up event queue */
    mmio_write(mmio + (VIRTIO_MMIO_QUEUE_SEL / 4), 0);
    uint32_t qmax = mmio_read(mmio + (VIRTIO_MMIO_QUEUE_NUM_MAX / 4));
    if (qmax > IN_VQ_NUM) qmax = IN_VQ_NUM;
    mmio_write(mmio + (VIRTIO_MMIO_QUEUE_NUM / 4), qmax);

    mmio_write(mmio + (VIRTIO_MMIO_QUEUE_DESC_LOW / 4), (uint32_t)IN_VQ_DESC);
    mmio_write(mmio + (VIRTIO_MMIO_QUEUE_DESC_HIGH / 4), 0);
    mmio_write(mmio + (VIRTIO_MMIO_QUEUE_DRIVER_LOW / 4), (uint32_t)IN_VQ_AVAIL);
    mmio_write(mmio + (VIRTIO_MMIO_QUEUE_DRIVER_HIGH / 4), 0);
    mmio_write(mmio + (VIRTIO_MMIO_QUEUE_DEVICE_LOW / 4), (uint32_t)IN_VQ_USED);
    mmio_write(mmio + (VIRTIO_MMIO_QUEUE_DEVICE_HIGH / 4), 0);

    mmio_write(mmio + (VIRTIO_MMIO_QUEUE_READY / 4), 1);

    mmio_write(mmio + (VIRTIO_MMIO_STATUS / 4),
               VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER |
               VIRTIO_STATUS_FEATURES_OK | VIRTIO_STATUS_DRIVER_OK);

    /* Pre-fill the available ring with all event buffers */
    for (int i = 0; i < EVENT_BUF_COUNT && i < (int)qmax; i++) {
        invq_desc[invq_next].addr  = (uint64_t)&events[i];
        invq_desc[invq_next].len   = sizeof(virtio_input_event_t);
        invq_desc[invq_next].flags = VIRTQ_DESC_F_WRITE;
        invq_desc[invq_next].next  = 0;

        invq_avail->ring[invq_avail->idx % IN_VQ_NUM] = invq_next;
        invq_avail->idx++;
        invq_next = (invq_next + 1) % IN_VQ_NUM;
    }
    __asm__ volatile("dmb sy");
    invq_last_used = invq_used->idx;

    return 0;
}

/**
 * poll_device - Poll a virtio-input device for new events
 * @mmio: MMIO base
 * @queue: Destination event queue
 * @events: Buffer array for this device
 *
 * Notifies the device and reads any completed used-ring entries.
 * Returns number of new events dequeued.
 */
static int poll_device(vu32 *mmio, input_event_queue_t *queue,
                       virtio_input_event_t *events)
{
    /* Notify device to give us events */
    mmio_write(mmio + (VIRTIO_MMIO_QUEUE_NOTIFY / 4), 0);

    int new_events = 0;
    __asm__ volatile("dmb sy");

    uint16_t used_idx = invq_used->idx;
    while (invq_last_used != used_idx) {
        uint32_t desc_id = invq_used->ring[invq_last_used % IN_VQ_NUM].id;
        virtio_input_event_t *ev = &events[desc_id];

        if (queue->count < INPUT_EVENT_QUEUE_SIZE) {
            queue->events[queue->tail] = *ev;
            queue->tail = (queue->tail + 1) % INPUT_EVENT_QUEUE_SIZE;
            queue->count++;
            new_events++;
        }

        /* Re-submit this descriptor */
        invq_desc[desc_id].addr  = (uint64_t)ev;
        invq_desc[desc_id].len   = sizeof(virtio_input_event_t);
        invq_desc[desc_id].flags = VIRTQ_DESC_F_WRITE;
        invq_desc[desc_id].next  = 0;
        invq_avail->ring[invq_avail->idx % IN_VQ_NUM] = desc_id;
        invq_avail->idx++;
        invq_next = (desc_id + 1) % IN_VQ_NUM;

        invq_last_used++;
        __asm__ volatile("dmb sy");
        used_idx = invq_used->idx;
    }

    return new_events;
}

/* ============================================================
 * Public API
 * ============================================================ */

/**
 * virtio_input_init - Scan for and initialize input devices
 */
int virtio_input_init(void)
{
    memset(&g_input, 0, sizeof(g_input));
    uint64_t base = 0x0A000000UL;

    for (int slot = 0; slot < 32; slot++) {
        vu32 *addr = (vu32 *)(base + slot * 0x200);
        uint32_t magic = mmio_read(addr + (VIRTIO_MMIO_MAGIC_VALUE / 4));

        if (magic != 0x74726976)  /* "virt" */
            continue;

        uint32_t dev_id = mmio_read(addr + (VIRTIO_MMIO_DEVICE_ID / 4));
        if (dev_id != VIRTIO_INPUT_DEVICE_ID)
            continue;

        /* Determine if keyboard or mouse by reading config */
        uint32_t cfg_select = mmio_read(addr + (VIRTIO_MMIO_CONFIG + 0x08));
        uint32_t cfg_subsel = mmio_read(addr + (VIRTIO_MMIO_CONFIG + 0x0C));

        printk("[INPUT] Found virtio-input at slot %d (sel=%u, sub=%u)\n",
               slot, cfg_select, cfg_subsel);

        /* Try to initialize as keyboard first, then mouse */
        if (!g_input.keyboard_present &&
            init_input_device(addr, kbd_event_buf) == 0) {
            g_input.keyboard_present = 1;
            g_input.kbd_mmio = (uint64_t)addr;
            printk("[INPUT] Keyboard initialized\n");
        } else if (!g_input.mouse_present &&
                   init_input_device(addr, mouse_event_buf) == 0) {
            g_input.mouse_present = 1;
            g_input.mouse_mmio = (uint64_t)addr;
            printk("[INPUT] Mouse initialized\n");
        }
    }

    return (g_input.keyboard_present || g_input.mouse_present) ? 0 : -1;
}

/**
 * virtio_input_poll - Poll all input devices for new events
 */
int virtio_input_poll(void)
{
    int total = 0;

    if (g_input.keyboard_present) {
        int n = poll_device((vu32 *)g_input.kbd_mmio,
                            &g_input.kbd_queue, kbd_event_buf);

        /* Process keyboard events for key state tracking */
        for (int i = 0; i < n; i++) {
            int idx = (g_input.kbd_queue.tail - n + i + INPUT_EVENT_QUEUE_SIZE)
                      % INPUT_EVENT_QUEUE_SIZE;
            virtio_input_event_t *ev = &g_input.kbd_queue.events[idx];

            if (ev->type == EV_KEY && ev->code < 256) {
                g_input.keys_pressed[ev->code] = (ev->value != 0);
            }
        }
        total += n;
    }

    if (g_input.mouse_present) {
        int n = poll_device((vu32 *)g_input.mouse_mmio,
                            &g_input.mouse_queue, mouse_event_buf);

        /* Process mouse events for position tracking */
        for (int i = 0; i < n; i++) {
            int idx = (g_input.mouse_queue.tail - n + i + INPUT_EVENT_QUEUE_SIZE)
                      % INPUT_EVENT_QUEUE_SIZE;
            virtio_input_event_t *ev = &g_input.mouse_queue.events[idx];

            if (ev->type == EV_REL) {
                if (ev->code == REL_X) g_input.mouse_x += (int32_t)ev->value;
                if (ev->code == REL_Y) g_input.mouse_y += (int32_t)ev->value;
            }
            if (ev->type == EV_KEY && ev->code >= BTN_LEFT && ev->code <= BTN_MIDDLE) {
                int bit = ev->code - BTN_LEFT;
                if (ev->value)
                    g_input.mouse_buttons |= (1 << bit);
                else
                    g_input.mouse_buttons &= ~(1 << bit);
            }
        }
        total += n;
    }

    return total;
}

/* ============================================================
 * Event access
 * ============================================================ */

int input_kbd_read_event(virtio_input_event_t *ev)
{
    if (g_input.kbd_queue.count == 0)
        return -1;

    *ev = g_input.kbd_queue.events[g_input.kbd_queue.head];
    g_input.kbd_queue.head = (g_input.kbd_queue.head + 1) % INPUT_EVENT_QUEUE_SIZE;
    g_input.kbd_queue.count--;
    return 0;
}

int input_mouse_read_event(virtio_input_event_t *ev)
{
    if (g_input.mouse_queue.count == 0)
        return -1;

    *ev = g_input.mouse_queue.events[g_input.mouse_queue.head];
    g_input.mouse_queue.head = (g_input.mouse_queue.head + 1) % INPUT_EVENT_QUEUE_SIZE;
    g_input.mouse_queue.count--;
    return 0;
}

void input_get_mouse(int *x, int *y, int *buttons)
{
    *x = g_input.mouse_x;
    *y = g_input.mouse_y;
    *buttons = g_input.mouse_buttons;
}

/* ============================================================
 * Keycode → ASCII translation
 * ============================================================ */

static const char keymap_normal[] = {
    [KEY_A] = 'a', [KEY_B] = 'b', [KEY_C] = 'c', [KEY_D] = 'd',
    [KEY_E] = 'e', [KEY_F] = 'f', [KEY_G] = 'g', [KEY_H] = 'h',
    [KEY_I] = 'i', [KEY_J] = 'j', [KEY_K] = 'k', [KEY_L] = 'l',
    [KEY_M] = 'm', [KEY_N] = 'n', [KEY_O] = 'o', [KEY_P] = 'p',
    [KEY_Q] = 'q', [KEY_R] = 'r', [KEY_S] = 's', [KEY_T] = 't',
    [KEY_U] = 'u', [KEY_V] = 'v', [KEY_W] = 'w', [KEY_X] = 'x',
    [KEY_Y] = 'y', [KEY_Z] = 'z',
    [KEY_0] = '0', [KEY_1] = '1', [KEY_2] = '2', [KEY_3] = '3',
    [KEY_4] = '4', [KEY_5] = '5', [KEY_6] = '6', [KEY_7] = '7',
    [KEY_8] = '8', [KEY_9] = '9',
    [KEY_SPACE] = ' ', [KEY_MINUS] = '-', [KEY_EQUAL] = '=',
    [KEY_LEFTBRACE] = '[', [KEY_RIGHTBRACE] = ']',
    [KEY_SEMICOLON] = ';', [KEY_APOSTROPHE] = '\'',
    [KEY_GRAVE] = '`', [KEY_BACKSLASH] = '\\',
    [KEY_COMMA] = ',', [KEY_DOT] = '.', [KEY_SLASH] = '/',
    [KEY_TAB] = '\t', [KEY_ENTER] = '\n', [KEY_BACKSPACE] = '\b',
    [KEY_KP0] = '0', [KEY_KP1] = '1', [KEY_KP2] = '2',
    [KEY_KP3] = '3', [KEY_KP4] = '4', [KEY_KP5] = '5',
    [KEY_KP6] = '6', [KEY_KP7] = '7', [KEY_KP8] = '8',
    [KEY_KP9] = '9',
    [KEY_KPASTERISK] = '*', [KEY_KPMINUS] = '-',
    [KEY_KPPLUS] = '+', [KEY_KPDOT] = '.',
};

static const char keymap_shift[] = {
    [KEY_A] = 'A', [KEY_B] = 'B', [KEY_C] = 'C', [KEY_D] = 'D',
    [KEY_E] = 'E', [KEY_F] = 'F', [KEY_G] = 'G', [KEY_H] = 'H',
    [KEY_I] = 'I', [KEY_J] = 'J', [KEY_K] = 'K', [KEY_L] = 'L',
    [KEY_M] = 'M', [KEY_N] = 'N', [KEY_O] = 'O', [KEY_P] = 'P',
    [KEY_Q] = 'Q', [KEY_R] = 'R', [KEY_S] = 'S', [KEY_T] = 'T',
    [KEY_U] = 'U', [KEY_V] = 'V', [KEY_W] = 'W', [KEY_X] = 'X',
    [KEY_Y] = 'Y', [KEY_Z] = 'Z',
    [KEY_0] = ')', [KEY_1] = '!', [KEY_2] = '@', [KEY_3] = '#',
    [KEY_4] = '$', [KEY_5] = '%', [KEY_6] = '^', [KEY_7] = '&',
    [KEY_8] = '*', [KEY_9] = '(',
    [KEY_MINUS] = '_', [KEY_EQUAL] = '+',
    [KEY_LEFTBRACE] = '{', [KEY_RIGHTBRACE] = '}',
    [KEY_SEMICOLON] = ':', [KEY_APOSTROPHE] = '"',
    [KEY_GRAVE] = '~', [KEY_BACKSLASH] = '|',
    [KEY_COMMA] = '<', [KEY_DOT] = '>', [KEY_SLASH] = '?',
};

char input_keycode_to_ascii(uint16_t keycode, int shift)
{
    if (keycode >= sizeof(keymap_normal))
        return 0;

    if (shift)
        return keymap_shift[keycode];
    return keymap_normal[keycode];
}
