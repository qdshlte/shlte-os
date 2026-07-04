# Shlte OS

<div align="center">

**An ARM64 microkernel operating system built from scratch**

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Architecture: ARM64](https://img.shields.io/badge/Architecture-ARM64-brightgreen.svg)]()
[![Status: Early Development](https://img.shields.io/badge/Status-Early%20Development-orange.svg)]()

</div>

---

## Overview

Shlte OS is a minimal ARM64 microkernel operating system written from the ground up. It features a GRUB bootloader, virtual memory with MMU/page tables, GIC interrupt handling, UART console I/O, basic process management, and a system call interface — all designed to run on QEMU's ARM64 virt machine.

## Features

| Component | Status | Description |
|-----------|--------|-------------|
| Bootloader | ✅ | Assembly entry (`boot.S`) → C kernel transition |
| MMU & Page Tables | ✅ | Virtual memory setup, BSS clearing, cache enable |
| UART Console | ✅ | Kernel printf via PL011 UART |
| Interrupt Controller | ✅ | GICv2/v3 distributor + CPU interface |
| Exception Handling | ✅ | Synchronous, IRQ, FIQ vectors at EL1/EL0 |
| Memory Allocator | ✅ | Simple bump allocator with page-level management |
| Process Management | ✅ | Basic process creation and context |
| System Calls | ✅ | User-to-kernel transition framework |
| Root Filesystem | ✅ | initramfs with init script and shell |

## Quick Start

### Prerequisites

```bash
# Ubuntu / Debian
sudo apt update
sudo apt install gcc-aarch64-linux-gnu qemu-system-arm make genisoimage

# Fedora / RHEL
sudo dnf install aarch64-linux-gnu-gcc qemu-system-arm make cdrkit

# Arch Linux
sudo pacman -S gcc-aarch64-linux-gnu qemu make cdrkit
```

### Build & Run

```bash
# Build kernel binary
make kernel

# Run on QEMU
make run

# Debug mode (wait for GDB connection)
make debug
```

Then connect with GDB in another terminal:

```bash
aarch64-linux-gnu-gdb build/kernel.elf
(gdb) target remote localhost:1234
(gdb) continue
```

Or use the convenience script:

```bash
./scripts/build.sh build
./scripts/build.sh run
./scripts/build.sh debug
```

## Architecture

### Boot Sequence

```
QEMU loads kernel.bin @ 0x80000
    │
    ▼
boot.S (ARM64 assembly entry)
    ├── Save device tree pointer
    ├── Set up exception vector table
    ├── EL2 → EL1 privilege drop
    ├── Clear BSS segment
    ├── Enable MMU & L1 data cache
    ▼
kernel_main() (C entry point)
    ├── Print kernel banner via UART
    ├── Initialize MMU / page tables
    ├── Initialize GIC interrupt controller
    ├── Initialize filesystem
    ├── Initialize user space (usr_init)
    └── Start init process → infinite WFI loop
```

### Memory Map

```
Address          | Region
-----------------+------------------------------------------
0x00000000       | Reserved
0x08000000       | GIC registers (virt machine)
0x09000000       | UART (virt machine)
0x00080000       | Kernel binary load address
0x10000000       | Kernel heap start (16 MB)
```

### Linker Script

The kernel is linked at physical address `0x80000` with sections laid out as:

```
.text  →  .rodata  →  .data  →  .bss  →  __kernel_end
```

## Project Structure

```
shlte-os/
├── Makefile              # Build system (kernel, run, debug, clean)
├── linker.lds            # Kernel linker script
├── boot/
│   ├── boot.S            # Assembly entry point & early init
│   └── grub/             # GRUB configuration (optional)
├── arch/
│   └── arm64/
│       ├── kernel.c      # C kernel entry (kernel_main)
│       ├── mm/           # MMU & page table management
│       ├── interrupt/    # GIC driver & exception vectors
│       └── fs/           # Filesystem interface
├── lib/
│   ├── printk.c          # UART console driver
│   ├── string.c          # memcpy, memset, strcmp, etc.
│   ├── mm.c              # Physical memory allocator
│   ├── process.c         # Process management
│   ├── syscall.c         # System call handler
│   ├── interrupt_dispatch.c  # IRQ routing
│   └── include/shlte/    # Kernel headers
├── rootfs/               # initramfs contents
│   ├── etc/init.sh       # Init script
│   └── ...
├── scripts/
│   └── build.sh          # Convenience build wrapper
└── README.md
```

## Development Roadmap

### Completed ✅
- [x] Kernel entry & boot sequence
- [x] UART printk console
- [x] MMU initialization & page tables
- [x] GICv2/v3 interrupt controller
- [x] Exception vector table (EL1/EL0)
- [x] Simple physical memory allocator
- [x] Basic process creation
- [x] System call framework
- [x] Root filesystem (initramfs)

### Planned 🚧
- [ ] ext2 / squashfs filesystem driver
- [ ] Full round-robin scheduler
- [ ] User-space memory management (mmap, brk)
- [ ] TCP/IP network stack
- [ ] SMP / multi-core support
- [ ] Power management (PSCI)
- [ ] Signal handling
- [ ] Inter-process communication (IPC)

## Build Targets

| Target | Description |
|--------|-------------|
| `make` / `make kernel` | Build kernel binary (`build/kernel.bin`) |
| `make iso` | Build bootable ISO (placeholder for ARM64) |
| `make run` | Build & launch on QEMU (nographic) |
| `make debug` | Build & launch with GDB server on `:1234` |
| `make clean` | Remove all build artifacts |
| `make help` | Show build help |

## QEMU Run Command

Under the hood, Shlte OS runs on QEMU with these parameters:

```bash
qemu-system-aarch64 \
    -M virt -cpu cortex-a53 \
    -m 512M \
    -bios none \
    -kernel build/kernel.bin \
    -serial stdio \
    -nographic
```

## License

[MIT License](LICENSE)

## AI Disclosure

This project was developed with the assistance of artificial intelligence tools. AI was used to aid in writing and debugging code, generating documentation, and reviewing implementation decisions. All code and design decisions were ultimately reviewed and approved by the human maintainer.

## Contributing

Issues and pull requests are welcome! Please feel free to submit bug reports, feature requests, or patches.

---

**Shlte OS** — An ARM64 microkernel, built from scratch.
