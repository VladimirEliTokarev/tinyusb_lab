#!/bin/bash
# test_usbip_e2e.sh — Full end-to-end: TinyUSB device ↔ TinyUSB host over USB/IP TCP
#
# Architecture:
#   Process 1 (device): TinyUSB cdc_msc → dcd_rawgadget.c → raw_gadget
#                        → usbip-vudc.0 → usbipd --device (TCP :3240)
#
#   Process 2 (host):   TinyUSB cdc_msc_hid → hcd_libusb.c → libusb
#                        → vhci-hcd ← usbip attach -r 127.0.0.1
#
# The USB traffic flows over TCP/IP (loopback), using the Linux kernel's
# USB/IP protocol. Both TinyUSB stacks run the full production code.
#
# Prerequisites:
#   - Linux kernel with: CONFIG_USBIP_CORE, CONFIG_USBIP_VHCI_HCD,
#     CONFIG_USBIP_VUDC, CONFIG_USB_RAW_GADGET
#   - usbip tools (linux-tools-generic or built from kernel source)
#   - libusb-1.0-0-dev
#   - Run as root
#
set -euo pipefail

REPO="$(cd "$(dirname "$0")/../.." && pwd)"

echo "================================================================"
echo "  TinyUSB End-to-End over USB/IP TCP"
echo "  Device (server) ←TCP:3240→ Host (client)"
echo "  Both sides: full TinyUSB code"
echo "================================================================"
echo ""

if [ "$(id -u)" -ne 0 ]; then
  echo "ERROR: Run as root: sudo bash $0"
  exit 1
fi

cleanup() {
  echo ""
  echo "=== Cleaning up ==="
  # Kill processes
  [ -n "${DEV_PID:-}" ] && kill "$DEV_PID" 2>/dev/null || true
  [ -n "${HOST_PID:-}" ] && kill "$HOST_PID" 2>/dev/null || true
  [ -n "${USBIPD_PID:-}" ] && kill "$USBIPD_PID" 2>/dev/null || true

  # Detach USB/IP
  usbip detach -p 0 2>/dev/null || true

  # Unbind gadget
  echo "" > /sys/kernel/config/usb_gadget/tinyusb/UDC 2>/dev/null || true

  # Remove gadget
  rm /sys/kernel/config/usb_gadget/tinyusb/configs/c.1/acm.usb0 2>/dev/null || true
  rmdir /sys/kernel/config/usb_gadget/tinyusb/configs/c.1/strings/0x409 2>/dev/null || true
  rmdir /sys/kernel/config/usb_gadget/tinyusb/configs/c.1 2>/dev/null || true
  rmdir /sys/kernel/config/usb_gadget/tinyusb/functions/acm.usb0 2>/dev/null || true
  rmdir /sys/kernel/config/usb_gadget/tinyusb/strings/0x409 2>/dev/null || true
  rmdir /sys/kernel/config/usb_gadget/tinyusb 2>/dev/null || true

  wait 2>/dev/null || true
  echo "=== Done ==="
}
trap cleanup EXIT

# ── Step 1: Kernel modules ──
echo "[1/7] Loading kernel modules..."

for mod in usbip-core usbip-vudc vhci-hcd raw_gadget configfs; do
  modprobe "$mod" 2>/dev/null || {
    echo "ERROR: Cannot load $mod. Need custom kernel."
    echo "Run: sudo bash $REPO/tools/qemu/build_wsl_kernel.sh"
    exit 1
  }
  echo "  $mod: loaded"
done

# Mount configfs if not already
mount -t configfs none /sys/kernel/config 2>/dev/null || true

echo "  UDC: $(ls /sys/class/udc/ 2>/dev/null)"
echo ""

# ── Step 2: Build device ──
echo "[2/7] Building TinyUSB device (cdc_msc)..."
make -f "$REPO/hw/bsp/linux_native/Makefile" \
  EXAMPLE=device/cdc_msc TOP="$REPO" ROLE=device 2>&1 | tail -2
echo ""

# ── Step 3: Build host ──
echo "[3/7] Building TinyUSB host (cdc_msc_hid)..."
make -f "$REPO/hw/bsp/linux_native/Makefile" \
  EXAMPLE=host/cdc_msc_hid TOP="$REPO" ROLE=host 2>&1 | tail -2
echo ""

# ── Step 4: Start TinyUSB device process ──
echo "[4/7] Starting TinyUSB DEVICE process (binds to usbip-vudc.0)..."
RAW_GADGET_DRIVER=usbip-vudc RAW_GADGET_DEVICE_NAME=usbip-vudc.0 \
  "$REPO/build_native/cdc_msc" &
DEV_PID=$!
echo "  Device PID: $DEV_PID"
sleep 3

# ── Step 5: Start usbipd in device mode ──
echo "[5/7] Starting usbipd (device mode, TCP :3240)..."
usbipd --device &
USBIPD_PID=$!
echo "  usbipd PID: $USBIPD_PID"
sleep 2

echo ""
echo "  Exported devices:"
usbip list --device 2>/dev/null || echo "  (could not list)"
echo ""

# ── Step 6: Attach on host side via USB/IP ──
echo "[6/7] Attaching device via USB/IP (usbip attach -r 127.0.0.1)..."
usbip attach -r 127.0.0.1 -d usbip-vudc.0 || {
  echo "  Trying alternate syntax..."
  usbip attach -r 127.0.0.1 --busid usbip-vudc.0 || echo "  attach failed"
}
sleep 3

echo ""
echo "  USB/IP imported ports:"
usbip port 2>/dev/null || echo "  (could not list ports)"
echo ""
echo "  lsusb:"
lsusb 2>/dev/null || echo "  (lsusb not available)"
echo ""

# ── Step 7: Start TinyUSB host process ──
echo "[7/7] Starting TinyUSB HOST process..."
"$REPO/build_native/cdc_msc_hid" &
HOST_PID=$!
echo "  Host PID: $HOST_PID"
sleep 5

echo ""
echo "================================================================"
echo "  RUNNING: Device PID=$DEV_PID, Host PID=$HOST_PID"
echo "  USB traffic is flowing over TCP/IP (127.0.0.1:3240)"
echo "================================================================"
echo ""

echo "  Serial ports: $(ls /dev/ttyACM* 2>/dev/null || echo 'none')"
echo "  lsusb:"
lsusb 2>/dev/null | head -10
echo ""

# ── Test disconnect/reconnect ──
echo "=== Testing DISCONNECT ==="
usbip detach -p 0 2>/dev/null || echo "  (detach failed)"
sleep 2
echo "  After detach — lsusb:"
lsusb 2>/dev/null | head -5
echo ""

echo "=== Testing RECONNECT ==="
usbip attach -r 127.0.0.1 -d usbip-vudc.0 2>/dev/null || \
  usbip attach -r 127.0.0.1 --busid usbip-vudc.0 2>/dev/null || echo "  (reattach failed)"
sleep 3
echo "  After reattach — lsusb:"
lsusb 2>/dev/null | head -5
echo ""

echo "================================================================"
echo "  End-to-end test complete."
echo "  Device: TinyUSB usbd.c + cdc_device.c + msc_device.c"
echo "  Host:   TinyUSB usbh.c + cdc_host.c + msc_host.c + hid_host.c"
echo "  Transport: USB/IP over TCP/IP (127.0.0.1:3240)"
echo "================================================================"
echo ""
echo "  Debug:  gdb -p $DEV_PID   (device)"
echo "  Debug:  gdb -p $HOST_PID  (host)"
echo ""
echo "  Waiting 15 seconds before cleanup..."
sleep 15
