/*
 * lib/e1000.c - Intel e1000/e1000e Gigabit Ethernet Driver
 *
 * PCIe network driver supporting Intel 82540EM/82574L and
 * similar e1000-family controllers. Uses legacy interrupt
 * or polling mode for packet I/O.
 */

#include <shlte/types.h>
#include <shlte/printk.h>
#include <shlte/string.h>
#include <shlte/mm.h>
#include <shlte/pcie.h>
#include <shlte/net.h>

/* E1000 registers */
#define E1000_CTRL      0x0000
#define E1000_STATUS    0x0008
#define E1000_EECD      0x0010
#define E1000_ICR       0x00C0
#define E1000_IMS       0x00D0
#define E1000_RCTL      0x0100
#define E1000_TCTL      0x0400
#define E1000_TIPG      0x0410
#define E1000_RDBAL     0x2800
#define E1000_RDBAH     0x2804
#define E1000_RDLEN     0x2808
#define E1000_RDH       0x2810
#define E1000_RDT       0x2818
#define E1000_TDBAL     0x3800
#define E1000_TDBAH     0x3804
#define E1000_TDLEN     0x3808
#define E1000_TDH       0x3810
#define E1000_TDT       0x3818
#define E1000_RA_BASE   0x5400
#define E1000_MTA       0x5200

/* RCTL bits */
#define RCTL_EN         (1 << 1)
#define RCTL_SBP        (1 << 2)
#define RCTL_UPE        (1 << 3)
#define RCTL_MPE        (1 << 4)
#define RCTL_LPE        (1 << 5)
#define RCTL_LBM_NO     (0 << 6)
#define RCTL_BAM        (1 << 15)
#define RCTL_BSIZE_2048 (0 << 16)
#define RCTL_BSIZE_4096 (0x30 << 16)
#define RCTL_SECRC      (1 << 26)

/* TCTL bits */
#define TCTL_EN         (1 << 1)
#define TCTL_PSP        (1 << 3)
#define TCTL_CT(x)      ((x) << 4)
#define TCTL_COLD(x)    ((x) << 12)

/* CTRL bits */
#define CTRL_FD         (1 << 0)
#define CTRL_SLU        (1 << 6)
#define CTRL_RST        (1 << 26)
#define CTRL_PHY_RST    (1 << 31)

/* Receive descriptor */
typedef struct __attribute__((packed)) {
    uint64_t addr;
    uint16_t length;
    uint16_t checksum;
    uint8_t  status;
    uint8_t  errors;
    uint16_t special;
} e1000_rx_desc_t;

/* Transmit descriptor */
typedef struct __attribute__((packed)) {
    uint64_t addr;
    uint16_t length;
    uint8_t  cso;
    uint8_t  cmd;
    uint8_t  status;
    uint8_t  css;
    uint16_t special;
} e1000_tx_desc_t;

#define E1000_TXD_CMD_EOP  (1 << 0)
#define E1000_TXD_CMD_RS   (1 << 3)
#define E1000_RXD_STAT_DD  (1 << 0)
#define E1000_RXD_STAT_EOP (1 << 1)

#define E1000_NUM_RX_DESC  32
#define E1000_NUM_TX_DESC  32

typedef struct {
    pci_device_t  *pci;
    volatile void *mmio;
    uint8_t        mac[ETH_ALEN];

    e1000_rx_desc_t rx_desc[E1000_NUM_RX_DESC] __attribute__((aligned(16)));
    e1000_tx_desc_t tx_desc[E1000_NUM_TX_DESC] __attribute__((aligned(16)));
    uint8_t         rx_buf[E1000_NUM_RX_DESC][2048] __attribute__((aligned(16)));
    int             rx_tail;
    int             tx_tail;

    net_device_t   *netdev;
} e1000_dev_t;

static uint32_t e1000_read(e1000_dev_t *d, uint32_t reg)
{
    volatile uint32_t *p = (volatile uint32_t *)((uint64_t)d->mmio + reg);
    return *p;
}

static void e1000_write(e1000_dev_t *d, uint32_t reg, uint32_t val)
{
    volatile uint32_t *p = (volatile uint32_t *)((uint64_t)d->mmio + reg);
    *p = val;
}

static int e1000_send(struct net_device *ndev, const void *data, size_t len)
{
    e1000_dev_t *d = (e1000_dev_t *)ndev->driver_data;
    if (!d || len > 2048) return -1;

    int idx = d->tx_tail;
    d->tx_desc[idx].addr   = (uint64_t)data;
    d->tx_desc[idx].length = (uint16_t)len;
    d->tx_desc[idx].cmd    = E1000_TXD_CMD_EOP | E1000_TXD_CMD_RS;
    d->tx_desc[idx].status = 0;

    d->tx_tail = (idx + 1) % E1000_NUM_TX_DESC;
    e1000_write(d, E1000_TDT, (uint32_t)d->tx_tail);

    /* Wait for completion */
    for (volatile int t = 0; t < 1000000; t++) {
        if (d->tx_desc[idx].status & 0x0F) break;
    }

    return (int)len;
}

static int e1000_poll(struct net_device *ndev)
{
    e1000_dev_t *d = (e1000_dev_t *)ndev->driver_data;
    if (!d) return 0;

    int count = 0;
    int idx = (d->rx_tail + 1) % E1000_NUM_RX_DESC;

    while (d->rx_desc[idx].status & E1000_RXD_STAT_DD) {
        uint16_t len = d->rx_desc[idx].length;
        if (len > 0 && len <= ETH_MAX_FRAME) {
            memcpy(ndev->rx_buf, d->rx_buf[idx], len);
            ndev->rx_len = len;

            eth_hdr_t *eth = (eth_hdr_t *)ndev->rx_buf;
            uint16_t etype = ((uint16_t)eth->ethertype << 8) |
                             (uint16_t)(eth->ethertype >> 8);
            if (etype == ETH_P_ARP)
                net_arp_handle(ndev, ndev->rx_buf + ETH_HLEN, len - ETH_HLEN);
            else if (etype == ETH_P_IP)
                net_ip_handle(ndev, ndev->rx_buf + ETH_HLEN, len - ETH_HLEN);

            count++;
        }
        d->rx_desc[idx].status = 0;
        idx = (idx + 1) % E1000_NUM_RX_DESC;
    }
    d->rx_tail = (idx - 1 + E1000_NUM_RX_DESC) % E1000_NUM_RX_DESC;
    e1000_write(d, E1000_RDT, (uint32_t)d->rx_tail);

    return count;
}

int e1000_init(pci_device_t *pci, net_device_t *ndev)
{
    e1000_dev_t *d = (e1000_dev_t *)kmalloc(sizeof(e1000_dev_t));
    if (!d) return -1;
    memset(d, 0, sizeof(*d));
    d->pci = pci;
    d->netdev = ndev;
    ndev->driver_data = d;

    pci_enable_device(pci);

    /* Map BAR0 (MMIO registers) */
    d->mmio = (volatile void *)pci->bars[0].base;

    /* Read MAC from EEPROM */
    uint32_t ral = e1000_read(d, E1000_RA_BASE);
    uint32_t rah = e1000_read(d, E1000_RA_BASE + 4);
    d->mac[0] = (uint8_t)(ral & 0xFF);
    d->mac[1] = (uint8_t)((ral >> 8) & 0xFF);
    d->mac[2] = (uint8_t)((ral >> 16) & 0xFF);
    d->mac[3] = (uint8_t)((ral >> 24) & 0xFF);
    d->mac[4] = (uint8_t)(rah & 0xFF);
    d->mac[5] = (uint8_t)((rah >> 8) & 0xFF);
    memcpy(ndev->mac, d->mac, ETH_ALEN);

    /* Reset */
    e1000_write(d, E1000_CTRL, e1000_read(d, E1000_CTRL) | CTRL_RST);
    for (volatile int t = 0; t < 100000; t++) __asm__("nop");

    /* Set up RX descriptors */
    for (int i = 0; i < E1000_NUM_RX_DESC; i++) {
        d->rx_desc[i].addr = (uint64_t)d->rx_buf[i];
        d->rx_desc[i].status = 0;
    }
    e1000_write(d, E1000_RDBAL, (uint32_t)(uint64_t)d->rx_desc);
    e1000_write(d, E1000_RDBAH, 0);
    e1000_write(d, E1000_RDLEN, E1000_NUM_RX_DESC * sizeof(e1000_rx_desc_t));
    e1000_write(d, E1000_RDH, 0);
    d->rx_tail = E1000_NUM_RX_DESC - 1;
    e1000_write(d, E1000_RDT, (uint32_t)d->rx_tail);
    e1000_write(d, E1000_RCTL, RCTL_EN | RCTL_BAM | RCTL_SECRC |
                RCTL_BSIZE_2048 | RCTL_UPE | RCTL_MPE);

    /* Set up TX descriptors */
    for (int i = 0; i < E1000_NUM_TX_DESC; i++)
        d->tx_desc[i].status = 1;
    e1000_write(d, E1000_TDBAL, (uint32_t)(uint64_t)d->tx_desc);
    e1000_write(d, E1000_TDBAH, 0);
    e1000_write(d, E1000_TDLEN, E1000_NUM_TX_DESC * sizeof(e1000_tx_desc_t));
    e1000_write(d, E1000_TDH, 0);
    e1000_write(d, E1000_TDT, 0);
    e1000_write(d, E1000_TCTL, TCTL_EN | TCTL_PSP | TCTL_CT(0x0F) | TCTL_COLD(0x40));

    /* Enable link */
    e1000_write(d, E1000_CTRL, e1000_read(d, E1000_CTRL) | CTRL_SLU);

    ndev->send = e1000_send;
    ndev->poll = e1000_poll;
    ndev->mtu  = ETH_MTU;
    ndev->up   = 1;

    printk("[e1000] MAC=%02x:%02x:%02x:%02x:%02x:%02x\n",
           d->mac[0], d->mac[1], d->mac[2], d->mac[3], d->mac[4], d->mac[5]);
    return 0;
}
