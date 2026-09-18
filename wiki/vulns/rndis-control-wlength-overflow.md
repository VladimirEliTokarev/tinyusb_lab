# RNDIS Control wLength Global Buffer Overflow

| Field | Value |
|-------|-------|
| **CWE** | CWE-125: Out-of-bounds Read |
| **CVSS** | 5.2 (Medium) |
| **Verdict** | real |
| **File** | `src/class/net/ecm_rndis_device.c` |
| **Function** | `netd_control_xfer_cb()` → `rndis_class_set_handler()` |
| **Status** | Open |
| **Source** | waynelow |

## Root Cause

In `netd_control_xfer_cb()`, during the `CONTROL_STAGE_DATA` phase for
class-specific OUT requests (host → device), the host-supplied `wLength` field
is passed directly to `rndis_class_set_handler()` without any bounds check:

```c
// ecm_rndis_device.c, line ~315
if (!_netd_itf.ecm_mode) {
  rndis_class_set_handler(_netd_epbuf.ctrl, request->wLength);
}
```

The control buffer `_netd_epbuf.ctrl` is allocated with a fixed size of
`NETD_CONTROL_SIZE` (120 bytes). However, `rndis_class_set_handler()` in
`lib/networking/rndis_reports.c` casts the `data` pointer to various RNDIS
message structures and operates on the buffer as if it contained
`size` bytes of valid data.

When `wLength > 120`, the function reads and interprets memory beyond the
120-byte control buffer. Since `_netd_epbuf` is a global variable, the overflow
reads into the ASan global redzone (in test harnesses) or into adjacent global
variables (on real hardware), including transmit buffers, notification buffers,
and interface state.

The `rndis_class_set_handler()` function itself ignores the `size` parameter
entirely — it is declared as `(void)size;` — so even if a correct size were
passed, the function would not use it for bounds checking. This makes the
vulnerability doubly problematic: the caller doesn't clamp, and the callee
doesn't validate.

## Trigger

1. Connect to a device running TinyUSB's RNDIS network class.
2. Send a class-specific SET control request with `wLength > 120`:
   - `bmRequestType = 0x21` (host-to-device, class, interface)
   - `bRequest` = any valid RNDIS encapsulated command
   - `wIndex` = RNDIS interface number
   - `wLength = 256` (or any value > 120)
3. The host sends 120 bytes of data (USB controller caps at buffer size),
   but `request->wLength` retains the original 256.
4. `rndis_class_set_handler(_netd_epbuf.ctrl, 256)` is called.
5. Internal RNDIS message parsing reads structures that span past the
   120-byte `ctrl` buffer into adjacent globals.

## Impact

- **Type**: Global buffer out-of-bounds read
- **Bytes readable**: `wLength - 120` bytes past `_netd_epbuf.ctrl`
  (up to ~65,415 bytes with `wLength = 0xFFFF`)
- **What gets corrupted**: No direct write corruption; reads expose adjacent
  globals including `_netd_epbuf.rx` (receive buffer), `_netd_epbuf.tx`
  (transmit buffer), `_netd_epbuf.notify`, and `_netd_itf` (interface state
  with endpoint addresses)
- **DoS**: Process crash under ASan; HardFault on MCU when accessing unmapped
  memory. The RNDIS response may contain leaked data sent back to the host.

## PoC

See `students/waynelow/bug6_rndis_control_wlength_global_overflow/poc.cc`

```c
// Craft RNDIS SET_MSG with wLength > NETD_CONTROL_SIZE (120)
tusb_control_request_t request = {
    .bmRequestType = 0x21,
    .bRequest      = 0x00,  // SEND_ENCAPSULATED_COMMAND
    .wValue        = 0,
    .wIndex        = rndis_itf_num,
    .wLength       = 256,   // > 120 = NETD_CONTROL_SIZE
};

// Fill ctrl buffer with a valid RNDIS QUERY_MSG header
rndis_query_msg_t *msg = (rndis_query_msg_t *)ctrl_buf;
msg->MessageType   = REMOTE_NDIS_QUERY_MSG;
msg->MessageLength = 256;   // Claims 256 bytes of message
msg->Oid           = OID_GEN_SUPPORTED_LIST;

// Trigger CONTROL_STAGE_DATA -> rndis_class_set_handler(ctrl, 256)
// rndis_query() reads OID list past the 120-byte buffer
```

**ASan output:**
```
ERROR: AddressSanitizer: global-buffer-overflow on address 0x64f274b08270
READ of size 1 at 0x64f274b08270 thread T0
  #0 rndis_class_set_handler  rndis_reports.c
  #1 netd_control_xfer_cb     ecm_rndis_device.c:315
0x64f274b08270 is located 0 bytes after global variable '_netd_epbuf' of size 3344
```

## Fix

Cap `wLength` before the call, and fix `rndis_class_set_handler()` to actually
use its `size` parameter:

```c
// In netd_control_xfer_cb(), CONTROL_STAGE_DATA path:
uint16_t safe_len = tu_min16(request->wLength, NETD_CONTROL_SIZE);
rndis_class_set_handler(_netd_epbuf.ctrl, safe_len);

// In rndis_class_set_handler(), validate MessageLength:
void rndis_class_set_handler(uint8_t *data, int size) {
  encapsulated_buffer = data;
  rndis_generic_msg_t *msg = (rndis_generic_msg_t *)data;
  if (msg->MessageLength > (uint32_t)size) return; // reject oversized
  // ... rest of handler ...
}
```

## Related

- [[hid-control-xfer-oob-read]] — Same wLength unchecked pattern in HID/DFU/BTH
- [[rndis-dataoffset-integer-overflow]] — Another RNDIS bounds issue
