/*
 * nvme.c - NVMe SSD Driver
 *
 * PCIe NVMe controller driver supporting admin commands (identify,
 * create I/O queues) and NVM commands (read, write, flush).
 *
 * Uses a single admin queue pair and one I/O queue pair with
 * polled completion (no interrupt-driven I/O for now).
 */

#include <shlte/types.h>
#include <shlte/printk.h>
#include <shlte/string.h>
#include <shlte/mm.h>
#include <shlte/pcie.h>
#include <shlte/nvme.h>

/* ============================================================
 * MMIO helpers
 * ============================================================ */

static inline uint32_t nvme_reg_read32(volatile void *regs, uint32_t offset)
{
    volatile uint32_t *addr = (volatile uint32_t *)((uint64_t)regs + offset);
    __asm__ volatile("dmb sy" ::: "memory");
    return *addr;
}

static inline uint64_t nvme_reg_read64(volatile void *regs, uint32_t offset)
{
    volatile uint64_t *addr = (volatile uint64_t *)((uint64_t)regs + offset);
    __asm__ volatile("dmb sy" ::: "memory");
    return *addr;
}

static inline void nvme_reg_write32(volatile void *regs, uint32_t offset, uint32_t val)
{
    volatile uint32_t *addr = (volatile uint32_t *)((uint64_t)regs + offset);
    __asm__ volatile("dmb sy" ::: "memory");
    *addr = val;
    __asm__ volatile("dmb sy" ::: "memory");
}

static inline void nvme_reg_write64(volatile void *regs, uint32_t offset, uint64_t val)
{
    volatile uint64_t *addr = (volatile uint64_t *)((uint64_t)regs + offset);
    __asm__ volatile("dmb sy" ::: "memory");
    *addr = val;
    __asm__ volatile("dmb sy" ::: "memory");
}

/* ============================================================
 * Queue operations
 * ============================================================ */

/**
 * nvme_submit_sync - Submit a command and poll for completion
 *
 * Returns 0 on success, -1 on error.
 */
static int nvme_submit_sync(nvme_queue_t *q, nvme_ctrl_t *ctrl,
                             nvme_sqe_t *cmd, nvme_cqe_t *cqe_out)
{
    /* Copy command to SQ */
    memcpy(&q->sq[q->sq_tail], cmd, sizeof(nvme_sqe_t));
    uint16_t cmd_id = q->sq_tail;  /* command_id = SQ entry index */
    q->sq[q->sq_tail].command_id = cmd_id;
    q->sq_tail = (q->sq_tail + 1) & NVME_QUEUE_MASK;

    /* Ring doorbell */
    __asm__ volatile("dmb sy" ::: "memory");
    nvme_reg_write32(ctrl->regs, q->sq_doorbell, q->sq_tail);

    /* Poll for completion */
    for (volatile int timeout = 0; timeout < 5000000; timeout++) {
        __asm__ volatile("dmb sy" ::: "memory");
        nvme_cqe_t *cqe = &q->cq[q->cq_head];
        uint8_t phase = (uint8_t)((cqe->status >> 15) & 1);

        if (phase == q->cq_phase) {
            /* Got a completion */
            if (cqe_out) memcpy(cqe_out, cqe, sizeof(nvme_cqe_t));

            q->cq_head = (q->cq_head + 1) & NVME_QUEUE_MASK;
            if (q->cq_head == 0) q->cq_phase ^= 1;

            /* Ring CQ doorbell */
            nvme_reg_write32(ctrl->regs, q->cq_doorbell, q->cq_head);

            /* Check status */
            uint16_t status = cqe->status & 0xFFFE;
            if (status != 0) {
                printk("[NVMe] Command failed: status=0x%x, sqid=%d, cid=%d\n",
                       status, q->qid, cmd_id);
                return -1;
            }
            return 0;
        }
    }

    printk("[NVMe] Command timeout!\n");
    return -1;
}

/* ============================================================
 * Admin commands
 * ============================================================ */

static int nvme_admin_identify_ctrl(nvme_ctrl_t *ctrl, nvme_identify_ctrl_t *data)
{
    nvme_sqe_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.opcode = NVME_ADMIN_IDENTIFY;
    cmd.nsid   = 0;                  /* Identify controller */
    cmd.prp1   = (uint64_t)data;
    cmd.cdw10  = 1;                  /* CNS=1: identify controller */

    return nvme_submit_sync(&ctrl->admin_q, ctrl, &cmd, NULL);
}

static int nvme_admin_identify_ns(nvme_ctrl_t *ctrl, uint32_t nsid,
                                   nvme_identify_ns_t *data)
{
    nvme_sqe_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.opcode = NVME_ADMIN_IDENTIFY;
    cmd.nsid   = nsid;
    cmd.prp1   = (uint64_t)data;
    cmd.cdw10  = 0;                  /* CNS=0: identify namespace */

    return nvme_submit_sync(&ctrl->admin_q, ctrl, &cmd, NULL);
}

static int nvme_admin_create_io_cq(nvme_ctrl_t *ctrl, uint16_t qid,
                                    nvme_queue_t *q)
{
    nvme_sqe_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.opcode = NVME_ADMIN_CREATE_IO_CQ;
    cmd.prp1   = (uint64_t)q->cq;
    cmd.cdw10  = ((uint32_t)(NVME_QUEUE_SIZE - 1) << 16) | qid;
    cmd.cdw11  = 1;  /* Physically contiguous, IRQ enabled */

    return nvme_submit_sync(&ctrl->admin_q, ctrl, &cmd, NULL);
}

static int nvme_admin_create_io_sq(nvme_ctrl_t *ctrl, uint16_t qid,
                                    nvme_queue_t *q, uint16_t cqid)
{
    nvme_sqe_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.opcode = NVME_ADMIN_CREATE_IO_SQ;
    cmd.prp1   = (uint64_t)q->sq;
    cmd.cdw10  = ((uint32_t)(NVME_QUEUE_SIZE - 1) << 16) | qid;
    cmd.cdw11  = ((uint32_t)cqid << 16) | 1;  /* CQID + physically contiguous */

    return nvme_submit_sync(&ctrl->admin_q, ctrl, &cmd, NULL);
}

static int nvme_set_num_queues(nvme_ctrl_t *ctrl, uint16_t num_queues)
{
    nvme_sqe_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.opcode = NVME_ADMIN_SET_FEATURES;
    cmd.cdw10  = 7;   /* Feature ID: number of queues */
    cmd.cdw11  = ((uint32_t)(num_queues - 1) << 16) | (num_queues - 1);

    nvme_cqe_t cqe;
    int ret = nvme_submit_sync(&ctrl->admin_q, ctrl, &cmd, &cqe);
    if (ret == 0) {
        uint16_t allocated = (uint16_t)(cqe.cmd_specific & 0xFFFF);
        if (allocated + 1 < num_queues) {
            printk("[NVMe] Requested %d queues, got %d\n", num_queues, allocated + 1);
            num_queues = allocated + 1;
        }
    }
    return ret;
}

/* ============================================================
 * Initialization
 * ============================================================ */

int nvme_init(pci_device_t *pci_dev, nvme_ctrl_t *ctrl)
{
    if (!pci_dev || !ctrl) return -1;

    memset(ctrl, 0, sizeof(*ctrl));
    ctrl->pci = pci_dev;

    /* Map BAR0 (controller registers) */
    if (pci_dev->bars[0].size == 0) {
        printk("[NVMe] BAR0 not found\n");
        return -1;
    }

    ctrl->regs = (volatile void *)pci_dev->bars[0].base;

    /* Enable PCI device */
    pci_enable_device(pci_dev);

    /* Reset controller if needed */
    uint32_t csts = nvme_reg_read32(ctrl->regs, 0x1C);
    if (csts & NVME_CSTS_RDY) {
        /* Disable controller */
        nvme_reg_write32(ctrl->regs, 0x14, 0);
        /* Wait for CSTS.RDY = 0 */
        for (volatile int t = 0; t < 5000000; t++) {
            if (!(nvme_reg_read32(ctrl->regs, 0x1C) & NVME_CSTS_RDY))
                break;
        }
    }

    /* Read controller capabilities */
    uint64_t cap = nvme_reg_read64(ctrl->regs, 0x00);
    uint32_t mqes = (uint32_t)((cap >> 16) & 0xFFFF);
    ctrl->max_transfer = mqes > 1024 ? 1024 : mqes;
    ctrl->doorbell_stride = 1 << (2 + (uint32_t)((cap >> 32) & 0xF));

    uint32_t version = nvme_reg_read32(ctrl->regs, 0x08);

    printk("[NVMe] Version %d.%d.%d, MQES=%d\n",
           (version >> 16) & 0xFFFF, (version >> 8) & 0xFF, version & 0xFF, mqes);

    /* Allocate admin queues (contiguous physical memory) */
    ctrl->admin_q.sq = (nvme_sqe_t *)kmalloc_simple(sizeof(nvme_sqe_t) * NVME_QUEUE_SIZE);
    ctrl->admin_q.cq = (nvme_cqe_t *)kmalloc_simple(sizeof(nvme_cqe_t) * NVME_QUEUE_SIZE);

    if (!ctrl->admin_q.sq || !ctrl->admin_q.cq) {
        printk("[NVMe] Cannot allocate admin queues\n");
        return -1;
    }

    memset(ctrl->admin_q.sq, 0, sizeof(nvme_sqe_t) * NVME_QUEUE_SIZE);
    memset(ctrl->admin_q.cq, 0, sizeof(nvme_cqe_t) * NVME_QUEUE_SIZE);

    ctrl->admin_q.qid = 0;
    ctrl->admin_q.sq_doorbell = 0x1000;
    ctrl->admin_q.cq_doorbell = 0x1000 + (uint32_t)(4 * ctrl->doorbell_stride);
    ctrl->admin_q.cq_phase = 1;

    /* Set admin queue attributes */
    uint32_t aqa = ((NVME_QUEUE_SIZE - 1) << 16) | (NVME_QUEUE_SIZE - 1);
    nvme_reg_write32(ctrl->regs, 0x24, aqa);

    /* Set admin queue base addresses */
    nvme_reg_write64(ctrl->regs, 0x28, (uint64_t)ctrl->admin_q.sq);
    nvme_reg_write64(ctrl->regs, 0x30, (uint64_t)ctrl->admin_q.cq);

    /* Enable controller */
    uint32_t cc = NVME_CC_ENABLE;
    cc |= (0 << 20);   /* I/O CQ entry size = 2^0 * 16 = 16 bytes */
    cc |= (0 << 16);   /* I/O SQ entry size = 2^0 * 64 = 64 bytes */
    nvme_reg_write32(ctrl->regs, 0x14, cc);

    /* Wait for CSTS.RDY = 1 */
    int ready = 0;
    for (volatile int t = 0; t < 5000000; t++) {
        csts = nvme_reg_read32(ctrl->regs, 0x1C);
        if (csts & NVME_CSTS_RDY) { ready = 1; break; }
    }
    if (!ready) {
        printk("[NVMe] Controller not ready after enable\n");
        return -1;
    }
    if (csts & NVME_CSTS_CFS) {
        printk("[NVMe] Controller fatal status!\n");
        return -1;
    }

    /* Identify controller */
    nvme_identify_ctrl_t *id_ctrl = (nvme_identify_ctrl_t *)
        kmalloc_simple(sizeof(nvme_identify_ctrl_t));
    if (!id_ctrl) return -1;

    if (nvme_admin_identify_ctrl(ctrl, id_ctrl) != 0) {
        printk("[NVMe] Identify controller failed\n");
        return -1;
    }

    id_ctrl->mn[39] = '\0';
    id_ctrl->fr[7]  = '\0';
    printk("[NVMe] Model: %s, Firmware: %s\n", id_ctrl->mn, id_ctrl->fr);

    /* Identify namespace 1 */
    nvme_identify_ns_t *id_ns = (nvme_identify_ns_t *)
        kmalloc_simple(sizeof(nvme_identify_ns_t));
    if (!id_ns) return -1;

    if (nvme_admin_identify_ns(ctrl, 1, id_ns) != 0) {
        printk("[NVMe] Identify namespace failed\n");
        return -1;
    }

    ctrl->ns_size   = id_ns->nsze;
    ctrl->num_namespaces = 1;

    /* Determine LBA size */
    uint8_t flbas = id_ns->flbas & 0x0F;
    ctrl->lba_shift = id_ns->lbaf[flbas].lbads;
    uint32_t block_size = 1U << ctrl->lba_shift;

    printk("[NVMe] Namespace: %lu blocks, %u bytes/block (%lu MB)\n",
           ctrl->ns_size, block_size,
           (ctrl->ns_size * block_size) / (1024 * 1024));

    /* Set up one I/O queue pair */
    nvme_set_num_queues(ctrl, 1);
    ctrl->io_queues = 1;
    ctrl->io_q = (nvme_queue_t *)kmalloc(sizeof(nvme_queue_t));
    if (!ctrl->io_q) return -1;
    memset(ctrl->io_q, 0, sizeof(nvme_queue_t));

    ctrl->io_q->sq = (nvme_sqe_t *)kmalloc_simple(sizeof(nvme_sqe_t) * NVME_QUEUE_SIZE);
    ctrl->io_q->cq = (nvme_cqe_t *)kmalloc_simple(sizeof(nvme_cqe_t) * NVME_QUEUE_SIZE);
    if (!ctrl->io_q->sq || !ctrl->io_q->cq) return -1;

    memset(ctrl->io_q->sq, 0, sizeof(nvme_sqe_t) * NVME_QUEUE_SIZE);
    memset(ctrl->io_q->cq, 0, sizeof(nvme_cqe_t) * NVME_QUEUE_SIZE);

    ctrl->io_q->qid = 1;
    ctrl->io_q->sq_doorbell = 0x1000 + (uint32_t)(2 * 1 * ctrl->doorbell_stride);
    ctrl->io_q->cq_doorbell = 0x1000 + (uint32_t)((2 * 1 + 1) * ctrl->doorbell_stride);
    ctrl->io_q->cq_phase = 1;

    if (nvme_admin_create_io_cq(ctrl, 1, ctrl->io_q) != 0) {
        printk("[NVMe] Create I/O CQ failed\n");
        return -1;
    }
    if (nvme_admin_create_io_sq(ctrl, 1, ctrl->io_q, 1) != 0) {
        printk("[NVMe] Create I/O SQ failed\n");
        return -1;
    }

    ctrl->initialized = 1;
    printk("[NVMe] Initialized successfully\n");
    return 0;
}

/* ============================================================
 * I/O operations
 * ============================================================ */

int nvme_read(nvme_ctrl_t *ctrl, uint64_t lba, uint32_t count, void *buf)
{
    if (!ctrl || !ctrl->initialized || !buf || count == 0) return -1;
    if (lba + count > ctrl->ns_size) return -1;
    if (count > ctrl->max_transfer) count = ctrl->max_transfer;

    nvme_sqe_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.opcode = NVME_CMD_READ;
    cmd.nsid   = 1;
    cmd.prp1   = (uint64_t)buf;
    cmd.cdw10  = (uint32_t)(lba & 0xFFFFFFFF);
    cmd.cdw11  = (uint32_t)(lba >> 32);
    cmd.cdw12  = count - 1;   /* 0-based */

    return nvme_submit_sync(ctrl->io_q, ctrl, &cmd, NULL);
}

int nvme_write(nvme_ctrl_t *ctrl, uint64_t lba, uint32_t count, const void *buf)
{
    if (!ctrl || !ctrl->initialized || !buf || count == 0) return -1;
    if (lba + count > ctrl->ns_size) return -1;
    if (count > ctrl->max_transfer) count = ctrl->max_transfer;

    nvme_sqe_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.opcode = NVME_CMD_WRITE;
    cmd.nsid   = 1;
    cmd.prp1   = (uint64_t)buf;
    cmd.cdw10  = (uint32_t)(lba & 0xFFFFFFFF);
    cmd.cdw11  = (uint32_t)(lba >> 32);
    cmd.cdw12  = count - 1;

    return nvme_submit_sync(ctrl->io_q, ctrl, &cmd, NULL);
}

uint64_t nvme_get_capacity(nvme_ctrl_t *ctrl)
{
    return ctrl ? ctrl->ns_size : 0;
}

uint32_t nvme_get_block_size(nvme_ctrl_t *ctrl)
{
    return ctrl ? (1U << ctrl->lba_shift) : 0;
}
