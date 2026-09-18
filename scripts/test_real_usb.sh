#!/bin/bash
# test_real_usb.sh — Build and run TinyUSB CDC/MSC on real USB via Raw Gadget
#
# Prerequisites:
#   - Custom WSL2 kernel with CONFIG_USB_DUMMY_HCD and CONFIG_USB_RAW_GADGET
#   - OR a real Linux machine / VM with these modules
#
# What happens:
#   1. Loads dummy_hcd + raw_gadget kernel modules
#   2. Builds TinyUSB cdc_msc example as a native Linux binary
#   3. Runs it — the device appears in lsusb and gets /dev/ttyACM0
#   4. Verifies with lsusb
#
set -euo pipefail

REPO="$(cd "$(dirname "$0")/../.." && pwd)"

echo "========================================"
echo " TinyUSB Real USB Test (Raw Gadget)"
echo "========================================"
echo ""

# Step 1: Check/load kernel modules
echo "[1/4] Checking kernel modules..."
if ! lsmod | grep -q dummy_hcd; then
  echo "  Loading dummy_hcd..."
  sudo modprobe dummy_hcd || {
    echo "ERROR: dummy_hcd not available in this kernel."
    echo "Run: bash $REPO/tools/qemu/build_wsl_kernel.sh"
    exit 1
  }
fi

if ! lsmod | grep -q raw_gadget; then
  echo "  Loading raw_gadget..."
  sudo modprobe raw_gadget || {
    echo "ERROR: raw_gadget not available in this kernel."
    echo "Run: bash $REPO/tools/qemu/build_wsl_kernel.sh"
    exit 1
  }
fi

echo "  dummy_hcd: $(lsmod | grep dummy_hcd | awk '{print $1}')"
echo "  raw_gadget: $(lsmod | grep raw_gadget | awk '{print $1}')"
echo "  UDC: $(ls /sys/class/udc/ 2>/dev/null || echo 'none')"
echo "  /dev/raw-gadget: $(ls -la /dev/raw-gadget 2>/dev/null || echo 'not found')"
echo ""

# Step 2: Build native
echo "[2/4] Building TinyUSB cdc_msc (native x86)..."
cd "$REPO"
make -f hw/bsp/linux_native/Makefile EXAMPLE=device/cdc_msc TOP="$REPO" clean 2>/dev/null || true
make -f hw/bsp/linux_native/Makefile EXAMPLE=device/cdc_msc TOP="$REPO" all
echo ""

# Step 3: Run
echo "[3/4] Starting TinyUSB device (runs for 10 seconds)..."
echo "  You should see the device appear in lsusb."
echo ""
sudo timeout 10 "$REPO/build_native/cdc_msc" &
PID=$!
sleep 3

# Step 4: Check lsusb
echo ""
echo "[4/4] Checking lsusb..."
lsusb 2>/dev/null || echo "  lsusb not available"
echo ""
echo "Looking for TinyUSB device..."
lsusb 2>/dev/null | grep -i "cafe\|tinyusb\|cdc\|composite" || echo "  (not found yet — may need more time)"
echo ""

# Check for /dev/ttyACM*
echo "Serial ports:"
ls -la /dev/ttyACM* 2>/dev/null || echo "  No /dev/ttyACM* found"
echo ""

# Wait and cleanup
wait $PID 2>/dev/null || true
echo "========================================"
echo " Test complete."
echo "========================================"
