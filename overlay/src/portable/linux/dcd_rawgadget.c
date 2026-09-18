/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2024 TinyUSB contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * DCD driver for Linux Raw Gadget (/dev/raw-gadget).
 *
 * Uses the Raw Gadget kernel interface (ioctls on /dev/raw-gadget) to
 * present a TinyUSB device to the Linux kernel's USB host stack via
 * dummy_hcd or a real UDC. Real USB transactions flow through the
 * kernel's USB core — the device appears in lsusb and gets real
 * /dev/ttyACM0 etc.
 *
 * Compile as native Linux x86/x64 with the linux_native BSP.
 */

#include "tusb_option.h"

#if CFG_TUD_ENABLED && defined(CFG_TUSB_OS_LINUX_NATIVE)

#include "device/dcd.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <linux/usb/ch9.h>
#include "raw_gadget.h"

//--------------------------------------------------------------------+
// Configuration
//--------------------------------------------------------------------+

#ifndef RAW_GADGET_DEVICE
#define RAW_GADGET_DEVICE "/dev/raw-gadget"
#endif

#ifndef RAW_GADGET_DRIVER_NAME
#define RAW_GADGET_DRIVER_NAME "dummy_udc"
#endif

#ifndef RAW_GADGET_DEVICE_NAME
#define RAW_GADGET_DEVICE_NAME "dummy_udc.0"
#endif

//--------------------------------------------------------------------+
// Internal state
//--------------------------------------------------------------------+

#define RG_EP_COUNT     16
#define RG_EP_BUF_SIZE  1024

typedef struct {
  int      handle;   // Raw Gadget endpoint handle from EP_ENABLE, -1 if closed
  uint8_t  addr;     // USB endpoint address
  bool     stalled;
  // Pending transfer state
  uint8_t* buffer;
  uint16_t total_bytes;
  uint16_t xferred_bytes;
  bool     busy;
} rg_ep_state_t;

typedef struct {
  int             fd;           // /dev/raw-gadget file descriptor
  bool            initialized;
  bool            running;
  bool            configured;
  bool            int_enabled;
  uint8_t         dev_addr;
  pthread_t       event_thread;
  bool            event_thread_running;
  rg_ep_state_t   ep_in[RG_EP_COUNT];
  rg_ep_state_t   ep_out[RG_EP_COUNT];
  // EP0 IN accumulation buffer for multi-part control transfers
  uint8_t         ep0_in_buf[RG_EP_BUF_SIZE];
  uint16_t        ep0_in_len;
  uint16_t        ep0_in_expected; // wLength from last SETUP
} rg_dcd_t;

static rg_dcd_t _dcd;

//--------------------------------------------------------------------+
// Raw Gadget ioctl helpers
//--------------------------------------------------------------------+

static int rg_init(int fd, const char* driver, const char* device, uint8_t speed) {
  struct usb_raw_init init;
  memset(&init, 0, sizeof(init));
  strncpy((char*)init.driver_name, driver, UDC_NAME_LENGTH_MAX - 1);
  strncpy((char*)init.device_name, device, UDC_NAME_LENGTH_MAX - 1);
  init.speed = speed;
  return ioctl(fd, USB_RAW_IOCTL_INIT, &init);
}

static int rg_run(int fd) {
  return ioctl(fd, USB_RAW_IOCTL_RUN);
}

static int rg_event_fetch(int fd, struct usb_raw_event* event, uint32_t buf_size) {
  event->length = buf_size;
  return ioctl(fd, USB_RAW_IOCTL_EVENT_FETCH, event);
}

static int rg_ep0_write(int fd, const void* data, uint32_t len) {
  struct {
    struct usb_raw_ep_io io;
    uint8_t data[RG_EP_BUF_SIZE];
  } buf;
  memset(&buf, 0, sizeof(buf));
  buf.io.ep = 0;
  buf.io.flags = 0;
  buf.io.length = len;
  if (len > 0 && data) {
    memcpy(buf.data, data, len < RG_EP_BUF_SIZE ? len : RG_EP_BUF_SIZE);
  }
  return ioctl(fd, USB_RAW_IOCTL_EP0_WRITE, &buf);
}

static int rg_ep0_read(int fd, void* data, uint32_t len) {
  struct {
    struct usb_raw_ep_io io;
    uint8_t data[RG_EP_BUF_SIZE];
  } buf;
  memset(&buf, 0, sizeof(buf));
  buf.io.ep = 0;
  buf.io.flags = 0;
  buf.io.length = len;
  int ret = ioctl(fd, USB_RAW_IOCTL_EP0_READ, &buf);
  if (ret >= 0 && data) {
    uint32_t copy = (uint32_t)ret < len ? (uint32_t)ret : len;
    memcpy(data, buf.data, copy);
  }
  return ret;
}

static int rg_ep_enable(int fd, const struct usb_endpoint_descriptor* desc) {
  return ioctl(fd, USB_RAW_IOCTL_EP_ENABLE, desc);
}

static int rg_ep_disable(int fd, uint32_t handle) {
  return ioctl(fd, USB_RAW_IOCTL_EP_DISABLE, &handle);
}

static int rg_ep_write(int fd, int handle, const void* data, uint32_t len) {
  struct {
    struct usb_raw_ep_io io;
    uint8_t data[RG_EP_BUF_SIZE];
  } buf;
  memset(&buf, 0, sizeof(buf));
  buf.io.ep = handle;
  buf.io.flags = (len == 0) ? USB_RAW_IO_FLAGS_ZERO : 0;
  buf.io.length = len;
  if (len > 0 && data) {
    memcpy(buf.data, data, len < RG_EP_BUF_SIZE ? len : RG_EP_BUF_SIZE);
  }
  return ioctl(fd, USB_RAW_IOCTL_EP_WRITE, &buf);
}

static int rg_ep_read(int fd, int handle, void* data, uint32_t len) {
  struct {
    struct usb_raw_ep_io io;
    uint8_t data[RG_EP_BUF_SIZE];
  } buf;
  memset(&buf, 0, sizeof(buf));
  buf.io.ep = handle;
  buf.io.flags = 0;
  buf.io.length = len;
  int ret = ioctl(fd, USB_RAW_IOCTL_EP_READ, &buf);
  if (ret >= 0 && data) {
    uint32_t copy = (uint32_t)ret < len ? (uint32_t)ret : len;
    memcpy(data, buf.data, copy);
  }
  return ret;
}

static int rg_configure(int fd) {
  return ioctl(fd, USB_RAW_IOCTL_CONFIGURE);
}

static int rg_vbus_draw(int fd, uint32_t ma) {
  uint32_t units = ma / 2;
  return ioctl(fd, USB_RAW_IOCTL_VBUS_DRAW, &units);
}

static int rg_ep0_stall(int fd) {
  return ioctl(fd, USB_RAW_IOCTL_EP0_STALL);
}

static int rg_ep_set_halt(int fd, uint32_t handle) {
  return ioctl(fd, USB_RAW_IOCTL_EP_SET_HALT, &handle);
}

static int rg_ep_clear_halt(int fd, uint32_t handle) {
  return ioctl(fd, USB_RAW_IOCTL_EP_CLEAR_HALT, &handle);
}

//--------------------------------------------------------------------+
// Endpoint helpers
//--------------------------------------------------------------------+

static rg_ep_state_t* ep_get(uint8_t ep_addr) {
  uint8_t num = ep_addr & 0x0F;
  if (num >= RG_EP_COUNT) return NULL;
  return (ep_addr & 0x80) ? &_dcd.ep_in[num] : &_dcd.ep_out[num];
}

//--------------------------------------------------------------------+
// Event thread: fetches Raw Gadget events and injects into TinyUSB
//--------------------------------------------------------------------+

static void* event_thread_func(void* arg) {
  (void)arg;
  struct {
    struct usb_raw_event event;
    uint8_t data[256];
  } buf;

  printf("[dcd_rawgadget] event thread started\n");

  while (_dcd.event_thread_running) {
    memset(&buf, 0, sizeof(buf));
    int ret = rg_event_fetch(_dcd.fd, &buf.event, sizeof(buf.data));
    if (ret < 0) {
      if (errno == EINTR) continue;
      perror("[dcd_rawgadget] event_fetch failed");
      break;
    }

    switch (buf.event.type) {
      case USB_RAW_EVENT_CONNECT:
        printf("[dcd_rawgadget] EVENT: CONNECT\n");
        dcd_event_bus_reset(0, TUSB_SPEED_FULL, false);
        break;

      case USB_RAW_EVENT_CONTROL: {
        struct usb_ctrlrequest* ctrl = (struct usb_ctrlrequest*)buf.data;
        printf("[dcd_rawgadget] EVENT: CONTROL bmReq=0x%02x bReq=0x%02x wVal=0x%04x wIdx=0x%04x wLen=%u\n",
               ctrl->bRequestType, ctrl->bRequest,
               ctrl->wValue, ctrl->wIndex, ctrl->wLength);
        fflush(stdout);
        // Reset EP0 accumulation for new control transfer
        _dcd.ep0_in_len = 0;
        _dcd.ep0_in_expected = ctrl->wLength;
        dcd_event_setup_received(0, (uint8_t const*)ctrl, false);
        break;
      }

      case USB_RAW_EVENT_SUSPEND:
        printf("[dcd_rawgadget] EVENT: SUSPEND\n");
        dcd_event_bus_signal(0, DCD_EVENT_SUSPEND, false);
        break;

      case USB_RAW_EVENT_RESUME:
        printf("[dcd_rawgadget] EVENT: RESUME\n");
        dcd_event_bus_signal(0, DCD_EVENT_RESUME, false);
        break;

      case USB_RAW_EVENT_RESET:
        printf("[dcd_rawgadget] EVENT: RESET\n");
        dcd_event_bus_reset(0, TUSB_SPEED_FULL, false);
        break;

      case USB_RAW_EVENT_DISCONNECT:
        printf("[dcd_rawgadget] EVENT: DISCONNECT\n");
        dcd_event_bus_signal(0, DCD_EVENT_UNPLUGGED, false);
        break;

      default:
        printf("[dcd_rawgadget] EVENT: unknown (%d)\n", buf.event.type);
        break;
    }
  }

  printf("[dcd_rawgadget] event thread exiting\n");
  return NULL;
}

//--------------------------------------------------------------------+
// DCD API implementation
//--------------------------------------------------------------------+

bool dcd_init(uint8_t rhport, const tusb_rhport_init_t* rh_init) {
  (void)rhport;
  (void)rh_init;

  memset(&_dcd, 0, sizeof(_dcd));
  for (int i = 0; i < RG_EP_COUNT; i++) {
    _dcd.ep_in[i].handle = -1;
    _dcd.ep_out[i].handle = -1;
  }

  _dcd.fd = open(RAW_GADGET_DEVICE, O_RDWR);
  if (_dcd.fd < 0) {
    perror("[dcd_rawgadget] open /dev/raw-gadget");
    return false;
  }

  const char* drv = getenv("RAW_GADGET_DRIVER") ? getenv("RAW_GADGET_DRIVER") : RAW_GADGET_DRIVER_NAME;
  const char* dev = getenv("RAW_GADGET_DEVICE_NAME") ? getenv("RAW_GADGET_DEVICE_NAME") : RAW_GADGET_DEVICE_NAME;

  printf("[dcd_rawgadget] init: driver=%s device=%s\n", drv, dev);

  if (rg_init(_dcd.fd, drv, dev, USB_SPEED_FULL) < 0) {
    perror("[dcd_rawgadget] USB_RAW_IOCTL_INIT");
    close(_dcd.fd);
    return false;
  }

  if (rg_run(_dcd.fd) < 0) {
    perror("[dcd_rawgadget] USB_RAW_IOCTL_RUN");
    close(_dcd.fd);
    return false;
  }

  _dcd.initialized = true;
  _dcd.running = true;

  // Start event thread
  _dcd.event_thread_running = true;
  if (pthread_create(&_dcd.event_thread, NULL, event_thread_func, NULL) != 0) {
    perror("[dcd_rawgadget] pthread_create");
    _dcd.event_thread_running = false;
  }

  printf("[dcd_rawgadget] initialized and running\n");
  return true;
}

void dcd_int_handler(uint8_t rhport) {
  (void)rhport;
  // Events are handled by the event thread via blocking ioctls.
  // tud_task() processes the queued events.
}

void dcd_int_enable(uint8_t rhport) {
  (void)rhport;
  _dcd.int_enabled = true;
}

void dcd_int_disable(uint8_t rhport) {
  (void)rhport;
  _dcd.int_enabled = false;
}

void dcd_set_address(uint8_t rhport, uint8_t dev_addr) {
  (void)rhport;
  _dcd.dev_addr = dev_addr;
  // Send ZLP status on EP0 IN
  rg_ep0_write(_dcd.fd, NULL, 0);
  dcd_event_xfer_complete(0, 0x80, 0, XFER_RESULT_SUCCESS, false);
}

void dcd_remote_wakeup(uint8_t rhport) {
  (void)rhport;
}

void dcd_connect(uint8_t rhport) {
  (void)rhport;
}

void dcd_disconnect(uint8_t rhport) {
  (void)rhport;
}

void dcd_sof_enable(uint8_t rhport, bool en) {
  (void)rhport;
  (void)en;
}

//--------------------------------------------------------------------+
// Endpoint API
//--------------------------------------------------------------------+

bool dcd_edpt_open(uint8_t rhport, tusb_desc_endpoint_t const* desc_ep) {
  (void)rhport;

  uint8_t ep_addr = desc_ep->bEndpointAddress;
  rg_ep_state_t* ep = ep_get(ep_addr);
  if (!ep) return false;

  // Map TinyUSB descriptor to Linux USB descriptor for Raw Gadget
  struct usb_endpoint_descriptor linux_desc;
  memset(&linux_desc, 0, sizeof(linux_desc));
  linux_desc.bLength          = sizeof(linux_desc);
  linux_desc.bDescriptorType  = USB_DT_ENDPOINT;
  linux_desc.bEndpointAddress = desc_ep->bEndpointAddress;
  linux_desc.bmAttributes     = desc_ep->bmAttributes.xfer;
  linux_desc.wMaxPacketSize   = desc_ep->wMaxPacketSize;
  linux_desc.bInterval        = desc_ep->bInterval;

  int handle = rg_ep_enable(_dcd.fd, &linux_desc);
  if (handle < 0) {
    perror("[dcd_rawgadget] EP_ENABLE");
    return false;
  }

  ep->handle = handle;
  ep->addr = ep_addr;
  ep->stalled = false;
  ep->busy = false;

  printf("[dcd_rawgadget] ep_open: addr=0x%02x handle=%d\n", ep_addr, handle);
  return true;
}

void dcd_edpt_close_all(uint8_t rhport) {
  (void)rhport;
  for (int i = 1; i < RG_EP_COUNT; i++) {
    if (_dcd.ep_in[i].handle >= 0) {
      rg_ep_disable(_dcd.fd, _dcd.ep_in[i].handle);
      _dcd.ep_in[i].handle = -1;
      _dcd.ep_in[i].busy = false;
    }
    if (_dcd.ep_out[i].handle >= 0) {
      rg_ep_disable(_dcd.fd, _dcd.ep_out[i].handle);
      _dcd.ep_out[i].handle = -1;
      _dcd.ep_out[i].busy = false;
    }
  }
}

bool dcd_edpt_xfer(uint8_t rhport, uint8_t ep_addr, uint8_t* buffer,
                   uint16_t total_bytes, bool is_isr) {
  (void)rhport;
  (void)is_isr;

  uint8_t ep_num = ep_addr & 0x0F;
  uint8_t dir = ep_addr & 0x80;

  // EP0 transfers use the ep0 ioctls.
  // Raw Gadget handles EP0 as single request-response pairs:
  // - EP0_WRITE sends the full IN response (one ioctl per control transfer)
  // - The status ZLP is handled implicitly by Raw Gadget
  if (ep_num == 0) {
    if (dir) {
      // EP0 IN: accumulate data, send all at once via EP0_WRITE
      if (total_bytes == 0) {
        // ZLP status stage or flush accumulated data
        if (_dcd.ep0_in_len > 0) {
          // Flush accumulated EP0 IN data
          printf("[dcd_rawgadget] EP0 IN flush %u accumulated bytes\n", _dcd.ep0_in_len);
          fflush(stdout);
          int ret = rg_ep0_write(_dcd.fd, _dcd.ep0_in_buf, _dcd.ep0_in_len);
          printf("[dcd_rawgadget] EP0 IN flush ret=%d\n", ret);
          fflush(stdout);
          _dcd.ep0_in_len = 0;
        }
        dcd_event_xfer_complete(0, ep_addr, 0, XFER_RESULT_SUCCESS, false);
        return true;
      }
      // Accumulate data
      if (_dcd.ep0_in_len + total_bytes <= RG_EP_BUF_SIZE) {
        memcpy(_dcd.ep0_in_buf + _dcd.ep0_in_len, buffer, total_bytes);
        _dcd.ep0_in_len += total_bytes;
      }
      // Check if we have the full response
      bool is_complete = (_dcd.ep0_in_len >= _dcd.ep0_in_expected) ||
                         (total_bytes < 64); // short packet = last
      if (is_complete) {
        // Send all at once
        uint16_t send_len = _dcd.ep0_in_len;
        printf("[dcd_rawgadget] EP0 IN write %u bytes (complete)\n", send_len);
        fflush(stdout);
        int ret = rg_ep0_write(_dcd.fd, _dcd.ep0_in_buf, send_len);
        printf("[dcd_rawgadget] EP0 IN write ret=%d\n", ret);
        fflush(stdout);
        // Reset accumulation state
        _dcd.ep0_in_len = 0;
        _dcd.ep0_in_expected = 0;
        uint32_t xferred = (ret >= 0) ? send_len : 0;
        uint8_t result = (ret >= 0) ? XFER_RESULT_SUCCESS : XFER_RESULT_FAILED;
        dcd_event_xfer_complete(0, ep_addr, xferred, result, false);
        return (ret >= 0);
      } else {
        // More data coming — report success for this chunk
        printf("[dcd_rawgadget] EP0 IN accumulate %u bytes (total %u/%u)\n",
               total_bytes, _dcd.ep0_in_len, _dcd.ep0_in_expected);
        fflush(stdout);
        dcd_event_xfer_complete(0, ep_addr, total_bytes, XFER_RESULT_SUCCESS, false);
        return true;
      }
    } else {
      // EP0 OUT: receive data from host or ZLP status
      if (total_bytes == 0) {
        // ZLP status stage — Raw Gadget handles this implicitly
        printf("[dcd_rawgadget] EP0 OUT ZLP (status stage, skip ioctl)\n");
        fflush(stdout);
        dcd_event_xfer_complete(0, ep_addr, 0, XFER_RESULT_SUCCESS, false);
        return true;
      }
      printf("[dcd_rawgadget] EP0 OUT read %u bytes\n", total_bytes);
      fflush(stdout);
      int ret = rg_ep0_read(_dcd.fd, buffer, total_bytes);
      printf("[dcd_rawgadget] EP0 OUT read ret=%d\n", ret);
      fflush(stdout);
      uint32_t xferred = (ret >= 0) ? (uint32_t)ret : 0;
      uint8_t result = (ret >= 0) ? XFER_RESULT_SUCCESS : XFER_RESULT_FAILED;
      dcd_event_xfer_complete(0, ep_addr, xferred, result, false);
      return (ret >= 0);
    }
  }

  // Non-control endpoints
  rg_ep_state_t* ep = ep_get(ep_addr);
  if (!ep || ep->handle < 0) return false;

  int ret;
  if (dir) {
    // IN: device -> host
    ret = rg_ep_write(_dcd.fd, ep->handle, buffer, total_bytes);
  } else {
    // OUT: host -> device
    ret = rg_ep_read(_dcd.fd, ep->handle, buffer, total_bytes);
  }

  uint32_t xferred = (ret >= 0) ? (uint32_t)ret : 0;
  uint8_t result = (ret >= 0) ? XFER_RESULT_SUCCESS : XFER_RESULT_FAILED;
  dcd_event_xfer_complete(0, ep_addr, xferred, result, false);
  return (ret >= 0);
}

void dcd_edpt_stall(uint8_t rhport, uint8_t ep_addr) {
  (void)rhport;
  uint8_t ep_num = ep_addr & 0x0F;

  if (ep_num == 0) {
    rg_ep0_stall(_dcd.fd);
  } else {
    rg_ep_state_t* ep = ep_get(ep_addr);
    if (ep && ep->handle >= 0) {
      rg_ep_set_halt(_dcd.fd, ep->handle);
      ep->stalled = true;
    }
  }
}

void dcd_edpt_clear_stall(uint8_t rhport, uint8_t ep_addr) {
  (void)rhport;

  rg_ep_state_t* ep = ep_get(ep_addr);
  if (ep && ep->handle >= 0) {
    rg_ep_clear_halt(_dcd.fd, ep->handle);
    ep->stalled = false;
  }
}

// ISO endpoint stubs (not supported by Raw Gadget/dummy_hcd)
bool dcd_edpt_iso_alloc(uint8_t rhport, uint8_t ep_addr, uint16_t largest_packet_size) {
  (void)rhport; (void)ep_addr; (void)largest_packet_size;
  return false;
}

bool dcd_edpt_iso_activate(uint8_t rhport, tusb_desc_endpoint_t const* desc_ep) {
  (void)rhport; (void)desc_ep;
  return false;
}

#endif // CFG_TUD_ENABLED && defined(CFG_TUSB_OS_LINUX_NATIVE)
