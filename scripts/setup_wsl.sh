#!/bin/bash
# setup_wsl.sh - Set up WSL2 Ubuntu environment for TinyUSB QEMU Lab
#
# Run inside WSL2 Ubuntu:
#   bash /mnt/d/work/Research/tinyusb/tools/qemu/setup_wsl.sh
#
set -euo pipefail

echo "=== TinyUSB QEMU Lab - WSL2 Setup ==="

# Install packages
echo "[1/5] Installing packages..."
sudo apt-get update
sudo apt-get install -y \
    qemu-system-arm \
    gcc-arm-none-eabi \
    gdb-multiarch \
    cmake \
    ninja-build \
    python3 \
    python3-pip \
    linux-tools-generic \
    git \
    make

# Try installing usbredirserver (may not be available on all Ubuntu versions)
sudo apt-get install -y usbredirserver 2>/dev/null || \
    echo "WARNING: usbredirserver not available, usb-redir backend won't work"

# Symlink repo
echo "[2/5] Setting up repo symlink..."
REPO_PATH="/mnt/d/work/Research/tinyusb"
LINK_PATH="$HOME/tinyusb"

if [ -L "$LINK_PATH" ]; then
    echo "  Symlink already exists: $LINK_PATH"
elif [ -e "$LINK_PATH" ]; then
    echo "  WARNING: $LINK_PATH exists but is not a symlink, skipping"
else
    ln -s "$REPO_PATH" "$LINK_PATH"
    echo "  Created: $LINK_PATH -> $REPO_PATH"
fi

# Fetch TinyUSB dependencies
echo "[3/5] Fetching TinyUSB dependencies..."
cd "$LINK_PATH"
python3 tools/get_deps.py broadcom_32bit

# Verify QEMU
echo "[4/5] Verifying QEMU..."
qemu-system-arm --version | head -1

# Check vhci_hcd kernel module
echo "[5/5] Checking vhci_hcd kernel module..."
if sudo modprobe vhci-hcd 2>/dev/null; then
    echo "  vhci_hcd loaded successfully"
    echo "  USB/IP backend will work"
else
    echo "  WARNING: vhci_hcd not available in this WSL2 kernel"
    echo "  USB/IP backend won't work (usb-redir still available)"
fi

echo ""
echo "=== Setup complete! ==="
echo ""
echo "Quick start:"
echo "  cd ~/tinyusb"
echo ""
echo "Build device firmware (CDC/MSC example):"
echo "  cd examples/device/cdc_msc"
echo "  mkdir -p build && cd build"
echo "  cmake -DBOARD=qemu_raspi0_device -G Ninja -DCMAKE_BUILD_TYPE=Debug .."
echo "  cmake --build ."
echo ""
echo "Build host firmware:"
echo "  cd examples/host/cdc_msc_hid"
echo "  mkdir -p build && cd build"
echo "  cmake -DBOARD=qemu_raspi0_host -G Ninja -DCMAKE_BUILD_TYPE=Debug .."
echo "  cmake --build ."
echo ""
echo "Run the lab:"
echo "  python3 tools/qemu/run_lab.py \\"
echo "    --device-elf examples/device/cdc_msc/build/cdc_msc.elf \\"
echo "    --host-elf examples/host/cdc_msc_hid/build/cdc_msc_hid.elf \\"
echo "    --pcap /tmp/vusb.pcap -v"
