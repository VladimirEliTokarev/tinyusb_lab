# MSC Host Capacity Array — OOB via Unchecked LUN

| Field | Value |
|-------|-------|
| **CWE** | CWE-129: Improper Validation of Array Index |
| **CVSS** | 5.3 (Medium) |
| **Verdict** | plausible |
| **File** | `src/class/msc/msc_host.c` |
| **Function** | `tuh_msc_get_block_count()` / `tuh_msc_get_block_size()` |
| **Status** | Open |
| **Source** | student-sbingqua (BUG-008) |

## Root Cause

The MSC host interface stores per-LUN capacity data in a fixed-size array:

```c
struct {
  uint32_t block_size;
  uint32_t block_count;
} capacity[CFG_TUH_MSC_MAXLUN];
```

The public API functions `tuh_msc_get_block_count()` and `tuh_msc_get_block_size()` (lines 109–116) accept a `lun` parameter and index directly into this array without validation:

```c
uint32_t tuh_msc_get_block_count(uint8_t dev_addr, uint8_t lun) {
  msch_interface_t* p_msc = get_itf(dev_addr);
  return p_msc->capacity[lun].block_count;   // no bounds check on lun
}

uint32_t tuh_msc_get_block_size(uint8_t dev_addr, uint8_t lun) {
  msch_interface_t* p_msc = get_itf(dev_addr);
  return p_msc->capacity[lun].block_size;    // no bounds check on lun
}
```

If `lun >= CFG_TUH_MSC_MAXLUN`, the access reads past the `capacity` array, leaking data from subsequent fields in `msch_interface_t` or from adjacent structs in the `_msch_itf` array.

## Trigger

A malicious MSC device reports a `max_lun` value greater than `CFG_TUH_MSC_MAXLUN` during the GET_MAX_LUN request. If the application then iterates LUNs up to the reported value and calls `tuh_msc_get_block_count()` or `tuh_msc_get_block_size()` for each, the out-of-bounds access occurs.

Alternatively, application code that directly passes an unchecked LUN value to these APIs triggers the issue.

## Impact

**Out-of-bounds read** — reads `uint32_t` values from memory past the `capacity` array. The `capacity` array is followed by other fields in `msch_interface_t` (e.g., `mounted`, `stage`, `buffer`, `complete_cb`). This can leak:

- Function pointers (`complete_cb`) → defeats ASLR on host systems
- DMA buffer addresses
- Internal state flags

The verdict is **plausible** because triggering it requires the application to pass an unchecked LUN value, which depends on how the host application handles `max_lun`.

## PoC

```c
// Assume CFG_TUH_MSC_MAXLUN = 4
// A malicious device reports max_lun = 15

// Application code (common pattern):
uint8_t max_lun = tuh_msc_get_maxlun(dev_addr);
for (uint8_t lun = 0; lun <= max_lun; lun++) {
  uint32_t blocks = tuh_msc_get_block_count(dev_addr, lun);
  // When lun >= 4: reads past capacity[] array
  // lun=4 reads from 'mounted' and 'stage' fields
  // lun=8+ reads from adjacent _msch_itf[] entry
  printf("LUN %u: %u blocks\n", lun, blocks);
}
```

## Fix

Add bounds validation on `lun`:

```c
uint32_t tuh_msc_get_block_count(uint8_t dev_addr, uint8_t lun) {
  msch_interface_t* p_msc = get_itf(dev_addr);
  TU_VERIFY(lun < CFG_TUH_MSC_MAXLUN, 0);
  return p_msc->capacity[lun].block_count;
}
```

Also clamp `max_lun` during GET_MAX_LUN processing:

```c
p_msc->max_lun = TU_MIN(max_lun_response, CFG_TUH_MSC_MAXLUN - 1);
```

## Related

- [[hub-driver-array-underflow]] — similar unchecked array index pattern
- [[ep2drv-bind-heap-overflow]] — unchecked endpoint number as array index
