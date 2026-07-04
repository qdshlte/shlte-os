#!/usr/bin/env bash
# mkdisk.sh — Create a QEMU disk image with spfs filesystem
#
# Creates a raw disk image with:
#   Block 0: spfs superblock (magic 0x53504653 "SPFS")
#   Blocks 1-7: empty directory entries (128 slots, 64 bytes each)
#   Blocks 8+: free for file data
#
# The disk is 64MB by default (16384 blocks of 4KB).

set -e

OUTPUT="${1:-build/disk.img}"
SIZE_MB="${2:-64}"

mkdir -p "$(dirname "$OUTPUT")"

echo "[DISK] Creating $SIZE_MB MB disk image: $OUTPUT"

# Create empty disk image
dd if=/dev/zero of="$OUTPUT" bs=1M count="$SIZE_MB" 2>/dev/null
echo "[DISK] Created raw image ($SIZE_MB MB)"

TOTAL_BLOCKS=$((SIZE_MB * 1024 * 1024 / 4096))
DATA_START=8  # block 0=superblock, 1-7=directory

# Write superblock using Python
python3 -c "
import struct, sys
blocks = int(sys.argv[1])
data_start = int(sys.argv[2])
# Superblock at block 0
sb = struct.pack('<IIIIIIII',
    0x53504653,     # magic: SPFS
    1,              # version
    blocks,         # total blocks
    4096,           # block size
    0,              # dir_count (empty FS)
    128,            # dir_max (max files)
    data_start,     # data_block_start
    0               # reserved pad start
)
sb += b'\x00' * (4096 - len(sb))

with open(sys.argv[3], 'r+b') as f:
    f.write(sb)
print(f'Superblock written: {blocks} blocks, data starts at block {data_start}')
print('spfs filesystem initialized successfully.')
" "$TOTAL_BLOCKS" "$DATA_START" "$OUTPUT"
echo "[DISK] Disk ready: $OUTPUT ($SIZE_MB MB, $(($TOTAL_BLOCKS)) blocks)"
echo "         Use with QEMU: -drive file=$OUTPUT,if=none,id=hd0 -device virtio-blk-device,drive=hd0"
