# MSC READ10 Example Out-of-Bounds RAM Disk Access

| Field | Value |
|-------|-------|
| **CWE** | CWE-125: Out-of-bounds Read |
| **CVSS** | 4.6 (Medium) |
| **Verdict** | real |
| **File** | `examples/device/cdc_msc/src/msc_disk.c` |
| **Function** | `tud_msc_read10_cb()` |
| **Status** | Fixed (PR #2939) |
| **Source** | github-pr |

## Root Cause

The MSC (Mass Storage Class) example's `tud_msc_read10_cb()` callback, which
handles SCSI READ(10) commands from the host, contained an insufficient bounds
check before performing a `memcpy` from the RAM disk array.

The original code checked only whether the logical block address (`lba`) was
within bounds, but did **not** validate that `offset + bufsize` stayed within
the block:

```c
// VULNERABLE (before fix):
int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset,
                          void *buffer, uint32_t bufsize) {
  if (lba >= DISK_BLOCK_NUM) return -1;
  // Missing: no check that offset + bufsize <= DISK_BLOCK_SIZE

  uint8_t const *addr = msc_disk[lba] + offset;
  memcpy(buffer, addr, bufsize);
  return (int32_t)bufsize;
}
```

Where `DISK_BLOCK_SIZE = 512` and `DISK_BLOCK_NUM = 16`. The MSC device stack
calls this callback with `offset` and `bufsize` values derived from the SCSI
command and the endpoint buffer size. If `offset + bufsize > DISK_BLOCK_SIZE`
(512), the `memcpy` reads past the current block in the `msc_disk[][]` array
into adjacent blocks or past the entire array.

While the MSC stack typically constrains these values, a custom host or fuzzer
can invoke the callback with arbitrary parameters. The `cdc_msc` example ships
as reference code that many developers copy verbatim, amplifying the impact.

## Trigger

1. Connect to a device running the `cdc_msc` example.
2. Issue a SCSI READ(10) command targeting:
   - `lba = 15` (last valid block, index 15 of 16)
   - The MSC stack splits the read into chunks using `offset` and `bufsize`
   - If `offset = 256` and `bufsize = 512`, the read starts at byte 256 of
     block 15 and attempts to copy 512 bytes
   - `256 + 512 = 768 > 512 = DISK_BLOCK_SIZE` — reads 256 bytes past block 15
3. The `memcpy` reads 256 bytes from memory after `msc_disk[15]`, potentially
   from stack, heap, or BSS depending on the linker layout.

## Impact

- **Type**: Out-of-bounds read
- **Bytes readable**: Up to `bufsize - (DISK_BLOCK_SIZE - offset)` bytes past
  the target block; in worst case, up to the endpoint buffer size (typically
  512 or 1024 bytes) beyond the ramdisk array
- **What gets corrupted**: No write corruption; the OOB data is sent back to
  the host in the SCSI read response, potentially leaking stack/heap contents
  or other global variables stored adjacent to `msc_disk[]`
- **Scope**: This is example code, but it serves as the reference implementation
  that developers copy for their products. The same pattern may exist in
  production firmware derived from this example.

## PoC

```c
// Simulated callback invocation with OOB parameters
uint8_t buf[1024];
int32_t ret = tud_msc_read10_cb(
    0,      // lun
    15,     // lba = last block
    256,    // offset = halfway into block
    buf,    // buffer
    512     // bufsize = full block size
);
// memcpy reads 512 bytes starting at msc_disk[15][256]
// Only 256 bytes are within bounds; 256 bytes are OOB
```

## Fix

PR #2939 added a comprehensive overflow check:

```c
int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset,
                          void *buffer, uint32_t bufsize) {
  (void) lun;
  if (lba >= DISK_BLOCK_NUM) return -1;

  // Check for overflow of offset + bufsize
  if (lba * DISK_BLOCK_SIZE + offset + bufsize > DISK_BLOCK_NUM * DISK_BLOCK_SIZE) {
    return -1;
  }

  uint8_t const *addr = msc_disk[lba] + offset;
  memcpy(buffer, addr, bufsize);
  return (int32_t)bufsize;
}
```

The fix validates the total byte range `[lba * 512 + offset, lba * 512 + offset + bufsize)`
against the entire ramdisk capacity before copying.

## Related

- [[hid-control-xfer-oob-read]] — Similar missing length check in HID class
