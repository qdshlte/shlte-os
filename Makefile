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

# Compiler/Linker flags
CFLAGS := -std=c11 -ffreestanding -nostdlib -nostartfiles \
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
            $(wildcard arch/$(ARCH)/interrupt/*.c) \
            $(wildcard arch/$(ARCH)/fs/*.c)
LIB_SRC  := $(wildcard lib/*.c)

# Object files
BOOT_OBJ := $(patsubst boot/%.c,$(BUILD_DIR)/%.o,$(wildcard boot/*.c)) \
            $(patsubst boot/%.S,$(BUILD_DIR)/%.o,$(wildcard boot/*.S))
ARCH_OBJ := $(patsubst arch/$(ARCH)/%.c,$(BUILD_DIR)/%.o,$(wildcard arch/$(ARCH)/*.c)) \
            $(patsubst arch/$(ARCH)/mm/%.c,$(BUILD_DIR)/mm/%.o,$(wildcard arch/$(ARCH)/mm/*.c)) \
            $(patsubst arch/$(ARCH)/interrupt/%.c,$(BUILD_DIR)/interrupt/%.o,$(wildcard arch/$(ARCH)/interrupt/*.c)) \
            $(patsubst arch/$(ARCH)/fs/%.c,$(BUILD_DIR)/fs/%.o,$(wildcard arch/$(ARCH)/fs/*.c)) \
            $(patsubst arch/$(ARCH)/%.S,$(BUILD_DIR)/%.o,$(wildcard arch/$(ARCH)/*.S))
LIB_OBJ  := $(patsubst lib/%.c,$(BUILD_DIR)/%.o,$(LIB_SRC))

ALL_OBJS := $(BOOT_OBJ) $(ARCH_OBJ) $(LIB_OBJ)

# ISO creation tools
ISOCREATOR ?= genisoimage

.PHONY: all kernel iso run clean help

all: kernel

help:
	@echo "Shlte OS Build System"
	@echo "====================="
	@echo ""
	@echo "Targets:"
	@echo "  make [all]     - Build kernel and ISO image"
	@echo "  make kernel    - Build kernel binary only"
	@echo "  make iso       - Build bootable ISO image"
	@echo "  make run       - Run in QEMU"
	@echo "  make rootfs    - Build rootfs"
	@echo "  make clean     - Clean build artifacts"
	@echo "  make help      - Show this help"
	@echo ""
	@echo "Options:"
	@echo "  CROSS_COMPILE=<prefix> - Set cross-compiler prefix"
	@echo "  DEBUG=1                - Enable debug symbols"

kernel: $(KERNEL_BIN)

$(KERNEL_BIN): $(ALL_OBJS)
	@echo "[LD] $@"
	$(LD) $(LDFLAGS) -o $(KERNEL_ELF) $^
	$(OBJCOPY) -O binary $(KERNEL_ELF) $@
	@echo "[OK] Kernel built: $@ ($(shell du -h $@ | cut -f1))"

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
# ISO Image (placeholder - ARM64 uses kernel.bin directly)
# ============================================================
iso: kernel
	@echo "[NOTE] ARM64: ISO not needed. Use 'make run' for QEMU."

# ============================================================
# QEMU (ARM64 virt machine)
# ============================================================
run: kernel
	@echo "[QEMU] Starting Shlte OS on ARM64 virt machine..."
	qemu-system-aarch64 \
	    -M virt -cpu cortex-a53 \
	    -m 512M \
	    -bios none \
	    -kernel $(KERNEL_BIN) \
	    -serial stdio \
	    -nographic
	@echo "[QEMU] Stopped."

debug: kernel
	@echo "[QEMU+GDB] Starting Shlte OS (waiting for gdb connect on :1234)..."
	qemu-system-aarch64 \
	    -M virt -cpu cortex-a53 \
	    -m 512M \
	    -bios none \
	    -kernel $(KERNEL_BIN) \
	    -serial stdio \
	    -S -s \
	    -nographic

# ============================================================
# Clean
# ============================================================
clean:
	@echo "[CLEAN] Removing build artifacts..."
	rm -rf $(BUILD_DIR)
	@echo "[OK] Clean complete."
