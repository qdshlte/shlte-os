/*
 * shlte/tcp.h - TCP Protocol Implementation
 *
 * Transmission Control Protocol (RFC 793) with connection
 * management, flow control, retransmission, and BSD socket
 * integration. Supports up to 32 concurrent connections.
 */

#ifndef SHLTE_TCP_H
#define SHLTE_TCP_H

#include <shlte/types.h>
#include <shlte/net.h>

/* ============================================================
 * TCP Header
 * ============================================================ */

#define TCP_FIN  0x01
#define TCP_SYN  0x02
#define TCP_RST  0x04
#define TCP_PSH  0x08
#define TCP_ACK  0x10
#define TCP_URG  0x20

typedef struct __attribute__((packed)) {
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq_num;
    uint32_t ack_num;
    uint8_t  data_offset;    /* High nibble: data offset in 32-bit words */
    uint8_t  flags;
    uint16_t window;
    uint16_t checksum;
    uint16_t urgent;
    /* Options follow if data_offset > 5 */
} tcp_hdr_t;

/* TCP option kinds */
#define TCP_OPT_END      0
#define TCP_OPT_NOP      1
#define TCP_OPT_MSS      2
#define TCP_OPT_WSCALE   3
#define TCP_OPT_SACK_OK  4
#define TCP_OPT_TIMESTAMP 8

/* ============================================================
 * TCP Pseudo-header (for checksum calculation)
 * ============================================================ */

typedef struct __attribute__((packed)) {
    uint8_t  src_ip[4];
    uint8_t  dst_ip[4];
    uint8_t  zero;
    uint8_t  protocol;       /* 6 for TCP */
    uint16_t tcp_length;
} tcp_pseudo_hdr_t;

/* ============================================================
 * TCP Connection States
 * ============================================================ */

typedef enum {
    TCP_CLOSED,
    TCP_LISTEN,
    TCP_SYN_SENT,
    TCP_SYN_RECEIVED,
    TCP_ESTABLISHED,
    TCP_FIN_WAIT_1,
    TCP_FIN_WAIT_2,
    TCP_CLOSE_WAIT,
    TCP_CLOSING,
    TCP_LAST_ACK,
    TCP_TIME_WAIT
} tcp_state_t;

/* ============================================================
 * TCP Control Block (TCB)
 * ============================================================ */

#define TCP_MAX_CONN    32
#define TCP_MSS         1460     /* Maximum Segment Size (Ethernet MTU - 40) */
#define TCP_WINDOW      65535    /* Receive window */
#define TCP_RTO_MS      1000     /* Retransmission timeout (ms) */
#define TCP_MAX_RETRIES 5
#define TCP_RXBUF_SIZE  65536
#define TCP_TXBUF_SIZE  65536

typedef struct tcp_conn {
    int      in_use;

    /* Connection identification */
    uint8_t  local_ip[4];
    uint16_t local_port;
    uint8_t  remote_ip[4];
    uint16_t remote_port;

    /* State */
    tcp_state_t state;

    /* Sequence numbers */
    uint32_t snd_una;        /* Oldest unacknowledged sequence number */
    uint32_t snd_nxt;        /* Next sequence number to send */
    uint32_t snd_wnd;        /* Send window (advertised by remote) */
    uint32_t rcv_nxt;        /* Next expected receive sequence number */
    uint32_t rcv_wnd;        /* Receive window (local) */
    uint32_t iss;            /* Initial send sequence number */

    /* Retransmission */
    uint32_t rto_ms;         /* Current RTO in ms */
    uint32_t srtt;           /* Smoothed RTT */
    int      retries;

    /* Buffers */
    uint8_t  rcv_buf[TCP_RXBUF_SIZE];
    uint32_t rcv_buf_start;  /* Sequence number of first byte in buffer */
    uint32_t rcv_buf_len;

    uint8_t  snd_buf[TCP_TXBUF_SIZE];
    uint32_t snd_buf_start;
    uint32_t snd_buf_len;

    /* Linked list */
    struct tcp_conn *next;
} tcp_conn_t;

/* ============================================================
 * API
 * ============================================================ */

/* Stack initialization */
int tcp_init(void);

/* Connection management */
tcp_conn_t *tcp_connect(net_device_t *dev, const uint8_t *dst_ip,
                         uint16_t dst_port, uint16_t src_port);
tcp_conn_t *tcp_listen(uint16_t port);
tcp_conn_t *tcp_accept(tcp_conn_t *listener);
int tcp_close(tcp_conn_t *conn);

/* Data transfer */
int tcp_send(tcp_conn_t *conn, const void *data, size_t len);
int tcp_recv(tcp_conn_t *conn, void *buf, size_t len);

/* Packet handler (called from IP layer) */
void tcp_input(net_device_t *dev, const uint8_t *pkt, size_t len,
               const uint8_t *src_ip, const uint8_t *dst_ip);

/* Periodic timer (called every 100ms from timer IRQ) */
void tcp_timer_tick(void);

/* Socket integration */
int tcp_socket_connect(int sock_fd, const uint8_t *ip, uint16_t port);
int tcp_socket_listen(int sock_fd);
int tcp_socket_accept(int sock_fd);

#endif /* SHLTE_TCP_H */
