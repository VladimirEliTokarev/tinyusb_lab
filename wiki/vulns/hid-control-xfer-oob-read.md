# HID/DFU/BTH Control Transfer OOB Reads

| Field | Value |
|-------|-------|
| **CWE** | CWE-125: Out-of-bounds Read |
| **CVSS** | 5.3 (Medium) |
| **Verdict** | real |
| **File** | `src/class/hid/hid_device.c`, `src/class/dfu/dfu_device.c`, `src/class/bth/bth_device.c` |
| **Function** | `hidd_control_xfer_cb()`, `dfu_req_dnload_setup()`, `btd_control_xfer_cb()` |
| **Status** | Fixed (PRs #913, #1090, #1093, #1106) |
| **Source** | github-issue |

## Root Cause

Multiple USB device class drivers did not validate the `wLength` field from
incoming SETUP control requests against the size of the data buffer before
calling `tud_control_xfer()`. The USB host specifies `wLength` to indicate the
maximum number of bytes it is willing to accept in the data stage of a control
transfer. When the device honors that length without clamping it to the actual
size of the data it has available, the DMA controller or software copy reads
past the end of the source buffer.

In `hidd_control_xfer_cb()`, for `HID_REQ_CONTROL_GET_REPORT`, the response
length was derived directly from the application callback return value combined
with `request->wLength`, but the initial buffer passed to the xfer was sized at
`CFG_TUD_HID_EP_BUFSIZE`. A host requesting more than that buffer could trigger
an OOB read.

Similar patterns existed in `dfu_req_dnload_setup()` where `wLength` was passed
directly as the transfer length without checking against the firmware block
buffer size, and in `btd_control_xfer_cb()` where Bluetooth HCI event buffer
lengths were not bounded.

## Trigger

1. Enumerate the target device (HID, DFU, or BTH class).
2. Send a class-specific GET control request (e.g., `HID_REQ_CONTROL_GET_REPORT`)
   with `wLength` set larger than the internal endpoint buffer size
   (e.g., `wLength = 4096` when `CFG_TUD_HID_EP_BUFSIZE = 64`).
3. The device responds with data read beyond the buffer boundary.

Any USB host (including a malicious host via USB-C or OTG) can craft this
request. No authentication or special privileges are required.

## Impact

- **Type**: Out-of-bounds read (information disclosure + potential DoS)
- **Bytes leaked**: Up to `wLength - buffer_size` bytes of adjacent memory
  (heap or global, depending on buffer allocation)
- **What gets corrupted**: No direct write corruption, but adjacent memory
  contents (keys, state, other class driver data) may be returned to the host
  in the control transfer data stage
- **DoS**: On MCUs with MPU or memory protection, reading past mapped regions
  can cause a HardFault / bus fault crash

## PoC

Craft a USB control transfer with an oversized `wLength`:

```c
// From a malicious USB host / test harness
struct usb_ctrlrequest setup = {
    .bRequestType = 0xA1,  // Device-to-host, class, interface
    .bRequest     = HID_REQ_CONTROL_GET_REPORT,
    .wValue       = (HID_REPORT_TYPE_INPUT << 8) | 0x01,
    .wIndex       = 0,     // HID interface number
    .wLength      = 4096,  // Much larger than CFG_TUD_HID_EP_BUFSIZE (64)
};
usb_control_msg(dev, &setup, buf, 4096, 1000);
// buf will contain data read past the endpoint buffer
```

## Fix

The fixes across PRs #913, #1090, #1093, and #1106 clamp `wLength` to the
actual buffer size before passing it to `tud_control_xfer()`:

```c
// Example fix pattern applied in hidd_control_xfer_cb():
uint16_t req_len = tu_min16(request->wLength, CFG_TUD_HID_EP_BUFSIZE);
tud_control_xfer(rhport, request, report_buf, req_len);
```

The same `tu_min16()` clamping pattern was applied in DFU and BTH class
drivers to ensure the transfer length never exceeds the backing buffer size.

## Related

- [[rndis-control-wlength-overflow]] — Same pattern in RNDIS control path
- [[ncm-ndp16-wlength-oob-read]] — Unchecked length in NCM NDP16 parsing
