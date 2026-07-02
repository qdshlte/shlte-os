#!/bin/sh
#
# Shlte OS - init script
# This is the first process started by the kernel
#

echo "============================================"
echo "  Shlte OS - Starting User Space..."
echo "============================================"
echo ""

# Print system information
echo "Welcome to Shlte OS!"
echo "Kernel: ARM64 Microkernel v0.1"
echo "Build:  $(date 2>/dev/null || echo 'N/A')"
echo ""

# Check if we have a shell
if [ -x /bin/sh ]; then
    echo "[OK] Shell available: /bin/sh"
else
    echo "[WARN] No shell found at /bin/sh"
fi

# Print mounted filesystems
echo ""
echo "Mount points:"
if [ -d /proc ]; then
    ls /proc 2>/dev/null
else
    echo "  (proc not available)"
fi

# Print environment
echo ""
echo "Environment:"
echo "  HOME=/root"
echo "  SHELL=/bin/sh"
echo "  PATH=/bin:/sbin:/usr/bin:/usr/sbin"

echo ""
echo "System ready. Type 'help' for commands."
echo ""

# If we have bash, exec it; otherwise fall back to sh
if [ -x /bin/bash ]; then
    exec /bin/bash --norc
elif [ -x /bin/sh ]; then
    exec /bin/sh
else
    echo "[FATAL] No shell available!"
    while true; do
        sleep 1
    done
fi
