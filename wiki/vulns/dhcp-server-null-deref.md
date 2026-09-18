# DHCP Server NULL Pointer Dereference in udp_recv_proc()

| Field | Value |
|-------|-------|
| **CWE** | CWE-476: NULL Pointer Dereference |
| **CVSS** | 6.5 (Medium) |
| **Verdict** | real / real for networking use case |
| **File** | `lib/networking/dhserver.c` |
| **Function** | `udp_recv_proc()` |
| **Status** | Open |
| **Source** | student (syn_zheng_yi) |

## Root Cause

In `udp_recv_proc()` (line 287), the network interface pointer is obtained via `netif_get_by_index(p->if_idx)`:

```c
struct netif *netif = netif_get_by_index(p->if_idx);
```

When a DHCP packet arrives via the raw IP input path (`netif_input` → `ip_input`) instead of the Ethernet path (`ethernet_input`), the pbuf's `if_idx` field is never populated by lwIP — it remains 0. The `netif_get_by_index(0)` call returns `NULL` because index 0 is reserved.

The NULL `netif` pointer is subsequently dereferenced at multiple locations without any check:

- Line 327: `*netif_ip4_addr(netif)` inside `fill_options()` for DHCP OFFER
- Line 329: `*netif_ip4_netmask(netif)` inside `fill_options()` for DHCP OFFER
- Line 371: Same dereferences for DHCP ACK path

These dereferences cause an immediate segmentation fault / hard fault.

## Trigger

1. The TinyUSB device is running the networking stack with the DHCP server enabled.
2. An attacker sends a single crafted UDP packet to port 67 (DHCP server port).
3. The packet enters via the raw IP input path (e.g., when the device uses a non-Ethernet network interface, or when packets bypass the Ethernet layer).
4. `p->if_idx` is 0 → `netif_get_by_index(0)` returns NULL → immediate crash on the first `netif_ip4_addr(netif)` call.

No authentication is required. A single packet is sufficient.

## Impact

- **DoS**: Immediate crash (SEGV on hosted systems, hard fault on bare-metal embedded devices).
- On embedded devices without an OS, this causes a device reset.
- The DHCP server is reachable from any device on the local network segment, making this trivially exploitable in network-enabled TinyUSB deployments.
- This is a pre-authentication, zero-interaction crash — the packet only needs to reach UDP port 67.

## Fix

Add a NULL check for `netif` immediately after retrieval:

```c
struct netif *netif = netif_get_by_index(p->if_idx);
if (netif == NULL) {
  pbuf_free(p);
  return;
}
```

This should be inserted at line 292, before any code that dereferences `netif`.

## Related

- Reported by syn_zheng_yi with ASan-confirmed crash trace
- lwIP `netif_get_by_index()` returns NULL for index 0 by design
- Similar NULL-deref patterns may exist in other lwIP callbacks that assume `if_idx` is always valid
