# TinyUSB Vulnerability Overview

## Summary

This wiki documents **27 memory-safety vulnerabilities** found in TinyUSB's
portable USB stack through a combination of manual code review, fuzzing
(OSS-Fuzz, student harnesses), and targeted security research.

### Key statistics

- **6 real** (CVE-worthy, exploitable in supported configurations)
- **1 plausible** (requires specific config but reachable)
- **20 weak** (hardening issues, dead code paths, or require unusual setup)
- **15 fixed** upstream, **12 still open**
- **10 have PoC code** (students/waynelow/ directory)

### Most critical findings

1. **ep2drv bind heap overflow** (WAYNELOW-2) — `ep_num` from `bEndpointAddress & 0x0F` indexes an array sized to 8 but can be 0-15. Reachable from both device and host stacks via malformed descriptors. **Write primitive.**

2. **ep_status global overflow** (WAYNELOW-3) — Same root cause in `tud_task_ext()`. Endpoint number 8-15 corrupts `_usbd_spin` and `_usbd_queued_setup` globals. **Write primitive, may enable code execution.**

3. **HID host parse_report heap overflow** (BUG-004) — `uint16_t` underflow in remaining-length calculation causes heap OOB read during HID report descriptor parsing. **Read primitive.**

4. **RNDIS integer overflow** (PR #3756) — `DataOffset + DataLength` wraps `uint32_t` on 32-bit targets, bypassing bounds check. Reads 2502+ bytes past 1602-byte buffer. **Large read primitive.** Fixed.

5. **NCM NDP16 wLength OOB** (PR #3741) — Uncapped `wLength` field causes ~65KB read past 3200-byte buffer. **Massive read primitive.** Fixed.

## Attack surface

TinyUSB processes untrusted data from USB peers at multiple layers:

```
USB Host (attacker)                    USB Device (attacker)
     |                                       |
     v                                       v
 SETUP packets (8 bytes)              Descriptors (device/config/string)
 Control data (wLength bytes)         HID report descriptors
 Bulk OUT data (class-specific)       Hub status responses
 Interrupt data                       MSC SCSI responses (CBW/CSW)
     |                                       |
     v                                       v
 usbd.c (device stack)                usbh.c (host stack)
 class drivers:                       class drivers:
   cdc_device.c                         cdc_host.c
   msc_device.c                         msc_host.c
   hid_device.c                         hid_host.c
   ncm_device.c                         hub.c
   ecm_rndis_device.c                   vendor_host.c
   audio_device.c
   video_device.c
   dfu_device.c
```

### Common vulnerability patterns

1. **Unchecked `wLength`** — USB control requests carry a `wLength` field
   specifying the transfer size. Multiple class drivers passed this directly to
   buffer operations without comparing against the actual buffer size.

2. **Uncapped array indices from USB data** — Endpoint numbers (0-15), LUN
   values, and device addresses from USB packets used as array indices without
   bounds checking against the actual array size (often 8 or fewer).

3. **Integer overflow in bounds checks** — Addition-based bounds checks
   (`offset + length <= size`) that wrap around on 32-bit targets, especially
   in RNDIS and NCM packet validation.

4. **Missing `bLength == 0` guards** — Descriptor walking loops advance by
   `bLength` bytes per iteration. A zero-length descriptor causes an infinite
   loop that hangs the entire USB stack.

5. **Stack-allocated descriptors** — Descriptor structures placed on the stack
   and passed by pointer to HCD drivers that store the pointer for later use,
   creating use-after-scope corruption.

## Relevance to the lab

All 27 vulnerabilities are in code paths exercised by our QEMU and native Linux
lab setups. The lab's fault injection (`vusbctl corrupt`, `vusbctl replay`)
can be used to trigger many of these bugs by sending malformed USB
packets/descriptors. The native Linux mode with ASan catches the memory
corruptions at runtime.
