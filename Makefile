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
ISO_IMAGE  := shlte-os.iso
USER_DIR   := user

# Compiler/Linker flags
CFLAGS := -std=c11 -ffreestanding -fno-builtin -nostdlib -nostartfiles \
          -march=armv8-a -mtune=cortex-a53 \
          -Wall -Wextra \
          -O2 -g \
          -DKERNEL_MODE \
          -fno-stack-protector -fno-pic \
          -mgeneral-regs-only \
          -I$(PWD)/arch/$(ARCH)/include \
          -I$(PWD)/lib/include \
          -I$(PWD)/boot/include

ASFLAGS := -march=armv8-a \
           -I$(PWD)/boot/include \
           -I$(PWD)/arch/$(ARCH)/include

LDFLAGS := -T $(PWD)/linker.lds \
           --nmagic

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

ALL_OBJS := $(BOOT_OBJ) $(ARCH_OBJ) $(LIB_OBJ)

# User-space programs (raw binaries)
USER_S_SRCS := $(wildcard $(USER_DIR)/*.S)
USER_C_SRCS := $(wildcard $(USER_DIR)/*.c)
USER_BINS   := $(patsubst $(USER_DIR)/%.S,$(BUILD_DIR)/%.bin,$(USER_S_SRCS)) \
               $(patsubst $(USER_DIR)/%.c,$(BUILD_DIR)/%.bin,$(USER_C_SRCS))
USER_OBJS   := $(patsubst $(USER_DIR)/%.S,$(BUILD_DIR)/user_%.o,$(USER_S_SRCS)) \
               $(patsubst $(USER_DIR)/%.c,$(BUILD_DIR)/user_%.o,$(USER_C_SRCS))

# Disk image
DISK_IMG := $(BUILD_DIR)/disk.img

# ISO creation tools
ISOCREATOR ?= genisoimage

.PHONY: all kernel user-programs rootfs run debug disk clean help

all: kernel

help:
	@echo "Shlte OS Build System"
	@echo "====================="
	@echo ""
	@echo "Targets:"
	@echo "  make [all]     - Build kernel (default)"
	@echo "  make kernel    - Build kernel binary only"
	@echo "  make rootfs    - Build kernel + user programs + disk image"
	@echo "  make disk      - Create spfs disk image (64 MB, empty)"
	@echo "  make run       - rootfs + run in QEMU"
	@echo "  make debug     - rootfs + run in QEMU with GDB stub"
	@echo "  make clean     - Clean build artifacts"
	@echo "  make help      - Show this help"
	@echo ""
	@echo "Options:"
	@echo "  CROSS_COMPILE=<prefix> - Set cross-compiler prefix"
	@echo "  DEBUG=1                - Enable debug symbols"

# ============================================================
# Compilation rules
# ============================================================

# Boot .S files
$(BUILD_DIR)/%.o: boot/%.S
	@mkdir -p $(dir $@)
	@echo "[AS] $<"
	$(AS) $(ASFLAGS) -o $@ $<

# Arch .S files
$(BUILD_DIR)/%.o: arch/$(ARCH)/%.S
	@mkdir -p $(dir $@)
	@echo "[AS] $<"
	$(AS) $(ASFLAGS) -o $@ $<

# EFI stub compiled without -mgeneral-regs-only (UEFI uses SIMD)
$(BUILD_DIR)/efi_main.o: boot/efi_main.c
	@mkdir -p $(dir $@)
	@echo "[CC EFI] $<"
	$(CC) $(filter-out -mgeneral-regs-only,$(CFLAGS)) -c -o $@ $<

# Boot .c files
$(BUILD_DIR)/%.o: boot/%.c
	@mkdir -p $(dir $@)
	@echo "[CC] $<"
	$(CC) $(CFLAGS) -c -o $@ $<

# Arch .c files
$(BUILD_DIR)/%.o: arch/$(ARCH)/%.c
	@mkdir -p $(dir $@)
	@echo "[CC] $<"
	$(CC) $(CFLAGS) -c -o $@ $<

# Arch/mm .c files
$(BUILD_DIR)/mm/%.o: arch/$(ARCH)/mm/%.c
	@mkdir -p $(dir $@)
	@echo "[CC] $<"
	$(CC) $(CFLAGS) -c -o $@ $<

# Arch/interrupt .c files
$(BUILD_DIR)/interrupt/%.o: arch/$(ARCH)/interrupt/%.c
	@mkdir -p $(dir $@)
	@echo "[CC] $<"
	$(CC) $(CFLAGS) -c -o $@ $<

# Lib .c files
$(BUILD_DIR)/%.o: lib/%.c
	@mkdir -p $(dir $@)
	@echo "[CC] $<"
	$(CC) $(CFLAGS) -c -o $@ $<

# ============================================================
# User-space programs (raw binaries for rootfs)
# ============================================================

# User C compilation flags
USER_CFLAGS := -std=c11 -ffreestanding -fno-builtin -fno-builtin-memcpy \
               -fno-builtin-memset -fno-builtin-memmove \
               -nostdlib -nostartfiles \
               -march=armv8-a -mtune=cortex-a53 -O2 -fno-stack-protector \
               -fno-pic -mgeneral-regs-only -I$(PWD)/user

# Compile user C programs
$(BUILD_DIR)/user_%.o: $(USER_DIR)/%.c
	@mkdir -p $(dir $@)
	@echo "[CC USER] $<"
	$(CC) $(USER_CFLAGS) -c -o $@ $<

# Assemble user programs into ELF relocatable objects
$(BUILD_DIR)/user_%.o: $(USER_DIR)/%.S
	@mkdir -p $(dir $@)
	@echo "[AS] $<"
	$(AS) $(ASFLAGS) -o $@ $<

# Link as raw binary (no ELF headers, no relocations)
$(BUILD_DIR)/%.bin: $(BUILD_DIR)/user_%.o
	@echo "[BIN] $@"
	$(LD) -Ttext=0 -o $(BUILD_DIR)/$*_temp.elf $<
	$(OBJCOPY) -O binary $(BUILD_DIR)/$*_temp.elf $@
	rm -f $(BUILD_DIR)/$*_temp.elf
	@echo "[OK] User binary: $@ ($$(du -h $@ | cut -f1))"

# Default init binary: prefer C shell + crt0 + string, fall back to assembly shell
ifneq (,$(wildcard $(USER_DIR)/shell.c))
INIT_BIN_OBJS := $(BUILD_DIR)/user_crt0.o $(BUILD_DIR)/user_string.o $(BUILD_DIR)/user_shell.o
else
INIT_BIN_OBJS := $(BUILD_DIR)/user_init.o
endif

$(BUILD_DIR)/init.bin: $(INIT_BIN_OBJS)
	@echo "[BIN] $@ (C shell)"
	$(LD) -Ttext=0 -o $(BUILD_DIR)/init_temp.elf $(INIT_BIN_OBJS)
	$(OBJCOPY) -O binary $(BUILD_DIR)/init_temp.elf $@
	rm -f $(BUILD_DIR)/init_temp.elf
	@echo "[OK] User binary: $@ ($$(du -h $@ | cut -f1))"

# Convert user binary to embeddable ELF object for linker
$(BUILD_DIR)/init_embedded.o: $(BUILD_DIR)/init.bin
	@echo "[EMBED] $@"
	# First convert binary to ELF .data section
	$(OBJCOPY) -I binary -O elf64-littleaarch64 -B aarch64 $< $@
	# Then rename the section to .rootfs
	$(OBJCOPY) --rename-section .data=.rootfs,contents,alloc,load,readonly,data $@
	@echo "[OK] Embedded rootfs object: $@"

user-programs: $(USER_BINS)

# Kernel with embedded rootfs
KERNEL_EMBED_OBJS := $(BUILD_DIR)/init_embedded.o

kernel: $(KERNEL_BIN)

$(KERNEL_BIN): $(ALL_OBJS) $(KERNEL_EMBED_OBJS)
	@echo "[LD] $@"
	$(LD) $(LDFLAGS) -o $(KERNEL_ELF) $(ALL_OBJS) $(KERNEL_EMBED_OBJS)
	$(OBJCOPY) -O binary $(KERNEL_ELF) $@
	@echo "[OK] Kernel built: $@ ($$(du -h $@ | cut -f1))"

# ============================================================
# Disk image with rootfs (optional — for SPFS/virtio-blk)
# ============================================================
disk:
	@echo "[DISK] Creating spfs disk image..."
	@mkdir -p $(BUILD_DIR)
	scripts/mkdisk.sh $(DISK_IMG) 64
	@echo "[OK] Disk image ready: $(DISK_IMG)"

rootfs: kernel user-programs
	@echo "[ROOTFS] Building rootfs disk image..."
	@mkdir -p $(BUILD_DIR)
	scripts/mkdisk.sh $(DISK_IMG) 64 $(USER_BINS)
	@echo "[OK] Rootfs ready: $(DISK_IMG)"

# ============================================================
# QEMU (ARM64 virt machine)
# ============================================================
run: kernel
	@echo "[QEMU] Starting Shlte OS..."
	@echo "       Kernel: $(KERNEL_BIN)"
	qemu-system-aarch64 \
	    -M virt -cpu cortex-a53 \
	    -m 512M \
	    -kernel $(KERNEL_BIN) \
	    -nographic
	@echo "[QEMU] Stopped."

debug: kernel
	@echo "[QEMU+GDB] Starting Shlte OS (waiting for gdb connect on :1234)..."
	qemu-system-aarch64 \
	    -M virt -cpu cortex-a53 \
	    -m 512M \
	    -kernel $(KERNEL_BIN) \
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
