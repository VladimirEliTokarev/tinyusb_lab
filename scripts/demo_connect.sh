#!/bin/bash
# demo_connect.sh — Demonstrate USB connect/disconnect/reset cycle
# Shows the broker sending ATTACH/DETACH/RESET to the device stack
# and the device stack processing them through dcd_vusb.c -> usbd.c
set -e

REPO="$HOME/tinyusb"
DEVICE_ELF="$REPO/examples/device/cdc_msc/build/cdc_msc.elf"

rm -f /tmp/vusb-dev.sock /tmp/vusb-ctrl.sock /tmp/vusb.pcap

echo "========================================"
echo " TinyUSB QEMU Lab: Connect/Disconnect"
echo "========================================"
echo ""

# Start broker (filter out SOF spam for readability)
echo "[1] Starting vusbd broker..."
python3 "$REPO/tools/qemu/vusbd.py" \
  --device-sock /tmp/vusb-dev.sock \
  --ctrl-sock /tmp/vusb-ctrl.sock \
  --pcap /tmp/vusb.pcap -v 2>&1 | grep -v "SOF" &
BROKER=$!
sleep 1

# Start device QEMU (not paused — for demo purposes)
echo "[2] Starting device QEMU..."
qemu-system-arm -M raspi0 \
  -kernel "$DEVICE_ELF" \
  -serial unix:/tmp/vusb-dev.sock,server=off \
  -serial mon:stdio \
  -nographic -d guest_errors &
DEV=$!
sleep 2

echo ""
echo "========================================"
echo " Device is running and connected."
echo " The broker auto-sent ATTACH on connect."
echo " Now demonstrating manual control..."
echo "========================================"
echo ""

# Detach = simulate unplugging the USB cable
echo ">>> vusbctl detach  (simulate USB cable unplug)"
python3 "$REPO/tools/qemu/vusbctl.py" detach
sleep 1

echo ""

# Attach = simulate plugging the USB cable back in
echo ">>> vusbctl attach  (simulate USB cable plug-in)"
python3 "$REPO/tools/qemu/vusbctl.py" attach
sleep 1

echo ""

# Reset = USB bus reset (host tells device to reset)
echo ">>> vusbctl reset  (USB bus reset)"
python3 "$REPO/tools/qemu/vusbctl.py" reset
sleep 1

echo ""

# Status check
echo ">>> vusbctl status"
python3 "$REPO/tools/qemu/vusbctl.py" status

echo ""
echo "========================================"
echo " Results"
echo "========================================"
if [ -f /tmp/vusb.pcap ]; then
  PCAP_SIZE=$(stat -c%s /tmp/vusb.pcap)
  echo "PCAP: /tmp/vusb.pcap ($PCAP_SIZE bytes)"
  echo "  Contains: ATTACH -> SOFs -> DETACH -> ATTACH -> RESET"
  echo "  Open in Wireshark to see the full frame sequence"
fi

echo ""
echo "========================================"
echo " How to debug both ends with GDB"
echo "========================================"
echo ""
echo "Terminal 1 — start the lab paused:"
echo "  python3 ~/tinyusb/tools/qemu/run_lab.py \\"
echo "    --device-elf $DEVICE_ELF \\"
echo "    --host-elf ~/tinyusb/examples/host/cdc_msc_hid/build/cdc_msc_hid.elf \\"
echo "    --pcap /tmp/vusb.pcap -v"
echo ""
echo "Terminal 2 — attach GDB to device stack:"
echo "  gdb-multiarch $DEVICE_ELF -ex 'target remote :1234'"
echo "  (gdb) break dcd_event_bus_reset"
echo "  (gdb) break dcd_event_setup_received"
echo "  (gdb) break tud_mount_cb"
echo "  (gdb) continue"
echo ""
echo "Terminal 3 — attach GDB to host stack:"
echo "  gdb-multiarch ~/tinyusb/examples/host/cdc_msc_hid/build/cdc_msc_hid.elf -ex 'target remote :1235'"
echo "  (gdb) break hcd_event_handler"
echo "  (gdb) break tuh_mount_cb"
echo "  (gdb) continue"
echo ""
echo "Terminal 4 — trigger events:"
echo "  python3 ~/tinyusb/tools/qemu/vusbctl.py attach"
echo "  python3 ~/tinyusb/tools/qemu/vusbctl.py detach"
echo "  python3 ~/tinyusb/tools/qemu/vusbctl.py reset"
echo "  python3 ~/tinyusb/tools/qemu/vusbctl.py corrupt   # fault inject"
echo ""

# Cleanup
kill $DEV $BROKER 2>/dev/null
wait 2>/dev/null
echo "Demo complete."
