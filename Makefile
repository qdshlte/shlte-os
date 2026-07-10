# ============================================================
# Shlte OS - ARM64 / x86_64 Microkernel
# A minimal OS supporting multiple architectures
# ============================================================

# Target architecture (arm64 or x86_64)
ARCH ?= arm64

# Architecture-specific configuration
ifeq ($(ARCH), arm64)
    CROSS_COMPILE ?= aarch64-linux-gnu-
    QEMU          := qemu-system-aarch64
    QEMU_FLAGS    := -M virt -cpu cortex-a53 -m 512M -bios none -serial stdio -nographic
    LD_SCRIPT     := linker.lds
    BOOT_S        := boot/boot.S
    CFLAGS_ARCH   := -march=armv8-a -mtune=cortex-a53
    ASFLAGS_ARCH  := -march=armv8-a
    KERNEL_LOAD   := 0x80000
else ifeq ($(ARCH), x86_64)
    CROSS_COMPILE ?=
    QEMU          := qemu-system-x86_64
    QEMU_FLAGS    := -m 512M -serial stdio -nographic -no-reboot
    LD_SCRIPT     := linker_x86_64.lds
    BOOT_S        := boot/boot_x86_64.S
    CFLAGS_ARCH   := -mno-red-zone -mcmodel=kernel
    ASFLAGS_ARCH  :=
    KERNEL_LOAD   := 0x100000
else
    $(error Unsupported ARCH: $(ARCH). Supported: arm64, x86_64)
endif

# Toolchain
CC      := $(CROSS_COMPILE)gcc
AS      := $(CROSS_COMPILE)as
LD      := $(CROSS_COMPILE)ld
OBJCOPY := $(CROSS_COMPILE)objcopy
OBJDUMP := $(CROSS_COMPILE)objdump

# Target
TARGET := shlteos

# Build directories
BUILD_DIR  := build
KERNEL_BIN := $(BUILD_DIR)/kernel.bin
KERNEL_ELF := $(BUILD_DIR)/kernel.elf

# Include paths
INCLUDES := -I$(PWD)/arch/$(ARCH)/include \
            -I$(PWD)/lib/include

# Common compiler/linker flags
CFLAGS := -std=c11 -ffreestanding -nostdlib -nostartfiles \
          -Wall -Wextra \
          -O2 -g \
          -DKERNEL_MODE \
          -fno-stack-protector -fno-pic \
          $(CFLAGS_ARCH) \
          $(INCLUDES)

ASFLAGS := $(ASFLAGS_ARCH) \
           -I$(PWD)/boot/include \
           -I$(PWD)/arch/$(ARCH)/include

LDFLAGS := -T $(PWD)/$(LD_SCRIPT) \
           --nmagic \
           -z noexecstack \
           -Ttext=$(KERNEL_LOAD)

# Source files
BOOT_C   := $(wildcard boot/*.c)
ARCH_C   := $(wildcard arch/$(ARCH)/*.c) \
            $(wildcard arch/$(ARCH)/mm/*.c) \
            $(wildcard arch/$(ARCH)/interrupt/*.c)
LIB_C    := $(wildcard lib/*.c)

# Object files
BOOT_C_OBJ  := $(patsubst boot/%.c, $(BUILD_DIR)/boot/%.o, $(BOOT_C))
BOOT_S_NAME := $(notdir $(BOOT_S:.S=.o))
BOOT_S_OBJ  := $(BUILD_DIR)/boot/$(BOOT_S_NAME)
ARCH_C_OBJ  := $(patsubst arch/$(ARCH)/%.c, $(BUILD_DIR)/arch/%.o, $(ARCH_C))
LIB_C_OBJ   := $(patsubst lib/%.c, $(BUILD_DIR)/lib/%.o, $(LIB_C))

ALL_OBJS := $(BOOT_C_OBJ) $(BOOT_S_OBJ) $(ARCH_C_OBJ) $(LIB_C_OBJ)

# Remove empty entries
ALL_OBJS := $(filter %.o, $(ALL_OBJS))

.PHONY: all kernel run debug clean help

all: kernel

help:
	@echo "Shlte OS Build System"
	@echo "====================="
	@echo ""
	@echo "Targets:"
	@echo "  make [all]            - Build kernel (default ARCH=arm64)"
	@echo "  make kernel           - Build kernel binary"
	@echo "  make run              - Build & run in QEMU"
	@echo "  make debug            - Build & run with GDB server on :1234"
	@echo "  make clean            - Clean build artifacts"
	@echo "  make help             - Show this help"
	@echo ""
	@echo "Options:"
	@echo "  ARCH=arm64|x86_64     - Select target architecture (default: arm64)"
	@echo "  CROSS_COMPILE=<prefix> - Override cross-compiler prefix"
	@echo "  DEBUG=1               - Enable debug symbols"
	@echo ""
	@echo "Examples:"
	@echo "  make                          # Build for arm64"
	@echo "  make ARCH=x86_64              # Build for x86_64"
	@echo "  make run ARCH=x86_64          # Run x86_64 kernel in QEMU"
	@echo "  make debug                    # Debug arm64 kernel with GDB"

kernel: $(KERNEL_BIN)

$(KERNEL_BIN): $(ALL_OBJS)
	@echo "[LD] $@ ($(ARCH))"
	$(LD) $(LDFLAGS) -o $(KERNEL_ELF) $^
	$(OBJCOPY) -O binary $(KERNEL_ELF) $@
	@echo "[OK] Kernel built: $@ ($(shell du -h $@ 2>/dev/null | cut -f1))"

# Assembly files (use CC for .S to get C preprocessor)
$(BUILD_DIR)/boot/%.o: boot/%.S
	@mkdir -p $(dir $@)
	@echo "[AS] $< ($(ARCH))"
	$(CC) $(ASFLAGS) -c -o $@ $<

# C files - lib ($(BUILD_DIR)/lib/%.o)
$(BUILD_DIR)/lib/%.o: lib/%.c
	@mkdir -p $(dir $@)
	@echo "[CC] $< ($(ARCH))"
	$(CC) $(CFLAGS) -c -o $@ $<

# C files - arch ($(BUILD_DIR)/arch/%.o)
$(BUILD_DIR)/arch/%.o: arch/$(ARCH)/%.c
	@mkdir -p $(dir $@)
	@echo "[CC] $< ($(ARCH))"
	$(CC) $(CFLAGS) -c -o $@ $<

# C files - boot ($(BUILD_DIR)/boot/%.o)
$(BUILD_DIR)/boot/%.o: boot/%.c
	@mkdir -p $(dir $@)
	@echo "[CC] $< ($(ARCH))"
	$(CC) $(CFLAGS) -c -o $@ $<

# ============================================================
# ISO Image
# ============================================================
iso: kernel
	@echo "[NOTE] ISO not needed. Use 'make run' for QEMU."

# ============================================================
# QEMU
# ============================================================
run: kernel
	@echo "[QEMU] Starting Shlte OS ($(ARCH))..."
	$(QEMU) $(QEMU_FLAGS) -kernel $(KERNEL_BIN)
	@echo "[QEMU] Stopped."

debug: kernel
	@echo "[QEMU+GDB] Starting Shlte OS $(ARCH) (gdb on :1234)..."
	$(QEMU) $(QEMU_FLAGS) -kernel $(KERNEL_BIN) -S -s

# ============================================================
# Clean
# ============================================================
clean:
	@echo "[CLEAN] Removing build artifacts..."
	rm -rf $(BUILD_DIR)
	@echo "[OK] Clean complete."
