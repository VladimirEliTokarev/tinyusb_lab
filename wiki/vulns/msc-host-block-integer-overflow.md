# MSC Host Integer Overflow in block_count × block_size

| Field | Value |
|-------|-------|
| **CWE** | CWE-190: Integer Overflow or Wraparound |
| **CVSS** | 5.3 (Medium) |
| **Verdict** | weak |
| **File** | `src/class/msc/msc_host.c` |
| **Function** | `tuh_msc_read10()` / `tuh_msc_write10()` |
| **Status** | Open |
| **Source** | student (sbingqua BUG-010) |

## Root Cause

In both `tuh_msc_read10()` (line 244) and `tuh_msc_write10()` (line 266), the total transfer size is computed by multiplying `block_count` by `block_size` without overflow checking:

```c
cbw.total_bytes = block_count * p_msc->capacity[lun].block_size;
```

Both `block_count` (a `uint16_t` parameter) and `block_size` (a `uint32_t` from the device's READ CAPACITY response) are promoted to `uint32_t` for the multiplication. If a malicious device reports a large `block_size` (e.g., 0x10000 or larger), the product can overflow the `uint32_t` range, wrapping around to a small value.

The `block_size` is populated from the device at line 505 via `tu_ntohl(resp->block_size)` — this is entirely device-controlled data parsed from the SCSI READ CAPACITY(10) response.

## Trigger

1. A malicious USB mass storage device is connected to the host.
2. The device responds to READ CAPACITY(10) with a large `block_size` value (e.g., 0x00020000 = 128KB).
3. The host application calls `tuh_msc_read10()` with a reasonable `block_count` (e.g., 256).
4. The multiplication `256 * 131072 = 33554432` fits in uint32, but with `block_size = 0x80000000` and `block_count = 4`, the result wraps to 0.
5. `cbw.total_bytes` is set to the wrapped value, causing a mismatch between the CBW's declared transfer length and the actual buffer size.

## Impact

- **Read/Write mismatch**: The SCSI command's `block_count` field tells the device how many blocks to transfer, but `cbw.total_bytes` (used by the USB transport layer) reflects the wrapped value. The device sends the full data, but the host's USB transfer completes early or overflows the caller's buffer.
- **Write**: If the USB DCD/HCD continues transferring data beyond `total_bytes` into the caller's buffer, this results in a heap or stack buffer overflow.
- **DoS**: Transfer size mismatch causes CSW phase errors, potentially hanging the MSC state machine.

## Fix

Use a safe multiplication with overflow detection before setting `cbw.total_bytes`:

```c
uint32_t total;
if (__builtin_mul_overflow((uint32_t)block_count, p_msc->capacity[lun].block_size, &total)) {
  TU_LOG_DRV("MSC: block_count * block_size overflow\r\n");
  return false;
}
cbw.total_bytes = total;
```

Or validate `block_size` at enumeration time (READ CAPACITY response) to reject unreasonable values.

## Related

- [[msc-host-maxlun-overflow]] — Another integer issue in MSC host from malicious device responses
- `config_read_capacity_complete()` at line 502 trusts `block_size` from device without validation
