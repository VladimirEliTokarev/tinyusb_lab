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
 * Virtual USB Device Controller Driver (DCD).
 *
 * Implements the TinyUSB DCD interface over a byte stream (PL011 UART
 * backed by a QEMU Unix socket chardev). Receives USB transactions from
 * the broker and injects events into the TinyUSB device stack.
 */

#include "tusb_option.h"

#if CFG_TUD_ENABLED && defined(QEMU_ROLE_DEVICE)

#include "device/dcd.h"
#include "vusb_proto.h"

#include <string.h>

//--------------------------------------------------------------------+
// PL011 transport (implemented in hw/bsp/qemu/family.c)
//--------------------------------------------------------------------+
extern void     pl011_write(const uint8_t* buf, uint32_t len);
extern uint32_t pl011_read(uint8_t* buf, uint32_t max_len);
extern bool     pl011_readable(void);

//--------------------------------------------------------------------+
// Internal state
//--------------------------------------------------------------------+

#define VUSB_EP_COUNT    16

typedef struct {
  uint8_t* buffer;
  uint16_t total_bytes;
  uint16_t xferred_bytes;
  bool     busy;
  bool     stalled;
} vusb_ep_state_t;

typedef struct {
  bool            initialized;
  bool            int_enabled;
  bool            connected;
  bool            sof_enabled;
  uint8_t         dev_addr;
  uint32_t        frame_count;
  tusb_speed_t    speed;
  vusb_ep_state_t ep_in[VUSB_EP_COUNT];
  vusb_ep_state_t ep_out[VUSB_EP_COUNT];

  // RX frame assembly buffer
  uint8_t         rx_buf[VUSB_MAX_FRAME_SIZE];
  uint16_t        rx_pos;
} vusb_dcd_t;

static vusb_dcd_t _dcd;

//--------------------------------------------------------------------+
// Frame I/O helpers
//--------------------------------------------------------------------+

static void vusb_send_frame(uint8_t type, const void* payload, uint16_t len) {
  vusb_frame_hdr_t hdr;
  hdr.type = type;
  hdr.payload_len = len;
  pl011_write((const uint8_t*)&hdr, VUSB_FRAME_HDR_SIZE);
  if (len > 0 && payload != NULL) {
    pl011_write((const uint8_t*)payload, len);
  }
}

static inline vusb_ep_state_t* vusb_ep_get(uint8_t ep_addr) {
  uint8_t ep_num = tu_edpt_number(ep_addr);
  uint8_t dir    = tu_edpt_dir(ep_addr);
  if (ep_num >= VUSB_EP_COUNT) return NULL;
  return (dir == TUSB_DIR_IN) ? &_dcd.ep_in[ep_num] : &_dcd.ep_out[ep_num];
}

//--------------------------------------------------------------------+
// Frame processing (called from dcd_int_handler poll loop)
//--------------------------------------------------------------------+

static void vusb_process_frame(const uint8_t* frame, uint16_t frame_len) {
  if (frame_len < VUSB_FRAME_HDR_SIZE) return;

  const vusb_frame_hdr_t* hdr = (const vusb_frame_hdr_t*)frame;
  const uint8_t* payload = frame + VUSB_FRAME_HDR_SIZE;
  uint16_t plen = hdr->payload_len;

  if (VUSB_FRAME_HDR_SIZE + plen > frame_len) return;

  switch (hdr->type) {
    case VUSB_FRAME_ATTACH:
      _dcd.connected = true;
      dcd_event_bus_reset(0, TUSB_SPEED_FULL, false);
      break;

    case VUSB_FRAME_DETACH:
      _dcd.connected = false;
      dcd_event_bus_signal(0, DCD_EVENT_UNPLUGGED, false);
      break;

    case VUSB_FRAME_RESET:
      dcd_event_bus_reset(0, TUSB_SPEED_FULL, false);
      break;

    case VUSB_FRAME_SETUP:
      if (plen >= VUSB_SETUP_SIZE) {
        dcd_event_setup_received(0, payload, false);
      }
      break;

    case VUSB_FRAME_DATA_OUT: {
      if (plen < 1) break;
      const vusb_data_hdr_t* dhdr = (const vusb_data_hdr_t*)payload;
      uint8_t ep_addr = dhdr->ep_addr;
      uint16_t data_len = plen - 1;
      const uint8_t* data = payload + 1;

      vusb_ep_state_t* ep = vusb_ep_get(ep_addr);
      if (ep && ep->busy && ep->buffer) {
        uint16_t copy_len = (data_len < (ep->total_bytes - ep->xferred_bytes))
                            ? data_len
                            : (ep->total_bytes - ep->xferred_bytes);
        memcpy(ep->buffer + ep->xferred_bytes, data, copy_len);
        ep->xferred_bytes += copy_len;
        ep->busy = false;
        dcd_event_xfer_complete(0, ep_addr, ep->xferred_bytes,
                                XFER_RESULT_SUCCESS, false);
      }
      break;
    }

    case VUSB_FRAME_DATA_IN: {
      if (plen < sizeof(vusb_in_req_t)) break;
      const vusb_in_req_t* req = (const vusb_in_req_t*)payload;
      uint8_t ep_addr = req->ep_addr;

      vusb_ep_state_t* ep = vusb_ep_get(ep_addr);
      if (ep && ep->stalled) {
        vusb_handshake_t hs = { .ep_addr = ep_addr, .xferred_len = 0 };
        vusb_send_frame(VUSB_FRAME_STALL, &hs, sizeof(hs));
      } else if (ep && ep->busy && ep->buffer) {
        uint16_t send_len = ep->total_bytes - ep->xferred_bytes;
        if (send_len > req->max_len) send_len = req->max_len;

        uint8_t resp[1 + VUSB_MAX_PAYLOAD];
        resp[0] = ep_addr;
        if (send_len > 0) {
          memcpy(resp + 1, ep->buffer + ep->xferred_bytes, send_len);
        }
        vusb_send_frame(VUSB_FRAME_DATA_RESP, resp, 1 + send_len);

        ep->xferred_bytes += send_len;
        if (ep->xferred_bytes >= ep->total_bytes || send_len == 0) {
          ep->busy = false;
          dcd_event_xfer_complete(0, ep_addr, ep->xferred_bytes,
                                  XFER_RESULT_SUCCESS, false);
        }
      } else {
        vusb_handshake_t hs = { .ep_addr = ep_addr, .xferred_len = 0 };
        vusb_send_frame(VUSB_FRAME_NAK, &hs, sizeof(hs));
      }
      break;
    }

    case VUSB_FRAME_SOF: {
      if (plen >= sizeof(vusb_sof_t)) {
        const vusb_sof_t* sof = (const vusb_sof_t*)payload;
        _dcd.frame_count = sof->frame_number;
        if (_dcd.sof_enabled) {
          dcd_event_sof(0, _dcd.frame_count, false);
        }
      }
      break;
    }

    default:
      break;
  }
}

//--------------------------------------------------------------------+
// DCD API implementation
//--------------------------------------------------------------------+

bool dcd_init(uint8_t rhport, const tusb_rhport_init_t* rh_init) {
  (void) rhport;
  (void) rh_init;

  memset(&_dcd, 0, sizeof(_dcd));
  _dcd.initialized = true;
  _dcd.speed = TUSB_SPEED_FULL;
  return true;
}

void dcd_int_handler(uint8_t rhport) {
  (void) rhport;

  if (!_dcd.int_enabled || !_dcd.initialized) return;

  // Poll the PL011 for incoming frames
  while (pl011_readable()) {
    uint8_t byte;
    if (pl011_read(&byte, 1) != 1) break;

    _dcd.rx_buf[_dcd.rx_pos++] = byte;

    // Check if we have a complete frame header
    if (_dcd.rx_pos >= VUSB_FRAME_HDR_SIZE) {
      const vusb_frame_hdr_t* hdr = (const vusb_frame_hdr_t*)_dcd.rx_buf;
      uint16_t total = VUSB_FRAME_HDR_SIZE + hdr->payload_len;

      if (total > VUSB_MAX_FRAME_SIZE) {
        _dcd.rx_pos = 0; // discard oversized frame
        continue;
      }

      if (_dcd.rx_pos >= total) {
        vusb_process_frame(_dcd.rx_buf, total);
        // Shift remaining bytes (if any) to start of buffer
        uint16_t remaining = _dcd.rx_pos - total;
        if (remaining > 0) {
          memmove(_dcd.rx_buf, _dcd.rx_buf + total, remaining);
        }
        _dcd.rx_pos = remaining;
      }
    }

    if (_dcd.rx_pos >= VUSB_MAX_FRAME_SIZE) {
      _dcd.rx_pos = 0; // safety reset
    }
  }
}

void dcd_int_enable(uint8_t rhport) {
  (void) rhport;
  _dcd.int_enabled = true;
}

void dcd_int_disable(uint8_t rhport) {
  (void) rhport;
  _dcd.int_enabled = false;
}

void dcd_set_address(uint8_t rhport, uint8_t dev_addr) {
  (void) rhport;
  _dcd.dev_addr = dev_addr;

  // Notify the broker of address assignment
  vusb_set_addr_t addr_pkt = { .dev_addr = dev_addr };
  vusb_send_frame(VUSB_FRAME_SET_ADDR, &addr_pkt, sizeof(addr_pkt));

  // Send ZLP status on EP0 IN
  dcd_edpt_xfer(rhport, tu_edpt_addr(0, TUSB_DIR_IN), NULL, 0, false);
}

void dcd_remote_wakeup(uint8_t rhport) {
  (void) rhport;
}

void dcd_connect(uint8_t rhport) {
  (void) rhport;
  _dcd.connected = true;
}

void dcd_disconnect(uint8_t rhport) {
  (void) rhport;
  _dcd.connected = false;
}

void dcd_sof_enable(uint8_t rhport, bool en) {
  (void) rhport;
  _dcd.sof_enabled = en;
}

//--------------------------------------------------------------------+
// Endpoint API
//--------------------------------------------------------------------+

bool dcd_edpt_open(uint8_t rhport, tusb_desc_endpoint_t const* desc_ep) {
  (void) rhport;

  uint8_t ep_addr = desc_ep->bEndpointAddress;
  vusb_ep_state_t* ep = vusb_ep_get(ep_addr);
  if (!ep) return false;

  memset(ep, 0, sizeof(*ep));
  return true;
}

void dcd_edpt_close_all(uint8_t rhport) {
  (void) rhport;

  // Reset all non-control endpoints
  for (int i = 1; i < VUSB_EP_COUNT; i++) {
    memset(&_dcd.ep_in[i], 0, sizeof(vusb_ep_state_t));
    memset(&_dcd.ep_out[i], 0, sizeof(vusb_ep_state_t));
  }
}

bool dcd_edpt_xfer(uint8_t rhport, uint8_t ep_addr, uint8_t* buffer,
                   uint16_t total_bytes, bool is_isr) {
  (void) rhport;
  (void) is_isr;

  vusb_ep_state_t* ep = vusb_ep_get(ep_addr);
  if (!ep) return false;

  ep->buffer = buffer;
  ep->total_bytes = total_bytes;
  ep->xferred_bytes = 0;
  ep->busy = true;

  uint8_t dir = tu_edpt_dir(ep_addr);
  uint8_t ep_num = tu_edpt_number(ep_addr);

  if (dir == TUSB_DIR_IN && ep_num == 0 && total_bytes == 0) {
    // ZLP on EP0 IN - complete immediately (status stage)
    ep->busy = false;
    dcd_event_xfer_complete(0, ep_addr, 0, XFER_RESULT_SUCCESS, false);
  }

  return true;
}

void dcd_edpt_stall(uint8_t rhport, uint8_t ep_addr) {
  (void) rhport;

  vusb_ep_state_t* ep = vusb_ep_get(ep_addr);
  if (ep) {
    ep->stalled = true;
    ep->busy = false;
  }
}

void dcd_edpt_clear_stall(uint8_t rhport, uint8_t ep_addr) {
  (void) rhport;

  vusb_ep_state_t* ep = vusb_ep_get(ep_addr);
  if (ep) {
    ep->stalled = false;
  }
}

#endif // CFG_TUD_ENABLED && defined(QEMU_ROLE_DEVICE)
