# NCM Infinite Recursion Stack Overflow

| Field | Value |
|-------|-------|
| **CWE** | CWE-674: Uncontrolled Recursion |
| **CVSS** | 6.1 (Medium) |
| **Verdict** | real |
| **File** | `src/class/net/ncm_device.c` |
| **Function** | `tud_network_recv_renew()` → `recv_transfer_datagram_to_glue_logic()` → `tud_network_recv_cb()` → `tud_network_recv_renew()` |
| **Status** | Fixed (PR #2713) |
| **Source** | github-issue |

## Root Cause

The NCM device driver has a circular call dependency between the application
glue layer and the driver's receive path:

1. `tud_network_recv_renew()` — called by the application to indicate it is
   ready to receive the next datagram
2. `recv_transfer_datagram_to_glue_logic()` — transfers the next datagram
   from the NTB to the application via callback
3. `tud_network_recv_cb()` — application callback that processes the datagram
4. The application calls `tud_network_recv_renew()` again from within the
   callback to accept the next datagram

This creates an unbounded recursion cycle:

```
tud_network_recv_renew()
  → recv_transfer_datagram_to_glue_logic()
    → tud_network_recv_cb()
      → tud_network_recv_renew()
        → recv_transfer_datagram_to_glue_logic()
          → tud_network_recv_cb()
            → ... (infinite recursion)
```

Each recursion level adds a stack frame (~64-128 bytes depending on
architecture). With embedded targets having limited stack space (typically
2-8 KB), even a modest NTB containing 6 datagrams could exhaust the stack
in 20-40 recursion levels, causing a stack overflow and crash.

The original `ecm_rndis_device.c` (RNDIS variant) has the same pattern but is
less likely to trigger deep recursion because RNDIS NTBs typically contain only
a single datagram. NCM NTBs, by design, aggregate multiple datagrams (up to
`CFG_TUD_NCM_OUT_MAX_DATAGRAMS_PER_NTB`, typically 6+), making this recursion
reliably exploitable.

## Trigger

1. Connect to a device running TinyUSB's NCM network class.
2. Send a valid NCM NTB containing multiple datagrams (e.g., 6 small Ethernet
   frames packed into one NTB).
3. The application's `tud_network_recv_cb()` processes each datagram and
   immediately calls `tud_network_recv_renew()` to request the next one.
4. For each datagram in the NTB, one recursion level is added.
5. With enough datagrams per NTB (or enough queued NTBs), the stack overflows.

In practice, a legitimate network stack (e.g., lwIP) calling
`tud_network_recv_renew()` from its receive callback is enough to trigger this
on any NTB with more than one datagram.

## Impact

- **Type**: Stack overflow → crash (Denial of Service)
- **Bytes consumed**: ~64-128 bytes per recursion level × number of datagrams
- **What gets corrupted**: Stack memory — return addresses, saved registers,
  local variables of the call chain. On targets without stack guard pages,
  the overflow corrupts the heap or other data segments below the stack.
- **DoS**: Guaranteed crash. On ARM Cortex-M targets, this manifests as a
  UsageFault (stack overflow) or HardFault. The device becomes completely
  unresponsive and requires a reset.

## PoC

```c
// Application glue code that triggers the recursion
bool tud_network_recv_cb(const uint8_t *src, uint16_t size) {
  // Process the Ethernet frame
  process_frame(src, size);
  // Signal readiness for next datagram — triggers recursion!
  tud_network_recv_renew();
  return true;
}

// Send an NCM NTB with 6 datagrams from the host
// -> 6 levels of recursion per NTB
// -> with NTBs queued, recursion depth multiplies
```

## Fix

PR #2713 introduced a re-entrancy guard and an iterative loop to replace
the recursive call pattern:

```c
void tud_network_recv_renew(void) {
  ncm_interface.tud_network_recv_renew_process_again = true;

  if (ncm_interface.tud_network_recv_renew_active) {
    // Re-entrant call detected — will process in the outer loop
    return;
  }

  while (ncm_interface.tud_network_recv_renew_process_again) {
    ncm_interface.tud_network_recv_renew_process_again = false;
    ncm_interface.tud_network_recv_renew_active = true;
    recv_transfer_datagram_to_glue_logic();
    ncm_interface.tud_network_recv_renew_active = false;
  }
  recv_try_to_start_new_reception(ncm_interface.rhport);
}
```

When `tud_network_recv_cb()` calls `tud_network_recv_renew()` recursively,
the guard (`tud_network_recv_renew_active`) detects re-entry and sets the
`process_again` flag instead of recursing. The outer `while` loop then
processes remaining datagrams iteratively with O(1) stack depth.

## Related

- [[host-descriptor-parser-hang]] — Another unbounded loop / infinite execution bug
