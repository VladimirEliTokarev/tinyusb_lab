#!/bin/bash
# Single connect/disconnect with full frame logging
REPO="$HOME/tinyusb"
DEVICE_ELF="$REPO/examples/device/cdc_msc/build/cdc_msc.elf"

rm -f /tmp/vusb-dev.sock /tmp/vusb-ctrl.sock /tmp/vusb.pcap /tmp/broker.log

echo "=== Starting broker with full frame log ==="
python3 "$REPO/tools/qemu/vusbd.py" \
  --device-sock /tmp/vusb-dev.sock \
  --ctrl-sock /tmp/vusb-ctrl.sock \
  --pcap /tmp/vusb.pcap -v > /tmp/broker.log 2>&1 &
BROKER=$!
sleep 1

echo "=== Starting device QEMU ==="
qemu-system-arm -M raspi0 \
  -kernel "$DEVICE_ELF" \
  -serial unix:/tmp/vusb-dev.sock,server=off \
  -serial mon:stdio \
  -nographic -d guest_errors > /dev/null 2>&1 &
DEV=$!
sleep 3

echo ""
echo "========== STEP 1: ATTACH (auto, on connect) =========="
echo "Broker auto-sent ATTACH when device QEMU connected."
echo ""

sleep 2

echo "========== STEP 2: DETACH (manual unplug) =========="
python3 "$REPO/tools/qemu/vusbctl.py" detach
sleep 2

echo ""
echo "========== STEP 3: ATTACH (manual plug-in) =========="
python3 "$REPO/tools/qemu/vusbctl.py" attach
sleep 2

echo ""
echo "========== STEP 4: RESET (bus reset) =========="
python3 "$REPO/tools/qemu/vusbctl.py" reset
sleep 2

echo ""
echo "========== STEP 5: DETACH (final unplug) =========="
python3 "$REPO/tools/qemu/vusbctl.py" detach
sleep 1

# Kill processes
kill $DEV $BROKER 2>/dev/null
wait 2>/dev/null

echo ""
echo "============================================="
echo " BROKER LOG (all frames that went over the wire)"
echo "============================================="
grep -E "ATTACH|DETACH|RESET|SETUP|DATA|STALL|ACK|NAK|device_connected|Device guest" /tmp/broker.log
echo ""
echo "============================================="
echo " PCAP file"
echo "============================================="
ls -la /tmp/vusb.pcap
echo ""
echo "============================================="
echo " Raw PCAP hex (first 200 bytes)"
echo "============================================="
xxd /tmp/vusb.pcap | head -20
echo ""
echo "Done."
