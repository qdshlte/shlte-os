/*
 * shlte/net.h - Network Subsystem
 *
 * Provides network device abstraction, ARP/IPv4/UDP protocols,
 * and BSD-socket-like API for user programs.
 */

#ifndef SHLTE_NET_H
#define SHLTE_NET_H

#include <shlte/types.h>

/* ============================================================
 * Ethernet
 * ============================================================ */

#define ETH_ALEN        6
#define ETH_HLEN        14
#define ETH_MTU         1500
#define ETH_MAX_FRAME   1518

#define ETH_P_IP        0x0800
#define ETH_P_ARP       0x0806

typedef struct __attribute__((packed)) {
    uint8_t  dst[ETH_ALEN];
    uint8_t  src[ETH_ALEN];
    uint16_t ethertype;
} eth_hdr_t;

/* ============================================================
 * ARP
 * ============================================================ */

#define ARP_REQUEST     1
#define ARP_REPLY       2
#define ARP_HTYPE_ETH   1
#define ARP_TABLE_SIZE  64

typedef struct __attribute__((packed)) {
    uint16_t htype;          /* Hardware type (1 = Ethernet) */
    uint16_t ptype;          /* Protocol type (0x0800 = IPv4) */
    uint8_t  hlen;           /* Hardware address length (6) */
    uint8_t  plen;           /* Protocol address length (4) */
    uint16_t oper;           /* Operation (1=request, 2=reply) */
    uint8_t  sha[ETH_ALEN];  /* Sender hardware address */
    uint8_t  spa[4];         /* Sender protocol address */
    uint8_t  tha[ETH_ALEN];  /* Target hardware address */
    uint8_t  tpa[4];         /* Target protocol address */
} arp_pkt_t;

/* ARP table entry */
typedef struct {
    uint8_t  ip[4];
    uint8_t  mac[ETH_ALEN];
    int      valid;
} arp_entry_t;

/* ============================================================
 * IPv4
 * ============================================================ */

#define IPPROTO_ICMP    1
#define IPPROTO_TCP     6
#define IPPROTO_UDP     17

typedef struct __attribute__((packed)) {
    uint8_t  ver_ihl;        /* Version (4) + IHL (5) = 0x45 */
    uint8_t  dscp_ecn;
    uint16_t total_len;
    uint16_t id;
    uint16_t flags_frag;
    uint8_t  ttl;
    uint8_t  protocol;
    uint16_t checksum;
    uint8_t  src[4];
    uint8_t  dst[4];
} ip_hdr_t;

/* ============================================================
 * UDP
 * ============================================================ */

typedef struct __attribute__((packed)) {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t length;
    uint16_t checksum;
} udp_hdr_t;

/* ============================================================
 * DHCP
 * ============================================================ */

#define DHCP_DISCOVER   1
#define DHCP_OFFER      2
#define DHCP_REQUEST    3
#define DHCP_ACK        5

#define DHCP_SERVER_PORT    67
#define DHCP_CLIENT_PORT    68

typedef struct __attribute__((packed)) {
    uint8_t  op;
    uint8_t  htype;
    uint8_t  hlen;
    uint8_t  hops;
    uint32_t xid;
    uint16_t secs;
    uint16_t flags;
    uint8_t  ciaddr[4];
    uint8_t  yiaddr[4];
    uint8_t  siaddr[4];
    uint8_t  giaddr[4];
    uint8_t  chaddr[16];
    uint8_t  sname[64];
    uint8_t  file[128];
    uint32_t magic_cookie;
    uint8_t  options[];
} dhcp_pkt_t;

#define DHCP_MAGIC_COOKIE   0x63825363
#define DHCP_OPT_MSG_TYPE   53
#define DHCP_OPT_REQ_IP     50
#define DHCP_OPT_SERVER_ID  54
#define DHCP_OPT_END        255

/* ============================================================
 * Network device interface
 * ============================================================ */

struct net_device;

typedef int (*net_send_fn)(struct net_device *dev, const void *data, size_t len);
typedef int (*net_poll_fn)(struct net_device *dev);

typedef struct net_device {
    char     name[16];
    uint8_t  mac[ETH_ALEN];
    uint8_t  ip[4];
    uint8_t  gateway[4];
    uint8_t  netmask[4];
    uint8_t  dns[4];
    int      mtu;
    int      up;

    void    *driver_data;
    net_send_fn send;
    net_poll_fn poll;

    /* Receive buffer */
    uint8_t  rx_buf[ETH_MAX_FRAME];
    int      rx_len;
    int      rx_ready;

    /* ARP cache */
    arp_entry_t arp_table[ARP_TABLE_SIZE];

    struct net_device *next;
} net_device_t;

/* ============================================================
 * Socket types
 * ============================================================ */

#define SOCK_DGRAM      2    /* UDP */
#define SOCK_STREAM     1    /* TCP */
#define AF_INET         2

#define MAX_SOCKETS     16

typedef struct {
    int      fd;
    int      type;           /* SOCK_DGRAM / SOCK_STREAM */
    uint16_t local_port;
    uint8_t  remote_ip[4];
    uint16_t remote_port;
    int      in_use;
    uint8_t  rx_buf[2048];
    int      rx_len;
    int      rx_head;
} socket_t;

/* ============================================================
 * Checksum
 * ============================================================ */

static inline uint16_t net_checksum(const void *data, size_t len)
{
    uint32_t sum = 0;
    const uint16_t *p = (const uint16_t *)data;

    while (len > 1) {
        sum += *p++;
        len -= 2;
    }
    if (len) sum += *(const uint8_t *)p;

    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)(~sum);
}

/* ============================================================
 * API
 * ============================================================ */

/* Device management */
net_device_t *net_alloc_device(void);
int net_register_device(net_device_t *dev);
net_device_t *net_get_device(const char *name);

/* Packet I/O */
int net_send_packet(net_device_t *dev, const void *data, size_t len);
int net_poll_all(void);

/* Protocol handlers */
void net_arp_handle(net_device_t *dev, const uint8_t *pkt, size_t len);
void net_ip_handle(net_device_t *dev, const uint8_t *pkt, size_t len);
void net_udp_handle(net_device_t *dev, const uint8_t *pkt, size_t len, uint8_t src_ip[4]);

/* ARP */
int  net_arp_resolve(net_device_t *dev, const uint8_t *ip, uint8_t *mac_out);
void net_arp_send_request(net_device_t *dev, const uint8_t *target_ip);

/* IP */
int net_ip_send(net_device_t *dev, const uint8_t *dst_ip, uint8_t proto,
                const void *data, size_t len);

/* UDP */
int net_udp_send(net_device_t *dev, const uint8_t *dst_ip,
                 uint16_t src_port, uint16_t dst_port,
                 const void *data, size_t len);

/* DHCP */
int net_dhcp_discover(net_device_t *dev);

/* Socket API */
int  net_socket(int type);
int  net_bind(int fd, uint16_t port);
int  net_sendto(int fd, const void *data, size_t len,
                const uint8_t *dst_ip, uint16_t dst_port);
int  net_recvfrom(int fd, void *buf, size_t len,
                  uint8_t *src_ip, uint16_t *src_port);
int  net_close(int fd);

/* Stack initialization */
int net_init(void);

#endif /* SHLTE_NET_H */
