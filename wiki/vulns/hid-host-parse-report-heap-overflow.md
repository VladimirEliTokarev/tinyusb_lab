# HID Host Parse Report Descriptor — Heap OOB Read

| Field | Value |
|-------|-------|
| **CWE** | CWE-125: Out-of-bounds Read |
| **CVSS** | 6.8 (Medium) |
| **Verdict** | real |
| **File** | `src/class/hid/hid_host.c` |
| **Function** | `tuh_hid_parse_report_descriptor()` |
| **Status** | Open |
| **Source** | student-sbingqua (BUG-004) |

## Root Cause

In `tuh_hid_parse_report_descriptor()`, after consuming the 1-byte header at line 710–711, `desc_len` is decremented by 1. The `size` field (0–4 bytes) is then extracted from the header. On line 720, the code reads `desc_report[0]` unconditionally when `size > 0`, and the loop at lines 722–724 reads `desc_report[i]` for `i` in `0..size-1`. However, `desc_len` may already be 0 or less than `size` at this point — the remaining-length is a `uint16_t`, so decrementing it past zero causes an unsigned underflow to a large value, bypassing the `while (desc_len && ...)` loop guard and allowing reads well beyond the allocated descriptor buffer.

```c
// hid_host.c:710-720
header.byte = *desc_report++;
desc_len--;                        // can underflow to 0xFFFF if desc_len was 0

uint8_t const tag  = header.tag;
uint8_t const type = header.type;
uint8_t size = header.size;
if (size == 3) {
  size = 4;
}

uint8_t const data8 = (size > 0) ? desc_report[0] : 0;  // OOB if desc_len < size
```

## Trigger

A USB HID device provides a malformed report descriptor where the total byte count is just enough for the header byte(s) but not the item data payload. For example, a descriptor that ends with a header declaring `size=4` but has 0 remaining data bytes.

## Impact

**Out-of-bounds heap read** — up to 4 bytes past the descriptor buffer can be read. On a host stack, this could leak sensitive heap data. Depending on the allocator and heap layout, it could also cause a crash if the read crosses into an unmapped page. In a USB security context, a malicious device can trigger this to probe host memory layout.

## PoC

```c
// Craft a minimal HID report descriptor that ends with a header
// claiming size=4 but no payload follows
uint8_t malicious_desc[] = {
  0x05, 0x01,       // USAGE_PAGE (Generic Desktop) - valid 1-byte item
  0x09, 0x06,       // USAGE (Keyboard) - valid 1-byte item
  0xA1, 0x01,       // COLLECTION (Application) - valid 1-byte item
  0x06,             // USAGE_PAGE header with size=2, but only 1 byte follows
  0x42              // truncated: missing second data byte
};

tuh_hid_report_info_t info[4];
uint8_t count = tuh_hid_parse_report_descriptor(info, 4,
    malicious_desc, sizeof(malicious_desc));
// desc_report reads past malicious_desc[] bounds
```

## Fix

Add a bounds check before accessing item data bytes. After decrementing `desc_len` for the header, verify `desc_len >= size` before reading any data bytes:

```c
desc_len--;
// ... extract size from header ...
if (desc_len < size) {
  break;  // truncated item, stop parsing
}
```

## Related

- [[hid-host-usage-page-memcpy-overflow]] — same function, different bug class
- [[descriptor-walk-oob-infinite-loop]] — similar descriptor parsing issue
