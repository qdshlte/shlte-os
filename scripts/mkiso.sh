#!/usr/bin/env bash
# mkiso.sh — Build a bootable ARM64 ISO with GRUB
#
# Output: build/shlte-os.iso
#
# The ISO contains:
#   /boot/kernel.bin       — The kernel binary
#   /boot/initramfs.cpio   — The initramfs CPIO archive (rootfs)
#   /boot/grub/grub.cfg    — GRUB config
#
# For ARM64 QEMU with UEFI, boot with:
#   qemu-system-aarch64 -M virt -cpu cortex-a53 -bios edk2-aarch64-code.fd \
#       -cdrom shlte-os.iso
#
# For direct kernel boot (default shlte-os workflow):
#   qemu-system-aarch64 -M virt -cpu cortex-a53 \
#       -kernel kernel.bin -initrd initramfs.cpio

set -e

ISO_DIR="$1"
KERNEL_BIN="$2"
INITRAMFS_CPIO="$3"
GRUB_CFG="$4"
OUTPUT_ISO="$5"

if [ -z "$OUTPUT_ISO" ]; then
    echo "Usage: $0 <iso-dir> <kernel-bin> <initramfs-cpio> <grub-cfg> <output-iso>"
    exit 1
fi

echo "[ISO] Creating ISO image..."

# Clean and re-create ISO staging directory
rm -rf "$ISO_DIR"
mkdir -p "$ISO_DIR/boot/grub"

# Copy kernel and initramfs
cp "$KERNEL_BIN" "$ISO_DIR/boot/kernel.bin"

if [ -f "$INITRAMFS_CPIO" ]; then
    cp "$INITRAMFS_CPIO" "$ISO_DIR/boot/initramfs.cpio"
fi

# Copy GRUB config
if [ -f "$GRUB_CFG" ]; then
    cp "$GRUB_CFG" "$ISO_DIR/boot/grub/"
fi

# Create the ISO using genisoimage
genisoimage \
    -R -J -joliet-long \
    -input-charset utf-8 \
    -b boot/grub/grub.cfg \
    -no-emul-boot \
    -o "$OUTPUT_ISO" \
    "$ISO_DIR" 2>&1

echo "[OK] ISO created: $OUTPUT_ISO"
echo "      Run with: qemu-system-aarch64 -M virt -cdrom $OUTPUT_ISO"
echo "      (Requires UEFI: -bios edk2-aarch64-code.fd)"
echo ""
echo "      Direct kernel boot:"
echo "      qemu-system-aarch64 -M virt -kernel $KERNEL_BIN -initrd ${INITRAMFS_CPIO:-<none>}"
