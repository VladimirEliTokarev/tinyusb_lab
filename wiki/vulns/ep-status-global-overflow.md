# Endpoint Status / ep2drv — Global Buffer Overflow

| Field | Value |
|-------|-------|
| **CWE** | CWE-787: Out-of-bounds Write |
| **CVSS** | 6.8 (Medium) |
| **Verdict** | real |
| **File** | `src/device/usbd.c` |
| **Function** | `tud_task_ext()` |
| **Status** | Open |
| **Source** | waynelow (WAYNELOW-3) |

## Root Cause

In `tud_task_ext()` at line 674 of `usbd.c`, when a `DCD_EVENT_XFER_COMPLETE` event is processed, the endpoint number is extracted from the event's `ep_addr`:

```c
case DCD_EVENT_XFER_COMPLETE: {
  uint8_t const ep_addr = event.xfer_complete.ep_addr;
  uint8_t const epnum   = tu_edpt_number(ep_addr);  // ep_addr & 0x0F → 0-15
  uint8_t const ep_dir  = tu_edpt_dir(ep_addr);

  _usbd_dev.ep_status[epnum][ep_dir].busy    = 0;   // OOB WRITE
  _usbd_dev.ep_status[epnum][ep_dir].claimed = 0;   // OOB WRITE
```

Both `ep_status` and `ep2drv` arrays in `_usbd_dev` are sized to `CFG_TUD_ENDPPOINT_MAX` (typically 8):

```c
uint8_t ep2drv[CFG_TUD_ENDPPOINT_MAX][2];
tu_edpt_state_t ep_status[CFG_TUD_ENDPPOINT_MAX][2];
```

Since `tu_edpt_number()` returns the low 4 bits (0–15), any DCD event with `ep_addr` ≥ 8 writes past both arrays. The `_usbd_dev` struct is followed in `.bss` by `_usbd_queued_setup` and other globals.

## Trigger

A DCD (Device Controller Driver) fires a transfer-complete interrupt with an endpoint address whose number field is ≥ `CFG_TUD_ENDPPOINT_MAX`. On a device with a compromised or buggy DCD, or where USB packets can be injected, this is reachable. The event is dequeued in `tud_task_ext()` and the endpoint number is used without bounds checking.

## Impact

**Global variable corruption** — writing to `ep_status[epnum]` and later `ep2drv[epnum]` with `epnum` ≥ 8 overwrites:

| Offset | Global | Effect |
|--------|--------|--------|
| `_usbd_dev` + sizeof(ep_status) | `_usbd_queued_setup` | Corrupts setup packet counter |
| Beyond `_usbd_dev` | `_usbd_spin` | Corrupts spinlock state |

This can cause:
- Denial of service (device stack hangs or crashes)
- Bypass of SETUP packet processing logic
- Potential arbitrary code execution if adjacent function pointers are overwritten

## PoC

The researcher (waynelow) provided `poc.cc`:

```cpp
// Inject a DCD_EVENT_XFER_COMPLETE with ep_addr = 0x8F (EP 15 IN)
// On a system with CFG_TUD_ENDPPOINT_MAX = 8:
//
// ep_status array: 8 * 2 * sizeof(tu_edpt_state_t) = 16 bytes
// Writing ep_status[15][1] → 15*2 + 1 = 31 → offset +31
// This is 15 elements past the array → corrupts _usbd_queued_setup
//
// Memory layout after _usbd_dev:
//   static usbd_device_t    _usbd_dev;
//   static volatile uint8_t _usbd_queued_setup;  ← CORRUPTED
```

See `students/waynelow/bug3_ep_status_tud_task_ext_global_overflow/poc.cc` for the full reproducer.

## Fix

Validate `epnum` before using it as an array index:

```c
case DCD_EVENT_XFER_COMPLETE: {
  uint8_t const ep_addr = event.xfer_complete.ep_addr;
  uint8_t const epnum   = tu_edpt_number(ep_addr);
  uint8_t const ep_dir  = tu_edpt_dir(ep_addr);

  TU_ASSERT(epnum < CFG_TUD_ENDPPOINT_MAX,);

  _usbd_dev.ep_status[epnum][ep_dir].busy    = 0;
  _usbd_dev.ep_status[epnum][ep_dir].claimed = 0;
  // ...
```

## Related

- [[ep2drv-bind-heap-overflow]] — same array sizing issue in tu_bind_driver_to_ep_itf
- [[audio-audiod-open-heap-overflow]] — another descriptor-driven OOB
