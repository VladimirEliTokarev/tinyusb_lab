# Host Descriptor Parser Infinite Loop (bLength=0)

| Field | Value |
|-------|-------|
| **CWE** | CWE-834: Excessive Iteration |
| **CVSS** | 4.3 (Medium) |
| **Verdict** | real |
| **File** | `src/host/usbh.c` |
| **Function** | `enum_parse_configuration_desc()` |
| **Status** | Fixed (PR #2852) |
| **Source** | github-pr |

## Root Cause

The USB host stack's configuration descriptor parser iterates through a chain
of USB descriptors using `tu_desc_next()`, which advances the pointer by the
current descriptor's `bLength` field:

```c
static inline uint8_t const* tu_desc_next(void const* desc) {
  uint8_t const* desc_u8 = (uint8_t const*)desc;
  return desc_u8 + tu_desc_len(desc);  // advance by bLength
}
```

When parsing a configuration descriptor, the parser walks through all
descriptors within `wTotalLength` bytes. Two conditions can cause an infinite
loop:

1. **bLength = 0**: If a descriptor has `bLength = 0`, `tu_desc_next()` returns
   the same pointer — the parser never advances and loops forever on the same
   descriptor.

2. **wTotalLength > actual data size**: If `wTotalLength` is larger than the
   actual data received from the device, the parser continues reading past valid
   data into uninitialized or garbage memory. Combined with (1), if the garbage
   memory contains a zero byte at the bLength position, the parser hangs.

A malicious or malfunctioning USB device can trivially craft a configuration
descriptor with either condition. This is particularly dangerous for USB host
applications on embedded systems that cannot recover from a hung task without
a watchdog reset.

## Trigger

1. Connect a malicious USB device to a host running TinyUSB.
2. The device responds to `GET_DESCRIPTOR(Configuration)` with a descriptor
   that has:
   - `wTotalLength = 256` (claiming 256 bytes of descriptor data)
   - Actual data: 64 bytes, with the last descriptor having `bLength = 0`
3. The host parser enters `enum_parse_configuration_desc()`:
   ```
   while (p_desc < desc_end) {   // desc_end = desc_cfg + 256
     // p_desc points to descriptor with bLength = 0
     p_desc = tu_desc_next(p_desc);  // p_desc += 0 → no progress
     // Loop repeats infinitely
   }
   ```
4. The USB host task hangs forever, preventing any further USB enumeration
   or communication.

Alternatively, a device can report `wTotalLength = 0xFFFF` with only a few
bytes of actual descriptor data. The parser reads past the descriptor buffer
and eventually encounters a zero byte, triggering the same hang.

## Impact

- **Type**: Infinite loop → Denial of Service
- **What gets corrupted**: No memory corruption, but the USB host task becomes
  permanently unresponsive
- **DoS**: Complete USB host stack hang. No new devices can be enumerated.
  Existing devices may time out and become unresponsive. On RTOS-based systems
  without a watchdog, the system requires a power cycle.
- **Attack surface**: Any untrusted USB device connected to a TinyUSB host
  (e.g., USB keyboards, flash drives, network adapters)

## PoC

```c
// Malicious USB device descriptor response
uint8_t config_desc[] = {
    // Configuration descriptor
    9,                  // bLength
    TUSB_DESC_CONFIGURATION,
    0x00, 0x01,         // wTotalLength = 256 (lie: actual data is 18 bytes)
    1,                  // bNumInterfaces
    1,                  // bConfigurationValue
    0,                  // iConfiguration
    0x80,               // bmAttributes
    50,                 // bMaxPower

    // Interface descriptor
    9,                  // bLength
    TUSB_DESC_INTERFACE,
    0, 0, 0, 0xFF, 0, 0, 0,

    // Zero-length descriptor (triggers infinite loop)
    0,                  // bLength = 0 ← MALICIOUS
    0xFF,               // bDescriptorType (doesn't matter)
};
// Host parser loops forever at offset 18
```

## Fix

PR #2852 added a zero-length descriptor check in the parsing loop:

```c
while (tu_desc_in_bounds(p_desc, desc_end)) {
  if (0 == tu_desc_len(p_desc)) {
    // A zero-length descriptor is off-spec (e.g., wrong wTotalLength).
    // Parsed interfaces should still be usable.
    TU_LOG_USBH("Encountered a zero-length descriptor after %u bytes\r\n",
                 (uint32_t)p_desc - (uint32_t)desc_cfg);
    break;
  }
  // ... normal parsing continues ...
}
```

When `bLength = 0` is detected, the parser breaks out of the loop and
continues with the interfaces already parsed, rather than hanging indefinitely.

## Related

- [[ncm-stack-overflow-recursion]] — Another unbounded execution bug in NCM
- [[ep0-desc-stack-corruption]] — Host-side descriptor handling issue
