#!/bin/sh
#
# Shlte OS - Build scripts helper
#

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

echo "=== Shlte OS Build Helper ==="
echo ""

case "${1:-help}" in
    build)
        echo "[BUILD] Building Shlte OS..."
        cd "$PROJECT_DIR"
        make all
        ;;
    iso)
        echo "[ISO] Building ISO image..."
        cd "$PROJECT_DIR"
        make iso
        ;;
    run)
        echo "[RUN] Starting QEMU..."
        cd "$PROJECT_DIR"
        make run
        ;;
    debug)
        echo "[DEBUG] Starting QEMU with gdb server..."
        cd "$PROJECT_DIR"
        make debug
        ;;
    clean)
        echo "[CLEAN] Cleaning build artifacts..."
        cd "$PROJECT_DIR"
        make clean
        ;;
    help|*)
        echo "Usage: $0 {build|iso|run|debug|clean|help}"
        echo ""
        echo "Commands:"
        echo "  build  - Build kernel and ISO image"
        echo "  iso    - Build bootable ISO image"
        echo "  run    - Run in QEMU"
        echo "  debug  - Run in QEMU with GDB server"
        echo "  clean  - Clean build artifacts"
        echo "  help   - Show this help"
        ;;
esac
