# MSC Device uint32→uint16 Truncation of total_len

| Field | Value |
|-------|-------|
| **CWE** | CWE-681: Incorrect Conversion between Numeric Types |
| **CVSS** | 3.5 (Low) |
| **Verdict** | weak |
| **File** | `src/class/msc/msc_device.c` |
| **Function** | `proc_read10_cmd()` (SCSI command dispatch) |
| **Status** | Open |
| **Source** | student (sbingqua BUG-014) |

## Root Cause

In `msc_device.c`, the `total_len` field (a `uint32_t` sourced from the CBW's `total_bytes`) is cast to `uint16_t` in multiple locations where it is passed to endpoint transfer functions and user callbacks:

```c
// Line 540 — endpoint transfer with truncated length
TU_ASSERT(usbd_edpt_xfer(rhport, p_msc->ep_out, _mscd_epbuf.buf, (uint16_t) p_msc->total_len, false));

// Line 548 — user SCSI callback with truncated length
resplen = tud_msc_scsi_cb(p_cbw->lun, p_cbw->command, _mscd_epbuf.buf, (uint16_t)p_msc->total_len);
```

The CBW's `total_bytes` field is a 32-bit value set by the USB host. If the host specifies a transfer length > 65535 bytes (0xFFFF), the cast to `uint16_t` silently truncates it. For example, a `total_bytes` of 0x10000 (64KB) becomes 0 after truncation, and 0x10001 (65537) becomes 1.

## Trigger

1. A USB host sends a SCSI command (via CBW) with `total_bytes > 65535`.
2. The MSC device stack processes the command and reaches the dispatch logic.
3. The `(uint16_t) p_msc->total_len` cast truncates the 32-bit value.
4. The endpoint transfer or user callback receives an incorrect (truncated) length.

This is most relevant for non-standard SCSI commands dispatched through `tud_msc_scsi_cb()`, where the host can specify arbitrary transfer lengths.

## Impact

- **Logic error**: The endpoint transfer requests fewer bytes than the host expects to send/receive, causing a CBW/CSW phase mismatch.
- **DoS**: Phase errors in the MSC BOT protocol cause the host to issue a Bulk-Only Mass Storage Reset, disrupting the device.
- **Write** (limited): If the truncated value is 0 or very small, the `usbd_edpt_xfer()` completes immediately, but the host continues sending data that the device is no longer expecting. Depending on the DCD implementation, this data may overwrite endpoint buffers.
- On practical deployments, standard READ10/WRITE10 commands use `block_count × block_size` which is handled separately, so this primarily affects vendor-specific SCSI commands.

## Fix

Either widen the parameter types to accept `uint32_t`, or clamp/validate before casting:

```c
// Option 1: Validate before cast
if (p_msc->total_len > UINT16_MAX) {
  TU_LOG_DRV("  SCSI total_len exceeds uint16 range\r\n");
  fail_scsi_op(p_msc, MSC_CSW_STATUS_FAILED);
  return;
}

// Option 2: Use tu_min32 to clamp
uint16_t xfer_len = (uint16_t) tu_min32(p_msc->total_len, CFG_TUD_MSC_EP_BUFSIZE);
```

## Related

- [[msc-host-block-integer-overflow]] — Integer overflow on the host side of MSC
- Lines 540, 548, 572, 601 all contain the same `(uint16_t) p_msc->total_len` cast
