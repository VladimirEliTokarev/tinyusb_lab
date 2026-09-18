# EP0 Descriptor Stack Corruption (Use-After-Scope)

| Field | Value |
|-------|-------|
| **CWE** | CWE-562: Return of Stack Variable Address |
| **CVSS** | 5.1 (Medium) |
| **Verdict** | plausible |
| **File** | `src/host/usbh.c` |
| **Function** | `usbh_edpt_control_open()` |
| **Status** | Closed (by-design — HCD must copy descriptor) |
| **Source** | github-issue |

## Root Cause

In `usbh_edpt_control_open()`, a `tusb_desc_endpoint_t` structure is
constructed as a **local (stack) variable** and its address is passed to
`hcd_edpt_open()`:

```c
static bool usbh_edpt_control_open(uint8_t dev_addr, uint8_t max_packet_size) {
  tusb_desc_endpoint_t ep0_desc = {
    .bLength          = sizeof(tusb_desc_endpoint_t),
    .bDescriptorType  = TUSB_DESC_ENDPOINT,
    .bEndpointAddress = 0,
    .bmAttributes     = { .xfer = TUSB_XFER_CONTROL },
    .wMaxPacketSize   = max_packet_size,
    .bInterval        = 0
  };

  return hcd_edpt_open(usbh_get_rhport(dev_addr), dev_addr, &ep0_desc);
}
```

The `ep0_desc` variable lives on the stack and is valid only for the duration
of `usbh_edpt_control_open()`. If an HCD (Host Controller Driver)
implementation stores the pointer to this descriptor (rather than copying the
contents), any subsequent dereference of that pointer accesses freed stack
memory.

The critical field at risk is `wMaxPacketSize`. If the HCD reads this value
later (e.g., during a control transfer setup), it reads whatever happens to
occupy that stack location, which is typically a different function's local
variables or return address.

This was reported as GH-3083. The maintainers closed it as **by-design**,
arguing that all HCD implementations must copy the descriptor contents within
`hcd_edpt_open()` rather than storing the pointer. However, this invariant is:
- Not documented in the HCD API contract
- Not enforced by the type system or runtime checks
- Easy to violate in new HCD implementations

## Trigger

1. A USB host running TinyUSB begins device enumeration.
2. `usbh_edpt_control_open(0, 8)` is called to open EP0 with an 8-byte
   max packet size for the initial GET_DESCRIPTOR.
3. The HCD's `hcd_edpt_open()` stores the pointer `&ep0_desc` in its
   endpoint state structure instead of copying the 7-byte descriptor.
4. `usbh_edpt_control_open()` returns, and `ep0_desc` goes out of scope.
5. Later, during the first control transfer, the HCD dereferences the stale
   pointer to read `wMaxPacketSize`.
6. The stack location has been overwritten by subsequent function calls,
   yielding a corrupted `wMaxPacketSize` value.

This is architecture- and optimization-dependent. With `-O0` (debug builds),
the stack frame may still contain the original value for a while. With `-O2`
or higher, the stack is aggressively reused and corruption is more likely.

## Impact

- **Type**: Use-after-scope → stack data corruption / information disclosure
- **Bytes affected**: 7 bytes (size of `tusb_desc_endpoint_t`), most critically
  the 2-byte `wMaxPacketSize` field
- **What gets corrupted**: If the HCD reads a garbage `wMaxPacketSize`, it may:
  - Configure the hardware controller with an incorrect packet size, causing
    protocol errors and device enumeration failure (DoS)
  - Use a `wMaxPacketSize` of 0, causing division-by-zero or infinite-length
    transfers
  - Use a very large `wMaxPacketSize`, causing buffer overflows in subsequent
    transfer operations
- **Exploitability**: Requires a specific (non-copying) HCD implementation;
  not directly controllable by an external USB device. However, it represents
  a latent vulnerability in the host stack API design.

## PoC

```c
// Hypothetical HCD that stores pointer (violating implicit contract)
bool hcd_edpt_open(uint8_t rhport, uint8_t dev_addr,
                   tusb_desc_endpoint_t const *desc_ep) {
  // BAD: storing pointer instead of copying
  hcd_state[dev_addr].ep0_desc = desc_ep;
  // ... configure hardware ...
  return true;
}

// Later, during control transfer:
void hcd_setup_control_xfer(uint8_t dev_addr) {
  // desc_ep now points to recycled stack memory
  uint16_t mps = hcd_state[dev_addr].ep0_desc->wMaxPacketSize;
  // mps is garbage — configures hardware incorrectly
  hw_set_max_packet_size(mps);
}
```

## Fix

The issue was closed as by-design with the rationale that HCD implementations
**must** copy descriptor contents. A more defensive fix would be:

**Option A**: Make `ep0_desc` static or file-scope:
```c
static bool usbh_edpt_control_open(uint8_t dev_addr, uint8_t max_packet_size) {
  static tusb_desc_endpoint_t ep0_desc; // persistent storage
  ep0_desc = (tusb_desc_endpoint_t){
    .bLength = sizeof(tusb_desc_endpoint_t),
    // ... fields ...
    .wMaxPacketSize = max_packet_size,
  };
  return hcd_edpt_open(usbh_get_rhport(dev_addr), dev_addr, &ep0_desc);
}
```

**Option B**: Document the copy requirement in `hcd.h` and add a compile-time
or runtime assertion in debug builds.

## Related

- [[host-descriptor-parser-hang]] — Another host-side enumeration vulnerability
