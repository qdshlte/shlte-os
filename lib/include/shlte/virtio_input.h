/*
 * shlte/virtio_input.h - VirtIO-Input driver declarations
 *
 * Handles keyboard and mouse input via virtio-input devices
 * on the QEMU ARM64 virt machine (device ID 18).
 */

#ifndef SHLTE_VIRTIO_INPUT_H
#define SHLTE_VIRTIO_INPUT_H

#include <shlte/types.h>

/* Virtio-input device ID */
#define VIRTIO_INPUT_DEVICE_ID      18

/* Input device types (config space) */
#define VIRTIO_INPUT_CFG_UNSET      0x00
#define VIRTIO_INPUT_CFG_ID_NAME    0x01
#define VIRTIO_INPUT_CFG_ID_SERIAL  0x02
#define VIRTIO_INPUT_CFG_ID_DEVIDS  0x03
#define VIRTIO_INPUT_CFG_PROP_BITS  0x10
#define VIRTIO_INPUT_CFG_EV_BITS    0x11  /* + type */
#define VIRTIO_INPUT_CFG_ABS_INFO   0x12  /* + axis */

/* Event types (Linux input layer compatible) */
#define EV_SYN       0x00
#define EV_KEY       0x01
#define EV_REL       0x02
#define EV_ABS       0x03

/* Key codes */
#define KEY_ESC      1
#define KEY_1        2
#define KEY_2        3
#define KEY_3        4
#define KEY_4        5
#define KEY_5        6
#define KEY_6        7
#define KEY_7        8
#define KEY_8        9
#define KEY_9        10
#define KEY_0        11
#define KEY_MINUS    12
#define KEY_EQUAL    13
#define KEY_BACKSPACE 14
#define KEY_TAB      15
#define KEY_Q        16
#define KEY_W        17
#define KEY_E        18
#define KEY_R        19
#define KEY_T        20
#define KEY_Y        21
#define KEY_U        22
#define KEY_I        23
#define KEY_O        24
#define KEY_P        25
#define KEY_LEFTBRACE  26
#define KEY_RIGHTBRACE 27
#define KEY_ENTER    28
#define KEY_LEFTCTRL 29
#define KEY_A        30
#define KEY_S        31
#define KEY_D        32
#define KEY_F        33
#define KEY_G        34
#define KEY_H        35
#define KEY_J        36
#define KEY_K        37
#define KEY_L        38
#define KEY_SEMICOLON 39
#define KEY_APOSTROPHE 40
#define KEY_GRAVE    41
#define KEY_LEFTSHIFT 42
#define KEY_BACKSLASH 43
#define KEY_Z        44
#define KEY_X        45
#define KEY_C        46
#define KEY_V        47
#define KEY_B        48
#define KEY_N        49
#define KEY_M        50
#define KEY_COMMA    51
#define KEY_DOT      52
#define KEY_SLASH    53
#define KEY_RIGHTSHIFT 54
#define KEY_KPASTERISK 55
#define KEY_LEFTALT  56
#define KEY_SPACE    57
#define KEY_CAPSLOCK 58
#define KEY_F1       59
#define KEY_F2       60
#define KEY_F3       61
#define KEY_F4       62
#define KEY_F5       63
#define KEY_F6       64
#define KEY_F7       65
#define KEY_F8       66
#define KEY_F9       67
#define KEY_F10      68
#define KEY_NUMLOCK  69
#define KEY_SCROLLLOCK 70
#define KEY_KP7      71
#define KEY_KP8      72
#define KEY_KP9      73
#define KEY_KPMINUS  74
#define KEY_KP4      75
#define KEY_KP5      76
#define KEY_KP6      77
#define KEY_KPPLUS   78
#define KEY_KP1      79
#define KEY_KP2      80
#define KEY_KP3      81
#define KEY_KP0      82
#define KEY_KPDOT    83
#define KEY_RIGHTCTRL 97
#define KEY_RIGHTALT 100
#define KEY_HOME     102
#define KEY_UP       103
#define KEY_PAGEUP   104
#define KEY_LEFT     105
#define KEY_RIGHT    106
#define KEY_END      107
#define KEY_DOWN     108
#define KEY_PAGEDOWN 109
#define KEY_INSERT   110
#define KEY_DELETE   111

/* Relative axes */
#define REL_X        0x00
#define REL_Y        0x01
#define REL_WHEEL    0x08

/* Mouse buttons */
#define BTN_LEFT     0x110
#define BTN_RIGHT    0x111
#define BTN_MIDDLE   0x112

/* Virtio-input event (from virtqueue) */
typedef struct __attribute__((packed)) {
    uint16_t type;
    uint16_t code;
    uint32_t value;
} virtio_input_event_t;

/* Input event buffer */
#define INPUT_EVENT_QUEUE_SIZE  64

typedef struct {
    virtio_input_event_t events[INPUT_EVENT_QUEUE_SIZE];
    int head;
    int tail;
    int count;
} input_event_queue_t;

/* Keyboard state */
typedef struct {
    int keys_pressed[256];     /* Key state (1=pressed) */
    input_event_queue_t kbd_queue;
    input_event_queue_t mouse_queue;
    int mouse_x, mouse_y;      /* Mouse position */
    int mouse_buttons;         /* Mouse button state */
    int keyboard_present;
    int mouse_present;
    uint64_t kbd_mmio;
    uint64_t mouse_mmio;
} input_state_t;

extern input_state_t g_input;

/* API */
int  virtio_input_init(void);
int  virtio_input_poll(void);

/* Keyboard */
int  input_kbd_read_event(virtio_input_event_t *ev);
char input_keycode_to_ascii(uint16_t keycode, int shift);

/* Mouse */
int  input_mouse_read_event(virtio_input_event_t *ev);
void input_get_mouse(int *x, int *y, int *buttons);

#endif /* SHLTE_VIRTIO_INPUT_H */
