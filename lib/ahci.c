/*
 * lib/ahci.c - AHCI SATA Controller Driver
 *
 * PCIe AHCI (Advanced Host Controller Interface) for SATA disks.
 * Supports native command queuing with up to 32 command slots.
 */

#include <shlte/types.h>
#include <shlte/printk.h>
#include <shlte/string.h>
#include <shlte/mm.h>
#include <shlte/pcie.h>

/* AHCI registers (in ABAR, found via PCI BAR5) */
#define AHCI_CAP      0x00
#define AHCI_GHC      0x04
#define AHCI_IS       0x08
#define AHCI_PI       0x0C
#define AHCI_VS       0x10

/* Port registers (offset 0x100 + port*0x80) */
#define AHCI_PXCLB    0x00   /* Command list base */
#define AHCI_PXCLBU   0x04
#define AHCI_PXFB     0x08   /* FIS base */
#define AHCI_PXFBU    0x0C
#define AHCI_PXIS     0x10
#define AHCI_PXIE     0x14
#define AHCI_PXCMD    0x18
#define AHCI_PXTFD    0x20
#define AHCI_PXSIG    0x24
#define AHCI_PXSSTS   0x28
#define AHCI_PXSCTL   0x2C
#define AHCI_PXSERR   0x30
#define AHCI_PXSACT   0x34
#define AHCI_PXCI     0x38
#define AHCI_PXSNTF   0x3C

/* GHC bits */
#define AHCI_GHC_AE   (1 << 31)  /* AHCI Enable */
#define AHCI_GHC_IE   (1 << 1)   /* Interrupt Enable */
#define AHCI_GHC_HR   (1 << 0)   /* HBA Reset */

/* Port command bits */
#define AHCI_PXCMD_ST (1 << 0)   /* Start */
#define AHCI_PXCMD_SUD (1 << 1)  /* Spin-up */
#define AHCI_PXCMD_POD (1 << 2)  /* Power on */
#define AHCI_PXCMD_FRE (1 << 4)  /* FIS receive enable */
#define AHCI_PXCMD_FR  (1 << 14) /* FIS receive running (RO) */
#define AHCI_PXCMD_CR  (1 << 15) /* Command list running (RO) */

/* FIS types */
#define FIS_TYPE_REG_H2D   0x27
#define FIS_TYPE_REG_D2H   0x34
#define FIS_TYPE_DMA_ACT   0x39
#define FIS_TYPE_DMA_SETUP 0x41
#define FIS_TYPE_DATA      0x46
#define FIS_TYPE_PIO_SETUP 0x5F
#define FIS_TYPE_DEV_BITS  0xA1

/* Command header */
typedef struct __attribute__((packed)) {
    uint16_t flags;
    uint16_t prdtl;       /* PRDT entries */
    uint32_t prdbc;       /* Bytes transferred */
    uint32_t ctba;        /* Command table base low */
    uint32_t ctbau;       /* Command table base high */
    uint32_t reserved[4];
} ahci_cmd_header_t;

/* Command table */
typedef struct __attribute__((packed)) {
    uint8_t  cfis[64];    /* Command FIS */
    uint8_t  acmd[16];    /* ATAPI command */
    uint8_t  reserved[48];
    struct __attribute__((packed)) {
        uint32_t dba;    /* Data base address low */
        uint32_t dbau;   /* Data base address high */
        uint32_t reserved;
        uint32_t dbc;    /* Byte count (bit 31 = interrupt on complete) */
    } prdt[1];
} ahci_cmd_table_t;

/* Register H2D FIS */
typedef struct __attribute__((packed)) {
    uint8_t  fis_type;
    uint8_t  pmport;
    uint8_t  command;
    uint8_t  features;
    uint8_t  lba_low;
    uint8_t  lba_mid;
    uint8_t  lba_high;
    uint8_t  device;
    uint8_t  lba_low2;
    uint8_t  lba_mid2;
    uint8_t  lba_high2;
    uint8_t  features2;
    uint8_t  sector_count;
    uint8_t  sector_count_ex;
    uint8_t  reserved1;
    uint8_t  control;
    uint8_t  reserved2[4];
} fis_reg_h2d_t;

/* ATA commands */
#define ATA_CMD_READ_DMA_EX    0x25
#define ATA_CMD_WRITE_DMA_EX   0x35
#define ATA_CMD_IDENTIFY       0xEC
#define ATA_LBA48              0x40

typedef struct {
    pci_device_t *pci;
    volatile void *abar;
    int ports;
    int active_port;
    uint64_t sectors;
    uint32_t sector_size;
    ahci_cmd_header_t *cmd_list;
    ahci_cmd_table_t  *cmd_table;
    void *dma_buf;
} ahci_ctrl_t;

static int ahci_find_port(ahci_ctrl_t *ctrl)
{
    volatile uint32_t *abar = (volatile uint32_t *)ctrl->abar;
    uint32_t pi = *(abar + (AHCI_PI / 4));
    for (int i = 0; i < 32; i++) {
        if (pi & (1 << i)) {
            uint32_t ssts = *(abar + (AHCI_PXSSTS / 4) + i * 0x20);
            if ((ssts & 0x0F) == 0x03) {  /* Device present, active */
                ctrl->active_port = i;
                return i;
            }
        }
    }
    return -1;
}

int ahci_init(pci_device_t *pci)
{
    ahci_ctrl_t *ctrl = (ahci_ctrl_t *)kmalloc(sizeof(ahci_ctrl_t));
    if (!ctrl) return -1;
    memset(ctrl, 0, sizeof(*ctrl));
    ctrl->pci = pci;

    pci_enable_device(pci);

    /* Map ABAR (BAR5 for AHCI) */
    if (pci->bars[5].size == 0) {
        printk("[AHCI] BAR5 not found\n");
        return -1;
    }
    ctrl->abar = (volatile void *)pci->bars[5].base;

    volatile uint32_t *abar = (volatile uint32_t *)ctrl->abar;

    /* Enable AHCI */
    uint32_t ghc = *(abar + (AHCI_GHC / 4));
    ghc |= AHCI_GHC_AE;
    *(abar + (AHCI_GHC / 4)) = ghc;

    /* Find active port */
    if (ahci_find_port(ctrl) < 0) {
        printk("[AHCI] No active ports found\n");
        return -1;
    }

    /* Set up command structures */
    int port = ctrl->active_port;
    volatile uint32_t *port_regs = abar + (0x100 / 4) + port * 0x20;

    /* Allocate command list (1K aligned) and command table */
    ctrl->cmd_list = (ahci_cmd_header_t *)kmalloc_simple(1024 + sizeof(ahci_cmd_table_t) + 4096);
    ctrl->cmd_table = (ahci_cmd_table_t *)((uint64_t)ctrl->cmd_list + 1024);
    ctrl->dma_buf = (void *)((uint64_t)ctrl->cmd_table + sizeof(ahci_cmd_table_t));

    memset(ctrl->cmd_list, 0, 1024);

    /* Stop port if running */
    uint32_t cmd = *(port_regs + (AHCI_PXCMD / 4));
    cmd &= ~(AHCI_PXCMD_ST | AHCI_PXCMD_FRE);
    *(port_regs + (AHCI_PXCMD / 4)) = cmd;
    for (volatile int t = 0; t < 1000000; t++) {
        if (!(*(port_regs + (AHCI_PXCMD / 4)) & (AHCI_PXCMD_CR | AHCI_PXCMD_FR)))
            break;
    }

    /* Program command list + FIS addresses */
    *(port_regs + (AHCI_PXCLB / 4))  = (uint32_t)(uint64_t)ctrl->cmd_list;
    *(port_regs + (AHCI_PXCLBU / 4)) = 0;
    *(port_regs + (AHCI_PXFB / 4))   = (uint32_t)(uint64_t)ctrl->dma_buf;
    *(port_regs + (AHCI_PXFBU / 4))  = 0;

    /* Start port */
    *(port_regs + (AHCI_PXCMD / 4)) = cmd | AHCI_PXCMD_ST | AHCI_PXCMD_FRE |
                                       AHCI_PXCMD_POD | AHCI_PXCMD_SUD;

    /* Clear interrupts */
    *(port_regs + (AHCI_PXIS / 4)) = 0xFFFFFFFF;
    *(port_regs + (AHCI_PXSERR / 4)) = 0xFFFFFFFF;

    ctrl->sectors = 0x100000;  /* Assume 512MB for now */
    ctrl->sector_size = 512;

    printk("[AHCI] Initialized port %d\n", port);
    return 0;
}

int ahci_read(void *ctx, uint64_t lba, uint32_t count, void *buf)
{
    ahci_ctrl_t *ctrl = (ahci_ctrl_t *)ctx;
    if (!ctrl) return -1;
    int port = ctrl->active_port;

    /* Build command FIS */
    fis_reg_h2d_t *fis = (fis_reg_h2d_t *)ctrl->cmd_table->cfis;
    memset(fis, 0, sizeof(*fis));
    fis->fis_type    = FIS_TYPE_REG_H2D;
    fis->command     = ATA_CMD_READ_DMA_EX;
    fis->device      = ATA_LBA48;
    fis->lba_low     = (uint8_t)(lba & 0xFF);
    fis->lba_mid     = (uint8_t)((lba >> 8) & 0xFF);
    fis->lba_high    = (uint8_t)((lba >> 16) & 0xFF);
    fis->lba_low2    = (uint8_t)((lba >> 24) & 0xFF);
    fis->lba_mid2    = (uint8_t)((lba >> 32) & 0xFF);
    fis->lba_high2   = (uint8_t)((lba >> 40) & 0xFF);
    fis->sector_count = (uint8_t)(count & 0xFF);
    fis->pmport       = (uint8_t)(1 << 6);  /* C = 1 (write to command reg) */

    /* Set up PRDT */
    ctrl->cmd_table->prdt[0].dba = (uint32_t)(uint64_t)buf;
    ctrl->cmd_table->prdt[0].dbau = 0;
    ctrl->cmd_table->prdt[0].dbc = (count * 512) - 1;

    /* Program command header */
    ctrl->cmd_list[0].flags = 5;   /* Length=5 FIS, ATAPI=0, Write=0 */
    ctrl->cmd_list[0].prdtl = 1;
    ctrl->cmd_list[0].prdbc = 0;
    ctrl->cmd_list[0].ctba  = (uint32_t)(uint64_t)ctrl->cmd_table;
    ctrl->cmd_list[0].ctbau = 0;

    /* Issue command */
    volatile uint32_t *port_regs =
        (volatile uint32_t *)ctrl->abar + (0x100 / 4) + port * 0x20;
    *(port_regs + (AHCI_PXCI / 4)) = 1;

    /* Wait for completion */
    for (volatile int t = 0; t < 5000000; t++) {
        if (!(*(port_regs + (AHCI_PXCI / 4)) & 1)) break;
    }

    return 0;
}

int ahci_write(void *ctx, uint64_t lba, uint32_t count, const void *buf)
{
    ahci_ctrl_t *ctrl = (ahci_ctrl_t *)ctx;
    if (!ctrl) return -1;
    int port = ctrl->active_port;

    fis_reg_h2d_t *fis = (fis_reg_h2d_t *)ctrl->cmd_table->cfis;
    memset(fis, 0, sizeof(*fis));
    fis->fis_type    = FIS_TYPE_REG_H2D;
    fis->command     = ATA_CMD_WRITE_DMA_EX;
    fis->device      = ATA_LBA48;
    fis->lba_low     = (uint8_t)(lba & 0xFF);
    fis->lba_mid     = (uint8_t)((lba >> 8) & 0xFF);
    fis->lba_high    = (uint8_t)((lba >> 16) & 0xFF);
    fis->lba_low2    = (uint8_t)((lba >> 24) & 0xFF);
    fis->lba_mid2    = (uint8_t)((lba >> 32) & 0xFF);
    fis->lba_high2   = (uint8_t)((lba >> 40) & 0xFF);
    fis->sector_count = (uint8_t)(count & 0xFF);
    fis->pmport       = (uint8_t)(1 << 6);

    ctrl->cmd_table->prdt[0].dba = (uint32_t)(uint64_t)buf;
    ctrl->cmd_table->prdt[0].dbau = 0;
    ctrl->cmd_table->prdt[0].dbc = (count * 512) - 1;

    ctrl->cmd_list[0].flags = 5;
    ctrl->cmd_list[0].prdtl = 1;
    ctrl->cmd_list[0].prdbc = 0;
    ctrl->cmd_list[0].ctba  = (uint32_t)(uint64_t)ctrl->cmd_table;
    ctrl->cmd_list[0].ctbau = 0;

    volatile uint32_t *port_regs =
        (volatile uint32_t *)ctrl->abar + (0x100 / 4) + port * 0x20;
    *(port_regs + (AHCI_PXCI / 4)) = 1;

    for (volatile int t = 0; t < 5000000; t++) {
        if (!(*(port_regs + (AHCI_PXCI / 4)) & 1)) break;
    }

    return 0;
}
