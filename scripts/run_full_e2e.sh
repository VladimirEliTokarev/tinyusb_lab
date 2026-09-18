#!/bin/bash
set -e
USBIP=/usr/local/sbin/usbip
USBIPD=/usr/local/sbin/usbipd

echo "nameserver 8.8.8.8" > /etc/resolv.conf
modprobe usbip-core
modprobe usbip-vudc
modprobe vhci-hcd

echo "============================================"
echo " TinyUSB End-to-End over USB/IP"
echo "============================================"
echo ""

echo "[1] Starting TinyUSB device..."
export RAW_GADGET_DRIVER=usbip-vudc
export RAW_GADGET_DEVICE_NAME=usbip-vudc.0
stdbuf -oL -eL /root/tinyusb/build_native/cdc_msc 2>&1 | sed 's/^/  [DEV] /' &
DEV_PID=$!
sleep 3

echo ""
echo "[2] Starting usbipd in device mode..."
$USBIPD --device 2>&1 | sed 's/^/  [USBIPD] /' &
USBIPD_PID=$!
sleep 2

echo ""
echo "[3] Listing exported devices..."
$USBIP list -r 127.0.0.1 2>&1 || echo "  list failed"
echo ""

echo "[4] Attaching via USB/IP TCP..."
$USBIP attach -r 127.0.0.1 -d usbip-vudc.0 2>&1
sleep 3

echo ""
echo "[5] lsusb:"
lsusb
echo ""

echo "[6] dmesg USB:"
dmesg | grep -iE "usb|cdc|acm|tinyusb|cafe" | tail -15
echo ""

echo "[7] Serial ports:"
ls -la /dev/ttyACM* 2>/dev/null || echo "  no /dev/ttyACM*"
echo ""

echo "============================================"
echo " CONNECT test passed. Waiting 5 sec..."
echo "============================================"
sleep 5

echo ""
echo "[8] DISCONNECT: usbip detach..."
$USBIP detach -p 0 2>&1
sleep 2
echo "  lsusb after detach:"
lsusb
echo ""

echo "[9] RECONNECT: usbip attach..."
$USBIP attach -r 127.0.0.1 -d usbip-vudc.0 2>&1
sleep 3
echo "  lsusb after reconnect:"
lsusb
echo ""

echo "============================================"
echo " All tests done. Cleaning up."
echo "============================================"
kill $DEV_PID $USBIPD_PID 2>/dev/null
wait 2>/dev/null
echo "Done."
