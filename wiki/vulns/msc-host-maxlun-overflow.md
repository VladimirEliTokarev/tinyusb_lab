# MSC Host max_lun uint8 Overflow

| Field | Value |
|-------|-------|
| **CWE** | CWE-190: Integer Overflow or Wraparound |
| **CVSS** | 3.5 (Low) |
| **Verdict** | weak |
| **File** | `src/class/msc/msc_host.c` |
| **Function** | `config_get_maxlun_complete()` |
| **Status** | Open |
| **Source** | student (sbingqua BUG-009) |

## Root Cause

In `config_get_maxlun_complete()` (line 452), the Get Max LUN response is incremented by 1 per the USB MSC spec (the response value is "number of LUNs minus one"):

```c
if (XFER_RESULT_SUCCESS == xfer->result) {
  uint8_t* enum_buf = usbh_get_enum_buf();
  p_msc->max_lun = enum_buf[0] + 1;
} else {
  p_msc->max_lun = 1;
}
```

The `max_lun` field and `enum_buf[0]` are both `uint8_t`. If a malicious USB device returns 255 (0xFF) in the Get Max LUN response, the expression `enum_buf[0] + 1` evaluates to 256, which wraps to 0 when stored in the `uint8_t` field.

A `max_lun` value of 0 means "no LUNs available," which contradicts the successful response and can cause incorrect behavior in LUN enumeration and array indexing into `capacity[CFG_TUH_MSC_MAXLUN]`.

## Trigger

1. A malicious USB mass storage device is connected to the host.
2. The device responds to the Get Max LUN request (bRequest=0xFE) with the value 255.
3. The host stack processes the response and sets `max_lun = 0` due to uint8 wraparound.

## Impact

- **DoS**: With `max_lun = 0`, the host may skip LUN enumeration entirely, rendering the device non-functional.
- **Logic error**: If code later iterates `for (lun = 0; lun < max_lun; lun++)`, the loop body never executes, silently breaking MSC functionality.
- The `capacity[]` array is sized by `CFG_TUH_MSC_MAXLUN` (typically small, e.g., 4), so even a correctly stored value of 255 would cause out-of-bounds access during capacity reads — the wraparound to 0 accidentally prevents this worse outcome.

## Fix

Clamp the max_lun value to `CFG_TUH_MSC_MAXLUN` and handle the edge case:

```c
if (XFER_RESULT_SUCCESS == xfer->result) {
  uint8_t* enum_buf = usbh_get_enum_buf();
  uint16_t raw = (uint16_t)enum_buf[0] + 1;
  p_msc->max_lun = (uint8_t)tu_min16(raw, CFG_TUH_MSC_MAXLUN);
} else {
  p_msc->max_lun = 1;
}
```

## Related

- [[msc-host-block-integer-overflow]] — Another integer issue in MSC host
- USB MSC BOT spec §3.2: Get Max LUN returns 0-based LUN count (0 = 1 LUN, 15 max per spec)
