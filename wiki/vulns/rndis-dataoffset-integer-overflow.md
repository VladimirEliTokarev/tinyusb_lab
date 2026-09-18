# RNDIS DataOffset Integer Overflow in handle_incoming_packet()

| Field | Value |
|-------|-------|
| **CWE** | CWE-190: Integer Overflow or Wraparound |
| **CVSS** | 5.2 (Medium) |
| **Verdict** | real |
| **File** | `src/class/net/ecm_rndis_device.c` |
| **Function** | `handle_incoming_packet()` |
| **Status** | Fixed (PR #3756) |
| **Source** | github-pr + waynelow |

## Root Cause

In `handle_incoming_packet()`, the RNDIS data packet header fields `DataOffset`
and `DataLength` are used to compute a pointer into the receive buffer and
validate that the referenced data falls within the received USB transfer. The
bounds check on line 333 was:

```c
if ((r->DataOffset + offsetof(rndis_data_packet_t, DataOffset) + r->DataLength) <= len)
```

All operands (`DataOffset`, `DataLength`, the offsetof constant, and `len`) are
`uint32_t`. On 32-bit embedded targets, when `DataOffset` and `DataLength` are
crafted such that their sum exceeds `UINT32_MAX`, the addition wraps around to
a small value that passes the `<= len` check.

Furthermore, the check validates against `len` (the USB-reported
`xferred_bytes`, up to 3352 = `NETD_PACKET_SIZE`) rather than the actual buffer
capacity `NETD_PACKET_SIZE` (1602 bytes). A malicious host can report
`xferred_bytes = 3352` even though the buffer `_netd_epbuf.rx` is only 1602
bytes, because the DMA length is controlled by the host-side controller.

The resulting pointer `pnt = &_netd_epbuf.rx[DataOffset + 8]` points up to
2502 bytes past the 1602-byte buffer, and `tud_network_recv_cb(pnt, size)`
reads `DataLength` bytes from that out-of-bounds location.

## Trigger

1. Connect to a device running TinyUSB's RNDIS network class.
2. Send a crafted RNDIS data packet with:
   - `MessageType = REMOTE_NDIS_PACKET_MSG` (0x00000001)
   - `MessageLength = 3352` (matches `xferred_bytes`)
   - `DataOffset = 0xFFFFFFF0` (large value near UINT32_MAX)
   - `DataLength = 0x18` (24 bytes)
   - The sum `0xFFFFFFF0 + 8 + 0x18 = 0x100000010` wraps to `0x10 = 16 ≤ 3352` ✓
3. The firmware computes `pnt = &rx[0xFFFFFFF8]` — on a 32-bit MCU this wraps
   the pointer, reading from an unintended memory region.
4. On a 64-bit test harness, the pointer arithmetic doesn't wrap but reads 2502+
   bytes past the buffer into the ASan global redzone, crashing immediately.

## Impact

- **Type**: Out-of-bounds read (global buffer overflow)
- **Bytes readable**: Up to ~2502 bytes past the 1602-byte `_netd_epbuf.rx` buffer
- **What gets corrupted**: No write corruption, but adjacent global variables
  (`_netd_epbuf.tx`, `_netd_epbuf.notify`, `_netd_epbuf.ctrl`, `_netd_itf`
  state) are exposed to the network callback and may be forwarded to the host
- **DoS**: ASan/HardFault crash on access to unmapped memory

## PoC

See `students/waynelow/bug7_rndis_handle_incoming_global_overflow/poc.cc`

```c
// Key excerpt from poc.cc — crafted RNDIS header
rndis_data_packet_t *r = (rndis_data_packet_t *)rx_buf;
r->MessageType   = REMOTE_NDIS_PACKET_MSG;  // 0x00000001
r->MessageLength = 3352;
r->DataOffset    = 3336;  // 3336 + 8 + 8 = 3352 (passes check, but 3344 > 1602)
r->DataLength    = 8;
// Triggers: pnt = &_netd_epbuf.rx[3344] — 1 byte past the 3344-byte _netd_epbuf global
```

## Fix

PR #3756 added an explicit guard at the top of `handle_incoming_packet()` to
clamp `len` against the actual buffer size, and added overflow-safe arithmetic:

```c
static void handle_incoming_packet(uint32_t len) {
  // Clamp against actual buffer capacity
  if (len > NETD_PACKET_SIZE) {
    len = NETD_PACKET_SIZE;
  }
  // ... existing validation with corrected bounds ...
}
```

Additionally, the bounds check was made overflow-safe by restructuring:
```c
// Instead of: (DataOffset + 8 + DataLength) <= len  [can overflow]
// Use:        DataOffset <= len - 8 - DataLength     [safe if checked separately]
if (r->DataLength > len || r->DataOffset > len - offsetof(...) - r->DataLength)
```

## Related

- [[rndis-control-wlength-overflow]] — Another RNDIS bounds-check bypass
- [[ncm-ndp16-wlength-oob-read]] — Similar OOB read in NCM class
