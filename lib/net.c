/*
 * net.c - Network Stack Implementation
 *
 * Virtio-net driver, ARP/IP/UDP protocol handlers, DHCP client,
 * and BSD-socket-compatible API.
 */

#include <shlte/types.h>
#include <shlte/printk.h>
#include <shlte/string.h>
#include <shlte/mm.h>
#include <shlte/net.h>

/* ============================================================
 * Virtio-net MMIO driver
 * ============================================================ */

#include <shlte/virtio_gpu.h>  /* For VIRTIO_MMIO_* constants */

#define VIRTIO_NET_DEVICE_ID    1

typedef volatile uint32_t vu32;

static inline uint32_t vread(vu32 *a) { return *a; }
static inline void vwrite(vu32 *a, uint32_t v) { *a = v; }

/* Virtqueue layout for net */
#define NET_VQ_BASE     0x41400000ULL
#define NET_VQ_DESC     0x41400000ULL
#define NET_VQ_AVAIL    0x41401000ULL
#define NET_VQ_USED     0x41402000ULL
#define NET_VQ_NUM      64

typedef struct __attribute__((packed)) {
    uint64_t addr; uint32_t len; uint16_t flags; uint16_t next;
} net_vq_desc_t;

typedef struct __attribute__((packed)) {
    uint16_t flags; uint16_t idx; uint16_t ring[NET_VQ_NUM]; uint16_t used_event;
} net_vq_avail_t;

typedef struct __attribute__((packed)) {
    uint32_t id; uint32_t len;
} net_vq_used_elem_t;

typedef struct __attribute__((packed)) {
    uint16_t flags; uint16_t idx;
    net_vq_used_elem_t ring[NET_VQ_NUM];
    uint16_t avail_event;
} net_vq_used_t;

static net_vq_desc_t  *nvq_desc  = (net_vq_desc_t *)NET_VQ_DESC;
static net_vq_avail_t *nvq_avail = (net_vq_avail_t *)NET_VQ_AVAIL;
static net_vq_used_t  *nvq_used  = (net_vq_used_t *)NET_VQ_USED;
static uint16_t nvq_next_desc = 0;
static uint16_t nvq_last_used = 0;

/* RX buffer pool */
#define NET_RX_BUFS     16
static uint8_t __attribute__((aligned(16))) net_rx_bufs[NET_RX_BUFS][ETH_MAX_FRAME];
static int net_rx_avail = NET_RX_BUFS;

/* Forward declarations */
static net_device_t *g_net_devices = NULL;
net_device_t *g_net_dev = NULL;
socket_t g_sockets[MAX_SOCKETS];
int g_num_sockets = 0;

/* ============================================================
 * Virtio-net device state
 * ============================================================ */

typedef struct {
    vu32   *mmio;
    uint8_t mac[ETH_ALEN];
    int     rx_q;     /* queue 0 = receive */
    int     tx_q;     /* queue 1 = transmit */
} virtio_net_t;

static virtio_net_t g_vnet;
static int g_net_initialized = 0;

/* ============================================================
 * Virtio-net send / receive
 * ============================================================ */

static int virtio_net_send(struct net_device *dev, const void *data, size_t len)
{
    (void)dev;
    if (!g_vnet.mmio || len > ETH_MAX_FRAME) return -1;

    uint16_t d = nvq_next_desc;
    nvq_desc[d].addr  = (uint64_t)data;
    nvq_desc[d].len   = (uint32_t)len;
    nvq_desc[d].flags = 0;
    nvq_desc[d].next  = 0;

    nvq_avail->ring[nvq_avail->idx % NET_VQ_NUM] = d;
    __asm__ volatile("dmb sy");
    nvq_avail->idx++;
    nvq_next_desc = (d + 1) % NET_VQ_NUM;

    /* Notify TX queue (queue 1) */
    vwrite(g_vnet.mmio + (VIRTIO_MMIO_QUEUE_SEL / 4), 1);
    vwrite(g_vnet.mmio + (VIRTIO_MMIO_QUEUE_NOTIFY / 4), 0);

    /* Poll for completion */
    for (volatile int t = 0; t < 1000000; t++) {
        __asm__ volatile("dmb sy");
        if (nvq_used->idx != nvq_last_used) break;
    }

    return (int)len;
}

static int virtio_net_poll(struct net_device *dev)
{
    if (!g_vnet.mmio) return 0;

    /* Re-fill RX buffers */
    vwrite(g_vnet.mmio + (VIRTIO_MMIO_QUEUE_SEL / 4), 0);
    while (net_rx_avail > 0) {
        uint16_t d = nvq_next_desc;
        int bi = NET_RX_BUFS - net_rx_avail;
        nvq_desc[d].addr  = (uint64_t)net_rx_bufs[bi];
        nvq_desc[d].len   = ETH_MAX_FRAME;
        nvq_desc[d].flags = 2;  /* VIRTQ_DESC_F_WRITE */
        nvq_desc[d].next  = 0;

        nvq_avail->ring[nvq_avail->idx % NET_VQ_NUM] = d;
        __asm__ volatile("dmb sy");
        nvq_avail->idx++;
        nvq_next_desc = (d + 1) % NET_VQ_NUM;
        net_rx_avail--;
    }

    /* Check for received packets */
    __asm__ volatile("dmb sy");
    if (nvq_used->idx == nvq_last_used) return 0;

    /* Process completion */
    nvq_last_used = nvq_used->idx;
    int count = 0;

    for (int i = 0; i < NET_RX_BUFS && count < 4; i++) {
        /* Read used ring entry */
        net_vq_used_elem_t *ue = &nvq_used->ring[i % NET_VQ_NUM];
        int bi = ue->id;
        if (bi >= NET_RX_BUFS) continue;

        int pkt_len = ue->len;
        if (pkt_len > 0 && pkt_len <= ETH_MAX_FRAME) {
            memcpy(dev->rx_buf, net_rx_bufs[bi], (size_t)pkt_len);
            dev->rx_len = pkt_len;

            /* Parse ethernet header */
            eth_hdr_t *eth = (eth_hdr_t *)dev->rx_buf;
            uint16_t etype = ((uint16_t)eth->ethertype << 8) | (uint16_t)(eth->ethertype >> 8);

            if (etype == ETH_P_ARP) {
                net_arp_handle(dev, dev->rx_buf + ETH_HLEN, (size_t)(pkt_len - ETH_HLEN));
            } else if (etype == ETH_P_IP) {
                net_ip_handle(dev, dev->rx_buf + ETH_HLEN, (size_t)(pkt_len - ETH_HLEN));
            }
            net_rx_avail++;
            count++;
        }
    }

    return count;
}

/* ============================================================
 * Virtio-net initialization
 * ============================================================ */

static int virtio_net_init(net_device_t *dev)
{
    memset(&g_vnet, 0, sizeof(g_vnet));
    uint64_t base = 0x0A000000ULL;

    /* Scan for virtio-net device */
    for (int slot = 0; slot < 32; slot++) {
        vu32 *mmio = (vu32 *)(base + slot * 0x200);
        if (vread(mmio + (VIRTIO_MMIO_MAGIC_VALUE / 4)) != 0x74726976)
            continue;
        if (vread(mmio + (VIRTIO_MMIO_DEVICE_ID / 4)) != VIRTIO_NET_DEVICE_ID)
            continue;

        g_vnet.mmio = mmio;

        /* Read MAC from config space */
        for (int i = 0; i < 6; i++) {
            dev->mac[i] = (uint8_t)vread(mmio + (VIRTIO_MMIO_CONFIG / 4) + i);
        }

        /* Init device */
        vwrite(mmio + (VIRTIO_MMIO_STATUS / 4), 0);
        vwrite(mmio + (VIRTIO_MMIO_STATUS / 4), 1);  /* ACKNOWLEDGE */
        vwrite(mmio + (VIRTIO_MMIO_STATUS / 4), 3);  /* + DRIVER */
        vwrite(mmio + (VIRTIO_MMIO_DRIVER_FEATURES_SEL / 4), 0);
        vwrite(mmio + (VIRTIO_MMIO_DRIVER_FEATURES / 4), 0);
        vwrite(mmio + (VIRTIO_MMIO_STATUS / 4), 11); /* + FEATURES_OK */

        /* Set up RX queue (0) */
        vwrite(mmio + (VIRTIO_MMIO_QUEUE_SEL / 4), 0);
        uint32_t qmax = vread(mmio + (VIRTIO_MMIO_QUEUE_NUM_MAX / 4));
        if (qmax > NET_VQ_NUM) qmax = NET_VQ_NUM;
        vwrite(mmio + (VIRTIO_MMIO_QUEUE_NUM / 4), qmax);
        vwrite(mmio + (VIRTIO_MMIO_QUEUE_DESC_LOW / 4), (uint32_t)NET_VQ_DESC);
        vwrite(mmio + (VIRTIO_MMIO_QUEUE_DESC_HIGH / 4), 0);
        vwrite(mmio + (VIRTIO_MMIO_QUEUE_DRIVER_LOW / 4), (uint32_t)NET_VQ_AVAIL);
        vwrite(mmio + (VIRTIO_MMIO_QUEUE_DRIVER_HIGH / 4), 0);
        vwrite(mmio + (VIRTIO_MMIO_QUEUE_DEVICE_LOW / 4), (uint32_t)NET_VQ_USED);
        vwrite(mmio + (VIRTIO_MMIO_QUEUE_DEVICE_HIGH / 4), 0);
        vwrite(mmio + (VIRTIO_MMIO_QUEUE_READY / 4), 1);

        /* Set up TX queue (1) - reuse same descriptors */
        vwrite(mmio + (VIRTIO_MMIO_QUEUE_SEL / 4), 1);
        vwrite(mmio + (VIRTIO_MMIO_QUEUE_NUM / 4), qmax);
        vwrite(mmio + (VIRTIO_MMIO_QUEUE_READY / 4), 1);

        vwrite(mmio + (VIRTIO_MMIO_STATUS / 4), 15); /* + DRIVER_OK */

        printk("[NET] virtio-net: MAC=%02x:%02x:%02x:%02x:%02x:%02x\n",
               dev->mac[0], dev->mac[1], dev->mac[2],
               dev->mac[3], dev->mac[4], dev->mac[5]);
        return 0;
    }

    printk("[NET] No virtio-net device found\n");
    return -1;
}

/* ============================================================
 * Protocol handlers
 * ============================================================ */

void net_arp_handle(net_device_t *dev, const uint8_t *pkt, size_t len)
{
    if (len < sizeof(arp_pkt_t)) return;
    arp_pkt_t *arp = (arp_pkt_t *)pkt;

    uint16_t oper = ((uint16_t)arp->oper << 8) | (uint16_t)(arp->oper >> 8);
    int hlen = arp->hlen, plen = arp->plen;

    /* Update ARP cache with sender info */
    for (int i = 0; i < ARP_TABLE_SIZE; i++) {
        if (!dev->arp_table[i].valid) {
            memcpy(dev->arp_table[i].ip, arp->spa, (size_t)plen);
            memcpy(dev->arp_table[i].mac, arp->sha, (size_t)hlen);
            dev->arp_table[i].valid = 1;
            break;
        }
        if (memcmp(dev->arp_table[i].ip, arp->spa, 4) == 0) {
            memcpy(dev->arp_table[i].mac, arp->sha, 6);
            break;
        }
    }

    if (oper == ARP_REQUEST) {
        /* Check if target is our IP */
        if (memcmp(arp->tpa, dev->ip, 4) == 0) {
            /* Send ARP reply */
            uint8_t reply[sizeof(eth_hdr_t) + sizeof(arp_pkt_t)];
            eth_hdr_t *eth = (eth_hdr_t *)reply;
            arp_pkt_t *arp_r = (arp_pkt_t *)(reply + sizeof(eth_hdr_t));

            memcpy(eth->dst, arp->sha, ETH_ALEN);
            memcpy(eth->src, dev->mac, ETH_ALEN);
            eth->ethertype = (uint16_t)((ETH_P_ARP >> 8) | ((ETH_P_ARP & 0xFF) << 8));

            memset(arp_r, 0, sizeof(arp_pkt_t));
            arp_r->htype = (uint16_t)((ARP_HTYPE_ETH >> 8) | ((ARP_HTYPE_ETH & 0xFF) << 8));
            arp_r->ptype = (uint16_t)((ETH_P_IP >> 8) | ((ETH_P_IP & 0xFF) << 8));
            arp_r->hlen  = ETH_ALEN;
            arp_r->plen  = 4;
            arp_r->oper  = (uint16_t)((ARP_REPLY >> 8) | ((ARP_REPLY & 0xFF) << 8));
            memcpy(arp_r->sha, dev->mac, ETH_ALEN);
            memcpy(arp_r->spa, dev->ip, 4);
            memcpy(arp_r->tha, arp->sha, ETH_ALEN);
            memcpy(arp_r->tpa, arp->spa, 4);

            dev->send(dev, reply, sizeof(reply));
        }
    }
}

void net_ip_handle(net_device_t *dev, const uint8_t *pkt, size_t len)
{
    if (len < sizeof(ip_hdr_t)) return;
    ip_hdr_t *ip = (ip_hdr_t *)pkt;
    int ihl = (ip->ver_ihl & 0x0F) * 4;

    if (ip->protocol == IPPROTO_UDP) {
        net_udp_handle(dev, pkt + ihl, len - ihl, ip->src);
    }
}

void net_udp_handle(net_device_t *dev, const uint8_t *pkt, size_t len, uint8_t src_ip[4])
{
    if (len < sizeof(udp_hdr_t)) return;
    udp_hdr_t *udp = (udp_hdr_t *)pkt;
    uint16_t dst_port = ((uint16_t)udp->dst_port << 8) | (uint16_t)(udp->dst_port >> 8);

    /* DHCP response (port 68) */
    if (dst_port == DHCP_CLIENT_PORT) {
        net_dhcp_discover(dev);  /* Process the response */
    }

    /* Deliver to matching socket */
    for (int i = 0; i < MAX_SOCKETS; i++) {
        if (g_sockets[i].in_use && g_sockets[i].local_port == dst_port) {
            size_t data_len = len - sizeof(udp_hdr_t);
            if (data_len > sizeof(g_sockets[i].rx_buf) - g_sockets[i].rx_len)
                data_len = sizeof(g_sockets[i].rx_buf) - g_sockets[i].rx_len;
            memcpy(g_sockets[i].rx_buf + g_sockets[i].rx_len,
                   pkt + sizeof(udp_hdr_t), data_len);
            g_sockets[i].rx_len += (int)data_len;
            memcpy(g_sockets[i].remote_ip, src_ip, 4);
            g_sockets[i].remote_port = (uint16_t)(((uint16_t)udp->src_port << 8) |
                                          (uint16_t)(udp->src_port >> 8));
        }
    }
}

/* ============================================================
 * ARP resolution
 * ============================================================ */

int net_arp_resolve(net_device_t *dev, const uint8_t *ip, uint8_t *mac_out)
{
    for (int i = 0; i < ARP_TABLE_SIZE; i++) {
        if (dev->arp_table[i].valid && memcmp(dev->arp_table[i].ip, ip, 4) == 0) {
            memcpy(mac_out, dev->arp_table[i].mac, ETH_ALEN);
            return 0;
        }
    }
    return -1;
}

void net_arp_send_request(net_device_t *dev, const uint8_t *target_ip)
{
    uint8_t pkt[sizeof(eth_hdr_t) + sizeof(arp_pkt_t)];
    eth_hdr_t *eth = (eth_hdr_t *)pkt;
    arp_pkt_t *arp = (arp_pkt_t *)(pkt + sizeof(eth_hdr_t));

    memset(eth->dst, 0xFF, ETH_ALEN);  /* Broadcast */
    memcpy(eth->src, dev->mac, ETH_ALEN);
    eth->ethertype = (uint16_t)((ETH_P_ARP >> 8) | ((ETH_P_ARP & 0xFF) << 8));

    memset(arp, 0, sizeof(arp_pkt_t));
    arp->htype = (uint16_t)((ARP_HTYPE_ETH >> 8) | ((ARP_HTYPE_ETH & 0xFF) << 8));
    arp->ptype = (uint16_t)((ETH_P_IP >> 8) | ((ETH_P_IP & 0xFF) << 8));
    arp->hlen  = ETH_ALEN;
    arp->plen  = 4;
    arp->oper  = (uint16_t)((ARP_REQUEST >> 8) | ((ARP_REQUEST & 0xFF) << 8));
    memcpy(arp->sha, dev->mac, ETH_ALEN);
    memcpy(arp->spa, dev->ip, 4);
    memset(arp->tha, 0, ETH_ALEN);
    memcpy(arp->tpa, target_ip, 4);

    dev->send(dev, pkt, sizeof(pkt));
}

/* ============================================================
 * IP / UDP send
 * ============================================================ */

int net_ip_send(net_device_t *dev, const uint8_t *dst_ip, uint8_t proto,
                const void *data, size_t len)
{
    uint8_t dst_mac[ETH_ALEN];

    /* Resolve MAC via ARP */
    if (net_arp_resolve(dev, dst_ip, dst_mac) != 0) {
        net_arp_send_request(dev, dst_ip);
        return -1;
    }

    size_t total = sizeof(eth_hdr_t) + sizeof(ip_hdr_t) + len;
    if (total > ETH_MAX_FRAME) return -1;

    uint8_t *pkt = (uint8_t *)kmalloc(total);
    if (!pkt) return -1;

    eth_hdr_t *eth = (eth_hdr_t *)pkt;
    ip_hdr_t  *ip  = (ip_hdr_t *)(pkt + sizeof(eth_hdr_t));

    memcpy(eth->dst, dst_mac, ETH_ALEN);
    memcpy(eth->src, dev->mac, ETH_ALEN);
    eth->ethertype = (uint16_t)((ETH_P_IP >> 8) | ((ETH_P_IP & 0xFF) << 8));

    size_t ip_len = sizeof(ip_hdr_t) + len;
    memset(ip, 0, sizeof(ip_hdr_t));
    ip->ver_ihl   = 0x45;
    ip->total_len = (uint16_t)((ip_len >> 8) | ((ip_len & 0xFF) << 8));
    ip->ttl       = 64;
    ip->protocol  = proto;
    memcpy(ip->src, dev->ip, 4);
    memcpy(ip->dst, dst_ip, 4);
    ip->checksum  = net_checksum(ip, sizeof(ip_hdr_t));

    memcpy(pkt + sizeof(eth_hdr_t) + sizeof(ip_hdr_t), data, len);

    int ret = dev->send(dev, pkt, total);
    kfree(pkt);
    return ret;
}

int net_udp_send(net_device_t *dev, const uint8_t *dst_ip,
                 uint16_t src_port, uint16_t dst_port,
                 const void *data, size_t len)
{
    size_t total = sizeof(udp_hdr_t) + len;
    uint8_t *buf = (uint8_t *)kmalloc(total);
    if (!buf) return -1;

    udp_hdr_t *udp = (udp_hdr_t *)buf;
    udp->src_port = (uint16_t)((src_port >> 8) | ((src_port & 0xFF) << 8));
    udp->dst_port = (uint16_t)((dst_port >> 8) | ((dst_port & 0xFF) << 8));
    udp->length   = (uint16_t)((total >> 8) | ((total & 0xFF) << 8));
    udp->checksum = 0;  /* Optional for IPv4 */
    memcpy(buf + sizeof(udp_hdr_t), data, len);

    int ret = net_ip_send(dev, dst_ip, IPPROTO_UDP, buf, total);
    kfree(buf);
    return ret;
}

/* ============================================================
 * DHCP client
 * ============================================================ */

int net_dhcp_discover(net_device_t *dev)
{
    uint8_t pkt[sizeof(dhcp_pkt_t) + 64];
    dhcp_pkt_t *dhcp = (dhcp_pkt_t *)pkt;

    memset(dhcp, 0, sizeof(*dhcp));
    dhcp->op    = 1;  /* BOOTREQUEST */
    dhcp->htype = 1;  /* Ethernet */
    dhcp->hlen  = ETH_ALEN;
    dhcp->xid   = 0x12345678;
    memcpy(dhcp->chaddr, dev->mac, ETH_ALEN);
    dhcp->magic_cookie = DHCP_MAGIC_COOKIE;

    /* Option: DHCP Discover */
    uint8_t *opts = dhcp->options;
    *opts++ = DHCP_OPT_MSG_TYPE; *opts++ = 1; *opts++ = DHCP_DISCOVER;
    *opts++ = DHCP_OPT_END;

    uint8_t broadcast_ip[4] = {255, 255, 255, 255};
    return net_udp_send(dev, broadcast_ip, DHCP_CLIENT_PORT, DHCP_SERVER_PORT,
                        dhcp, (size_t)(opts - (uint8_t *)dhcp));
}

/* ============================================================
 * Socket layer
 * ============================================================ */

int net_socket(int type)
{
    for (int i = 0; i < MAX_SOCKETS; i++) {
        if (!g_sockets[i].in_use) {
            memset(&g_sockets[i], 0, sizeof(socket_t));
            g_sockets[i].fd   = i + 100;
            g_sockets[i].type = type;
            g_sockets[i].in_use = 1;
            g_num_sockets++;
            return g_sockets[i].fd;
        }
    }
    return -1;
}

int net_bind(int fd, uint16_t port)
{
    for (int i = 0; i < MAX_SOCKETS; i++) {
        if (g_sockets[i].fd == fd && g_sockets[i].in_use) {
            g_sockets[i].local_port = port;
            return 0;
        }
    }
    return -1;
}

int net_sendto(int fd, const void *data, size_t len,
               const uint8_t *dst_ip, uint16_t dst_port)
{
    for (int i = 0; i < MAX_SOCKETS; i++) {
        if (g_sockets[i].fd == fd && g_sockets[i].in_use) {
            return net_udp_send(g_net_dev, dst_ip,
                                g_sockets[i].local_port, dst_port, data, len);
        }
    }
    return -1;
}

int net_recvfrom(int fd, void *buf, size_t len,
                 uint8_t *src_ip, uint16_t *src_port)
{
    for (int i = 0; i < MAX_SOCKETS; i++) {
        if (g_sockets[i].fd == fd && g_sockets[i].in_use &&
            g_sockets[i].rx_len > 0) {
            size_t n = (size_t)g_sockets[i].rx_len;
            if (n > len) n = len;
            memcpy(buf, g_sockets[i].rx_buf, n);
            if (src_ip)  memcpy(src_ip, g_sockets[i].remote_ip, 4);
            if (src_port) *src_port = g_sockets[i].remote_port;
            g_sockets[i].rx_len = 0;
            return (int)n;
        }
    }
    return -1;
}

int net_close(int fd)
{
    for (int i = 0; i < MAX_SOCKETS; i++) {
        if (g_sockets[i].fd == fd) {
            g_sockets[i].in_use = 0;
            g_num_sockets--;
            return 0;
        }
    }
    return -1;
}

/* ============================================================
 * Global device list
 * ============================================================ */

net_device_t *net_alloc_device(void)
{
    net_device_t *dev = (net_device_t *)kmalloc(sizeof(net_device_t));
    if (dev) memset(dev, 0, sizeof(*dev));
    return dev;
}

int net_register_device(net_device_t *dev)
{
    if (!dev) return -1;
    dev->next = g_net_devices;
    g_net_devices = dev;
    if (!g_net_dev) g_net_dev = dev;
    return 0;
}

int net_poll_all(void)
{
    int count = 0;
    for (net_device_t *d = g_net_devices; d; d = d->next) {
        if (d->poll) count += d->poll(d);
    }
    return count;
}

/* ============================================================
 * Network subsystem init
 * ============================================================ */

int net_init(void)
{
    net_device_t *dev = net_alloc_device();
    if (!dev) return -1;

    memcpy(dev->name, "eth0", 5);
    dev->mtu = ETH_MTU;

    if (virtio_net_init(dev) != 0) {
        kfree(dev);
        return -1;
    }

    /* Set default IP to 0.0.0.0 (will get from DHCP) */
    dev->ip[0] = 10; dev->ip[1] = 0; dev->ip[2] = 2; dev->ip[3] = 15;
    dev->netmask[0] = 255; dev->netmask[1] = 255; dev->netmask[2] = 255; dev->netmask[3] = 0;
    dev->gateway[0] = 10; dev->gateway[1] = 0; dev->gateway[2] = 2; dev->gateway[3] = 2;

    dev->send = virtio_net_send;
    dev->poll = virtio_net_poll;
    dev->up   = 1;

    net_register_device(dev);
    g_net_dev = dev;
    g_net_initialized = 1;

    printk("[NET] eth0: %d.%d.%d.%d/%d.%d.%d.%d\n",
           dev->ip[0], dev->ip[1], dev->ip[2], dev->ip[3],
           dev->netmask[0], dev->netmask[1], dev->netmask[2], dev->netmask[3]);

    return 0;
}
