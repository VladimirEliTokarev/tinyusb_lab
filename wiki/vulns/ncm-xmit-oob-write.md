# NCM tud_network_xmit() Global Buffer Overflow

| Field | Value |
|-------|-------|
| **CWE** | CWE-787: Out-of-bounds Write |
| **CVSS** | 4.6 (Medium) |
| **Verdict** | weak |
| **File** | `src/class/net/ncm_device.c` |
| **Function** | `tud_network_xmit()` |
| **Status** | Open |
| **Source** | student (sbingqua BUG-001/002) |

## Root Cause

In `tud_network_xmit()` (line 726), the datagram index `xmit_glue_ntb_datagram_ndx` is used to index into the fixed-size `ndp_datagram[]` array without a bounds check at the point of write:

```c
ntb->ndp_datagram[ncm_interface.xmit_glue_ntb_datagram_ndx].wDatagramIndex = ntb->nth.wBlockLength;
ntb->ndp_datagram[ncm_interface.xmit_glue_ntb_datagram_ndx].wDatagramLength = size;
ncm_interface.xmit_glue_ntb_datagram_ndx += 1;
```

The guard check `xmit_glue_ntb_datagram_ndx >= CFG_TUD_NCM_IN_MAX_DATAGRAMS_PER_NTB` exists in `tud_network_can_xmit()` (line 411), but the caller is expected to call `can_xmit` before `xmit`. If the caller bypasses `can_xmit` or if the callback `tud_network_xmit_cb` writes beyond the NTB data buffer, both the datagram index and the data payload can overflow their respective buffers.

Additionally, `tud_network_xmit_cb()` writes directly into `ntb->data + ntb->nth.wBlockLength` with no explicit size limit passed to the callback, relying on the caller to have pre-validated via `tud_network_can_xmit()`.

## Trigger

1. Network glue layer calls `tud_network_xmit()` without first checking `tud_network_can_xmit()`, or
2. Multiple rapid calls where `xmit_glue_ntb_datagram_ndx` exceeds `CFG_TUD_NCM_IN_MAX_DATAGRAMS_PER_NTB` between the check and the write (TOCTOU window), or
3. `tud_network_xmit_cb()` returns a size that, when added to `wBlockLength`, exceeds `CFG_TUD_NCM_IN_NTB_MAX_SIZE` — detected post-facto at line 746 but the write has already occurred.

## Impact

- **Write**: Out-of-bounds write into the global `xmit_ntb_t` buffer, corrupting adjacent memory in the NCM interface structure.
- **DoS**: The post-facto overflow check at line 746 returns early but the damage is already done — corrupted NTB metadata can cause subsequent transmissions to malfunction.
- On embedded targets without memory protection, this could corrupt arbitrary adjacent globals.

## Fix

Add a bounds check on `xmit_glue_ntb_datagram_ndx` directly inside `tud_network_xmit()` before indexing, and pass an explicit max-size parameter to `tud_network_xmit_cb()`:

```c
if (ncm_interface.xmit_glue_ntb_datagram_ndx >= CFG_TUD_NCM_IN_MAX_DATAGRAMS_PER_NTB) {
  TU_LOG_DRV("(EE) tud_network_xmit: datagram index overflow\n");
  return;
}
uint16_t remaining = CFG_TUD_NCM_IN_NTB_MAX_SIZE - ntb->nth.wBlockLength;
uint16_t size = tud_network_xmit_cb(ntb->data + ntb->nth.wBlockLength, ref, arg);
TU_ASSERT(size <= remaining, /*void*/);
```

## Related

- [[ecm-rndis-xmit-oob-write]] — Same pattern in ECM/RNDIS driver
- BUG-001: Unbounded `xmit_cb` write
- BUG-002: Unbounded datagram index
