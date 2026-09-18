# FIFO Write Overflow in Overwritable Mode

| Field | Value |
|-------|-------|
| **CWE** | CWE-787: Out-of-bounds Write |
| **CVSS** | 6.8 (Medium) |
| **Verdict** | real |
| **File** | `src/common/tusb_fifo.c` |
| **Function** | `tu_fifo_write_n()` / `_tu_fifo_remaining()` |
| **Status** | Fixed (PR #1789) |
| **Source** | github-issue |

## Root Cause

When `tu_fifo_t` is configured in overwritable mode (`overwritable = true`),
the internal helper `_tu_fifo_remaining()` returned an incorrect count of
available space in the ring buffer. Specifically, when the write index had
wrapped around and the buffer was full, the remaining-space calculation
produced a value greater than zero instead of correctly returning zero or the
buffer depth.

The overwritable mode is designed to allow new writes to silently overwrite the
oldest unread data (like a circular log buffer). However, the flawed remaining
calculation caused `tu_fifo_write_n()` to believe there was more contiguous
space than actually existed, leading to a `memcpy` that wrote past the end of
the FIFO's backing `buffer` array.

The root issue was an off-by-one in the index arithmetic: when `wr_idx` had
wrapped to a position before `rd_idx`, the remaining space was calculated as
`depth - (wr_idx - rd_idx)` without accounting for the overwritable flag's
special semantics around the full-buffer condition.

## Trigger

1. Configure a `tu_fifo_t` with `overwritable = true` and a small depth
   (e.g., 8 items).
2. Fill the FIFO completely so that `wr_idx` catches up to `rd_idx`.
3. Without reading any items, call `tu_fifo_write_n()` with a batch of items
   that exceeds the buffer depth.
4. The write proceeds with an inflated remaining-space count, causing `memcpy`
   to overflow the buffer.

This can be triggered by any USB class driver that uses an overwritable FIFO
for logging or telemetry, when the consumer (read side) is slower than the
producer (write side from USB interrupt/task context).

## Impact

- **Type**: Heap/global buffer overflow (write)
- **Bytes overflowed**: Up to `n - actual_remaining` items × `item_size` bytes
  past the end of the FIFO buffer
- **What gets corrupted**: Memory immediately following the FIFO buffer —
  typically other driver state, endpoint buffers, or FIFO metadata (rd_idx,
  wr_idx, depth) which can cascade into further corruption
- **Severity**: On bare-metal embedded targets without memory protection, this
  can corrupt arbitrary adjacent data structures. On targets with MPU, it
  triggers a memory fault (DoS).

## PoC

```c
// Minimal reproduction
tu_fifo_t fifo;
uint8_t buf[8];
tu_fifo_config(&fifo, buf, 8, 1, true); // overwritable = true

// Fill completely
uint8_t data[8] = {0,1,2,3,4,5,6,7};
tu_fifo_write_n(&fifo, data, 8);

// Now write more without reading — triggers overflow
uint8_t overflow_data[16] = {0};
tu_fifo_write_n(&fifo, overflow_data, 16);
// memcpy writes 16 bytes into 8-byte buffer
```

## Fix

PR #1789 corrected the `_tu_fifo_remaining()` calculation for overwritable
FIFOs to properly return the buffer depth (not free space) when the buffer is
full, and ensured that `tu_fifo_write_n()` clamps the write count:

```c
// Corrected remaining calculation for overwritable mode:
// When overwritable and buffer is full, remaining = depth (we can overwrite everything)
// But the actual memcpy must wrap properly within the buffer bounds
static uint16_t _tu_fifo_remaining(tu_fifo_t* f, uint16_t wr_idx, uint16_t rd_idx) {
  // ... corrected wrap-around arithmetic ...
}
```

The fix ensures that even in overwritable mode, the memcpy destination is
always computed modulo the buffer depth, preventing any write past the
buffer boundary.

## Related

- [[cdc-out-endpoint-buffer-overflow]] — Similar write-past-buffer in CDC class
