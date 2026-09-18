# Hub Driver Array Index Underflow

| Field | Value |
|-------|-------|
| **CWE** | CWE-129: Improper Validation of Array Index |
| **CVSS** | 4.6 (Medium) |
| **Verdict** | real |
| **File** | `src/host/hub.c` |
| **Function** | `get_hub_itf()` |
| **Status** | Open |
| **Source** | student-sbingqua (BUG-015) |

## Root Cause

The `get_hub_itf()` inline function at line 65 of `hub.c` computes an array index without validating the input:

```c
TU_ATTR_ALWAYS_INLINE static inline hub_interface_t* get_hub_itf(uint8_t daddr) {
  return &hub_itfs[daddr-1-CFG_TUH_DEVICE_MAX];
}
```

The `hub_itfs` array has `CFG_TUH_HUB` elements. The index `daddr - 1 - CFG_TUH_DEVICE_MAX` assumes `daddr` is always in the valid hub address range (`CFG_TUH_DEVICE_MAX + 1` through `CFG_TUH_DEVICE_MAX + CFG_TUH_HUB`). If `daddr` is less than `CFG_TUH_DEVICE_MAX + 1`, the subtraction underflows (unsigned arithmetic wraps), producing an enormous index that accesses memory far outside the `hub_itfs` array.

The same pattern exists in `get_hub_epbuf()` at line 69.

## Trigger

Any code path that calls `get_hub_itf()` with an invalid `dev_addr` — for example, if `hub_close()` or `hub_xfer_cb()` is reached with a device address of 0 or a non-hub address. A malicious USB device that manipulates hub enumeration state or triggers unexpected bus events can cause this.

## Impact

**Array out-of-bounds access** — the underflowed index accesses memory well before the `hub_itfs` array in the `.bss` or `.data` segment. This constitutes:

- **OOB read**: reading fields from a bogus `hub_interface_t` pointer
- **OOB write**: if the returned pointer is used for writes (e.g., zeroing the struct in `hub_close`)
- On embedded targets with flat memory, this can corrupt firmware globals or peripheral registers

## PoC

```c
// Assume CFG_TUH_DEVICE_MAX = 4, CFG_TUH_HUB = 1
// hub_itfs is a 1-element array

// Normal: daddr=5 → index = 5-1-4 = 0 ✓
// Bug:    daddr=0 → index = 0-1-4 = 251 (uint8_t wraps) → OOB

// Calling hub_close(0) triggers:
hub_interface_t* p_hub = get_hub_itf(0);
// p_hub now points 251 * sizeof(hub_interface_t) bytes past hub_itfs
tu_memclr(p_hub, sizeof(hub_interface_t));  // corrupts memory
```

## Fix

Add bounds validation in `get_hub_itf()` and all callers:

```c
TU_ATTR_ALWAYS_INLINE static inline hub_interface_t* get_hub_itf(uint8_t daddr) {
  TU_ASSERT(daddr > CFG_TUH_DEVICE_MAX &&
            daddr <= CFG_TUH_DEVICE_MAX + CFG_TUH_HUB, NULL);
  return &hub_itfs[daddr - 1 - CFG_TUH_DEVICE_MAX];
}
```

## Related

- [[hub-close-oob-read]] — same root cause, different trigger path (waynelow variant)
- [[vendor-host-array-underflow]] — identical pattern in vendor_host.c
