# Vendor Host — Array Index Underflow on dev_addr

| Field | Value |
|-------|-------|
| **CWE** | CWE-129: Improper Validation of Array Index |
| **CVSS** | 3.5 (Low) |
| **Verdict** | weak |
| **File** | `src/class/vendor/vendor_host.c` |
| **Function** | `tusbh_custom_read()` / `tusbh_custom_write()` / multiple |
| **Status** | Open |
| **Source** | student-sbingqua (BUG-016) |

## Root Cause

The vendor host class driver uses `dev_addr - 1` as an array index throughout the file without validation:

```c
custom_interface_info_t custom_interface[CFG_TUH_DEVICE_MAX];

// In tusbh_custom_read() at line 64:
if ( !hcd_pipe_is_idle(custom_interface[dev_addr-1].pipe_in) )

// In tusbh_custom_write() at line 78:
if ( !hcd_pipe_is_idle(custom_interface[dev_addr-1].pipe_out) )
```

When `dev_addr` is 0, the expression `dev_addr - 1` evaluates to 255 (unsigned underflow for `uint8_t`) or a very large value, causing access far outside the `custom_interface` array.

This is the same class of bug as the hub driver array underflow (BUG-015), repeated across a different class driver.

## Trigger

The vendor host APIs are called with `dev_addr = 0`. This can happen if:

1. Application code passes an uninitialized or default device address
2. A bus error during enumeration leaves the device address at 0
3. A malicious USB device manipulates the host stack's address tracking

The `cush_validate_paras()` function does check `tusbh_custom_is_mounted()` first, which may reject `dev_addr = 0` in some configurations. However, if the mounted check passes (e.g., due to corrupted state from another bug), the underflow occurs.

## Impact

**Out-of-bounds read** — reads `custom_interface_info_t` fields from memory 255 elements before the array start (or up to ~255 * `sizeof(custom_interface_info_t)` bytes backward in memory). On embedded targets:

- Reads from potentially unmapped or peripheral memory regions
- Could cause a hard fault / bus fault
- May leak data from unrelated memory regions

The verdict is **weak** because:
- The `mounted` check provides a partial guard in the normal path
- The vendor host driver appears to be legacy/less-maintained code
- Reaching the vulnerable code requires specific host-side conditions

## PoC

```c
// Assume CFG_TUH_DEVICE_MAX = 4
// custom_interface[4] is at some .bss address

// Call with dev_addr = 0:
tusb_error_t err = tusbh_custom_read(0, vendor_id, product_id, buf, len);

// Inside: custom_interface[0 - 1] = custom_interface[255]
// Reads from &custom_interface[0] + 255 * sizeof(custom_interface_info_t)
// → far past array bounds

// Or with dev_addr = 0 in the open callback path:
// custom_interface[dev_addr-1].pipe_in = ...  → writes to [255]
```

## Fix

Add bounds validation at the start of each function:

```c
static inline bool validate_dev_addr(uint8_t dev_addr) {
  return (dev_addr > 0) && (dev_addr <= CFG_TUH_DEVICE_MAX);
}

tusb_error_t tusbh_custom_read(uint8_t dev_addr, ...) {
  TU_ASSERT(validate_dev_addr(dev_addr), TUSB_ERROR_INVALID_PARA);
  // ...
}
```

Alternatively, adopt the same `get_itf()` pattern used in other class drivers with built-in bounds checking.

## Related

- [[hub-driver-array-underflow]] — identical `addr - 1` pattern in hub.c (BUG-015)
- [[hub-close-oob-read]] — same root cause with upper-bound overflow (WAYNELOW-4)
