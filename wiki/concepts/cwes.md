# CWE Taxonomy of TinyUSB Findings

## Distribution

| CWE | Count | Category | Description |
|-----|-------|----------|-------------|
| CWE-125 | 10 | Memory | Out-of-bounds Read |
| CWE-787 | 6 | Memory | Out-of-bounds Write |
| CWE-129 | 4 | Input | Improper Validation of Array Index |
| CWE-190 | 3 | Numeric | Integer Overflow or Wraparound |
| CWE-120 | 2 | Memory | Buffer Copy without Checking Size of Input |
| CWE-674 | 1 | Control | Uncontrolled Recursion |
| CWE-835 | 1 | Control | Loop with Unreachable Exit Condition |
| CWE-562 | 1 | Memory | Return of Stack Variable Address |
| CWE-681 | 1 | Numeric | Incorrect Conversion between Numeric Types |
| CWE-476 | 1 | Pointer | NULL Pointer Dereference |

## Analysis

**80% of findings (22/27) are memory safety issues** (CWE-125, CWE-787, CWE-120,
CWE-129, CWE-562). This is expected for a C codebase processing untrusted binary
data without runtime bounds checking.

**The dominant pattern is unchecked indexing** — USB data used as array indices
(endpoint numbers, LUN values, device addresses) or buffer sizes (wLength,
DataLength, wLength in NDP16) without validation against the actual allocation.

**Integer arithmetic issues** (CWE-190, CWE-681) appear specifically in network
class drivers (RNDIS, NCM, MSC) where 32-bit addition-based bounds checks wrap
around, or where uint32 values are silently truncated to uint16.

## CWE hierarchy

```
CWE-119: Improper Restriction of Operations within Memory Buffer
├── CWE-125: Out-of-bounds Read (10 findings)
├── CWE-787: Out-of-bounds Write (6 findings)
└── CWE-120: Buffer Copy without Checking Size (2 findings)

CWE-682: Incorrect Calculation
├── CWE-190: Integer Overflow (3 findings)
└── CWE-681: Incorrect Conversion (1 finding)

CWE-129: Improper Validation of Array Index (4 findings)
CWE-674: Uncontrolled Recursion (1 finding)
CWE-835: Loop with Unreachable Exit (1 finding)
CWE-562: Return of Stack Variable Address (1 finding)
CWE-476: NULL Pointer Dereference (1 finding)
```
