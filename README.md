# TinyUSB Full-Stack Lab

Debug the entire TinyUSB USB stack — device and host — without any physical
hardware. Both sides run as real TinyUSB code with real USB transactions
flowing through the Linux kernel.

## Two modes

### Mode 1: QEMU (bare-metal ARM, emulated DWC2 USB controller)

Two QEMU `raspi0` instances running TinyUSB bare-metal firmware, connected
by a Python broker over Unix sockets. The host side drives QEMU's emulated
DWC2 MMIO registers with production `hcd_dwc2.c`.

### Mode 2: Native Linux (real USB via dummy_hcd + Raw Gadget)

Two native x86 Linux processes on the same machine. The device process uses
Raw Gadget (`/dev/raw-gadget`) to present a TinyUSB device to the kernel's
`dummy_hcd` virtual USB controller. The host process uses libusb to enumerate
and talk to the device. Real USB transactions flow through the kernel's USB
core — the device appears in `lsusb`.

## Quick start

```bash
# Clone this repo
git clone https://github.com/vladimirelitokarev/tinyusb_lab.git
cd tinyusb_lab

# Run setup (clones upstream TinyUSB, installs packages, overlays lab files)
bash setup.sh

# Run QEMU mode test
cd tinyusb
bash tools/qemu_lab/test_lab.sh
```

## Architecture

### QEMU mode

```
QEMU A (device):                    QEMU B (host):
  TinyUSB usbd.c                      TinyUSB usbh.c
  dcd_vusb.c (virtual DCD)            hcd_dwc2.c (real emulated HW)
  PL011 UART                          QEMU DWC2 model
       |                                   |
       +------ vusbd.py broker -----------+
               (Unix socket, PCAP tap,
                fault injection, vusbctl)
```

### Native Linux mode

```
Process 1 (device):                 Process 2 (host):
  TinyUSB usbd.c                      TinyUSB usbh.c
  dcd_rawgadget.c                      hcd_libusb.c
       |                                   |
  /dev/raw-gadget                      libusb-1.0
       |                                   |
  -----+---- Linux kernel ----------------+
  raw_gadget -> dummy_udc <--USB--> dummy_hcd
```

## What's in this repo

```
tinyusb_lab/
  README.md              # This file
  setup.sh               # One-command setup
  .gitignore

  overlay/               # Files overlaid onto upstream TinyUSB
    hw/bsp/qemu/         # QEMU raspi0 BSP (device + host boards)
    hw/bsp/linux_native/ # Native x86 Linux BSP + Makefile
    src/portable/virtual/ # Virtual DCD (QEMU broker transport)
    src/portable/linux/   # Raw Gadget DCD + libusb HCD
    src/portable/synopsys/dwc2/dwc2_bcm.h  # Patched for host mode

  scripts/               # Lab scripts
    vusbd.py             # QEMU broker daemon
    vusbctl.py           # Control CLI (attach/detach/corrupt/replay)
    run_lab.py           # QEMU dual-instance launcher
    build_wsl_kernel.sh  # Custom WSL2 kernel with dummy_hcd + raw_gadget
    test_lab.sh          # QEMU quick test
    run_real_e2e.sh      # Native USB end-to-end test
    demo_connect.sh      # Connect/disconnect demo

  .vscode/launch.json    # Dual-GDB compound debug config
```

## Prerequisites

- **WSL2 Ubuntu 24.04** (or any Linux with QEMU and gcc-arm-none-eabi)
- For native USB mode: custom kernel with `CONFIG_USB_DUMMY_HCD` and
  `CONFIG_USB_RAW_GADGET` (the `build_wsl_kernel.sh` script builds this)

## Debugging

### QEMU mode (GDB on ARM firmware)

```bash
# Start lab paused for GDB
python3 tools/qemu_lab/run_lab.py \
  --device-elf examples/device/cdc_msc/build/cdc_msc.elf \
  --host-elf examples/host/cdc_msc_hid/build/cdc_msc_hid.elf \
  --pcap /tmp/vusb.pcap -v

# Attach GDB
gdb-multiarch examples/device/cdc_msc/build/cdc_msc.elf -ex 'target remote :1234'
gdb-multiarch examples/host/cdc_msc_hid/build/cdc_msc_hid.elf -ex 'target remote :1235'
```

### Native mode (GDB on x86 processes)

```bash
sudo bash tools/qemu_lab/run_real_e2e.sh
# Then: gdb -p <device_pid>
# And:  gdb -p <host_pid>
```

### Wireshark

QEMU mode writes a PCAP file (`--pcap /tmp/vusb.pcap`). Native mode can use
`usbmon` for kernel-level USB tracing.

## Control the virtual USB cable

```bash
python3 tools/qemu_lab/vusbctl.py attach     # plug in
python3 tools/qemu_lab/vusbctl.py detach     # unplug
python3 tools/qemu_lab/vusbctl.py reset      # bus reset
python3 tools/qemu_lab/vusbctl.py corrupt    # corrupt next frame
python3 tools/qemu_lab/vusbctl.py truncate   # truncate next frame
python3 tools/qemu_lab/vusbctl.py stall 0x81 # force STALL on EP1 IN
```

## License

Lab-specific code is MIT licensed. Upstream TinyUSB is MIT licensed.
See [tinyusb/LICENSE](https://github.com/hathach/tinyusb/blob/master/LICENSE).
