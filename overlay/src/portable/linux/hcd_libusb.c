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
 * HCD driver for Linux libusb.
 *
 * Wraps libusb-1.0 to present a TinyUSB host controller interface.
 * Used with dummy_hcd to enumerate TinyUSB devices running via Raw Gadget
 * on the same machine, giving a full TinyUSB-to-TinyUSB end-to-end path.
 *
 * Compile as native Linux x86/x64 with the linux_native BSP.
 */

#include "tusb_option.h"

#if CFG_TUH_ENABLED && defined(CFG_TUSB_OS_LINUX_NATIVE)

#include "host/hcd.h"
#include "host/usbh.h"

#include <libusb-1.0/libusb.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

//--------------------------------------------------------------------+
// Configuration: which device to attach to
//--------------------------------------------------------------------+

// VID/PID of the TinyUSB device we're looking for (from cdc_msc example)
#ifndef HCD_LIBUSB_VID
#define HCD_LIBUSB_VID 0xCafe
#endif

#ifndef HCD_LIBUSB_PID
#define HCD_LIBUSB_PID 0x4000
#endif

//--------------------------------------------------------------------+
// Internal state
//--------------------------------------------------------------------+

#define LU_EP_COUNT  16

typedef struct {
  bool opened;
  uint8_t addr;
  uint8_t type;   // TUSB_XFER_*
  uint16_t mps;
} lu_ep_t;

typedef struct {
  // per-device state (single-device for simplicity)
  libusb_device_handle* devh;
  uint8_t               dev_addr;  // TinyUSB-assigned address
  bool                  connected;
  tusb_speed_t          speed;
  lu_ep_t               ep_in[LU_EP_COUNT];
  lu_ep_t               ep_out[LU_EP_COUNT];
} lu_dev_t;

typedef struct {
  libusb_context*  ctx;
  bool             initialized;
  bool             int_enabled;
  uint32_t         frame_count;
  pthread_t        hotplug_thread;
  bool             hotplug_running;
  lu_dev_t         dev;  // single device
} lu_hcd_t;

static lu_hcd_t _hcd;

//--------------------------------------------------------------------+
// Helpers
//--------------------------------------------------------------------+

static lu_ep_t* ep_get(uint8_t ep_addr) {
  uint8_t num = ep_addr & 0x0F;
  if (num >= LU_EP_COUNT) return NULL;
  return (ep_addr & 0x80) ? &_hcd.dev.ep_in[num] : &_hcd.dev.ep_out[num];
}

static tusb_speed_t libusb_speed_to_tusb(int speed) {
  switch (speed) {
    case LIBUSB_SPEED_LOW:   return TUSB_SPEED_LOW;
    case LIBUSB_SPEED_FULL:  return TUSB_SPEED_FULL;
    case LIBUSB_SPEED_HIGH:  return TUSB_SPEED_HIGH;
    case LIBUSB_SPEED_SUPER: return TUSB_SPEED_HIGH; // map to highest TinyUSB supports
    default:                 return TUSB_SPEED_FULL;
  }
}

//--------------------------------------------------------------------+
// Hotplug detection thread
//--------------------------------------------------------------------+

static void* hotplug_thread_func(void* arg) {
  (void)arg;
  printf("[hcd_libusb] hotplug thread started, looking for %04x:%04x\n",
         HCD_LIBUSB_VID, HCD_LIBUSB_PID);

  while (_hcd.hotplug_running) {
    if (!_hcd.dev.connected) {
      // Try to find and open the device
      libusb_device_handle* devh = libusb_open_device_with_vid_pid(
        _hcd.ctx, HCD_LIBUSB_VID, HCD_LIBUSB_PID);

      if (devh) {
        libusb_device* dev = libusb_get_device(devh);
        _hcd.dev.speed = libusb_speed_to_tusb(libusb_get_device_speed(dev));
        _hcd.dev.devh = devh;
        _hcd.dev.connected = true;

        // Detach kernel driver if active
        libusb_set_auto_detach_kernel_driver(devh, 1);

        printf("[hcd_libusb] device ATTACHED (%04x:%04x)\n",
               HCD_LIBUSB_VID, HCD_LIBUSB_PID);

        // Notify TinyUSB host stack
        hcd_event_device_attach(0, false);
      }
    } else {
      // Check if device is still present
      // Try a simple get-descriptor to see if it's alive
      uint8_t buf[2];
      int ret = libusb_control_transfer(_hcd.dev.devh,
        LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_STANDARD | LIBUSB_RECIPIENT_DEVICE,
        LIBUSB_REQUEST_GET_STATUS, 0, 0, buf, sizeof(buf), 100);

      if (ret < 0 && ret != LIBUSB_ERROR_TIMEOUT) {
        printf("[hcd_libusb] device DETACHED (error %d: %s)\n",
               ret, libusb_error_name(ret));
        libusb_close(_hcd.dev.devh);
        _hcd.dev.devh = NULL;
        _hcd.dev.connected = false;
        memset(_hcd.dev.ep_in, 0, sizeof(_hcd.dev.ep_in));
        memset(_hcd.dev.ep_out, 0, sizeof(_hcd.dev.ep_out));

        hcd_event_device_remove(0, false);
      }
    }

    usleep(500000); // poll every 500ms
  }

  printf("[hcd_libusb] hotplug thread exiting\n");
  return NULL;
}

//--------------------------------------------------------------------+
// Controller API
//--------------------------------------------------------------------+

bool hcd_configure(uint8_t rhport, uint32_t cfg_id, const void* cfg_param) {
  (void)rhport; (void)cfg_id; (void)cfg_param;
  return false;
}

bool hcd_init(uint8_t rhport, const tusb_rhport_init_t* rh_init) {
  (void)rhport; (void)rh_init;

  memset(&_hcd, 0, sizeof(_hcd));

  int ret = libusb_init(&_hcd.ctx);
  if (ret != LIBUSB_SUCCESS) {
    fprintf(stderr, "[hcd_libusb] libusb_init failed: %s\n", libusb_error_name(ret));
    return false;
  }

  _hcd.initialized = true;

  // Start hotplug thread
  _hcd.hotplug_running = true;
  if (pthread_create(&_hcd.hotplug_thread, NULL, hotplug_thread_func, NULL) != 0) {
    perror("[hcd_libusb] pthread_create");
    _hcd.hotplug_running = false;
  }

  printf("[hcd_libusb] initialized\n");
  return true;
}

bool hcd_deinit(uint8_t rhport) {
  (void)rhport;
  _hcd.hotplug_running = false;
  if (_hcd.hotplug_running) {
    pthread_join(_hcd.hotplug_thread, NULL);
  }
  if (_hcd.dev.devh) {
    libusb_close(_hcd.dev.devh);
    _hcd.dev.devh = NULL;
  }
  if (_hcd.ctx) {
    libusb_exit(_hcd.ctx);
    _hcd.ctx = NULL;
  }
  return true;
}

void hcd_int_handler(uint8_t rhport, bool in_isr) {
  (void)rhport; (void)in_isr;
  // Events come from the hotplug thread
}

void hcd_int_enable(uint8_t rhport) {
  (void)rhport;
  _hcd.int_enabled = true;
}

void hcd_int_disable(uint8_t rhport) {
  (void)rhport;
  _hcd.int_enabled = false;
}

uint32_t hcd_frame_number(uint8_t rhport) {
  (void)rhport;
  return _hcd.frame_count++;
}

//--------------------------------------------------------------------+
// Port API
//--------------------------------------------------------------------+

bool hcd_port_connect_status(uint8_t rhport) {
  (void)rhport;
  return _hcd.dev.connected;
}

void hcd_port_reset(uint8_t rhport) {
  (void)rhport;
  if (_hcd.dev.devh) {
    libusb_reset_device(_hcd.dev.devh);
    printf("[hcd_libusb] port reset\n");
  }
}

void hcd_port_reset_end(uint8_t rhport) {
  (void)rhport;
}

tusb_speed_t hcd_port_speed_get(uint8_t rhport) {
  (void)rhport;
  return _hcd.dev.connected ? _hcd.dev.speed : TUSB_SPEED_FULL;
}

void hcd_device_close(uint8_t rhport, uint8_t dev_addr) {
  (void)rhport; (void)dev_addr;
  // Release all claimed interfaces
  if (_hcd.dev.devh) {
    for (int i = 0; i < 8; i++) {
      libusb_release_interface(_hcd.dev.devh, i);
    }
  }
  memset(_hcd.dev.ep_in, 0, sizeof(_hcd.dev.ep_in));
  memset(_hcd.dev.ep_out, 0, sizeof(_hcd.dev.ep_out));
}

//--------------------------------------------------------------------+
// Endpoints API
//--------------------------------------------------------------------+

bool hcd_edpt_open(uint8_t rhport, uint8_t daddr, tusb_desc_endpoint_t const* ep_desc) {
  (void)rhport;

  uint8_t ep_addr = ep_desc->bEndpointAddress;
  lu_ep_t* ep = ep_get(ep_addr);
  if (!ep) return false;

  ep->opened = true;
  ep->addr = ep_addr;
  ep->type = ep_desc->bmAttributes.xfer;
  ep->mps = ep_desc->wMaxPacketSize;

  _hcd.dev.dev_addr = daddr;

  printf("[hcd_libusb] edpt_open: dev=%u addr=0x%02x type=%u mps=%u\n",
         daddr, ep_addr, ep->type, ep->mps);
  return true;
}

bool hcd_edpt_close(uint8_t rhport, uint8_t daddr, uint8_t ep_addr) {
  (void)rhport; (void)daddr;

  lu_ep_t* ep = ep_get(ep_addr);
  if (ep) {
    ep->opened = false;
  }
  return true;
}

bool hcd_setup_send(uint8_t rhport, uint8_t daddr, uint8_t const setup_packet[8]) {
  (void)rhport;

  if (!_hcd.dev.devh) return false;

  // Parse setup packet
  uint8_t bmRequestType = setup_packet[0];
  uint8_t bRequest      = setup_packet[1];
  uint16_t wValue       = setup_packet[2] | (setup_packet[3] << 8);
  uint16_t wIndex       = setup_packet[4] | (setup_packet[5] << 8);
  uint16_t wLength      = setup_packet[6] | (setup_packet[7] << 8);

  // Claim interface if needed (for SET_CONFIGURATION or interface requests)
  if (bRequest == 9 /* SET_CONFIGURATION */ && (bmRequestType & 0x1F) == 0) {
    // Claim interface 0 after configuration
    libusb_claim_interface(_hcd.dev.devh, 0);
    libusb_claim_interface(_hcd.dev.devh, 1);
  }

  uint8_t data_buf[512];
  memset(data_buf, 0, sizeof(data_buf));
  uint16_t data_len = wLength < sizeof(data_buf) ? wLength : sizeof(data_buf);

  int ret = libusb_control_transfer(_hcd.dev.devh,
    bmRequestType, bRequest, wValue, wIndex,
    data_buf, data_len, 1000);

  if (ret < 0) {
    printf("[hcd_libusb] setup_send failed: %s (bmReq=0x%02x bReq=0x%02x)\n",
           libusb_error_name(ret), bmRequestType, bRequest);
    hcd_event_xfer_complete(daddr, 0, 0, XFER_RESULT_STALLED, false);
    return true; // still "submitted"
  }

  hcd_event_xfer_complete(daddr, 0, (uint32_t)ret, XFER_RESULT_SUCCESS, false);
  return true;
}

bool hcd_edpt_xfer(uint8_t rhport, uint8_t daddr, uint8_t ep_addr,
                   uint8_t* buffer, uint16_t buflen) {
  (void)rhport;

  if (!_hcd.dev.devh) return false;

  lu_ep_t* ep = ep_get(ep_addr);
  if (!ep || !ep->opened) return false;

  int transferred = 0;
  int ret;

  switch (ep->type) {
    case TUSB_XFER_BULK:
      ret = libusb_bulk_transfer(_hcd.dev.devh, ep_addr, buffer, buflen,
                                 &transferred, 1000);
      break;

    case TUSB_XFER_INTERRUPT:
      ret = libusb_interrupt_transfer(_hcd.dev.devh, ep_addr, buffer, buflen,
                                      &transferred, 1000);
      break;

    default:
      printf("[hcd_libusb] unsupported xfer type %u on ep 0x%02x\n",
             ep->type, ep_addr);
      return false;
  }

  xfer_result_t result;
  if (ret == LIBUSB_SUCCESS || ret == LIBUSB_ERROR_TIMEOUT) {
    result = XFER_RESULT_SUCCESS;
  } else if (ret == LIBUSB_ERROR_PIPE) {
    result = XFER_RESULT_STALLED;
  } else {
    result = XFER_RESULT_FAILED;
  }

  hcd_event_xfer_complete(daddr, ep_addr, (uint32_t)transferred, result, false);
  return true;
}

bool hcd_edpt_abort_xfer(uint8_t rhport, uint8_t dev_addr, uint8_t ep_addr) {
  (void)rhport; (void)dev_addr; (void)ep_addr;
  // libusb synchronous transfers can't be easily aborted
  return false;
}

bool hcd_edpt_clear_stall(uint8_t rhport, uint8_t dev_addr, uint8_t ep_addr) {
  (void)rhport; (void)dev_addr;

  if (!_hcd.dev.devh) return false;
  int ret = libusb_clear_halt(_hcd.dev.devh, ep_addr);
  return (ret == LIBUSB_SUCCESS);
}

#endif // CFG_TUH_ENABLED && defined(CFG_TUSB_OS_LINUX_NATIVE)
