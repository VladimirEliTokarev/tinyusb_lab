# ECM/RNDIS tud_network_xmit() Buffer Overflow

| Field | Value |
|-------|-------|
| **CWE** | CWE-787: Out-of-bounds Write |
| **CVSS** | 4.6 (Medium) |
| **Verdict** | weak |
| **File** | `src/class/net/ecm_rndis_device.c` |
| **Function** | `tud_network_xmit()` |
| **Status** | Open |
| **Source** | student (sbingqua BUG-003) |

## Root Cause

In `tud_network_xmit()` (line 381), the user-supplied callback `tud_network_xmit_cb()` writes directly into the transmit buffer `_netd_epbuf.tx` with no explicit size bound:

```c
uint16_t len = (_netd_itf.ecm_mode) ? 0 : CFG_TUD_NET_PACKET_PREFIX_LEN;
uint8_t* data = _netd_epbuf.tx + len;
len += tud_network_xmit_cb(data, ref, arg);
```

The callback receives a raw pointer into the endpoint buffer and can write an arbitrary amount of data. The `_netd_epbuf.tx` buffer is a fixed-size static allocation (typically `CFG_TUD_NET_MTU + header`), but the callback's return value `len` is not validated against the buffer's capacity before being passed to `do_in_xfer()`.

In RNDIS mode, the RNDIS header (`rndis_data_packet_t`) is prepended at the start of the buffer, and `len` includes both the header and payload. If the callback writes more than `CFG_TUD_NET_MTU` bytes, the write overflows `_netd_epbuf.tx`.

## Trigger

1. The network glue layer calls `tud_network_xmit()` after `tud_network_can_xmit()` returns true.
2. The `tud_network_xmit_cb()` implementation writes more data than the buffer can hold (e.g., a jumbo frame or malformed packet from the network stack).
3. The overflow occurs before any length validation — the `len` value is used directly to set `hdr->MessageLength` and passed to `do_in_xfer()`.

## Impact

- **Write**: Out-of-bounds write past the end of the static `_netd_epbuf.tx` buffer, corrupting adjacent global data.
- **DoS**: Corrupted endpoint buffer state can cause the USB transfer to send garbage data or hang the endpoint.
- On embedded targets, the overflow corrupts whatever globals are laid out after `_netd_epbuf` in the `.bss` section.

## Fix

Validate the return value of `tud_network_xmit_cb()` against the buffer capacity:

```c
uint16_t max_payload = sizeof(_netd_epbuf.tx) - len;
uint16_t written = tud_network_xmit_cb(data, ref, arg);
if (written > max_payload) {
  TU_LOG_DRV("(EE) tud_network_xmit: payload overflow\n");
  return;
}
len += written;
```

## Related

- [[ncm-xmit-oob-write]] — Same callback trust pattern in NCM driver
- `tud_network_can_xmit()` provides a size gate, but the actual write in `tud_network_xmit()` does not re-check
