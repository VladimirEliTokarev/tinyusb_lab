# NCM NDP16 wLength Uncapped OOB Read

| Field | Value |
|-------|-------|
| **CWE** | CWE-125: Out-of-bounds Read |
| **CVSS** | 5.7 (Medium) |
| **Verdict** | real |
| **File** | `src/class/net/ncm_device.c` |
| **Function** | `recv_validate_datagram()` |
| **Status** | Fixed (PR #3741) |
| **Source** | github-pr + waynelow |

## Root Cause

In `recv_validate_datagram()`, the NDP16 header's `wLength` field is used to
compute the maximum datagram index (`max_ndx`) that controls iteration over
the NDP16 datagram pointer table:

```c
uint16_t max_ndx = (uint16_t)((ndp16->wLength - sizeof(ndp16_t)) / sizeof(ndp16_datagram_t));
```

Where `sizeof(ndp16_t) = 8` and `sizeof(ndp16_datagram_t) = 4`. The resulting
`max_ndx` is never validated against the actual receive buffer size
(`CFG_TUD_NCM_OUT_NTB_MAX_SIZE`, typically 3200 bytes). A crafted NTB with a
large `wLength` value produces an enormous `max_ndx`, causing the code to read
`ndp16_datagram[max_ndx - 1]` far beyond the buffer boundary.

The NTH16 signature (`"NCMH"`) and NDP16 signature (`"NCM0"`) are trivially
satisfied by the crafted packet, and all preceding sanity checks (header
length, block length, NDP position) pass because they only validate against
`len` (the USB transfer length) rather than the NDP's implied size.

On line 629, the access `ndp16_datagram[max_ndx - 1].wDatagramIndex` reads
at an offset of `(max_ndx - 1) * 4` bytes past the start of the datagram
table, which can reach ~62 KB past the 3200-byte buffer.

## Trigger

1. Connect to a device running TinyUSB's NCM network class.
2. Send a crafted NCM NTB (Network Transfer Block) with:
   - Valid NTH16 header: signature `"NCMH"`, `wHeaderLength = 12`,
     `wBlockLength = 64`, `wNdpIndex = 12`
   - NDP16 header at offset 12: signature `"NCM0"`, **`wLength = 0xF3E9`**
     (62,441), `wNextNdpIndex = 0`
3. The firmware computes `max_ndx = (62441 - 8) / 4 = 15,608`.
4. Access to `ndp16_datagram[15607]` reads at offset 62,428 bytes past the
   datagram table — approximately 59 KB beyond the 3200-byte `recv_ntb_t`.

## Impact

- **Type**: Out-of-bounds read (heap buffer overflow)
- **Bytes readable**: Up to ~62 KB past the 3200-byte receive NTB buffer
- **What gets corrupted**: No write, but reads expose heap metadata, other NTB
  buffers, NCM interface state, endpoint buffers, and potentially
  application-level data structures
- **DoS**: Immediate crash (SEGV/HardFault) when reading unmapped memory
  regions. ASan confirms: `SEGV on unknown address` at `recv_validate_datagram`

## PoC

See `students/waynelow/bug5_ncm_recv_validate_oob_read/poc.cc`

```c
// Crafted NCM NTB packet
uint8_t ntb[64] = {0};

// NTH16 header (12 bytes)
memcpy(ntb + 0, "NCMH", 4);     // dwSignature
*(uint16_t*)(ntb + 4) = 12;      // wHeaderLength
*(uint16_t*)(ntb + 6) = 1;       // wSequence
*(uint16_t*)(ntb + 8) = 64;      // wBlockLength
*(uint16_t*)(ntb + 10) = 12;     // wNdpIndex

// NDP16 header at offset 12
memcpy(ntb + 12, "NCM0", 4);     // dwSignature
*(uint16_t*)(ntb + 16) = 0xF3E9; // wLength = 62441 (MALICIOUS)
*(uint16_t*)(ntb + 18) = 0;      // wNextNdpIndex

// Feed to netd_xfer_cb as if received on ep_out with xferred_bytes = 64
// -> recv_validate_datagram reads ~62KB OOB
```

**ASan crash output:**
```
==7==ERROR: AddressSanitizer: SEGV on unknown address 0x55f589fe98f0
    #0 recv_validate_datagram ncm_device.c:629:50
    #1 netd_xfer_cb           ncm_device.c:924:10
```

## Fix

PR #3741 added an upper-bound check on `max_ndx` to ensure the entire NDP16
datagram table fits within the received NTB:

```c
// Compute max number of datagram entries that fit in the buffer
uint16_t max_ndx = (uint16_t)((ndp16->wLength - sizeof(ndp16_t)) / sizeof(ndp16_datagram_t));

// NEW: Validate that the datagram table does not extend past the NTB
uint16_t ndp_end = nth16->wNdpIndex + sizeof(ndp16_t) + max_ndx * sizeof(ndp16_datagram_t);
if (ndp_end > len || ndp_end > CFG_TUD_NCM_OUT_NTB_MAX_SIZE) {
  TU_LOG_DRV("(EE) NDP datagram table exceeds buffer\n");
  return false;
}
```

## Related

- [[rndis-dataoffset-integer-overflow]] — Similar OOB read in RNDIS class
- [[rndis-control-wlength-overflow]] — Unchecked wLength in RNDIS control path
- [[hid-control-xfer-oob-read]] — Unchecked wLength in HID control path
