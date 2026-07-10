/*
 * lib/xhci.c - USB xHCI Host Controller Driver
 *
 * USB 3.0/2.0/1.1 eXtensible Host Controller Interface.
 * Handles device enumeration, control/bulk/interrupt transfers,
 * and HID keyboard/mouse class drivers.
 */

#include <shlte/types.h>
#include <shlte/printk.h>
#include <shlte/string.h>
#include <shlte/mm.h>
#include <shlte/pcie.h>

/* xHCI PCI class codes */
#define XHCI_PCI_CLASS    0x0C
#define XHCI_PCI_SUBCLASS 0x03
#define XHCI_PCI_PROGIF   0x30

/* xHCI registers (in MMIO BAR0) */
#define XHCI_CAPLENGTH    0x00   /* Capability register length */
#define XHCI_HCIVERSION   0x02   /* Interface version */
#define XHCI_HCSPARAMS1   0x04
#define XHCI_HCSPARAMS2   0x08
#define XHCI_HCSPARAMS3   0x0C
#define XHCI_HCCPARAMS1   0x10
#define XHCI_DBOFF        0x14   /* Doorbell offset */
#define XHCI_RTSOFF       0x18   /* Runtime register space offset */

/* Operational registers (offset from CAPLENGTH) */
#define XHCI_USBCMD       0x00
#define XHCI_USBSTS       0x04
#define XHCI_PAGESIZE     0x08
#define XHCI_DNCTRL       0x14
#define XHCI_CRCR_LO      0x18
#define XHCI_CRCR_HI      0x1C
#define XHCI_CONFIG       0x38

/* USBCMD bits */
#define USBCMD_RS         (1 << 0)  /* Run/Stop */
#define USBCMD_HCRST      (1 << 1)  /* Host Controller Reset */
#define USBCMD_INTE       (1 << 2)  /* Interrupt Enable */

/* USBSTS bits */
#define USBSTS_HCH        (1 << 0)  /* HC Halted */
#define USBSTS_CNR        (1 << 11) /* Controller Not Ready */

/* Doorbell registers (offset from DBOFF) */
#define XHCI_DOORBELL(n)  ((n) * 4)

/* TRB types */
#define TRB_NORMAL        1
#define TRB_SETUP_STAGE   2
#define TRB_DATA_STAGE    3
#define TRB_STATUS_STAGE  4
#define TRB_LINK          6
#define TRB_EVENT_CMD     32
#define TRB_TRANSFER      34

/* TRB flags */
#define TRB_C      (1 << 0)  /* Cycle bit */
#define TRB_TC     (1 << 1)  /* Toggle Cycle */
#define TRB_IOC    (1 << 5)  /* Interrupt on Completion */
#define TRB_IDT    (1 << 6)  /* Immediate Data */
#define TRB_DIR_IN (1 << 16) /* Transfer direction: device to host */

/* TRB structure (16 bytes) */
typedef struct __attribute__((packed)) {
    uint64_t parameter;
    uint32_t status;
    uint32_t control;     /* Cycle bit in bit 0 of control */
} xhci_trb_t;

/* Device Context (slot context + endpoint contexts) */
typedef struct __attribute__((packed)) {
    uint32_t ctx[8];
} xhci_slot_ctx_t;

typedef struct __attribute__((packed)) {
    uint32_t ctx[8];
} xhci_ep_ctx_t;

typedef struct __attribute__((packed)) {
    xhci_slot_ctx_t slot;
    xhci_ep_ctx_t ep[31];
} xhci_device_context_t;

/* Input Control Context */
typedef struct __attribute__((packed)) {
    uint32_t drop_flags;
    uint32_t add_flags;
    uint32_t reserved[6];
} xhci_input_ctrl_t;

/* Command Ring TRB */
typedef struct __attribute__((packed)) {
    xhci_trb_t trb;
} xhci_cmd_trb_t;

/* Event Ring Segment */
typedef struct __attribute__((packed)) {
    uint64_t base;
    uint32_t size;
    uint32_t reserved;
} xhci_erst_entry_t;

/* HID Report Descriptor items */
#define HID_INPUT        0x80
#define HID_OUTPUT       0x90
#define HID_FEATURE      0xB0
#define HID_COLLECTION   0xA0

/* USB HID keyboard boot protocol report (8 bytes) */
typedef struct __attribute__((packed)) {
    uint8_t modifiers;
    uint8_t reserved;
    uint8_t keys[6];
} usb_kbd_report_t;

/* USB setup packet */
typedef struct __attribute__((packed)) {
    uint8_t  bmRequestType;
    uint8_t  bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} usb_setup_pkt_t;

/* Standard requests */
#define USB_REQ_GET_DESCRIPTOR   6
#define USB_REQ_SET_ADDRESS      5
#define USB_REQ_SET_CONFIGURATION 9
#define USB_REQ_GET_REPORT       1
#define USB_REQ_SET_IDLE         0x0A
#define USB_REQ_SET_PROTOCOL     0x0B

/* Descriptor types */
#define USB_DT_DEVICE      1
#define USB_DT_CONFIG      2
#define USB_DT_HID         0x21
#define USB_DT_REPORT      0x22
#define USB_DT_ENDPOINT    5

/* HID class requests */
#define HID_REQ_SET_IDLE   0x0A
#define HID_REQ_SET_PROTOCOL 0x0B
#define HID_PROTOCOL_BOOT  0
#define HID_PROTOCOL_REPORT 1

/* ============================================================
 * xHCI controller state
 * ============================================================ */

#define XHCI_MAX_SLOTS   64
#define XHCI_MAX_PORTS   16

typedef struct {
    pci_device_t *pci;
    volatile void *mmio;
    volatile void *op_regs;
    volatile void *db_regs;
    volatile void *rt_regs;

    uint32_t max_slots;
    uint32_t max_ports;
    uint32_t page_size;

    /* Command ring */
    xhci_trb_t *cmd_ring;
    uint32_t    cmd_ring_size;
    int         cmd_ring_idx;
    int         cmd_ring_cycle;

    /* Event ring */
    xhci_trb_t *event_ring;
    xhci_erst_entry_t *erst;
    int         event_ring_idx;
    int         event_ring_cycle;

    /* Device contexts */
    xhci_device_context_t *dcbaa[XHCI_MAX_SLOTS];

    /* Port status */
    int ports_connected[XHCI_MAX_PORTS];

    /* Keyboard state */
    uint8_t kbd_modifiers;
    uint8_t kbd_keys[6];
} xhci_ctrl_t;

/* ============================================================
 * Low-level register I/O
 * ============================================================ */

static inline uint32_t xhci_read32(xhci_ctrl_t *c, volatile void *reg)
{
    return *(volatile uint32_t *)reg;
}

static inline void xhci_write32(volatile void *reg, uint32_t val)
{
    *(volatile uint32_t *)reg = val;
}

static inline uint64_t xhci_read64(xhci_ctrl_t *c, volatile void *reg)
{
    return *(volatile uint64_t *)reg;
}

/* ============================================================
 * Command ring management
 * ============================================================ */

static void xhci_post_cmd(xhci_ctrl_t *c, xhci_trb_t *trb)
{
    int idx = c->cmd_ring_idx;
    c->cmd_ring[idx] = *trb;
    c->cmd_ring[idx].control |= c->cmd_ring_cycle;

    c->cmd_ring_idx = (idx + 1) % c->cmd_ring_size;
    if (c->cmd_ring_idx == 0)
        c->cmd_ring_cycle ^= 1;

    /* Ring doorbell 0 (command ring) */
    xhci_write32(c->db_regs, 0);
}

/* ============================================================
 * Port / device management
 * ============================================================ */

static int xhci_port_reset(xhci_ctrl_t *c, int port)
{
    volatile uint32_t *portsc = (volatile uint32_t *)((uint64_t)c->op_regs + 0x400 + port * 0x10);
    uint32_t val = xhci_read32(c, portsc);
    val |= (1 << 4);  /* Port Reset */
    xhci_write32(portsc, val);
    for (volatile int t = 0; t < 1000000; t++) {
        if (!(xhci_read32(c, portsc) & (1 << 4))) break;
    }
    return 0;
}

/* ============================================================
 * Initialization
 * ============================================================ */

int xhci_init(pci_device_t *pci)
{
    xhci_ctrl_t *c = (xhci_ctrl_t *)kmalloc(sizeof(xhci_ctrl_t));
    if (!c) return -1;
    memset(c, 0, sizeof(*c));
    c->pci = pci;

    pci_enable_device(pci);

    /* Map BAR0 */
    c->mmio = (volatile void *)pci->bars[0].base;

    uint8_t caplength = *(volatile uint8_t *)((uint64_t)c->mmio + XHCI_CAPLENGTH);
    c->op_regs = (volatile void *)((uint64_t)c->mmio + caplength);
    uint32_t dboff = xhci_read32(c, (volatile void *)((uint64_t)c->mmio + XHCI_DBOFF));
    c->db_regs = (volatile void *)((uint64_t)c->mmio + dboff);
    uint32_t rtsoff = xhci_read32(c, (volatile void *)((uint64_t)c->mmio + XHCI_RTSOFF));
    c->rt_regs = (volatile void *)((uint64_t)c->mmio + rtsoff);

    uint32_t hcsp1 = xhci_read32(c, (volatile void *)((uint64_t)c->mmio + XHCI_HCSPARAMS1));
    c->max_slots = (hcsp1 & 0xFF);
    c->max_ports = (hcsp1 >> 24) & 0xFF;

    /* Halt controller */
    volatile uint32_t *usbcmd = (volatile uint32_t *)c->op_regs;
    uint32_t cmd = xhci_read32(c, usbcmd);
    cmd &= ~USBCMD_RS;
    xhci_write32(usbcmd, cmd);
    for (volatile int t = 0; t < 5000000; t++) {
        if (xhci_read32(c, (volatile void *)((uint64_t)c->op_regs + XHCI_USBSTS)) & USBSTS_HCH)
            break;
    }

    /* Reset controller */
    xhci_write32(usbcmd, cmd | USBCMD_HCRST);
    for (volatile int t = 0; t < 5000000; t++) {
        if (!(xhci_read32(c, usbcmd) & USBCMD_HCRST)) break;
    }

    /* Allocate command ring (64 entries) */
    c->cmd_ring_size = 64;
    c->cmd_ring = (xhci_trb_t *)kmalloc_simple(c->cmd_ring_size * sizeof(xhci_trb_t) + 64);
    c->cmd_ring = (xhci_trb_t *)(((uint64_t)c->cmd_ring + 63) & ~63ULL);
    memset(c->cmd_ring, 0, c->cmd_ring_size * sizeof(xhci_trb_t));
    c->cmd_ring_cycle = 1;

    /* Set command ring pointer */
    volatile uint32_t *crcr_lo = (volatile uint32_t *)((uint64_t)c->op_regs + XHCI_CRCR_LO);
    volatile uint32_t *crcr_hi = (volatile uint32_t *)((uint64_t)c->op_regs + XHCI_CRCR_HI);
    xhci_write32(crcr_lo, (uint32_t)(uint64_t)c->cmd_ring | 1);
    xhci_write32(crcr_hi, 0);

    /* Allocate event ring */
    c->event_ring = (xhci_trb_t *)kmalloc_simple(256 * sizeof(xhci_trb_t) + 64);
    c->event_ring = (xhci_trb_t *)(((uint64_t)c->event_ring + 63) & ~63ULL);
    c->erst = (xhci_erst_entry_t *)kmalloc_simple(sizeof(xhci_erst_entry_t) + 64);
    c->erst = (xhci_erst_entry_t *)(((uint64_t)c->erst + 63) & ~63ULL);

    c->erst->base = (uint64_t)c->event_ring;
    c->erst->size = 256;

    /* Set ERST pointer */
    volatile uint64_t *erstp = (volatile uint64_t *)((uint64_t)c->rt_regs + 0x30);
    *erstp = (uint64_t)c->erst;

    /* Set max device slots */
    volatile uint32_t *config = (volatile uint32_t *)((uint64_t)c->op_regs + XHCI_CONFIG);
    xhci_write32(config, c->max_slots);

    /* Start controller */
    cmd = xhci_read32(c, usbcmd);
    xhci_write32(usbcmd, cmd | USBCMD_RS);
    for (volatile int t = 0; t < 5000000; t++) {
        if (!(xhci_read32(c, (volatile void *)((uint64_t)c->op_regs + XHCI_USBSTS)) & USBSTS_CNR))
            break;
    }

    /* Scan ports for devices */
    for (uint32_t port = 0; port < c->max_ports; port++) {
        volatile uint32_t *portsc =
            (volatile uint32_t *)((uint64_t)c->op_regs + 0x400 + port * 0x10);
        uint32_t psc = xhci_read32(c, (volatile void *)portsc);
        if (psc & 1) {  /* Current Connect Status */
            xhci_port_reset(c, (int)port);
            c->ports_connected[port] = 1;
            printk("[xHCI] Device on port %d\n", port);
        }
    }

    printk("[xHCI] Initialized: %d slots, %d ports\n",
           c->max_slots, c->max_ports);
    return 0;
}
