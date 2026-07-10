#!/bin/bash
#
# Shlte OS - Build scripts helper
# Provides convenient one-command build, run, and debug for the ARM64 microkernel.
#

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Check required tools
check_dependencies() {
    local missing=0
    for cmd in gcc make qemu-system-aarch64; do
        if ! command -v "$cmd" &> /dev/null; then
            echo -e "${RED}[ERROR] Required command not found: ${cmd}${NC}"
            missing=1
        fi
    done
    if [ $missing -eq 1 ]; then
        echo -e "${YELLOW}[INFO] Install with: sudo apt install build-essential qemu-system-arm${NC}"
        exit 1
    fi
}

# Print banner
print_banner() {
    echo -e "${BLUE}======================================${NC}"
    echo -e "${BLUE}   Shlte OS - ARM64 Microkernel${NC}"
    echo -e "${BLUE}======================================${NC}"
    echo ""
}

# Build the kernel
do_build() {
    echo -e "${GREEN}[BUILD]${NC} Building Shlte OS kernel..."
    cd "$PROJECT_DIR"
    make clean >/dev/null 2>&1 || true
    make kernel 2>&1
    if [ $? -eq 0 ]; then
        echo -e "${GREEN}[OK]${NC} Kernel built successfully: build/kernel.bin"
        ls -lh build/kernel.bin 2>/dev/null || true
    else
        echo -e "${RED}[FAIL]${NC} Kernel build failed!"
        exit 1
    fi
}

# Build ISO (ARM64 doesn't use ISO, but keep for compatibility)
do_iso() {
    echo -e "${YELLOW}[NOTE]${NC} ARM64: ISO not needed. Use 'build.sh run' for QEMU."
    cd "$PROJECT_DIR"
    make iso 2>&1 || true
}

# Run in QEMU
do_run() {
    # Ensure kernel is built
    if [ ! -f "$PROJECT_DIR/build/kernel.bin" ]; then
        echo -e "${YELLOW}[WARN]${NC} kernel.bin not found. Building first..."
        do_build
    fi
    
    echo -e "${GREEN}[QEMU]${NC} Starting Shlte OS on ARM64 virt machine..."
    echo -e "${YELLOW}[INFO]${NC} Press Ctrl+A then X to exit QEMU"
    echo ""
    cd "$PROJECT_DIR"
    make run 2>&1
}

# Debug with GDB
do_debug() {
    # Ensure kernel is built
    if [ ! -f "$PROJECT_DIR/build/kernel.bin" ]; then
        echo -e "${YELLOW}[WARN]${NC} kernel.bin not found. Building first..."
        do_build
    fi
    
    echo -e "${GREEN}[DEBUG]${NC} Starting QEMU with GDB server on port 1234..."
    echo -e "${YELLOW}[INFO]${NC} Connect with: gdb -ex 'target remote :1234' build/kernel.elf"
    echo ""
    cd "$PROJECT_DIR"
    make debug 2>&1
}

# Clean build artifacts
do_clean() {
    echo -e "${GREEN}[CLEAN]${NC} Removing build artifacts..."
    cd "$PROJECT_DIR"
    make clean 2>&1
    echo -e "${GREEN}[OK]${NC} Clean complete."
}

# Show help
show_help() {
    echo "Usage: $(basename "$0") {build|iso|run|debug|clean|help}"
    echo ""
    echo "Commands:"
    echo "  build  - Clean and build the kernel"
    echo "  iso    - Build bootable ISO (ARM64: prints note)"
    echo "  run    - Build (if needed) and run in QEMU"
    echo "  debug  - Run in QEMU with GDB server on :1234"
    echo "  clean  - Clean build artifacts"
    echo "  help   - Show this help"
    echo ""
    echo "Examples:"
    echo "  ./scripts/build.sh build    # Build kernel"
    echo "  ./scripts/build.sh run      # Run in QEMU"
    echo "  ./scripts/build.sh debug    # Debug with GDB"
    echo ""
    echo "Quick start:"
    echo "  ./scripts/build.sh run    # Build and run in one command"
}

# Main
print_banner
check_dependencies

case "${1:-help}" in
    build)
        do_build
        ;;
    iso)
        do_iso
        ;;
    run)
        do_run
        ;;
    debug)
        do_debug
        ;;
    clean)
        do_clean
        ;;
    help|*)
        show_help
        ;;
esac
