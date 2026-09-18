# TinyUSB Vulnerability Wiki — Index

A catalog of every known memory-corruption vulnerability in TinyUSB's portable
USB stack (excluding hardware-specific DCD/HCD drivers like DWC2/EHCI/OHCI).

**Total: 27 vulnerabilities** | 6 real | 1 plausible | 20 weak/hardening | 15 fixed | 12 open

---

## By Component

### Network Class (RNDIS / ECM / NCM)
| Page | CWE | Severity | Verdict | Status |
|------|-----|----------|---------|--------|
| [RNDIS integer overflow in handle_incoming_packet](vulns/rndis-dataoffset-integer-overflow.md) | CWE-190 | Medium | real | Fixed (PR #3756) |
| [RNDIS wLength global buffer overflow](vulns/rndis-control-wlength-overflow.md) | CWE-125 | Medium | real | Open |
| [NCM NDP16 wLength OOB read](vulns/ncm-ndp16-wlength-oob-read.md) | CWE-125 | Medium | real | Fixed (PR #3741) |
| [NCM stack overflow via recursion](vulns/ncm-stack-overflow-recursion.md) | CWE-674 | Medium | real | Fixed (PR #2713) |
| [NCM tud_network_xmit OOB write](vulns/ncm-xmit-oob-write.md) | CWE-787 | Medium | weak | Open |
| [ECM/RNDIS tud_network_xmit OOB write](vulns/ecm-rndis-xmit-oob-write.md) | CWE-787 | Medium | weak | Open |

### HID Class
| Page | CWE | Severity | Verdict | Status |
|------|-----|----------|---------|--------|
| [HID host parse_report heap overflow](vulns/hid-host-parse-report-heap-overflow.md) | CWE-125 | Medium-High | real | Open |
| [HID host USAGE_PAGE memcpy overflow](vulns/hid-host-usage-page-memcpy-overflow.md) | CWE-120 | Medium | real | Open |
| [HID/DFU/BTH control xfer OOB read](vulns/hid-control-xfer-oob-read.md) | CWE-125 | Medium | real | Fixed (PRs #913+) |

### MSC Class
| Page | CWE | Severity | Verdict | Status |
|------|-----|----------|---------|--------|
| [MSC read10 OOB access](vulns/msc-read10-oob-access.md) | CWE-125 | Medium | weak | Fixed (PR #2939) |
| [MSC host capacity[] OOB](vulns/msc-host-capacity-oob.md) | CWE-129 | Medium | plausible | Open |
| [MSC host max_lun overflow](vulns/msc-host-maxlun-overflow.md) | CWE-190 | Low | weak | Open |
| [MSC host block integer overflow](vulns/msc-host-block-integer-overflow.md) | CWE-190 | Medium | weak | Open |
| [MSC device total_len truncation](vulns/msc-device-total-len-truncation.md) | CWE-681 | Low | weak | Open |

### USB Core (usbd.c / usbh.c / hub.c)
| Page | CWE | Severity | Verdict | Status |
|------|-----|----------|---------|--------|
| [ep2drv bind heap overflow](vulns/ep2drv-bind-heap-overflow.md) | CWE-787 | Medium | real | Open |
| [ep_status global overflow](vulns/ep-status-global-overflow.md) | CWE-787 | Medium | real | Open |
| [Hub driver array index underflow](vulns/hub-driver-array-underflow.md) | CWE-129 | Medium | real | Open |
| [Hub close OOB read](vulns/hub-close-oob-read.md) | CWE-125 | Medium | real | Open |
| [Host descriptor parser hang](vulns/host-descriptor-parser-hang.md) | CWE-834 | Medium | real | Fixed (PR #2852) |
| [EP0 descriptor stack corruption](vulns/ep0-desc-stack-corruption.md) | CWE-562 | Medium | real | Closed (by-design) |
| [Vendor host array underflow](vulns/vendor-host-array-underflow.md) | CWE-129 | Low | weak | Open |

### Common / FIFO
| Page | CWE | Severity | Verdict | Status |
|------|-----|----------|---------|--------|
| [FIFO memory overflow](vulns/fifo-memory-overflow.md) | CWE-787 | Medium | real | Fixed (PR #1789) |
| [Descriptor walk OOB + infinite loop](vulns/descriptor-walk-oob-infinite-loop.md) | CWE-125/835 | Medium | weak | Partial |

### Audio / Video / CDC
| Page | CWE | Severity | Verdict | Status |
|------|-----|----------|---------|--------|
| [Audio audiod_open heap overflow](vulns/audio-audiod-open-heap-overflow.md) | CWE-125 | Medium | weak | Open |
| [Video device ctrl_sel OOB](vulns/video-device-ctrl-sel-oob.md) | CWE-125 | Low | weak | Open |
| [CDC OUT endpoint buffer overflow](vulns/cdc-out-endpoint-buffer-overflow.md) | CWE-120 | Medium | real | Open (DWC2-specific) |

### Networking Library
| Page | CWE | Severity | Verdict | Status |
|------|-----|----------|---------|--------|
| [DHCP server null deref](vulns/dhcp-server-null-deref.md) | CWE-476 | Medium | real | Open |

---

## By CWE

| CWE | Count | Description |
|-----|-------|-------------|
| CWE-125 | 10 | Out-of-bounds Read |
| CWE-787 | 6 | Out-of-bounds Write |
| CWE-129 | 4 | Improper Validation of Array Index |
| CWE-190 | 3 | Integer Overflow or Wraparound |
| CWE-120 | 2 | Buffer Copy without Checking Size |
| CWE-674 | 1 | Uncontrolled Recursion |
| CWE-834/835 | 1 | Excessive/Unreachable Loop |
| CWE-562 | 1 | Return of Stack Variable Address |
| CWE-681 | 1 | Incorrect Numeric Conversion |
| CWE-476 | 1 | NULL Pointer Dereference |

## Concept Pages

- [Attack Surface](concepts/attack-surface.md)
- [CWE Taxonomy](concepts/cwes.md)
- [Exploit Patterns](concepts/exploit-patterns.md)
