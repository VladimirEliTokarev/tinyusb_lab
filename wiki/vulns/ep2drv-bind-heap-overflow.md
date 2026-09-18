# Endpoint-to-Driver Bind — Heap Buffer Overflow

| Field | Value |
|-------|-------|
| **CWE** | CWE-787: Out-of-bounds Write |
| **CVSS** | 6.1 (Medium) |
| **Verdict** | real |
| **File** | `src/tusb.c` |
| **Function** | `tu_bind_driver_to_ep_itf()` |
| **Status** | Open |
| **Source** | waynelow (WAYNELOW-2) |

## Root Cause

In `tu_bind_driver_to_ep_itf()` at line 296 of `tusb.c`, endpoint descriptors are parsed and the endpoint number is extracted:

```c
if (desc_type == TUSB_DESC_ENDPOINT) {
  const uint8_t ep_addr  = ((const tusb_desc_endpoint_t *)p_desc)->bEndpointAddress;
  const uint8_t ep_num   = tu_edpt_number(ep_addr);  // ep_addr & 0x0F → 0-15
  const uint8_t ep_dir   = tu_edpt_dir(ep_addr);
  ep2drv[ep_num][ep_dir] = driver_id;                 // WRITE
}
```

`tu_edpt_number()` extracts the low 4 bits of `bEndpointAddress`, yielding values 0–15. However, the `ep2drv` array passed to this function is `_usbd_dev.ep2drv`, which is sized `CFG_TUD_ENDPPOINT_MAX` (defaults to `TUP_DCD_ENDPOINT_MAX`, typically 8 on most MCUs). When a malicious descriptor contains `bEndpointAddress` with endpoint number ≥ 8 (e.g., `0x89` = EP 9 IN), the write `ep2drv[ep_num][ep_dir] = driver_id` writes past the array bounds.

## Trigger

A USB host sends a configuration descriptor containing an endpoint descriptor with `bEndpointAddress` having an endpoint number ≥ `CFG_TUD_ENDPPOINT_MAX` (typically 8). For example, `bEndpointAddress = 0x8F` (EP 15, IN direction).

## Impact

**Heap/global buffer overflow** — writes a `driver_id` byte (controlled by the enumeration flow, typically 0–N where N is the number of class drivers) to an out-of-bounds offset in the `ep2drv` array. Since `ep2drv` is part of the `_usbd_dev` struct in `.bss`, the overwrite corrupts adjacent fields:

- `ep_status[]` array (endpoint state machine)
- Other global state following `_usbd_dev`

This can corrupt device stack state, potentially leading to arbitrary endpoint control or denial of service.

## PoC

The researcher (waynelow) provided `poc.c`:

```c
// Craft a configuration descriptor with:
// bEndpointAddress = 0x8F (EP 15, IN direction)
// On a device where CFG_TUD_ENDPPOINT_MAX = 8:
//   ep2drv[15][1] writes 7 elements past array end

// Memory layout of _usbd_dev:
//   offset +0x88: ep2drv[8][2]      (16 bytes)
//   offset +0x98: ep_status[8][2]   (next field)
// Writing ep2drv[15][1] hits offset +0x88 + 15*2 + 1 = +0xA7
// → corrupts ep_status entries
```

See `students/waynelow/bug2_ep2drv_bind_driver_heap_overflow/poc.c` for the full reproducer.

## Fix

Validate `ep_num` against the array size before indexing:

```c
if (desc_type == TUSB_DESC_ENDPOINT) {
  const uint8_t ep_addr = ((const tusb_desc_endpoint_t *)p_desc)->bEndpointAddress;
  const uint8_t ep_num  = tu_edpt_number(ep_addr);
  const uint8_t ep_dir  = tu_edpt_dir(ep_addr);
  TU_ASSERT(ep_num < CFG_TUD_ENDPPOINT_MAX, false);
  ep2drv[ep_num][ep_dir] = driver_id;
}
```

Note: some call sites like `usbd_edpt_open()` (line 1392) already have this check, but `tu_bind_driver_to_ep_itf()` does not.

## Related

- [[ep-status-global-overflow]] — same array sizing issue, different access path via tud_task_ext
- [[audio-audiod-open-heap-overflow]] — another descriptor parsing OOB
