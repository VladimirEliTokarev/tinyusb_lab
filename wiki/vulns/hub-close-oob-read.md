# Hub Close / get_hub_itf — OOB Read via Unbounded daddr

| Field | Value |
|-------|-------|
| **CWE** | CWE-125: Out-of-bounds Read |
| **CVSS** | 5.4 (Medium) |
| **Verdict** | real |
| **File** | `src/host/hub.c` |
| **Function** | `hub_close()` / `get_hub_itf()` |
| **Status** | Open |
| **Source** | waynelow (WAYNELOW-4) |

## Root Cause

The `get_hub_itf()` function computes an array index from the device address with no upper-bound validation:

```c
TU_ATTR_ALWAYS_INLINE static inline hub_interface_t* get_hub_itf(uint8_t daddr) {
  return &hub_itfs[daddr - 1 - CFG_TUH_DEVICE_MAX];
}
```

The `hub_itfs` array has `CFG_TUH_HUB` elements (typically 1). The index formula `daddr - 1 - CFG_TUH_DEVICE_MAX` is correct only when `daddr` is in the range `[CFG_TUH_DEVICE_MAX+1, CFG_TUH_DEVICE_MAX+CFG_TUH_HUB]`.

When `daddr` is large (≥ `CFG_TUH_DEVICE_MAX + CFG_TUH_HUB + 1`, e.g., 6 or higher with default configs), the index exceeds the array bounds. With `daddr = 6`, `CFG_TUH_DEVICE_MAX = 4`, `CFG_TUH_HUB = 1`: index = 6-1-4 = 1, but array only has 1 element (index 0). Larger `daddr` values read up to 250 elements past the array.

## Trigger

`hub_close()` is called during device disconnection. If the USB host stack's device address tracking is corrupted (e.g., due to a prior bug) or if a malicious hub manipulates the bus topology to cause `hub_close()` to be called with an out-of-range address, the OOB access occurs.

```c
// hub.c - hub_close() calls:
hub_interface_t* p_hub = get_hub_itf(daddr);
// Then reads p_hub->ep_in, p_hub->itf_num, etc.
```

## Impact

**Out-of-bounds read** — reads up to ~250 `hub_interface_t`-sized chunks past the `hub_itfs` array. On a host system, this reads from `.bss` or `.data` globals, potentially leaking:

- Endpoint buffer contents
- Device addresses and configuration state
- DMA buffer pointers

On embedded targets with flat memory, reading far enough could hit peripheral register space, causing undefined hardware behavior.

## PoC

The researcher (waynelow) provided `poc.c`:

```c
// With CFG_TUH_DEVICE_MAX = 4, CFG_TUH_HUB = 1:
// hub_itfs is 1 element

// Trigger hub_close with various daddr values:
for (uint8_t daddr = 6; daddr < 255; daddr++) {
  hub_interface_t* p = get_hub_itf(daddr);
  // p points (daddr-5) elements past hub_itfs[0]
  // Reading p->ep_in accesses memory at:
  //   &hub_itfs[0] + (daddr-5) * sizeof(hub_interface_t)
}
```

See `students/waynelow/bug4_hub_close_oob_read/poc.c` for the full reproducer.

## Fix

Add bounds validation before array access:

```c
TU_ATTR_ALWAYS_INLINE static inline hub_interface_t* get_hub_itf(uint8_t daddr) {
  uint8_t idx = daddr - 1 - CFG_TUH_DEVICE_MAX;
  TU_ASSERT(idx < CFG_TUH_HUB, NULL);
  return &hub_itfs[idx];
}
```

The same fix should be applied to `get_hub_epbuf()`.

## Related

- [[hub-driver-array-underflow]] — same function, underflow variant (BUG-015)
- [[vendor-host-array-underflow]] — identical pattern in vendor_host.c
