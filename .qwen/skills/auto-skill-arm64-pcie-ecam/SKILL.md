---
name: arm64-pcie-ecam
description: PCIe ECAM bus enumeration on ARM64 bare-metal — ECAM address computation, bus/device/function scanning, BAR parsing with 64-bit support, MSI-X capability discovery, and bridge recursion.
source: auto-skill
extracted_at: '2026-07-10T11:30:00.000Z'
---

# ARM64 PCIe ECAM Enumeration

## When to use
Enumerating PCIe devices on an ARM64 system where the PCIe root complex
provides ECAM (Enhanced Configuration Access Mechanism). Typical on
QEMU `-M virt` and real ARM64 servers (ThunderX, Graviton, etc.).

## ECAM address computation

ECAM maps each bus/device/function's 4KB config space into a contiguous
MMIO region:

```
ecam_addr = ecam_base + (bus << 20) + (device << 15) + (func << 12) + offset
```

The formula in C:

```c
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
```

**Always use memory barriers** around ECAM accesses — PCIe ordering rules
require them:

```c
uint32_t pci_config_read(uint8_t bus, uint8_t dev, uint8_t func, uint16_t offset)
{
    volatile uint32_t *addr = ecam_addr(bus, dev, func, offset);
    __asm__ volatile("dmb sy" ::: "memory");
    uint32_t val = *addr;
    __asm__ volatile("dmb sy" ::: "memory");
    return val;
}
```

## QEMU virt machine ECAM bases

| QEMU version / GIC | ECAM base |
|-------------------|-----------|
| Older virt (GICv2) | `0x3F000000` |
| Newer virt (GICv3) | `0x4010000000` (64-bit) |

Start with `0x3F000000` — if vendor ID returns `0xFFFF` for bus 0 device 0,
try `0x4010000000ULL`. The device tree (passed in x0) has the definitive
address in the `pcie@` node's `reg` property.

## Bus scanning

```c
void pci_scan_bus(uint8_t bus) {
    for (int dev = 0; dev < 32; dev++) {
        uint16_t vendor = pci_config_read16(bus, dev, 0, PCI_VENDOR_ID);
        if (vendor == 0xFFFF) continue;  // no device

        pci_scan_function(bus, dev, 0);

        // Check for multi-function device
        uint8_t header = pci_config_read8(bus, dev, 0, PCI_HEADER_TYPE);
        if (header & 0x80) {  // multi-function
            for (int func = 1; func < 8; func++)
                pci_scan_function(bus, dev, func);
        }
    }
}
```

## BAR parsing (handles 64-bit BARs correctly)

```c
static void pci_parse_bar(pci_device_t *dev, int bar_idx) {
    uint16_t bar_reg = PCI_BAR0 + bar_idx * 4;
    uint32_t bar_val = pci_config_read(bus, dev, func, bar_reg);
    if (bar_val == 0) return;

    int is_io = (bar_val & 0x1) ? 1 : 0;
    int is_64bit = 0;

    if (!is_io) {
        is_64bit = ((bar_val & 0x4) == 0x4);  // bit 2 set = 64-bit
        if (is_64bit) {
            uint32_t bar_high = pci_config_read(bus, dev, func, bar_reg + 4);
            dev->bars[bar_idx].base = ((uint64_t)bar_high << 32) |
                                       (bar_val & 0xFFFFFFF0);
        } else {
            dev->bars[bar_idx].base = bar_val & 0xFFFFFFF0;
        }
    }

    // Determine size: write all-ones, read back
    pci_config_write(bus, dev, func, bar_reg, 0xFFFFFFFF);
    uint32_t size_lo = pci_config_read(bus, dev, func, bar_reg);
    pci_config_write(bus, dev, func, bar_reg, bar_val);  // restore

    uint64_t size = 0;
    if (is_64bit) {
        uint64_t size64 = ((uint64_t)size_hi << 32) | (size_lo & ~0xF);
        size = ~size64 + 1;
    } else {
        size = (~(size_lo & ~0xF) + 1) & 0xFFFFFFFF;
    }
    dev->bars[bar_idx].size = size;
}
```

## PCI-to-PCI bridge recursion

When a device has `header_type & 0x7F == 0x01` (bridge), scan its
secondary bus:

```c
if ((header & 0x7F) == PCI_HEADER_TYPE_BRIDGE) {
    uint8_t sec_bus = pci_config_read8(bus, dev, func, PCI_SECONDARY_BUS);
    uint8_t sub_bus = pci_config_read8(bus, dev, func, PCI_SUBORDINATE_BUS);
    if (sec_bus > bus && sec_bus <= sub_bus) {
        for (int b = sec_bus; b <= sub_bus; b++)
            for (int d = 0; d < 32; d++)
                pci_scan_function(b, d, 0);
    }
}
```

## MSI-X capability discovery

MSI-X can be a **standard capability** (Capability Pointer chain at offset
0x34) or a **PCIe extended capability** (starting at offset 0x100 in config
space). Check both:

```c
// Standard capability chain
uint8_t cap_ptr = pci_config_read8(bus, dev, func, PCI_CAPABILITIES);
while (cap_ptr) {
    uint8_t id = pci_config_read8(bus, dev, func, cap_ptr);
    if (id == 0x11) return cap_ptr;  // MSI-X
    cap_ptr = pci_config_read8(bus, dev, func, cap_ptr + 1);
}

// PCIe extended capabilities (offset 0x100+, 4-byte headers)
uint16_t ext_offset = 0x100;
while (ext_offset) {
    uint32_t hdr = pci_config_read(bus, dev, func, ext_offset);
    uint16_t id = hdr & 0xFFFF;
    if (id == 0x11) return ext_offset;
    ext_offset = (hdr >> 20) & 0xFFC;
}
```

## Device enable for DMA

Before using any PCI device:

```c
void pci_enable_device(pci_device_t *dev) {
    uint16_t cmd = pci_config_read16(dev->bus, dev->device, dev->func, PCI_COMMAND);
    cmd |= 0x0004 | 0x0002;   // BUS_MASTER | MEMORY_SPACE
    cmd &= ~0x0400;            // clear INTX_DISABLE
    pci_config_write(dev->bus, dev->device, dev->func, PCI_COMMAND, cmd);
}
```

## Common pitfalls

| Pitfall | Symptom | Fix |
|---------|---------|-----|
| Forgetting DMB around ECAM reads | Stale/incorrect config data | Add `dmb sy` before and after |
| 64-bit BAR: only reading 32 bits | Upper 32 bits are garbage | Check bit 2, read second 32-bit register |
| Multi-function not scanned | Function 1-7 devices invisible | Check `header_type & 0x80` |
| Bridge secondary bus not scanned | Devices behind bridge invisible | Recurse into `PCI_SECONDARY_BUS` |
| MSI-X only checked in standard caps | NVMe MSI-X not found | Also scan PCIe extended caps at 0x100+ |
