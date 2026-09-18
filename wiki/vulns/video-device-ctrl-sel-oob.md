# Video Device OOB Array Read via Unchecked ctrl_sel

| Field | Value |
|-------|-------|
| **CWE** | CWE-125: Out-of-bounds Read |
| **CVSS** | 3.5 (Low) |
| **Verdict** | weak |
| **File** | `src/class/video/video_device.c` |
| **Function** | `handle_video_ctl_cs_req()` / `handle_video_stm_cs_req()` |
| **Status** | Open |
| **Source** | student (sbingqua BUG-012) |

## Root Cause

In `handle_video_ctl_cs_req()` (line 959) and `handle_video_stm_cs_req()` (line 1078), the `ctrl_sel` value extracted from the USB control request is used directly as an index into small fixed-size string arrays without bounds checking:

```c
// Line 958-959 (VC control)
uint8_t const ctrl_sel = TU_U16_HIGH(request->wValue);
TU_LOG_DRV("%s_Control(%s)\r\n", tu_str_video_vc_control_selector[ctrl_sel], ...);

// Line 1077-1078 (VS control)
uint8_t const ctrl_sel = TU_U16_HIGH(request->wValue);
TU_LOG_DRV("%s_Control(%s)\r\n", tu_str_video_vs_control_selector[ctrl_sel], ...);
```

The arrays are small:
- `tu_str_video_vc_control_selector[]` has 3 entries (indices 0–2)
- `tu_str_video_vs_control_selector[]` has 9 entries (indices 0–8)

But `ctrl_sel` is a `uint8_t` (range 0–255), so any value above the array size causes an out-of-bounds read. The read pointer is then passed to `TU_LOG_DRV()` → `printf`-family function, which dereferences it as a string.

**Note**: This code path is only active when `CFG_TUSB_DEBUG >= 1` (logging enabled), making it a debug-only issue.

## Trigger

1. USB Video Class device firmware has logging enabled (`CFG_TUSB_DEBUG >= 1`).
2. A USB host sends a control request with `wValue` high byte set to a value ≥ 3 (for VC) or ≥ 9 (for VS).
3. The `TU_LOG_DRV` macro indexes past the end of the string array.
4. The resulting pointer is dereferenced by the logging function as a null-terminated string.

## Impact

- **Read**: Out-of-bounds read of memory adjacent to the string arrays in `.rodata`. The `%s` format specifier in `TU_LOG_DRV` will read bytes until it encounters a null terminator.
- **DoS**: If the out-of-bounds pointer lands in unmapped memory (common on embedded targets with MPU), this causes a hard fault / device crash.
- **Info leak**: On targets with UART/RTT debug output, the OOB read contents are transmitted to the debug console, potentially leaking adjacent read-only data (other strings, constants).
- Severity is limited because this only triggers in debug builds.

## Fix

Add bounds checks before using `ctrl_sel` as an array index:

```c
// For VC control
uint8_t const ctrl_sel = TU_U16_HIGH(request->wValue);
char const* ctrl_name = (ctrl_sel < TU_ARRAY_SIZE(tu_str_video_vc_control_selector))
    ? tu_str_video_vc_control_selector[ctrl_sel] : "Unknown";
TU_LOG_DRV("%s_Control(%s)\r\n", ctrl_name, ...);

// For VS control
char const* ctrl_name = (ctrl_sel < TU_ARRAY_SIZE(tu_str_video_vs_control_selector))
    ? tu_str_video_vs_control_selector[ctrl_sel] : "Unknown";
TU_LOG_DRV("%s_Control(%s)\r\n", ctrl_name, ...);
```

## Related

- Debug-only path — requires `CFG_TUSB_DEBUG >= 1`
- Similar unchecked index patterns may exist in other USB class debug logging
