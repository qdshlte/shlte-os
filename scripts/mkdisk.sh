#!/usr/bin/env bash
# mkdisk.sh — Create a QEMU disk image with spfs filesystem
#
# Creates a raw disk image with spfs filesystem, then optionally
# writes binary files into it.
#
# Usage:
#   mkdisk.sh <output> [size_mb] [binary_files...]
#
# Each binary file is written as a raw binary onto the spfs filesystem.
# The filename (without extension) is used as the spfs file name.
# If a file's name is "init.bin", it will be stored as "init" on spfs.

set -e

OUTPUT="${1:-build/disk.img}"
SIZE_MB="${2:-64}"
shift 2 2>/dev/null || true

mkdir -p "$(dirname "$OUTPUT")"

echo "[DISK] Creating $SIZE_MB MB disk image: $OUTPUT"

# Create empty disk image
dd if=/dev/zero of="$OUTPUT" bs=1M count="$SIZE_MB" 2>/dev/null
echo "[DISK] Created raw image ($SIZE_MB MB)"

TOTAL_BLOCKS=$((SIZE_MB * 1024 * 1024 / 4096))
DATA_START=8   # block 0=superblock, block 1+=directory entries
BLOCK_SIZE=4096
DIRENT_SIZE=144          # sizeof(spfs_dirent_t) packed
ENTRIES_PER_BLOCK=$((BLOCK_SIZE / DIRENT_SIZE))  # = 28

# Write superblock using Python
python3 -c "
import struct, sys
blocks = int(sys.argv[1])
data_start = int(sys.argv[2])
dir_max = int(sys.argv[3])
# Superblock at block 0
sb = struct.pack('<IIIIIIII',
    0x53504653,     # magic: SPFS
    1,              # version
    blocks,         # total blocks
    4096,           # block size
    0,              # dir_count (empty FS)
    dir_max,        # dir_max (max files)
    data_start,     # data_block_start
    0               # reserved pad start
)
sb += b'\x00' * (4096 - len(sb))

with open(sys.argv[4], 'r+b') as f:
    f.write(sb)
print(f'Superblock written: {blocks} blocks, dir_max={dir_max}, data starts at block {data_start}')
print('spfs filesystem initialized successfully.')
" "$TOTAL_BLOCKS" "$DATA_START" "$ENTRIES_PER_BLOCK" "$OUTPUT"

# Write files onto the disk image using Python
FILES=("$@")

if [ ${#FILES[@]} -gt 0 ]; then
    echo "[DISK] Writing ${#FILES[@]} file(s) to spfs..."

    python3 -c "
import struct, sys, os

OUTPUT = sys.argv[1]
TOTAL_BLOCKS = int(sys.argv[2])
DATA_START = int(sys.argv[3])
BLOCK_SIZE = 4096
DIRENT_SIZE = 144        # spfs_dirent_t packed size
ENTRIES_PER_BLOCK = BLOCK_SIZE // DIRENT_SIZE  # = 28
files = sys.argv[4:]

def read_block(f, b):
    f.seek(b * BLOCK_SIZE)
    return bytearray(f.read(BLOCK_SIZE))

def write_block(f, b, data):
    f.seek(b * BLOCK_SIZE)
    f.write(data)

def read_dirent(f, idx):
    \"\"\"Read a directory entry by index. Returns 144-byte bytearray.\"\"\"
    block = 1 + idx // ENTRIES_PER_BLOCK
    off = (idx % ENTRIES_PER_BLOCK) * DIRENT_SIZE
    b = read_block(f, block)
    return b[off:off+DIRENT_SIZE]

def write_dirent(f, idx, ent):
    \"\"\"Write a directory entry by index. ent must be 144 bytes.\"\"\"
    block = 1 + idx // ENTRIES_PER_BLOCK
    off = (idx % ENTRIES_PER_BLOCK) * DIRENT_SIZE
    b = read_block(f, block)
    b[off:off+DIRENT_SIZE] = ent
    write_block(f, block, bytes(b))

def dirent_name(ent):
    \"\"\"Extract the file name from a dirent (null-terminated).\"\"\"
    end = ent.find(b'\\x00', 0, 128)
    if end < 0: end = 128
    return ent[:end].decode('ascii', errors='replace')

with open(OUTPUT, 'r+b') as f:
    sb_data = read_block(f, 0)
    magic, ver, total, bs, dir_count, dir_max, dstart = struct.unpack_from('<IIIIIII', sb_data, 0)
    print(f'[mkdisk] Current dir_count={dir_count}, dir_max={dir_max}, data_start={dstart}')

    def find_file(name):
        for i in range(dir_count):
            ent = read_dirent(f, i)
            if ent[0] != 0 and dirent_name(ent) == name:
                return i
        return -1

    def find_free_slot():
        for i in range(dir_max):
            ent = read_dirent(f, i)
            if ent[0] == 0:
                return i
        return -1

    def alloc_blocks(count):
        candidate = dstart
        while candidate + count <= total:
            collision = False
            for fi in range(dir_max):
                ent = read_dirent(f, fi)
                if ent[0] == 0:
                    continue
                ename = dirent_name(ent)
                if not ename:
                    continue
                blk_start = struct.unpack_from('<I', ent[136:140])[0]
                blk_count = struct.unpack_from('<I', ent[140:144])[0]
                if blk_count == 0:
                    continue
                fs = blk_start
                fe = blk_start + blk_count
                if candidate < fe and (candidate + count) > fs:
                    candidate = fe
                    collision = True
                    break
            if not collision:
                return candidate
        return -1

    written = []
    for filepath in files:
        basename = os.path.basename(filepath)
        spfs_name = basename[:-4] if basename.endswith('.bin') else basename

        with open(filepath, 'rb') as sf:
            data = sf.read()

        filesize = len(data)
        need_blocks = max(1, (filesize + BLOCK_SIZE - 1) // BLOCK_SIZE)
        print(f'[mkdisk] Writing \"{spfs_name}\" ({filesize} bytes, {need_blocks} blocks)...')

        existing = find_file(spfs_name)
        if existing >= 0:
            ent = read_dirent(f, existing)
            old_blocks = struct.unpack_from('<I', ent[140:144])[0]
            if old_blocks >= need_blocks:
                block_start = struct.unpack_from('<I', ent[136:140])[0]
            else:
                new_start = alloc_blocks(need_blocks)
                if new_start < 0:
                    print(f'[mkdisk] ERROR: Out of space for \"{spfs_name}\"'); sys.exit(1)
                block_start = new_start
        else:
            slot = find_free_slot()
            if slot < 0:
                print(f'[mkdisk] ERROR: Directory full'); sys.exit(1)
            new_start = alloc_blocks(need_blocks)
            if new_start < 0:
                print(f'[mkdisk] ERROR: Out of space for \"{spfs_name}\"'); sys.exit(1)
            block_start = new_start
            if slot >= dir_count:
                dir_count = slot + 1

        # Build directory entry (144 bytes)
        ent = bytearray(DIRENT_SIZE)
        nameb = spfs_name.encode('ascii')[:127]
        ent[:len(nameb)] = nameb
        ent[128] = 0   # type = SPFS_FILE
        struct.pack_into('<III', ent, 132, filesize, block_start, need_blocks)

        if existing >= 0:
            write_dirent(f, existing, ent)
        else:
            write_dirent(f, slot, ent)

        # Write data blocks
        remaining = filesize
        block_idx = 0
        data_offset = 0
        while remaining > 0:
            to_write = min(remaining, BLOCK_SIZE)
            blk_num = block_start + block_idx
            block_data = bytearray(BLOCK_SIZE)
            block_data[:to_write] = data[data_offset:data_offset + to_write]
            write_block(f, blk_num, bytes(block_data))
            remaining -= to_write
            data_offset += to_write
            block_idx += 1

        written.append(spfs_name)

    # Update superblock dir_count
    sb_new = bytearray(read_block(f, 0))
    struct.pack_into('<I', sb_new, 16, dir_count)
    write_block(f, 0, bytes(sb_new))

    print(f'[mkdisk] Wrote {len(written)} file(s): {\", \".join(written)}')
" "$OUTPUT" "$TOTAL_BLOCKS" "$DATA_START" "${FILES[@]}"
fi

echo "[DISK] Disk ready: $OUTPUT ($SIZE_MB MB, $TOTAL_BLOCKS blocks)"
echo "         Use with QEMU: -drive file=$OUTPUT,if=none,id=hd0 -device virtio-blk-device,drive=hd0"
