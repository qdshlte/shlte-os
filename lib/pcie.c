/*
 * pcie.c - PCI Express Bus Driver (ECAM)
 *
 * Enumerates PCIe devices using the Enhanced Configuration
 * Access Mechanism (ECAM), parses BARs, and provides MSI-X
 * interrupt configuration.
 */

#include <shlte/types.h>
#include <shlte/printk.h>
#include <shlte/string.h>
#include <shlte/mm.h>
#include <shlte/pcie.h>

/* ============================================================
 * Global state
 * ============================================================ */

static uint64_t g_ecam_base = 0;
static int      g_bus_start = 0;
static int      g_bus_end   = 255;

pci_device_t *g_pci_devices = NULL;
int           g_pci_device_count = 0;

/* ============================================================
 * ECAM access
 * ============================================================ */

/**
 * ecam_addr - Compute the MMIO address for a config space access
 */
static inline volatile uint32_t *ecam_addr(uint8_t bus, uint8_t dev,
                                            uint8_t func, uint16_t offset)
{
    uint64_t addr = g_ecam_base;
    addr += ((uint64_t)bus    << 20);
    addr += ((uint64_t)dev    << 15);
    addr += ((uint64_t)func   << 12);
    addr += (offset & 0xFFF);
    return (volatile uint32_t *)addr;
}

uint32_t pci_config_read(uint8_t bus, uint8_t dev, uint8_t func, uint16_t offset)
{
    volatile uint32_t *addr = ecam_addr(bus, dev, func, offset);
    __asm__ volatile("dmb sy" ::: "memory");
    uint32_t val = *addr;
    __asm__ volatile("dmb sy" ::: "memory");
    return val;
}

void pci_config_write(uint8_t bus, uint8_t dev, uint8_t func,
                      uint16_t offset, uint32_t val)
{
    volatile uint32_t *addr = ecam_addr(bus, dev, func, offset);
    __asm__ volatile("dmb sy" ::: "memory");
    *addr = val;
    __asm__ volatile("dmb sy" ::: "memory");
}

uint16_t pci_config_read16(uint8_t bus, uint8_t dev, uint8_t func, uint16_t offset)
{
    uint32_t v = pci_config_read(bus, dev, func, offset & ~3);
    return (uint16_t)((v >> ((offset & 3) * 8)) & 0xFFFF);
}

uint8_t pci_config_read8(uint8_t bus, uint8_t dev, uint8_t func, uint16_t offset)
{
    uint32_t v = pci_config_read(bus, dev, func, offset & ~3);
    return (uint8_t)((v >> ((offset & 3) * 8)) & 0xFF);
}

void pci_write_bar(uint8_t bus, uint8_t dev, uint8_t func, int bar, uint32_t val)
{
    pci_config_write(bus, dev, func, (uint16_t)(PCI_BAR0 + bar * 4), val);
}

/* ============================================================
 * Device management
 * ============================================================ */

static pci_device_t *pci_alloc_device(void)
{
    pci_device_t *dev = (pci_device_t *)kmalloc(sizeof(pci_device_t));
    if (dev) memset(dev, 0, sizeof(pci_device_t));
    return dev;
}

/**
 * pci_parse_bar - Parse a Base Address Register
 *
 * Returns the decoded base address and size.
 */
static void pci_parse_bar(pci_device_t *dev, int bar_idx)
{
    uint8_t bus = dev->bus, device = dev->device, func = dev->function;
    uint16_t bar_reg = (uint16_t)(PCI_BAR0 + bar_idx * 4);

    uint32_t bar_val = pci_config_read(bus, device, func, bar_reg);

    /* Check if BAR is implemented */
    if (bar_val == 0) return;

    int is_io = (bar_val & PCI_BAR_IO) ? 1 : 0;
    int is_64bit = 0;

    if (is_io) {
        dev->bars[bar_idx].base    = bar_val & 0xFFFFFFFC;
        dev->bars[bar_idx].is_io   = 1;
        dev->bars[bar_idx].is_64bit = 0;
    } else {
        is_64bit = ((bar_val & PCI_BAR_64BIT) == PCI_BAR_64BIT);
        dev->bars[bar_idx].is_64bit = is_64bit;
        dev->bars[bar_idx].is_prefetch = (bar_val & PCI_BAR_PREFETCH) ? 1 : 0;

        if (is_64bit) {
            uint32_t bar_high = pci_config_read(bus, device, func,
                                                (uint16_t)(bar_reg + 4));
            dev->bars[bar_idx].base = ((uint64_t)bar_high << 32) |
                                       (bar_val & 0xFFFFFFF0);
        } else {
            dev->bars[bar_idx].base = bar_val & 0xFFFFFFF0;
        }
    }

    /* Determine BAR size */
    pci_config_write(bus, device, func, bar_reg, 0xFFFFFFFF);
    uint32_t size_lo = pci_config_read(bus, device, func, bar_reg);
    pci_config_write(bus, device, func, bar_reg, bar_val);

    if (is_io) {
        size_lo &= 0xFFFFFFFC;
        dev->bars[bar_idx].size = (~size_lo + 1) & 0xFFFFFFFF;
    } else if (is_64bit) {
        uint32_t size_hi;
        uint16_t bar_high_reg = (uint16_t)(bar_reg + 4);
        pci_config_write(bus, device, func, bar_high_reg, 0xFFFFFFFF);
        size_hi = pci_config_read(bus, device, func, bar_high_reg);
        pci_config_write(bus, device, func, bar_high_reg,
                         (uint32_t)(dev->bars[bar_idx].base >> 32));

        uint64_t size64 = (((uint64_t)size_hi << 32) | (size_lo & 0xFFFFFFF0));
        dev->bars[bar_idx].size = ~size64 + 1;
    } else {
        size_lo &= 0xFFFFFFF0;
        dev->bars[bar_idx].size = (~size_lo + 1) & 0xFFFFFFFF;
    }
}

/**
 * pci_enable_device - Enable bus mastering and memory space
 */
void pci_enable_device(pci_device_t *dev)
{
    uint16_t cmd = (uint16_t)pci_config_read16(dev->bus, dev->device,
                                                dev->function, PCI_COMMAND);
    cmd |= PCI_CMD_BUS_MASTER | PCI_CMD_MEMORY;
    cmd &= ~PCI_CMD_INTX_DISABLE;
    pci_config_write(dev->bus, dev->device, dev->function,
                     PCI_COMMAND, (uint32_t)cmd);
}

/* ============================================================
 * Device enumeration
 * ============================================================ */

/**
 * pci_scan_function - Scan a single PCI function
 */
static void pci_scan_function(uint8_t bus, uint8_t device, uint8_t func)
{
    uint16_t vendor_id = pci_config_read16(bus, device, func, PCI_VENDOR_ID);
    if (vendor_id == 0xFFFF) return;  /* No device */

    uint16_t device_id  = pci_config_read16(bus, device, func, PCI_DEVICE_ID);
    uint8_t  class_code = pci_config_read8(bus, device, func, PCI_CLASS_DEVICE);
    uint8_t  subclass   = pci_config_read8(bus, device, func, PCI_CLASS_SUB);
    uint8_t  prog_if    = pci_config_read8(bus, device, func, PCI_CLASS_PROG);
    uint8_t  header     = pci_config_read8(bus, device, func, PCI_HEADER_TYPE);
    uint8_t  irq_pin    = pci_config_read8(bus, device, func, PCI_INTERRUPT_PIN);
    uint8_t  irq_line   = pci_config_read8(bus, device, func, PCI_INTERRUPT_LINE);

    pci_device_t *dev = pci_alloc_device();
    if (!dev) return;

    dev->bus         = bus;
    dev->device      = device;
    dev->function    = func;
    dev->vendor_id   = vendor_id;
    dev->device_id   = device_id;
    dev->class_code  = class_code;
    dev->subclass    = subclass;
    dev->prog_if     = prog_if;
    dev->header_type = header;
    dev->irq_pin     = irq_pin;
    dev->irq_line    = irq_line;

    /* Parse BARs */
    int max_bars = ((header & PCI_HEADER_TYPE_MASK) == PCI_HEADER_TYPE_DEVICE) ? 6 : 2;
    for (int b = 0; b < max_bars; b++) {
        pci_parse_bar(dev, b);
    }

    /* Add to global list */
    dev->next = g_pci_devices;
    g_pci_devices = dev;
    g_pci_device_count++;

    /* Log the device */
    const char *class_str = "Unknown";
    if (class_code == PCI_CLASS_STORAGE) {
        if (subclass == PCI_SUBCLASS_NVME)       class_str = "NVMe";
        else if (subclass == PCI_SUBCLASS_SATA_AHCI) class_str = "AHCI";
        else if (subclass == PCI_SUBCLASS_IDE)   class_str = "IDE";
        else                                     class_str = "Storage";
    } else if (class_code == PCI_CLASS_NETWORK) {
        class_str = "Network";
    } else if (class_code == PCI_CLASS_DISPLAY) {
        class_str = "Display";
    } else if (class_code == PCI_CLASS_BRIDGE) {
        class_str = "Bridge";
    }

    printk("[PCI] %02x:%02x.%d %04x:%04x %s (rev %02x)\n",
           bus, device, func, vendor_id, device_id, class_str,
           pci_config_read8(bus, device, func, PCI_REVISION_ID));

    for (int b = 0; b < max_bars; b++) {
        if (dev->bars[b].size > 0) {
            printk("[PCI]   BAR%d: base=0x%lx size=0x%lx %s%s\n",
                   b, dev->bars[b].base, dev->bars[b].size,
                   dev->bars[b].is_io ? "IO " : "MEM",
                   dev->bars[b].is_64bit ? "64" : "32");
        }
    }

    /* If this is a PCI-to-PCI bridge, scan the secondary bus */
    if ((header & PCI_HEADER_TYPE_MASK) == PCI_HEADER_TYPE_BRIDGE) {
        uint8_t sec_bus = pci_config_read8(bus, device, func, PCI_SECONDARY_BUS);
        uint8_t sub_bus = pci_config_read8(bus, device, func, PCI_SUBORDINATE_BUS);
        if (sec_bus > bus && sec_bus <= sub_bus && sec_bus < 256) {
            printk("[PCI] Bridge: scanning secondary bus %d..%d\n", sec_bus, sub_bus);
            for (int b = sec_bus; b <= sub_bus && b < 256; b++) {
                for (int d = 0; d < 32; d++) {
                    pci_scan_function((uint8_t)b, (uint8_t)d, 0);
                }
            }
        }
    }
}

/**
 * pci_scan_bus - Scan a PCI bus for all devices
 */
static void pci_scan_bus(uint8_t bus)
{
    for (int dev = 0; dev < 32; dev++) {
        uint16_t vendor = pci_config_read16(bus, (uint8_t)dev, 0, PCI_VENDOR_ID);
        if (vendor == 0xFFFF) continue;

        pci_scan_function(bus, (uint8_t)dev, 0);

        /* Check for multi-function device */
        uint8_t header = pci_config_read8(bus, (uint8_t)dev, 0, PCI_HEADER_TYPE);
        if (header & PCI_HEADER_TYPE_MULTIFUNC) {
            for (int func = 1; func < 8; func++) {
                vendor = pci_config_read16(bus, (uint8_t)dev, (uint8_t)func,
                                           PCI_VENDOR_ID);
                if (vendor != 0xFFFF)
                    pci_scan_function(bus, (uint8_t)dev, (uint8_t)func);
            }
        }
    }
}

/* ============================================================
 * Device lookup
 * ============================================================ */

pci_device_t *pci_find_by_class(uint8_t class_code, uint8_t subclass)
{
    for (pci_device_t *dev = g_pci_devices; dev; dev = dev->next) {
        if (dev->class_code == class_code && dev->subclass == subclass)
            return dev;
    }
    return NULL;
}

pci_device_t *pci_find_by_vendor(uint16_t vendor_id, uint16_t device_id)
{
    for (pci_device_t *dev = g_pci_devices; dev; dev = dev->next) {
        if (dev->vendor_id == vendor_id && dev->device_id == device_id)
            return dev;
    }
    return NULL;
}

/* ============================================================
 * MSI-X support
 * ============================================================ */

/**
 * pci_find_capability - Find a PCI capability by ID
 */
static uint8_t pci_find_capability(pci_device_t *dev, uint8_t cap_id)
{
    uint8_t cap_ptr = pci_config_read8(dev->bus, dev->device, dev->function,
                                        PCI_CAPABILITIES);
    int count = 0;
    while (cap_ptr && count < 48) {
        uint8_t id = pci_config_read8(dev->bus, dev->device, dev->function,
                                       cap_ptr);
        if (id == cap_id) return cap_ptr;
        cap_ptr = pci_config_read8(dev->bus, dev->device, dev->function,
                                    (uint16_t)(cap_ptr + 1));
        count++;
    }
    return 0;
}

/**
 * pci_find_ext_capability - Find a PCIe extended capability by ID
 */
static uint16_t pci_find_ext_capability(pci_device_t *dev, uint16_t cap_id)
{
    uint16_t offset = 0x100;  /* Extended capabilities start at 0x100 */
    int count = 0;
    while (offset >= 0x100 && count < 64) {
        uint32_t hdr = pci_config_read(dev->bus, dev->device, dev->function,
                                        offset);
        uint16_t id = (uint16_t)(hdr & 0xFFFF);
        if (id == cap_id) return offset;
        offset = (uint16_t)((hdr >> 20) & 0xFFC);
        if (offset == 0) break;
        count++;
    }
    return 0;
}

/**
 * pci_msix_init - Initialize MSI-X for a device
 *
 * Locates the MSI-X capability, finds the table BAR, and maps it.
 */
int pci_msix_init(pci_device_t *dev)
{
    if (!dev) return -1;

    /* Try standard capability first */
    uint8_t cap = pci_find_capability(dev, PCI_CAP_ID_MSIX);

    uint32_t msg_ctrl;
    uint32_t table_offset;
    uint8_t  table_bir;
    uint16_t cap_base;

    if (cap) {
        cap_base = cap;
        msg_ctrl = pci_config_read(dev->bus, dev->device, dev->function,
                                    (uint16_t)(cap + 2));
        uint32_t tbl = pci_config_read(dev->bus, dev->device, dev->function,
                                        (uint16_t)(cap + 4));
        table_bir    = (uint8_t)(tbl & 0x7);
        table_offset = tbl & 0xFFFFFFF8;
    } else {
        /* Try extended capability */
        cap = (uint8_t)pci_find_ext_capability(dev, PCI_EXT_CAP_ID_MSIX);
        if (!cap) return -1;
        cap_base = cap;
        msg_ctrl = pci_config_read(dev->bus, dev->device, dev->function,
                                    (uint16_t)(cap + 2));
        uint32_t tbl = pci_config_read(dev->bus, dev->device, dev->function,
                                        (uint16_t)(cap + 4));
        table_bir    = (uint8_t)(tbl & 0x7);
        table_offset = tbl & 0xFFFFFFF8;
    }

    uint32_t table_size = (msg_ctrl & 0x7FF) + 1;

    if (table_bir >= PCI_MAX_BARS || dev->bars[table_bir].size == 0) {
        printk("[PCI] MSI-X: invalid BAR index %d\n", table_bir);
        return -1;
    }

    dev->msix_table_phys  = dev->bars[table_bir].base + table_offset;
    dev->msix_table_size  = table_size;
    dev->msix_bar_index   = table_bir;

    /* Set enable bit */
    msg_ctrl |= 0x8000;
    pci_config_write(dev->bus, dev->device, dev->function, cap_base, msg_ctrl);

    printk("[PCI] MSI-X: %u vectors at phys 0x%lx\n",
           table_size, dev->msix_table_phys);
    return 0;
}

int pci_msix_set_vector(pci_device_t *dev, int vector, uint64_t addr, uint32_t data)
{
    if (!dev || vector < 0 || (uint32_t)vector >= dev->msix_table_size)
        return -1;

    msix_entry_t *table = (msix_entry_t *)dev->msix_table_phys;
    /* Note: msix_table_phys is a physical address. If MMU is on with
     * identity mapping, this works directly. Otherwise needs ioremap. */

    table[vector].msg_addr_low  = (uint32_t)(addr & 0xFFFFFFFF);
    table[vector].msg_addr_high = (uint32_t)(addr >> 32);
    table[vector].msg_data      = data;
    table[vector].vec_ctrl      = 0;  /* Unmasked */

    return 0;
}

void pci_msix_unmask(pci_device_t *dev, int vector)
{
    if (!dev || vector < 0 || (uint32_t)vector >= dev->msix_table_size) return;
    msix_entry_t *table = (msix_entry_t *)dev->msix_table_phys;
    table[vector].vec_ctrl &= ~1U;
}

void pci_msix_mask(pci_device_t *dev, int vector)
{
    if (!dev || vector < 0 || (uint32_t)vector >= dev->msix_table_size) return;
    msix_entry_t *table = (msix_entry_t *)dev->msix_table_phys;
    table[vector].vec_ctrl |= 1U;
}

/* ============================================================
 * Subsystem initialization
 * ============================================================ */

/**
 * pci_init - Initialize PCI subsystem and enumerate all devices
 * @ecam_base: Physical base address of the ECAM space
 * @bus_start: First bus to scan
 * @bus_end:   Last bus to scan
 */
int pci_init(uint64_t ecam_base, int bus_start, int bus_end)
{
    g_ecam_base = ecam_base;
    g_bus_start = bus_start;
    g_bus_end   = bus_end;

    g_pci_devices = NULL;
    g_pci_device_count = 0;

    printk("[PCI] Initializing ECAM at 0x%lx, buses %d-%d\n",
           ecam_base, bus_start, bus_end);

    for (int bus = bus_start; bus <= bus_end && bus < 256; bus++) {
        pci_scan_bus((uint8_t)bus);
    }

    printk("[PCI] Enumeration complete: %d device(s) found\n", g_pci_device_count);
    return g_pci_device_count;
}
