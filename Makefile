# ============================================================
# Shlte OS - ARM64 Microkernel
# A minimal Linux-like operating system
# ============================================================

# Cross-compilation toolchain
CROSS_COMPILE ?= aarch64-linux-gnu-
CC  := $(CROSS_COMPILE)gcc
AS  := $(CROSS_COMPILE)as
LD  := $(CROSS_COMPILE)ld
OBJCOPY := $(CROSS_COMPILE)objcopy
OBJDUMP := $(CROSS_COMPILE)objdump
AR  := $(CROSS_COMPILE)ar

# Target architecture
ARCH := arm64
TARGET := shlteos

# Build directories
BUILD_DIR  := build
ISO_DIR    := $(BUILD_DIR)/iso
KERNEL_BIN := $(BUILD_DIR)/kernel.bin
KERNEL_ELF := $(BUILD_DIR)/kernel.elf
ROOTFS_C   := $(BUILD_DIR)/rootfs_data.c
ROOTFS_OBJ := $(BUILD_DIR)/rootfs_data.o
INITRAMFS  := $(BUILD_DIR)/initramfs.cpio
ISO_IMAGE  := shlte-os.iso

# Compiler/Linker flags
CFLAGS := -std=c11 -ffreestanding -fno-builtin -nostdlib -nostartfiles \
          -march=armv8-a -mtune=cortex-a53 \
          -Wall -Wextra \
          -O2 -g \
          -DKERNEL_MODE \
          -fno-stack-protector -fno-pic \
          -I$(PWD)/arch/$(ARCH)/include \
          -I$(PWD)/lib/include

ASFLAGS := -march=armv8-a \
           -I$(PWD)/boot/include \
           -I$(PWD)/arch/$(ARCH)/include

LDFLAGS := -T $(PWD)/linker.lds \
           --nmagic \
           -Ttext=0x80000

# Source files
BOOT_SRC := $(wildcard boot/*.S) $(wildcard boot/*.c)
ARCH_SRC := $(wildcard arch/$(ARCH)/*.S) $(wildcard arch/$(ARCH)/*.c) \
            $(wildcard arch/$(ARCH)/mm/*.c) \
            $(wildcard arch/$(ARCH)/interrupt/*.c)
LIB_SRC  := $(wildcard lib/*.c)

# Object files
BOOT_OBJ := $(patsubst boot/%.c,$(BUILD_DIR)/%.o,$(wildcard boot/*.c)) \
            $(patsubst boot/%.S,$(BUILD_DIR)/%.o,$(wildcard boot/*.S))
ARCH_OBJ := $(patsubst arch/$(ARCH)/%.c,$(BUILD_DIR)/%.o,$(wildcard arch/$(ARCH)/*.c)) \
            $(patsubst arch/$(ARCH)/mm/%.c,$(BUILD_DIR)/mm/%.o,$(wildcard arch/$(ARCH)/mm/*.c)) \
            $(patsubst arch/$(ARCH)/interrupt/%.c,$(BUILD_DIR)/interrupt/%.o,$(wildcard arch/$(ARCH)/interrupt/*.c)) \
            $(patsubst arch/$(ARCH)/%.S,$(BUILD_DIR)/%.o,$(wildcard arch/$(ARCH)/*.S))
LIB_OBJ  := $(patsubst lib/%.c,$(BUILD_DIR)/%.o,$(LIB_SRC))

ALL_OBJS := $(BOOT_OBJ) $(ARCH_OBJ) $(LIB_OBJ) $(ROOTFS_OBJ)

# ISO creation tools
ISOCREATOR ?= genisoimage

.PHONY: all kernel iso run run-noinitrd debug rootfs cpio disk clean help

all: kernel cpio

help:
	@echo "Shlte OS Build System"
	@echo "====================="
	@echo ""
	@echo "Targets:"
	@echo "  make [all]     - Build kernel + initramfs (default)"
	@echo "  make kernel    - Build kernel binary only"
	@echo "  make rootfs    - Build embedded rootfs C source"
	@echo "  make cpio      - Build initramfs CPIO archive"
	@echo "  make disk      - Create spfs disk image (64MB)"
	@echo "  make iso       - Build bootable ISO (kernel+rootfs+disk+grub)"
	@echo "  make run       - Run in QEMU with initramfs + persistent disk"
	@echo "  make clean     - Clean build artifacts"
	@echo "  make help      - Show this help"
	@echo ""
	@echo "Package manager:"
	@echo "  sap install <file.deb>  - Install .deb packages"
	@echo "  sap list                - List installed packages"
	@echo ""
	@echo "Options:"
	@echo "  CROSS_COMPILE=<prefix> - Set cross-compiler prefix"
	@echo "  DEBUG=1                - Enable debug symbols"

kernel: $(KERNEL_BIN)

$(KERNEL_BIN): $(ALL_OBJS)
	@echo "[LD] $@"
	$(LD) $(LDFLAGS) -o $(KERNEL_ELF) $^
	$(OBJCOPY) -O binary $(KERNEL_ELF) $@
	@echo "[OK] Kernel built: $@ ($$(du -h $@ | cut -f1))"

# Compile .S (assembly) files
$(BUILD_DIR)/%.o: boot/%.S
	@mkdir -p $(dir $@)
	@echo "[AS] $<"
	$(AS) $(ASFLAGS) -o $@ $<

$(BUILD_DIR)/%.o: arch/$(ARCH)/%.S
	@mkdir -p $(dir $@)
	@echo "[AS] $<"
	$(AS) $(ASFLAGS) -o $@ $<

# Compile .c files
$(BUILD_DIR)/%.o: boot/%.c
	@mkdir -p $(dir $@)
	@echo "[CC] $<"
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/%.o: arch/$(ARCH)/%.c
	@mkdir -p $(dir $@)
	@echo "[CC] $<"
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/%.o: lib/%.c
	@mkdir -p $(dir $@)
	@echo "[CC] $<"
	$(CC) $(CFLAGS) -c -o $@ $<

# ============================================================
# RootFS targets
# ============================================================
rootfs: $(ROOTFS_C)

$(ROOTFS_C): scripts/mkrootfs.sh $(shell find rootfs/ -type f)
	@echo "[ROOTFS] Building embedded rootfs..."
	@mkdir -p $(BUILD_DIR)
	scripts/mkrootfs.sh rootfs/ $@

$(ROOTFS_OBJ): $(ROOTFS_C)
	@echo "[CC] $(ROOTFS_C)"
	$(CC) $(CFLAGS) -c -o $@ $<

# ============================================================
# CPIO initramfs archive
# ============================================================
cpio: $(INITRAMFS)

$(INITRAMFS): scripts/mkcpio.sh $(shell find rootfs/ -type f)
	@echo "[CPIO] Building initramfs CPIO archive..."
	@mkdir -p $(BUILD_DIR)
	scripts/mkcpio.sh rootfs/ $@

# ============================================================
# Disk image
# ============================================================
disk:
	@echo "[DISK] Creating spfs disk image..."
	@mkdir -p $(BUILD_DIR)
	scripts/mkdisk.sh $(BUILD_DIR)/disk.img 64
	@echo "[OK] Disk image ready: $(BUILD_DIR)/disk.img"

# ============================================================
# ISO Image
# ============================================================
ISO_OUTPUT_DIR := $(HOME)/P/iso

iso: kernel cpio disk
	@echo "[ISO] Building bootable ISO image..."
	@mkdir -p $(ISO_DIR)/boot
	@mkdir -p $(ISO_OUTPUT_DIR)
	# Add disk image to ISO staging
	cp $(BUILD_DIR)/disk.img $(ISO_DIR)/boot/disk.img
	scripts/mkiso.sh \
	    $(ISO_DIR) \
	    $(KERNEL_BIN) \
	    $(INITRAMFS) \
	    boot/grub/grub.cfg \
	    $(ISO_OUTPUT_DIR)/shlte-os.iso
	@echo "[OK] ISO ready: $(ISO_OUTPUT_DIR)/shlte-os.iso"
	@echo "      Size: $$(du -h $(ISO_OUTPUT_DIR)/shlte-os.iso | cut -f1)"
	@echo ""
	@echo "Run with:"
	@echo "  qemu-system-aarch64 -M virt -cpu cortex-a53 -m 512M \\"
	@echo "    -kernel $(KERNEL_BIN) -initrd $(INITRAMFS) \\"
	@echo "    -drive file=$(BUILD_DIR)/disk.img,if=none,id=hd0 \\"
	@echo "    -device virtio-blk-device,drive=hd0 -serial stdio -nographic"

# ============================================================
# QEMU (ARM64 virt machine)
# ============================================================
run: kernel cpio disk
	@echo "[QEMU] Starting Shlte OS (kernel + initrd + disk)..."
	@echo "       Kernel: $(KERNEL_BIN)"
	@echo "       Initrd: $(INITRAMFS)"
	@echo "       Disk:   $(BUILD_DIR)/disk.img"
	qemu-system-aarch64 \
	    -M virt -cpu cortex-a53 \
	    -m 512M \
	    -bios none \
	    -kernel $(KERNEL_BIN) \
	    -initrd $(INITRAMFS) \
	    -drive file=$(BUILD_DIR)/disk.img,if=none,id=hd0 \
	    -device virtio-blk-device,drive=hd0 \
	    -serial stdio \
	    -nographic
	@echo "[QEMU] Stopped."

run-noinitrd: kernel disk
	@echo "[QEMU] Starting Shlte OS (no initrd, with disk)..."
	qemu-system-aarch64 \
	    -M virt -cpu cortex-a53 \
	    -m 512M \
	    -bios none \
	    -kernel $(KERNEL_BIN) \
	    -drive file=$(BUILD_DIR)/disk.img,if=none,id=hd0 \
	    -device virtio-blk-device,drive=hd0 \
	    -serial stdio \
	    -nographic
	@echo "[QEMU] Stopped."

debug: kernel cpio disk
	@echo "[QEMU+GDB] Starting Shlte OS (waiting for gdb connect on :1234)..."
	qemu-system-aarch64 \
	    -M virt -cpu cortex-a53 \
	    -m 512M \
	    -bios none \
	    -kernel $(KERNEL_BIN) \
	    -initrd $(INITRAMFS) \
	    -serial stdio \
	    -S -s \
	    -nographic

# ============================================================
# Clean
# ============================================================
clean:
	@echo "[CLEAN] Removing build artifacts..."
	rm -rf $(BUILD_DIR)
	rm -f $(ISO_IMAGE)
	rm -f $(HOME)/P/iso/shlte-os.iso
	@echo "[OK] Clean complete."
