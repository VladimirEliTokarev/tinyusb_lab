# Audio Device audiod_open — 1-Byte Heap OOB Read

| Field | Value |
|-------|-------|
| **CWE** | CWE-125: Out-of-bounds Read |
| **CVSS** | 4.6 (Medium) |
| **Verdict** | real |
| **File** | `src/class/audio/audio_device.c` |
| **Function** | `audiod_open()` |
| **Status** | Open |
| **Source** | waynelow (WAYNELOW-1) |

## Root Cause

In `audiod_open()` (line 810), the function walks a configuration descriptor to find the Audio Streaming interface following the Audio Control interface. The descriptor walk loop at lines 828–831 advances through descriptors:

```c
uint8_t const *p_desc = (uint8_t const *) itf_desc;
uint8_t const *p_desc_end = p_desc + max_len;

p_desc = tu_desc_next(p_desc);
while (tu_desc_in_bounds(p_desc, p_desc_end) && tu_desc_type(p_desc) != TUSB_DESC_INTERFACE) {
  p_desc = tu_desc_next(p_desc);
}
```

When the configuration descriptor is truncated such that an endpoint descriptor stub sits at the very end of the buffer (e.g., only 1–2 bytes remaining), `tu_desc_in_bounds()` checks `p_desc < desc_end && tu_desc_next(p_desc) <= desc_end`. The `tu_desc_next` reads `p_desc[0]` (the `bLength` field) — but at the buffer boundary, `bLength` itself may be the last valid byte while the rest of the descriptor is out of bounds. The subsequent cast to `tusb_desc_interface_t` and field access at line 835 then reads 1 byte past the heap allocation.

## Trigger

A USB host sends a `SET_CONFIGURATION` with a configuration descriptor that is truncated mid-endpoint-descriptor at the end of the Audio Control interface region, leaving exactly 1 byte of an endpoint descriptor at the buffer boundary.

## Impact

**1-byte heap OOB read** — reads one byte past the descriptor buffer. On an embedded device, this typically reads from adjacent heap metadata or the next allocated object. Impact is limited to information disclosure or potential crash if the read triggers a fault.

## PoC

The researcher (waynelow) provided a `poc.cc` demonstrating the issue:

```cpp
// Craft a configuration descriptor where:
// - Audio Control interface is valid
// - Followed by a 1-byte endpoint descriptor stub at buffer end
// - bLength byte is readable, but bDescriptorType is past allocation

// The truncated descriptor triggers a 1-byte heap OOB read
// when audiod_open() reads tu_desc_type(p_desc) at the boundary
```

See `students/waynelow/bug1_audio_audiod_open_heap_overflow/poc.cc` for the full reproducer.

## Fix

Add explicit remaining-length check before accessing descriptor fields:

```c
// After the walk loop, before casting:
if ((uintptr_t)(p_desc_end - p_desc) < sizeof(tusb_desc_interface_t)) {
  return 0;  // not enough data for interface descriptor
}
```

This check already exists at line 834 but should also guard the `tu_desc_type()` call inside the loop condition.

## Related

- [[descriptor-walk-oob-infinite-loop]] — related descriptor walking safety issues
- [[ep2drv-bind-heap-overflow]] — another descriptor parsing OOB write
