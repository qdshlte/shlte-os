/*
 * shlte/pcie.h - PCI Express Bus Driver
 *
 * Supports ECAM (Enhanced Configuration Access Mechanism) for
 * PCIe device enumeration, BAR parsing, and MSI-X configuration.
 * Designed for ARM64 systems with device-tree or ACPI discovery.
 */

#ifndef SHLTE_PCIE_H
#define SHLTE_PCIE_H

#include <shlte/types.h>

/* ============================================================
 * PCI Configuration Space (Type 0 / Type 1 header)
 * ============================================================ */

/* Standard header (first 64 bytes of config space) */
#define PCI_VENDOR_ID        0x00   /* 16-bit */
#define PCI_DEVICE_ID        0x02   /* 16-bit */
#define PCI_COMMAND          0x04   /* 16-bit */
#define PCI_STATUS           0x06   /* 16-bit */
#define PCI_REVISION_ID      0x08   /* 8-bit */
#define PCI_CLASS_PROG       0x09   /* 8-bit */
#define PCI_CLASS_SUB        0x0A   /* 8-bit */
#define PCI_CLASS_DEVICE     0x0B   /* 8-bit */
#define PCI_CACHE_LINE_SIZE  0x0C   /* 8-bit */
#define PCI_LATENCY_TIMER    0x0D   /* 8-bit */
#define PCI_HEADER_TYPE      0x0E   /* 8-bit */
#define PCI_BIST             0x0F   /* 8-bit */

/* Type 0 (Device) header */
#define PCI_BAR0             0x10   /* 32-bit */
#define PCI_BAR1             0x14
#define PCI_BAR2             0x18
#define PCI_BAR3             0x1C
#define PCI_BAR4             0x20
#define PCI_BAR5             0x24
#define PCI_CARDBUS_CIS      0x28
#define PCI_SUBSYSTEM_VENDOR 0x2C
#define PCI_SUBSYSTEM_ID     0x2E
#define PCI_EXPANSION_ROM    0x30
#define PCI_CAPABILITIES     0x34   /* 8-bit pointer */
#define PCI_INTERRUPT_LINE   0x3C   /* 8-bit */
#define PCI_INTERRUPT_PIN    0x3D   /* 8-bit */
#define PCI_MIN_GNT          0x3E
#define PCI_MAX_LAT          0x3F

/* Type 1 (Bridge) header */
#define PCI_PRIMARY_BUS      0x18
#define PCI_SECONDARY_BUS    0x19
#define PCI_SUBORDINATE_BUS  0x1A
#define PCI_SEC_LATENCY      0x1B
#define PCI_IO_BASE          0x1C
#define PCI_IO_LIMIT         0x1D
#define PCI_MEM_BASE         0x20
#define PCI_MEM_LIMIT        0x22
#define PCI_PREF_MEM_BASE    0x24
#define PCI_PREF_MEM_LIMIT   0x26
#define PCI_PREF_BASE_UPPER  0x28
#define PCI_PREF_LIMIT_UPPER 0x2C
#define PCI_IO_BASE_UPPER    0x30
#define PCI_IO_LIMIT_UPPER   0x32

/* Extended capabilities (PCIe) */
#define PCI_EXT_CAP_ID_MSIX  0x11
#define PCI_EXT_CAP_ID_VNDR  0x0B
#define PCI_EXT_CAP_ID_AER   0x01

/* ============================================================
 * PCI Command Register bits
 * ============================================================ */

#define PCI_CMD_IO            0x0001
#define PCI_CMD_MEMORY        0x0002
#define PCI_CMD_BUS_MASTER    0x0004
#define PCI_CMD_INTX_DISABLE  0x0400

/* ============================================================
 * PCI Header Type bits
 * ============================================================ */

#define PCI_HEADER_TYPE_MASK      0x7F
#define PCI_HEADER_TYPE_DEVICE    0x00
#define PCI_HEADER_TYPE_BRIDGE    0x01
#define PCI_HEADER_TYPE_CARDBUS   0x02
#define PCI_HEADER_TYPE_MULTIFUNC 0x80

/* ============================================================
 * PCI Class codes
 * ============================================================ */

#define PCI_CLASS_STORAGE        0x01
#define PCI_CLASS_NETWORK        0x02
#define PCI_CLASS_DISPLAY        0x03
#define PCI_CLASS_BRIDGE         0x06
#define PCI_CLASS_SERIAL         0x0C

/* Storage subclasses */
#define PCI_SUBCLASS_IDE         0x01
#define PCI_SUBCLASS_SATA_AHCI   0x06
#define PCI_SUBCLASS_NVME        0x08

/* Network subclasses */
#define PCI_SUBCLASS_ETHERNET    0x00

/* Bridge subclasses */
#define PCI_SUBCLASS_HOST        0x00
#define PCI_SUBCLASS_PCI_TO_PCI  0x04

/* ============================================================
 * MSI-X capability structure
 * ============================================================ */

typedef struct __attribute__((packed)) {
    uint16_t msg_ctrl;
    uint16_t next;
} msix_cap_t;

/* MSI-X Table BAR Indicator Register (offset 2 in capability) */
#define MSIX_TABLE_BIR   0x03  /* bits [2:0] */
#define MSIX_TABLE_OFF   0x04  /* bits [31:3] (offset in bytes) */

/* MSI-X PBA BAR Indicator Register (offset 6 in capability) */
#define MSIX_PBA_BIR     0x06
#define MSIX_PBA_OFF     0x08

/* MSI-X table entry */
typedef struct __attribute__((packed)) {
    uint32_t msg_addr_low;
    uint32_t msg_addr_high;
    uint32_t msg_data;
    uint32_t vec_ctrl;          /* bit 0 = masked */
} msix_entry_t;

/* ============================================================
 * PCI capability IDs
 * ============================================================ */

#define PCI_CAP_ID_MSI     0x05
#define PCI_CAP_ID_MSIX    0x11
#define PCI_CAP_ID_PM      0x01
#define PCI_CAP_ID_PCIE    0x10

/* ============================================================
 * BAR flags
 * ============================================================ */

#define PCI_BAR_IO          0x01
#define PCI_BAR_MEM         0x00
#define PCI_BAR_64BIT       0x04
#define PCI_BAR_PREFETCH    0x08
#define PCI_BAR_MEM_MASK    0xFFFFFFF0

/* ============================================================
 * Device representation
 * ============================================================ */

#define PCI_MAX_BUS      256
#define PCI_MAX_DEV       32
#define PCI_MAX_FUNC       8
#define PCI_MAX_BARS       6

typedef struct {
    uint8_t  bus;
    uint8_t  device;
    uint8_t  function;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t  class_code;
    uint8_t  subclass;
    uint8_t  prog_if;
    uint8_t  revision;
    uint8_t  header_type;
    uint8_t  irq_pin;
    uint8_t  irq_line;

    /* BARs */
    struct {
        uint64_t base;          /* Physical address */
        uint64_t size;
        uint8_t  is_io;
        uint8_t  is_64bit;
        uint8_t  is_prefetch;
    } bars[PCI_MAX_BARS];

    /* MSI-X */
    uint64_t msix_table_phys;   /* Physical address of MSI-X table */
    uint32_t msix_table_size;
    uint32_t msix_bar_index;

    /* Linked list */
    struct pci_device *next;
} pci_device_t;

/* Global device list */
extern pci_device_t *g_pci_devices;
extern int g_pci_device_count;

/* ============================================================
 * API
 * ============================================================ */

/* Initialize PCI subsystem (scan bus) */
int pci_init(uint64_t ecam_base, int bus_start, int bus_end);

/* Read/Write config space */
uint32_t pci_config_read(uint8_t bus, uint8_t dev, uint8_t func, uint16_t offset);
void     pci_config_write(uint8_t bus, uint8_t dev, uint8_t func, uint16_t offset, uint32_t val);
uint16_t pci_config_read16(uint8_t bus, uint8_t dev, uint8_t func, uint16_t offset);
uint8_t  pci_config_read8(uint8_t bus, uint8_t dev, uint8_t func, uint16_t offset);

/* Enable device (bus mastering, memory space) */
void pci_enable_device(pci_device_t *dev);

/* Find device by class */
pci_device_t *pci_find_by_class(uint8_t class_code, uint8_t subclass);
pci_device_t *pci_find_by_vendor(uint16_t vendor_id, uint16_t device_id);

/* MSI-X */
int  pci_msix_init(pci_device_t *dev);
int  pci_msix_set_vector(pci_device_t *dev, int vector, uint64_t addr, uint32_t data);
void pci_msix_unmask(pci_device_t *dev, int vector);
void pci_msix_mask(pci_device_t *dev, int vector);

/* Write BAR (for programming bridge windows) */
void pci_write_bar(uint8_t bus, uint8_t dev, uint8_t func, int bar, uint32_t val);

#endif /* SHLTE_PCIE_H */
