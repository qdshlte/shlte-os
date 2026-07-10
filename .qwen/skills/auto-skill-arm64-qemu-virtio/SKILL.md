---
name: arm64-qemu-virtio
description: Detect and initialize virtio-mmio block devices on QEMU ARM64 'virt' machine — base address (0x0A000000 on QEMU 8.2), legacy v1 vs modern v2 slot probing, and common gotchas.
source: auto-skill
extracted_at: '2026-07-05T04:58:15.898Z'
---

# ARM64 QEMU virtio-mmio Block Device

## When to use
When your ARM64 bare-metal kernel needs to access a disk/block device on QEMU's `-M virt` machine via virtio, and the device isn't responding or crashes on first access.

## QEMU invocation

```bash
qemu-system-aarch64 -M virt -cpu cortex-a53 -m 512M \
  -kernel build/kernel.bin \
  -drive file=disk.img,if=none,id=hd0,format=raw \
  -device virtio-blk-device,drive=hd0 \
  -nographic
```

On newer QEMU (8.2+), this creates a **legacy (v1)** virtio-mmio transport, NOT modern (v2).

## Virtio-mmio base address: 0x0A000000 on QEMU 8.2

**Do NOT hardcode 0x0C000000** — that was the address on older QEMU versions. QEMU 8.2 places the first virtio-mmio slot at **0x0A000000**.

Each slot is 0x200 bytes. The `-M virt` machine creates up to 10 pre-allocated slots (even without any `-device` flag). Only slots with an attached device return a non-zero `DeviceID`.

## Slot probing (mandatory)

Always scan slots rather than assuming a fixed address:

```c
#define VIRTIO_MMIO_BASE  0x0A000000   /* QEMU 8.2+ */

for (uintptr_t base = VIRTIO_MMIO_BASE;
     base < VIRTIO_MMIO_BASE + 10 * 0x200;
     base += 0x200) {
    vuint32_t *mmio_slot = (vuint32_t *)base;
    uint32_t magic = mmio_slot[0];    /* offset 0x000 */
    uint32_t ver   = mmio_slot[1];    /* offset 0x004 */
    uint32_t dev   = mmio_slot[2];    /* offset 0x008 */

    if (magic != 0x74726976UL)      /* "virt" */
        continue;
    if (ver != 1 && ver != 2)       /* v1=legacy, v2=modern */
        continue;
    if (dev == 2) {                 /* VIRTIO_ID_BLOCK */
        mmio = mmio_slot;           /* use this slot */
        break;
    }
}
```

## Version: QEMU 8.2 returns v1 (legacy)

`-device virtio-blk-device` creates a **legacy (v1)** device on QEMU 8.2. Do NOT reject v1:

```c
// WRONG: rejects v1
if (version != 2) { ... }

// RIGHT: accepts both
if (version != 1 && version != 2) { ... }
```

Legacy v1 uses the same register layout as v2 for basic operations (Magic, Version, DeviceID, Status, QueuePFN). The main difference is:
- No `QueueReady` register (offset 0x044) — use QueuePFN-based legacy setup
- Feature negotiation uses `HostFeaturesSel` / `GuestFeaturesSel` the same way

## Debugging with QEMU trace

Use `-trace virtio*` to see exactly what the kernel reads:

```bash
qemu-system-aarch64 ... -trace virtio* 2>&1 | grep virtio_mmio_read
```

This shows each MMIO register read (offset in hex), confirming which slot is being accessed and what values are returned.

## Gotchas

| Gotcha | Symptom | Fix |
|--------|---------|-----|
| Wrong base address (0x0C000000) | Data abort on first virtio_read | Use 0x0A000000 for QEMU 8.2+ |
| DeviceID=0 on probed slot | "No block device found" | The device is at a different slot — scan all slots |
| Version=1 rejected | "Unsupported version: 1" | Accept both v1 and v2 |
| `mmio` declared `const` pointer | Compile error: assignment | Remove `const` qualifier: `static vuint32_t *mmio;` |
| Data abort inside vring ops | Vring at 0x40000000 overwritten by kernel BSS/page tables | Use a dedicated region, not a fixed address near the kernel |
