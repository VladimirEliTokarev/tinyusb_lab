# Descriptor Walk — OOB Read / Infinite Loop on bLength=0

| Field | Value |
|-------|-------|
| **CWE** | CWE-125: Out-of-bounds Read / CWE-835: Loop with Unreachable Exit Condition |
| **CVSS** | 4.3 (Medium) |
| **Verdict** | weak (confirmed by fuzzer) |
| **File** | `src/common/tusb_common.h`, `src/tusb.c` |
| **Function** | `tu_desc_in_bounds()`, `tu_desc_find()` |
| **Status** | Open |
| **Source** | student-sbingqua (BUG-005), tchinhe1 (fuzzer confirmation) |

## Root Cause

Multiple descriptor-walking functions rely on the `bLength` field (first byte of each USB descriptor) to advance through the descriptor buffer. When `bLength` is 0, the pointer never advances, creating an infinite loop.

**`tu_desc_find()` in `tusb.c:193`:**

```c
uint8_t const* tu_desc_find(uint8_t const* desc, uint8_t const* end, uint8_t byte1) {
  while (desc + 1 < end) {
    if (desc[1] == byte1) {
      return desc;
    }
    desc += desc[DESC_OFFSET_LEN];  // desc[0] is bLength — if 0, infinite loop
  }
  return NULL;
}
```

When `desc[0] == 0`, the `desc += 0` does nothing and the loop runs forever.

**`tu_desc_in_bounds()` in `tusb_common.h:390`:**

```c
TU_ATTR_ALWAYS_INLINE static inline bool tu_desc_in_bounds(const uint8_t *p_desc, const uint8_t *desc_end) {
  return p_desc < desc_end && tu_desc_next(p_desc) <= desc_end;
}
```

`tu_desc_next(p_desc)` computes `p_desc + p_desc[0]`. When `bLength == 0`, this returns `p_desc` itself, so `tu_desc_in_bounds` returns `true` (since `p_desc <= desc_end`), and any loop using this guard also hangs.

Additionally, a 1-byte OOB read occurs: when `p_desc` points to the last byte of the buffer and `bLength` is 0, the check `desc + 1 < end` reads `desc[1]` which is 1 byte past the buffer.

## Trigger

A malicious USB device provides a configuration descriptor (or any descriptor blob) containing a descriptor entry with `bLength = 0`. This causes:

1. Any `tu_desc_find()` / `tu_desc_find2()` / `tu_desc_find3()` call to loop forever
2. Any `while (tu_desc_in_bounds(...))` loop to loop forever
3. The device/host stack thread to hang, causing a denial of service

## Impact

- **Denial of Service** — the USB stack thread hangs permanently, blocking all USB operations
- **1-byte OOB read** — in `tu_desc_find`, reading `desc[1]` when only 1 byte remains
- On embedded RTOS targets, this hangs the USB task, potentially affecting the entire system if no watchdog is configured

The fuzzer (tchinhe1) independently confirmed this with corpus inputs that triggered the infinite loop.

## PoC

```c
// Minimal descriptor with bLength=0
uint8_t malicious_config_desc[] = {
  0x09, 0x02, 0x12, 0x00,  // Config descriptor header (wTotalLength=18)
  0x01, 0x01, 0x00, 0x80,
  0x32,
  // Interface descriptor
  0x09, 0x04, 0x00, 0x00,
  0x00, 0xFF, 0x00, 0x00,
  0x00,
  // Poison: descriptor with bLength=0
  0x00, 0x05   // bLength=0, bDescriptorType=ENDPOINT
};

// tu_desc_find(desc, end, TUSB_DESC_ENDPOINT) → infinite loop
// The fuzzer corpus also triggers this:
// students/tchinhe1/fuzz/corpus/7e15bb5c01e7dd56499e37c634cf791d3a519aee
```

See `students/tchinhe1/fuzz/CRASH_REPORT.md` for fuzzer findings.

## Fix

Add `bLength == 0` guards in all descriptor walking functions:

```c
uint8_t const* tu_desc_find(uint8_t const* desc, uint8_t const* end, uint8_t byte1) {
  while (desc + 1 < end) {
    if (desc[0] == 0) return NULL;  // prevent infinite loop
    if (desc[1] == byte1) {
      return desc;
    }
    desc += desc[0];
  }
  return NULL;
}
```

And in `tu_desc_in_bounds()`:

```c
static inline bool tu_desc_in_bounds(const uint8_t *p_desc, const uint8_t *desc_end) {
  return p_desc < desc_end && p_desc[0] != 0 && tu_desc_next(p_desc) <= desc_end;
}
```

## Related

- [[hid-host-parse-report-heap-overflow]] — descriptor parsing OOB in HID host
- [[audio-audiod-open-heap-overflow]] — descriptor walk boundary issue in audio
