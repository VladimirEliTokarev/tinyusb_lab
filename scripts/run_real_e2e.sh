#!/bin/bash
set -e
modprobe dummy_hcd 2>/dev/null || true
modprobe raw_gadget 2>/dev/null || true

echo "============================================"
echo " TinyUSB REAL USB Test (dummy_hcd)"
echo "============================================"
echo ""
echo "UDC: $(ls /sys/class/udc/)"
echo "/dev/raw-gadget: $(test -c /dev/raw-gadget && echo OK || echo MISSING)"
echo ""

echo "[1] Starting TinyUSB device (cdc_msc)..."
export RAW_GADGET_DRIVER=dummy_udc
export RAW_GADGET_DEVICE_NAME=dummy_udc.0
stdbuf -oL -eL /root/tinyusb/build_native/cdc_msc 2>&1 | sed 's/^/  [DEV] /' &
DEV_PID=$!
sleep 4

echo ""
echo "[2] lsusb after device start:"
lsusb
echo ""

echo "[3] dmesg USB:"
dmesg | tail -20
echo ""

echo "[4] /dev/ttyACM*:"
ls -la /dev/ttyACM* 2>/dev/null || echo "  none"
echo ""

echo "============================================"
echo " DISCONNECT: killing device process..."
echo "============================================"
kill $DEV_PID 2>/dev/null
wait $DEV_PID 2>/dev/null || true
sleep 2

echo ""
echo "[5] lsusb after disconnect:"
lsusb
echo ""

echo "============================================"
echo " RECONNECT: restarting device..."
echo "============================================"
stdbuf -oL -eL /root/tinyusb/build_native/cdc_msc 2>&1 | sed 's/^/  [DEV] /' &
DEV_PID=$!
sleep 4

echo ""
echo "[6] lsusb after reconnect:"
lsusb
echo ""

echo "[7] dmesg last lines:"
dmesg | tail -10
echo ""

echo "============================================"
echo " Test complete."
echo "============================================"
kill $DEV_PID 2>/dev/null
wait $DEV_PID 2>/dev/null || true
echo "Done."
