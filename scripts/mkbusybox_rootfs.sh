#!/usr/bin/env bash
# mkbusybox_rootfs.sh — Build a BusyBox-based rootfs for Shlte OS
#
# Generates a complete rootfs directory tree at $ROOTFS_DIR with:
#   /bin/busybox          — BusyBox binary (272 applets)
#   /bin/sh → /bin/busybox — Default shell
#   /init                 — BusyBox init + custom startup
#   /etc/inittab          — Init configuration
#   /etc/init.d/rcS       — Boot script
#   /usr/bin/sap          — Package manager
#   ... plus all standard Unix applets as symlinks
#
# Usage: mkbusybox_rootfs.sh <output-rootfs-dir>

set -e

BUSYBOX=$(which busybox 2>/dev/null || echo "/usr/bin/busybox")
ROOTFS_DIR="${1:-rootfs}"

if [ ! -x "$BUSYBOX" ]; then
    echo "Error: busybox not found"
    exit 1
fi

echo "[ROOTFS] Building BusyBox rootfs at $ROOTFS_DIR"
echo "         BusyBox: $BUSYBOX (v$($BUSYBOX --help | head -1 | grep -oP 'v[\d.]+' || echo '?'))"
echo "         Applets: $($BUSYBOX --list 2>/dev/null | wc -l)"

# Clean and recreate
rm -rf "$ROOTFS_DIR"
mkdir -p "$ROOTFS_DIR"

# =================================================================
# 1. Essential directory structure
# =================================================================
mkdir -p "$ROOTFS_DIR"/{bin,sbin,etc/init.d,etc/sap,etc/shell,dev,proc,sys,tmp,mnt,root,var/sap/{packages,files,pool},usr/bin,usr/sbin,lib,lib64}

# =================================================================
# 2. BusyBox binary + symlinks
# =================================================================
cp "$BUSYBOX" "$ROOTFS_DIR/bin/busybox"
# Strip debug info to save space
aarch64-linux-gnu-strip "$ROOTFS_DIR/bin/busybox" 2>/dev/null || strip "$ROOTFS_DIR/bin/busybox" 2>/dev/null || true

# Create all applet symlinks
(
    cd "$ROOTFS_DIR"
    "$ROOTFS_DIR/bin/busybox" --install -s .
) 2>/dev/null

# Ensure basic shells point to busybox
ln -sf /bin/busybox "$ROOTFS_DIR/bin/sh"
ln -sf /bin/busybox "$ROOTFS_DIR/bin/ash"

echo "   Created $(find "$ROOTFS_DIR" -type l | wc -l) symlinks"

# =================================================================
# 3. /init — Boot entry point
# =================================================================
cat > "$ROOTFS_DIR/init" << 'INITEOF'
#!/bin/sh
# ============================================
#  Shlte OS — /init (BusyBox-based)
#  PID 1 — system initialization + shell
# ============================================

# Mount essential filesystems
mount -t proc proc /proc 2>/dev/null
mount -t sysfs sysfs /sys 2>/dev/null
mount -t tmpfs tmpfs /tmp 2>/dev/null

# Boot banner
echo ""
echo "============================================"
echo "  Shlte OS v0.1 — ARM64 Microkernel"
echo "  BusyBox v1.36.1 — $(busybox --list | wc -l) applets"
echo "============================================"
echo ""

# Source environment
[ -f /etc/environment ] && . /etc/environment

# Display system info
echo "  Kernel    : ARM64 EL1 (microkernel)"
echo "  CPU       : Cortex-A53"
echo "  Timer     : 100 Hz (ARM Generic Timer)"
echo "  RootFS    : BusyBox initramfs"
echo ""

# Check for persistent storage
if [ -d /mnt ]; then
    mkdir -p /mnt/var/sap /mnt/var/sap/packages /mnt/var/sap/files /mnt/var/sap/pool 2>/dev/null
    echo "  Storage   : /mnt (persistent spfs)"
    PKG_COUNT=$(ls /mnt/var/sap/packages/ 2>/dev/null | wc -l)
    [ "$PKG_COUNT" -gt 0 ] && echo "  Packages  : $PKG_COUNT installed"
    echo ""
fi

# Run /etc/init.d/rcS if it exists
[ -x /etc/init.d/rcS ] && /etc/init.d/rcS

echo "============================================"
echo "  Welcome to Shlte OS!"
echo "  Type 'help' for commands, 'sap help' for packages"
echo "============================================"
echo ""

# Interactive shell (PID 1)
while true; do
    printf "shlte-os# "
    read -r cmd args rest

    case "$cmd" in
        help)
            echo "Commands:"
            echo "  help          — This help"
            echo "  info          — System information"
            echo "  ls [path]     — List files"
            echo "  cat <file>    — Show file"
            echo "  ps            — Process list"
            echo "  free          — Memory info"
            echo "  dmesg         — Kernel messages"
            echo "  uptime        — System uptime"
            echo "  reboot        — Reboot (WFI loop)"
            echo "  sap help      — Package manager"
            echo ""
            echo "Applets: ash, cat, ls, echo, sh, mount, vi, sed, awk, grep, find, ..."
            ;;
        info)
            echo "Shlte OS v0.1 — ARM64 Microkernel"
            echo "BusyBox 1.36.1 — $(busybox --list 2>/dev/null | wc -l) applets"
            echo "Copyright 2026 Shlte OS Team"
            ;;
        dmesg)
            cat /proc/kmsg 2>/dev/null || echo "No kernel messages available"
            ;;
        reboot|exit|halt)
            echo "Halting system."
            echo "Reboot/Shutdown not implemented in microkernel."
            while true; do :; done
            ;;
        "")
            ;;
        *)
            # Try to run the command via busybox
            /bin/busybox "$cmd" $args $rest 2>/dev/null || echo "sh: $cmd: not found (type 'help')"
            ;;
    esac
done
INITEOF
chmod 755 "$ROOTFS_DIR/init"

# =================================================================
# 4. /etc/inittab — BusyBox init config
# =================================================================
cat > "$ROOTFS_DIR/etc/inittab" << 'INITTAB'
# Shlte OS /etc/inittab (BusyBox init)
#
# Format: <id>:<runlevels>:<action>:<process>
#
# Actions: sysinit, respawn, askfirst, wait, once, restart, ctrlaltdel, shutdown

::sysinit:/etc/init.d/rcS
::askfirst:-/bin/sh
::ctrlaltdel:/sbin/reboot
::shutdown:/sbin/swapoff -a
::shutdown:/bin/umount -a -r
INITTAB

# =================================================================
# 5. /etc/init.d/rcS — System initialization
# =================================================================
cat > "$ROOTFS_DIR/etc/init.d/rcS" << 'RCSEOF'
#!/bin/sh
# /etc/init.d/rcS — System initialization script

echo "[INIT] Shlte OS boot sequence..."

# Set hostname
echo "shlte-os" > /proc/sys/kernel/hostname 2>/dev/null || hostname shlte-os 2>/dev/null || true

# Configure loopback
ifconfig lo 127.0.0.1 up 2>/dev/null || true

# Check for persistent storage
if [ -d /mnt ] && [ -w /mnt ]; then
    echo "[INIT] Persistent storage available at /mnt"
    mkdir -p /mnt/var/sap/packages /mnt/var/sap/files /mnt/var/sap/pool 2>/dev/null
    
    # Run sap list to show installed packages
    if [ -x /usr/bin/sap ]; then
        PKG_CNT=$(ls /mnt/var/sap/packages/ 2>/dev/null | wc -l)
        [ "$PKG_CNT" -gt 0 ] && echo "[SAP] $PKG_CNT package(s) installed"
    fi
fi

echo "[INIT] System ready."
RCSEOF
chmod 755 "$ROOTFS_DIR/etc/init.d/rcS"

# =================================================================
# 6. /usr/bin/sap — Package manager
# =================================================================
cat > "$ROOTFS_DIR/usr/bin/sap" << 'SAPEOF'
#!/bin/sh
# sap — Shlte OS Package Manager (BusyBox-compatible)
# Installs/removes .deb packages on /mnt

SAP_DIR="/mnt/var/sap"
PACKAGES_DIR="${SAP_DIR}/packages"
FILES_DIR="${SAP_DIR}/files"
INSTALL_DIR="/mnt"

sap_help() {
    echo "sap — Shlte OS Package Manager"
    echo ""
    echo "Usage:"
    echo "  sap install <file.deb>   Install a .deb package"
    echo "  sap remove <package>     Remove installed package"
    echo "  sap list                 List installed packages"
    echo "  sap files <package>      Show package files"
    echo "  sap info <package>       Show package info"
}

sap_list() {
    [ ! -d "$PACKAGES_DIR" ] && echo "No packages installed." && return 0
    local count=0
    for pkg in "$PACKAGES_DIR"/*; do
        [ -f "$pkg" ] || continue
        name=$(basename "$pkg")
        version=$(grep "^VERSION=" "$pkg" 2>/dev/null | cut -d= -f2)
        files=$(grep "^FILES=" "$pkg" 2>/dev/null | cut -d= -f2)
        printf "  %-20s  v%-10s  %s files\n" "$name" "${version:-?}" "${files:-?}"
        count=$((count + 1))
    done
    [ "$count" -eq 0 ] && echo "No packages installed."
}

sap_install() {
    local pkg="$1"
    [ ! -f "$pkg" ] && echo "Error: Package '$pkg' not found." && return 1
    
    local pkg_name
    pkg_name=$(basename "$pkg" | sed 's/\.deb$//' | sed 's/_.*$//')
    [ -z "$pkg_name" ] && echo "Error: Invalid package name." && return 1
    
    echo "Installing: $pkg_name"
    
    local tmpdir="${SAP_DIR}/.tmp_$$"
    mkdir -p "$tmpdir/data" 2>/dev/null
    
    # Extract .deb (ar archive) using busybox ar
    cd "$tmpdir" 2>/dev/null
    ar x "$pkg" 2>/dev/null
    
    # Extract data.tar.gz / data.tar
    if [ -f data.tar.gz ]; then
        gunzip -f data.tar.gz 2>/dev/null
    fi
    if [ -f data.tar ]; then
        tar xf data.tar -C data 2>/dev/null
    fi
    
    # Copy files to install dir
    > "${FILES_DIR}/${pkg_name}" 2>/dev/null
    local count=0
    find data -type f 2>/dev/null | while read -r f; do
        rel="${f#data/}"
        dest="${INSTALL_DIR}/${rel}"
        mkdir -p "$(dirname "$dest")" 2>/dev/null
        cp "$f" "$dest" 2>/dev/null && echo "$rel" >> "${FILES_DIR}/${pkg_name}" && count=$((count + 1))
    done
    
    # Save metadata
    {
        echo "PACKAGE=$pkg_name"
        echo "VERSION=1.0"
        echo "INSTALL_DATE=$(date 2>/dev/null || echo 'unknown')"
        echo "FILES=$count"
    } > "${PACKAGES_DIR}/${pkg_name}" 2>/dev/null
    
    rm -rf "$tmpdir"
    echo "Done. $count files installed to /mnt/"
}

sap_remove() {
    local pkg="$1"
    [ ! -f "${PACKAGES_DIR}/${pkg}" ] && echo "Error: Package '$pkg' not installed." && return 1
    
    echo "Removing: $pkg"
    [ -f "${FILES_DIR}/${pkg}" ] && while read -r f; do
        [ -n "$f" ] && rm -f "${INSTALL_DIR}/${f}" 2>/dev/null
    done < "${FILES_DIR}/${pkg}"
    
    rm -f "${FILES_DIR}/${pkg}" "${PACKAGES_DIR}/${pkg}" 2>/dev/null
    echo "Removed."
}

sap_info() {
    local pkg="$1"
    [ ! -f "${PACKAGES_DIR}/${pkg}" ] && echo "Not installed." && return 1
    echo "Package: $pkg"
    cat "${PACKAGES_DIR}/${pkg}" 2>/dev/null
    [ -f "${FILES_DIR}/${pkg}" ] && echo "Files: $(wc -l < "${FILES_DIR}/${pkg}" 2>/dev/null) installed"
}

sap_files() {
    local pkg="$1"
    [ ! -f "${FILES_DIR}/${pkg}" ] && echo "No files recorded." && return 1
    echo "Files in $pkg:"
    while read -r f; do
        [ -n "$f" ] && echo "  /${f}"
    done < "${FILES_DIR}/${pkg}"
}

# Main
case "${1:-help}" in
    install|i)     shift; sap_install "$1" ;;
    remove|rm)     shift; sap_remove "$1" ;;
    list|ls)       sap_list ;;
    info|show)     shift; sap_info "$1" ;;
    files)         shift; sap_files "$1" ;;
    help|--help|-h) sap_help ;;
    *)             echo "sap: unknown command '$1'"; sap_help ;;
esac
SAPEOF
chmod 755 "$ROOTFS_DIR/usr/bin/sap"

# =================================================================
# 7. Config files
# =================================================================
cat > "$ROOTFS_DIR/etc/environment" << 'ENVEOF'
HOME=/root
SHELL=/bin/sh
PATH=/sbin:/bin:/usr/sbin:/usr/bin
TERM=linux
HOSTNAME=shlte-os
ENVEOF

cat > "$ROOTFS_DIR/etc/sap/sources.list" << 'SAPSRC'
# sap package sources
# file:///mnt/var/sap/pool/
SRCSRC

cat > "$ROOTFS_DIR/etc/issue" << 'ISSUE'
Shlte OS v0.1 (ARM64) — BusyBox
ISSUE

cat > "$ROOTFS_DIR/etc/shell/welcome.sh" << 'WELCOME'
echo "Welcome to Shlte OS! Type 'help' for commands, 'sap help' for packages."
WELCOME

cat > "$ROOTFS_DIR/etc/passwd" << 'PASSWD'
root:x:0:0:root:/root:/bin/sh
nobody:x:65534:65534:nobody:/tmp:/bin/sh
PASSWD

echo ""
echo "[OK] BusyBox rootfs built at $ROOTFS_DIR"
echo "      Files: $(find "$ROOTFS_DIR" -type f | wc -l)"
echo "      Symlinks: $(find "$ROOTFS_DIR" -type l | wc -l)"
echo "      Total size: $(du -sh "$ROOTFS_DIR" | cut -f1)"
