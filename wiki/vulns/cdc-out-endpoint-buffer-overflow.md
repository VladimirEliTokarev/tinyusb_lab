# CDC OUT Endpoint Buffer Overflow (DWC2)

| Field | Value |
|-------|-------|
| **CWE** | CWE-120: Buffer Copy without Checking Size of Input |
| **CVSS** | 6.5 (Medium) |
| **Verdict** | real |
| **File** | `src/class/cdc/cdc_device.c`, `src/portable/synopsys/dwc2/dcd_dwc2.c` |
| **Function** | CDC OUT endpoint receive path / DWC2 DMA transfer |
| **Status** | Open |
| **Source** | github-issue |

## Root Cause

The CDC (Communications Device Class) device driver allocates an endpoint
buffer for the OUT (host-to-device) direction with a size determined by the
`CFG_TUD_CDC_RX_EPSIZE` configuration macro (default: `TUD_EPSIZE_BULK_MAX`,
which is 64 bytes for Full Speed or 512 bytes for High Speed).

The DWC2 (DesignWare USB 2.0 Controller) DCD (Device Controller Driver) has a
hardware behavior where the USB controller's DMA engine can write up to **2×
the max packet size** to the receive buffer in certain conditions:

1. **Back-to-back packets**: The DWC2 controller uses a ping-pong buffering
   scheme internally. When the software buffer provided is smaller than 2× the
   endpoint max packet size, the hardware DMA can write the second packet past
   the end of the software buffer.

2. **OUT transfers with no flow control**: For bulk OUT endpoints, the host
   can send packets continuously. The DWC2 hardware accepts and DMA-writes
   these packets even if the software hasn't processed the previous one.

The CDC device class configures its OUT endpoint with a buffer of exactly
`CFG_TUD_CDC_RX_EPSIZE` bytes. When this is less than 2× the max packet size
(e.g., `EP_BUFSIZE = 64` with `max_packet_size = 64`), the DWC2 DMA writes
up to 128 bytes into the 64-byte buffer, overflowing by up to 64 bytes.

## Trigger

1. Target: Any STM32 or other SoC using the Synopsys DWC2 USB controller with
   TinyUSB's CDC device class.
2. Configure `CFG_TUD_CDC_RX_EPSIZE` to be exactly the max packet size (64 for
   FS, 512 for HS) — which is the **default** configuration.
3. From the USB host, send two consecutive bulk OUT packets to the CDC data
   endpoint rapidly (e.g., send 128 bytes to a Full Speed CDC ACM serial port).
4. The DWC2 DMA engine writes both packets (128 bytes total) into the 64-byte
   endpoint buffer, overflowing by 64 bytes.

This is trivially triggered by any host sending serial data to a CDC ACM
device at moderate throughput. It does **not** require a malicious host — normal
operation of `screen /dev/ttyACM0` or `picocom` sending data quickly enough
will trigger it.

## Impact

- **Type**: Heap/global buffer overflow (write via DMA)
- **Bytes overflowed**: Up to `max_packet_size` bytes past the endpoint buffer
  (64 bytes for FS, 512 bytes for HS)
- **What gets corrupted**: Memory immediately after the CDC OUT endpoint buffer.
  Depending on the memory layout, this could be:
  - Other endpoint buffers (TX, notification)
  - CDC interface state structure (`cdcd_interface_t`)
  - FIFO metadata (read/write indices)
  - Application data structures
- **Severity**: On Full Speed (64-byte overflow), this typically corrupts
  adjacent CDC driver state. On High Speed (512-byte overflow), the blast
  radius is much larger and can corrupt unrelated driver state or application
  data.
- **Reliability**: This is a **normal-operation bug**, not just a security
  vulnerability. It can manifest as data corruption, CDC port hangs, or
  mysterious crashes during sustained serial communication.

## PoC

```c
// Host-side: send rapid bulk data to CDC OUT endpoint
// No special crafting needed — just sustained throughput

#include <libusb.h>

int main() {
    libusb_device_handle *dev;
    // ... open CDC ACM device ...

    uint8_t data[128];
    memset(data, 'A', sizeof(data));

    // Send 128 bytes in quick succession to FS CDC endpoint
    // DWC2 will DMA 128 bytes into the 64-byte EP buffer
    int transferred;
    libusb_bulk_transfer(dev, CDC_EP_OUT, data, 128, &transferred, 1000);

    // On the device side: memory after the 64-byte buffer is corrupted
    return 0;
}
```

```python
# Even simpler: just write to the serial port quickly
import serial
ser = serial.Serial('/dev/ttyACM0', 115200)
ser.write(b'A' * 4096)  # Sustained writes trigger DWC2 overflow
```

## Fix

The recommended fix is to ensure the endpoint buffer is **at least 2× the max
packet size** for DWC2 targets:

```c
// Option 1: In tusb_config.h for DWC2 targets
#define CFG_TUD_CDC_RX_EPSIZE (2 * TUD_EPSIZE_BULK_MAX)

// Option 2: In the DWC2 DCD driver, enforce minimum buffer size
// during endpoint open:
bool dcd_edpt_open(uint8_t rhport, tusb_desc_endpoint_t const *desc_ep) {
  uint16_t mps = tu_edpt_packet_size(desc_ep);
  // Require buffer >= 2*MPS for DWC2 ping-pong DMA
  TU_ASSERT(ep_buf_size >= 2 * mps);
  // ...
}

// Option 3: Use DWC2 DOEPTSIZ.PKTCNT=1 to receive one packet at a time
// (reduces throughput but prevents overflow)
```

The issue remains **OPEN** because the fix requires either a configuration
change (breaking backward compatibility) or a DCD-level enforcement that may
reduce throughput on DWC2 targets.

## Related

- [[fifo-memory-overflow]] — Another write-past-buffer overflow
- [[hid-control-xfer-oob-read]] — Endpoint buffer sizing issue in HID class
