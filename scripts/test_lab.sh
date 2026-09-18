#!/bin/bash
# Quick test of the QEMU lab
set -e

REPO="$HOME/tinyusb"
DEVICE_ELF="$REPO/examples/device/cdc_msc/build/cdc_msc.elf"
HOST_ELF="$REPO/examples/host/cdc_msc_hid/build/cdc_msc_hid.elf"
DEV_SOCK=/tmp/vusb-dev.sock
CTRL_SOCK=/tmp/vusb-ctrl.sock
PCAP=/tmp/vusb.pcap

rm -f "$DEV_SOCK" "$CTRL_SOCK" "$PCAP"

echo "=== Starting vusbd broker ==="
python3 "$REPO/tools/qemu/vusbd.py" \
  --device-sock "$DEV_SOCK" \
  --ctrl-sock "$CTRL_SOCK" \
  --pcap "$PCAP" -v &
BROKER_PID=$!
sleep 1

echo "=== Starting device QEMU ==="
timeout 8 qemu-system-arm -M raspi0 \
  -kernel "$DEVICE_ELF" \
  -serial "unix:$DEV_SOCK,server=off" \
  -serial mon:stdio \
  -nographic -d guest_errors 2>&1 &
QEMU_DEV_PID=$!

echo "=== Starting host QEMU ==="
timeout 8 qemu-system-arm -M raspi0 \
  -kernel "$HOST_ELF" \
  -serial null \
  -serial mon:stdio \
  -device usb-kbd \
  -nographic -d guest_errors 2>&1 &
QEMU_HOST_PID=$!

echo "=== Waiting 6 seconds for stack activity ==="
sleep 6

echo ""
echo "=== Results ==="
if [ -f "$PCAP" ]; then
  PCAP_SIZE=$(stat -c%s "$PCAP" 2>/dev/null || echo 0)
  echo "PCAP file: $PCAP ($PCAP_SIZE bytes)"
else
  echo "PCAP file: not created"
fi

echo ""
echo "=== Cleaning up ==="
kill $BROKER_PID 2>/dev/null || true
kill $QEMU_DEV_PID 2>/dev/null || true
kill $QEMU_HOST_PID 2>/dev/null || true
wait 2>/dev/null || true

echo "=== Done ==="
