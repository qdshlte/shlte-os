#!/usr/bin/env bash
# mkcpio.sh — Build a newc CPIO initramfs archive (uses BusyBox cpio)
set -e
ROOTFS_DIR="$1"; OUTPUT_CPIO="$2"
[ -z "$ROOTFS_DIR" ] && echo "Usage: $0 <rootfs-dir> <output-cpio>" && exit 1
mkdir -p "$(dirname "$OUTPUT_CPIO")"
echo "[CPIO] Building initramfs from $ROOTFS_DIR..."

if [ -x "$ROOTFS_DIR/bin/busybox" ]; then
    cd "$ROOTFS_DIR"
    find . | "$ROOTFS_DIR/bin/busybox" cpio -o -H newc --quiet 2>/dev/null > "$OUTPUT_CPIO"
    cd - >/dev/null
else
    echo "No BusyBox cpio available, installing package..."
    apt-get install -y cpio 2>/dev/null || {
        cd "$ROOTFS_DIR"
        find . | cpio -o -H newc --quiet 2>/dev/null > "$OUTPUT_CPIO"
        cd - >/dev/null
    }
fi

size=$(stat -c%s "$OUTPUT_CPIO" 2>/dev/null || echo "?")
files=$(python3 -c "d=open('$OUTPUT_CPIO','rb').read(); print(d.count(b'070701')-1)")
echo "[OK] CPIO: $OUTPUT_CPIO ($files files, $size bytes)"
