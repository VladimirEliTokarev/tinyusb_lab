#!/bin/bash
# build_wsl_kernel.sh — Build a custom WSL2 kernel with dummy_hcd + raw_gadget
#
# The default WSL2 Microsoft kernel does NOT ship these modules.
# This script builds a custom kernel with them enabled.
#
# Takes ~10-15 minutes on a modern machine.
#
# After building, configure WSL to use the custom kernel by adding to
# C:\Users\<user>\.wslconfig:
#   [wsl2]
#   kernel=D:\\work\\Research\\wsl-kernel\\vmlinux
#
# Then: wsl --shutdown && wsl -d Ubuntu-24.04
#
set -euo pipefail

KERNEL_DIR="$HOME/wsl2-kernel"
KERNEL_TAG="linux-msft-wsl-6.6.y"

echo "=== Building custom WSL2 kernel with USB gadget support ==="
echo ""

# Install build deps
sudo apt-get update -qq
sudo apt-get install -y -qq build-essential flex bison libssl-dev libelf-dev \
  bc dwarves python3 git

# Clone kernel source
if [ ! -d "$KERNEL_DIR" ]; then
  echo "Cloning WSL2 kernel source..."
  git clone --depth 1 --branch $KERNEL_TAG \
    https://github.com/microsoft/WSL2-Linux-Kernel.git "$KERNEL_DIR"
fi

cd "$KERNEL_DIR"

# Start from the default WSL2 config
if [ ! -f .config ]; then
  cp Microsoft/config-wsl .config
fi

# Enable USB gadget + dummy_hcd + raw_gadget + USB/IP
echo "Enabling USB gadget and USB/IP modules in kernel config..."
scripts/config --enable CONFIG_USB_GADGET
scripts/config --enable CONFIG_USB_RAW_GADGET
scripts/config --module CONFIG_USB_DUMMY_HCD
scripts/config --enable CONFIG_USB_CONFIGFS
scripts/config --enable CONFIG_USB_CONFIGFS_F_FS
scripts/config --enable CONFIG_USB_CONFIGFS_ACM
scripts/config --enable CONFIG_USB_CONFIGFS_ECM
scripts/config --enable CONFIG_USB_CONFIGFS_MASS_STORAGE

# USB/IP modules for TCP/IP transport between device and host
scripts/config --module CONFIG_USBIP_CORE
scripts/config --module CONFIG_USBIP_VHCI_HCD
scripts/config --module CONFIG_USBIP_VUDC

# Build
echo "Building kernel (this takes 10-15 minutes)..."
make -j$(nproc) KCONFIG_CONFIG=.config 2>&1 | tail -5

echo ""
echo "=== Kernel built: $KERNEL_DIR/vmlinux ==="
echo ""
echo "To use it, add to C:\\Users\\$USER\\.wslconfig:"
echo ""
echo "[wsl2]"
echo "kernel=$(echo $KERNEL_DIR/vmlinux | sed 's|/|\\\\|g')"
echo "networkingMode=mirrored"
echo "dnsTunneling=true"
echo ""
echo "Then run: wsl --shutdown"
echo "Then run: wsl -d Ubuntu-24.04"
echo "Then run: sudo modprobe dummy_hcd && ls /sys/class/udc/"
