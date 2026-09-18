# USB Attack Surface in TinyUSB

## Two threat models

### 1. Malicious USB Host attacks a TinyUSB Device

The device receives untrusted data from the host via:

- **SETUP packets** (8 bytes) — `bmRequestType`, `bRequest`, `wValue`, `wIndex`, `wLength`
- **Control OUT data** — `wLength` bytes following a SETUP, written to class driver buffers
- **Bulk OUT data** — class-specific: RNDIS packets, NCM NTBs, MSC CBW commands
- **Interrupt OUT data** — HID SET_REPORT

Key insight: **`wLength` is the primary attack vector** on the device side. Multiple
class drivers (HID, DFU, BTH, RNDIS) passed host-supplied `wLength` directly to
buffer operations without bounds checking.

### 2. Malicious USB Device attacks a TinyUSB Host

The host receives untrusted data from the device via:

- **Device/Config/String descriptors** — parsed during enumeration
- **HID report descriptors** — complex nested structure parsed by `tuh_hid_parse_report_descriptor()`
- **Hub status responses** — hub port status affecting device addressing
- **MSC SCSI responses** — CBW/CSW with LBA, block count, capacity
- **Bulk IN data** — class-specific responses

Key insight: **Descriptor parsing is the primary attack surface** on the host side.
A malicious device controls every byte of its descriptors and can craft them to
trigger OOB reads, integer overflows, and infinite loops in the host's parser.

## High-value targets by primitive

### Write primitives (most dangerous)

| Vuln | Primitive | Bytes | Target |
|------|-----------|-------|--------|
| ep2drv-bind-heap-overflow | Heap write | 1-15 bytes | ep2drv[] array past allocation |
| ep-status-global-overflow | Global write | 1-15 bytes | _usbd_spin, _usbd_queued_setup |
| fifo-memory-overflow | Heap/global write | Variable | Adjacent FIFO data structures |
| ncm-xmit-oob-write | Global write | Variable | NTB transmit buffer overflow |

### Read primitives (information disclosure)

| Vuln | Primitive | Bytes | Source |
|------|-----------|-------|--------|
| rndis-dataoffset-integer-overflow | Heap read | 2502+ bytes | Past RNDIS rx buffer |
| ncm-ndp16-wlength-oob-read | Heap read | ~65 KB | Past NCM receive buffer |
| hid-host-parse-report-heap-overflow | Heap read | Variable | Past HID report buffer |
| hid-control-xfer-oob-read | Global read | Variable | Past HID/DFU/BTH buffers |

### DoS (denial of service)

| Vuln | Effect |
|------|--------|
| descriptor-walk-oob-infinite-loop | Permanent hang (bLength=0) |
| ncm-stack-overflow-recursion | Stack overflow crash |
| host-descriptor-parser-hang | Enumeration hangs forever |
| dhcp-server-null-deref | Crash on first DHCP packet |
