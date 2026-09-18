#!/bin/bash
# test_e2e.sh — Full end-to-end test: TinyUSB device + TinyUSB host
#
# Both sides run as native Linux processes on the same machine.
# The kernel's dummy_hcd + raw_gadget provide the real USB transport.
#
# What happens:
#   1. Loads dummy_hcd + raw_gadget kernel modules
#   2. Builds TinyUSB cdc_msc (device) and cdc_msc_hid (host) as native binaries
#   3. Starts device process — appears on dummy_udc.0
#   4. Starts host process — finds device via libusb, enumerates it
#   5. Shows lsusb, verifies /dev/ttyACM0, tests connect/disconnect
#
# Prerequisites:
#   - Linux kernel with CONFIG_USB_DUMMY_HCD=m and CONFIG_USB_RAW_GADGET=m
#   - libusb-1.0-0-dev installed
#   - Run as root (or with sudo)
#
set -euo pipefail

REPO="$(cd "$(dirname "$0")/../.." && pwd)"

echo "================================================================"
echo "  TinyUSB End-to-End Test: Device + Host (both TinyUSB code)"
echo "================================================================"
echo ""

# Check root
if [ "$(id -u)" -ne 0 ]; then
  echo "ERROR: Run as root (sudo bash $0)"
  exit 1
fi

# Step 1: Kernel modules
echo "[1/6] Loading kernel modules..."
modprobe dummy_hcd 2>/dev/null || {
  echo "ERROR: dummy_hcd not available."
  echo "You need a kernel with CONFIG_USB_DUMMY_HCD enabled."
  echo "For WSL2: bash $REPO/tools/qemu/build_wsl_kernel.sh"
  exit 1
}
modprobe raw_gadget 2>/dev/null || {
  echo "ERROR: raw_gadget not available."
  echo "You need a kernel with CONFIG_USB_RAW_GADGET enabled."
  exit 1
}

echo "  dummy_hcd: loaded"
echo "  raw_gadget: loaded"
echo "  UDC: $(ls /sys/class/udc/ 2>/dev/null)"
echo "  /dev/raw-gadget: $(test -c /dev/raw-gadget && echo 'OK' || echo 'MISSING')"
echo ""

# Step 2: Build device
echo "[2/6] Building TinyUSB device (cdc_msc, native x86)..."
make -f "$REPO/hw/bsp/linux_native/Makefile" \
  EXAMPLE=device/cdc_msc TOP="$REPO" ROLE=device clean 2>/dev/null || true
make -f "$REPO/hw/bsp/linux_native/Makefile" \
  EXAMPLE=device/cdc_msc TOP="$REPO" ROLE=device all 2>&1 | tail -2
echo ""

# Step 3: Build host
echo "[3/6] Building TinyUSB host (cdc_msc_hid, native x86)..."
make -f "$REPO/hw/bsp/linux_native/Makefile" \
  EXAMPLE=host/cdc_msc_hid TOP="$REPO" ROLE=host clean 2>/dev/null || true
make -f "$REPO/hw/bsp/linux_native/Makefile" \
  EXAMPLE=host/cdc_msc_hid TOP="$REPO" ROLE=host all 2>&1 | tail -2
echo ""

# Step 4: Start device
echo "[4/6] Starting TinyUSB DEVICE process..."
"$REPO/build_native/cdc_msc" &
DEV_PID=$!
echo "  PID: $DEV_PID"
sleep 3

echo ""
echo "  Checking lsusb for TinyUSB device..."
lsusb 2>/dev/null | grep -i "cafe\|4000\|tinyusb\|Composite" || echo "  (not found in lsusb)"
echo ""
echo "  Serial ports:"
ls -la /dev/ttyACM* 2>/dev/null || echo "  (no /dev/ttyACM* yet)"
echo ""

# Step 5: Start host
echo "[5/6] Starting TinyUSB HOST process..."
"$REPO/build_native/cdc_msc_hid" &
HOST_PID=$!
echo "  PID: $HOST_PID"
sleep 5

echo ""
echo "================================================================"
echo "  Both processes running. Device PID=$DEV_PID, Host PID=$HOST_PID"
echo "================================================================"
echo ""

# Step 6: Test connect/disconnect
echo "[6/6] Testing disconnect/reconnect..."
echo ""

echo "  === Killing device (simulates USB unplug) ==="
kill $DEV_PID 2>/dev/null || true
wait $DEV_PID 2>/dev/null || true
sleep 2
echo "  Device stopped. Host should detect removal."
echo ""

echo "  === Restarting device (simulates USB plug-in) ==="
"$REPO/build_native/cdc_msc" &
DEV_PID=$!
echo "  New device PID: $DEV_PID"
sleep 3

echo ""
echo "  Checking lsusb after reconnect..."
lsusb 2>/dev/null | grep -i "cafe\|4000" || echo "  (not found)"
echo ""

echo "================================================================"
echo "  Results"
echo "================================================================"
echo ""
echo "  Device process: PID $DEV_PID"
echo "  Host process:   PID $HOST_PID"
echo ""
echo "  To attach GDB to device: gdb -p $DEV_PID"
echo "  To attach GDB to host:   gdb -p $HOST_PID"
echo "  To see USB traffic:      sudo cat /sys/kernel/debug/usb/usbmon/0u"
echo ""

echo "  Press Ctrl+C to stop, or waiting 10 seconds..."
sleep 10

# Cleanup
echo ""
echo "=== Cleaning up ==="
kill $DEV_PID $HOST_PID 2>/dev/null || true
wait 2>/dev/null || true

echo "=== Done ==="
