#!/bin/bash
# setup.sh — One-command setup for TinyUSB QEMU Lab
#
# Clones upstream TinyUSB, overlays lab files, installs packages,
# fetches dependencies, and optionally builds a custom WSL2 kernel.
#
# Usage:
#   git clone https://github.com/vladimirelitokarev/tinyusb_lab.git
#   cd tinyusb_lab
#   bash setup.sh
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TINYUSB_DIR="$SCRIPT_DIR/tinyusb"

echo "============================================"
echo " TinyUSB QEMU Lab Setup"
echo "============================================"
echo ""

# Fix WSL2 cross-filesystem git ownership check
git config --global --add safe.directory '*'

# Step 1: Clone upstream TinyUSB
if [ -d "$TINYUSB_DIR" ]; then
  echo "[1/5] TinyUSB already cloned, pulling latest..."
  cd "$TINYUSB_DIR" && git pull 2>/dev/null || true
  cd "$SCRIPT_DIR"
else
  echo "[1/5] Cloning upstream TinyUSB..."
  git clone https://github.com/hathach/tinyusb.git "$TINYUSB_DIR"
fi

# Step 2: Overlay lab files
echo "[2/5] Overlaying lab files..."
cp -r "$SCRIPT_DIR/overlay/"* "$TINYUSB_DIR/"
cp -r "$SCRIPT_DIR/scripts" "$TINYUSB_DIR/tools/qemu_lab"
cp "$SCRIPT_DIR/.vscode/launch.json" "$TINYUSB_DIR/.vscode/launch.json" 2>/dev/null || true

# Fix line endings
find "$TINYUSB_DIR/tools/qemu_lab" -type f -exec sed -i 's/\r$//' {} +
find "$TINYUSB_DIR/hw/bsp/qemu" -type f -exec sed -i 's/\r$//' {} +
find "$TINYUSB_DIR/hw/bsp/linux_native" -type f -exec sed -i 's/\r$//' {} +
find "$TINYUSB_DIR/src/portable/linux" -type f -exec sed -i 's/\r$//' {} +
find "$TINYUSB_DIR/src/portable/virtual" -type f -exec sed -i 's/\r$//' {} +

echo "  Overlay complete."

# Step 3: Install packages
echo "[3/5] Installing packages..."
sudo apt-get update -qq
sudo apt-get install -y -qq \
  qemu-system-arm gcc-arm-none-eabi gdb-multiarch \
  cmake ninja-build python3 make git \
  libusb-1.0-0-dev usbutils \
  2>/dev/null || echo "  (some packages may need manual install)"

# Step 4: Fetch TinyUSB dependencies
echo "[4/5] Fetching TinyUSB dependencies..."
cd "$TINYUSB_DIR"
python3 tools/get_deps.py broadcom_32bit 2>/dev/null || true

# Step 5: Add BCM2835 to host example whitelist
if ! grep -q "BCM2835" examples/host/cdc_msc_hid/only.txt 2>/dev/null; then
  echo "mcu:BCM2835" >> examples/host/cdc_msc_hid/only.txt
fi

echo ""
echo "============================================"
echo " Setup complete!"
echo "============================================"
echo ""
echo "The lab is ready at: $TINYUSB_DIR"
echo ""
echo "Quick start (QEMU mode):"
echo "  cd $TINYUSB_DIR"
echo "  # Build device firmware"
echo "  cd examples/device/cdc_msc && mkdir -p build && cd build"
echo "  cmake -DBOARD=qemu_raspi0_device -G Ninja -DCMAKE_BUILD_TYPE=Debug .."
echo "  cmake --build ."
echo ""
echo "  # Build host firmware"
echo "  cd $TINYUSB_DIR/examples/host/cdc_msc_hid && mkdir -p build && cd build"
echo "  cmake -DBOARD=qemu_raspi0_host -G Ninja -DCMAKE_BUILD_TYPE=Debug .."
echo "  cmake --build ."
echo ""
echo "  # Run QEMU lab"
echo "  bash $TINYUSB_DIR/tools/qemu_lab/test_lab.sh"
echo ""
echo "Native USB mode (requires custom kernel):"
echo "  sudo bash $TINYUSB_DIR/tools/qemu_lab/build_wsl_kernel.sh"
echo "  # Then update .wslconfig and restart WSL"
echo "  sudo bash $TINYUSB_DIR/tools/qemu_lab/run_real_e2e.sh"
