#!/bin/bash
echo "=== Loading modules ==="
modprobe usbip-core && echo "usbip-core: OK" || echo "usbip-core: FAIL"
modprobe usbip-vudc && echo "usbip-vudc: OK" || echo "usbip-vudc: FAIL"
modprobe vhci-hcd && echo "vhci-hcd: OK" || echo "vhci-hcd: FAIL"
modprobe dummy_hcd && echo "dummy_hcd: OK" || echo "dummy_hcd: FAIL or built-in"
echo ""
echo "=== UDC devices ==="
ls /sys/class/udc/
echo ""
echo "=== /dev/raw-gadget ==="
ls -la /dev/raw-gadget
echo ""
echo "=== Loaded modules ==="
lsmod
echo ""
echo "=== Ready for TinyUSB test ==="
