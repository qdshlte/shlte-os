/*
 * shlte/nvme.h - NVMe SSD Driver
 *
 * PCIe NVMe 1.3+ controller driver for high-performance storage.
 * Uses admin queue for initialization and I/O queues for data transfer.
 */

#ifndef SHLTE_NVME_H
#define SHLTE_NVME_H

#include <shlte/types.h>
#include <shlte/pcie.h>

/* ============================================================
 * NVMe Controller Registers (in BAR0)
 * ============================================================ */

/* CAP - Controller Capabilities (offset 0x00) */
#define NVME_CAP_MQES_MASK   0xFFFF0000ULL   /* Max queue entries */
#define NVME_CAP_MQES_SHIFT  16
#define NVME_CAP_CQR_MASK    0x00010000ULL   /* Contiguous queues required */
#define NVME_CAP_DSTRD_MASK  0x0000F000ULL   /* Doorbell stride */
#define NVME_CAP_DSTRD_SHIFT 32-12  /* Adjusted for 64-bit */

/* VS - Version (offset 0x08) */

/* CC - Controller Configuration (offset 0x14) */
#define NVME_CC_ENABLE       0x00000001
#define NVME_CC_IOCQES_SHIFT 20
#define NVME_CC_IOSQES_SHIFT 16
#define NVME_CC_SHN_MASK     0x0000C000
#define NVME_CC_SHN_NORMAL   0x00004000
#define NVME_CC_AMS_RR       0x00000000

/* CSTS - Controller Status (offset 0x1C) */
#define NVME_CSTS_RDY        0x00000001
#define NVME_CSTS_CFS        0x00000002
#define NVME_CSTS_SHST_MASK  0x0000000C

/* AQA - Admin Queue Attributes (offset 0x24) */
#define NVME_AQA_ASQS_SHIFT  16
#define NVME_AQA_ACQS_SHIFT  0

/* ASQ - Admin SQ Base Address (offset 0x28) */
/* ACQ - Admin CQ Base Address (offset 0x30) */

/* SQ0TDBL - SQ0 Tail Doorbell (offset 0x1000 + 2*queue_id*doorbell_stride) */

/* ============================================================
 * Admin Commands
 * ============================================================ */

#define NVME_ADMIN_DELETE_IO_SQ    0x00
#define NVME_ADMIN_CREATE_IO_SQ    0x01
#define NVME_ADMIN_GET_LOG_PAGE    0x02
#define NVME_ADMIN_DELETE_IO_CQ    0x04
#define NVME_ADMIN_CREATE_IO_CQ    0x05
#define NVME_ADMIN_IDENTIFY        0x06
#define NVME_ADMIN_SET_FEATURES    0x09
#define NVME_ADMIN_GET_FEATURES    0x0A

/* ============================================================
 * NVM Commands (I/O)
 * ============================================================ */

#define NVME_CMD_FLUSH       0x00
#define NVME_CMD_WRITE       0x01
#define NVME_CMD_READ        0x02

/* ============================================================
 * Data structures
 * ============================================================ */

/* 64-byte Submission Queue Entry */
typedef struct __attribute__((packed)) {
    uint8_t  opcode;
    uint8_t  flags;
    uint16_t command_id;
    uint32_t nsid;              /* Namespace ID */
    uint64_t reserved1;
    uint64_t metadata_ptr;
    uint64_t prp1;              /* PRP entry 1 (or data pointer) */
    uint64_t prp2;              /* PRP entry 2 */
    uint32_t cdw10;             /* Starting LBA low */
    uint32_t cdw11;             /* Starting LBA high */
    uint32_t cdw12;             /* Number of logical blocks */
    uint32_t cdw13;
    uint32_t cdw14;
    uint32_t cdw15;
} nvme_sqe_t;

/* 16-byte Completion Queue Entry */
typedef struct __attribute__((packed)) {
    uint32_t cmd_specific;
    uint32_t reserved;
    uint16_t sq_head;
    uint16_t sq_id;
    uint16_t command_id;
    uint16_t status;            /* 0 = success, bit 15 = phase tag */
} nvme_cqe_t;

/* Identify Controller data (4096 bytes, we only need key fields) */
typedef struct __attribute__((packed)) {
    uint16_t vid;
    uint16_t ssvid;
    char     sn[20];
    char     mn[40];
    char     fr[8];
    uint8_t  rab;
    uint8_t  ieee[3];
    uint8_t  cmic;
    uint8_t  mdts;
    uint16_t cntlid;
    uint32_t ver;
    uint32_t rtd3r;
    uint32_t rtd3e;
    uint32_t oaes;
    uint32_t ctratt;
    uint8_t  reserved[156];
    uint16_t oacs;
    uint8_t  acl;
    uint8_t  aerl;
    uint8_t  frmw;
    uint8_t  lpa;
    uint8_t  elpe;
    uint8_t  npss;
    uint8_t  avscc;
    uint8_t  apsta;
    uint16_t wctemp;
    uint16_t cctemp;
    uint16_t mtfa;
    uint32_t hmpre;
    uint32_t hmmin;
    uint64_t tnvmcap[2];
    uint64_t unvmcap[2];
    uint32_t rpmbs;
    uint16_t edstt;
    uint8_t  dsto;
    uint8_t  fwug;
    uint16_t kas;
    uint16_t hctma;
    uint16_t mntmt;
    uint16_t mxtmt;
    uint32_t sanicap;
    uint32_t hmminds;
    uint16_t hmmaxd;
    uint8_t  reserved2[3384];
} nvme_identify_ctrl_t;

/* Identify Namespace data */
typedef struct __attribute__((packed)) {
    uint64_t nsze;       /* Namespace size (total blocks) */
    uint64_t ncap;       /* Namespace capacity */
    uint64_t nuse;       /* Namespace utilization */
    uint8_t  nsfeat;
    uint8_t  nlbaf;      /* Number of LBA formats */
    uint8_t  flbas;      /* Formatted LBA size */
    uint8_t  mc;
    uint8_t  dpc;
    uint8_t  dps;
    uint8_t  nmic;
    uint8_t  rescap;
    uint8_t  fpi;
    uint8_t  dlfeat;
    uint16_t nawun;
    uint16_t nawupf;
    uint8_t  reserved[94];
    struct __attribute__((packed)) {
        uint16_t ms;
        uint8_t  lbads;  /* LBA data size (2^lbads) */
        uint8_t  rp;
    } lbaf[16];
} nvme_identify_ns_t;

/* ============================================================
 * Queue structures
 * ============================================================ */

#define NVME_QUEUE_SIZE   64     /* Queue depth (must be power of 2) */
#define NVME_QUEUE_MASK   (NVME_QUEUE_SIZE - 1)

typedef struct {
    nvme_sqe_t *sq;             /* Submission queue (physically contiguous) */
    nvme_cqe_t *cq;             /* Completion queue (physically contiguous) */
    uint16_t sq_head;
    uint16_t sq_tail;
    uint16_t cq_head;
    uint8_t  cq_phase;
    uint16_t qid;
    uint32_t sq_doorbell;       /* Offset to SQ doorbell register */
    uint32_t cq_doorbell;       /* Offset to CQ doorbell register */
} nvme_queue_t;

/* ============================================================
 * NVMe controller state
 * ============================================================ */

typedef struct {
    pci_device_t *pci;
    volatile void *regs;        /* MMIO registers (BAR0) */
    uint32_t doorbell_stride;   /* Doorbell stride (bytes) */

    nvme_queue_t admin_q;

    int num_namespaces;
    uint64_t ns_size;           /* Namespace size in blocks */
    uint32_t lba_shift;         /* log2(block size) */
    uint32_t max_transfer;      /* Max blocks per transfer */

    int io_queues;
    nvme_queue_t *io_q;

    int initialized;
} nvme_ctrl_t;

/* ============================================================
 * API
 * ============================================================ */

int  nvme_init(pci_device_t *pci_dev, nvme_ctrl_t *ctrl);
int  nvme_read(nvme_ctrl_t *ctrl, uint64_t lba, uint32_t count, void *buf);
int  nvme_write(nvme_ctrl_t *ctrl, uint64_t lba, uint32_t count, const void *buf);
uint64_t nvme_get_capacity(nvme_ctrl_t *ctrl);
uint32_t nvme_get_block_size(nvme_ctrl_t *ctrl);

#endif /* SHLTE_NVME_H */
