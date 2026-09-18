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
 * Virtual USB (VUSB) wire protocol.
 *
 * Length-prefixed binary frames exchanged over a byte stream (PL011 UART
 * backed by a QEMU chardev Unix socket). All multi-byte fields are
 * little-endian.
 *
 * Frame layout:
 *   [1 byte type] [2 byte payload_len LE] [payload_len bytes payload]
 *
 * Total frame size = 3 + payload_len.
 */

#ifndef VUSB_PROTO_H_
#define VUSB_PROTO_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

//--------------------------------------------------------------------+
// Frame types
//--------------------------------------------------------------------+
enum {
  VUSB_FRAME_ATTACH     = 0x01,  // host->device: device is now connected
  VUSB_FRAME_DETACH     = 0x02,  // host->device: device is now disconnected
  VUSB_FRAME_RESET      = 0x03,  // host->device: bus reset
  VUSB_FRAME_SETUP      = 0x04,  // host->device: 8-byte SETUP packet
  VUSB_FRAME_DATA_OUT   = 0x05,  // host->device: OUT data to endpoint
  VUSB_FRAME_DATA_IN    = 0x06,  // host->device: IN token (request data)
  VUSB_FRAME_ACK        = 0x07,  // device->host: ACK handshake
  VUSB_FRAME_NAK        = 0x08,  // device->host: NAK handshake
  VUSB_FRAME_STALL      = 0x09,  // device->host: STALL handshake
  VUSB_FRAME_DATA_RESP  = 0x0A,  // device->host: IN data response
  VUSB_FRAME_SOF        = 0x0B,  // host->device: start of frame
  VUSB_FRAME_SET_ADDR   = 0x0C,  // broker->device: address assigned
  VUSB_FRAME_SPEED      = 0x0D,  // device->host: report speed after reset
};

//--------------------------------------------------------------------+
// Frame header (3 bytes on the wire)
//--------------------------------------------------------------------+
typedef struct __attribute__((packed)) {
  uint8_t  type;
  uint16_t payload_len;
} vusb_frame_hdr_t;

#define VUSB_FRAME_HDR_SIZE  3
#define VUSB_MAX_PAYLOAD     1024
#define VUSB_MAX_FRAME_SIZE  (VUSB_FRAME_HDR_SIZE + VUSB_MAX_PAYLOAD)

//--------------------------------------------------------------------+
// Payload structures
//--------------------------------------------------------------------+

// VUSB_FRAME_SETUP: payload is always 8 bytes (USB SETUP packet)
#define VUSB_SETUP_SIZE  8

// VUSB_FRAME_DATA_OUT / VUSB_FRAME_DATA_RESP: ep_addr + data
typedef struct __attribute__((packed)) {
  uint8_t  ep_addr;
  // followed by transfer data bytes
} vusb_data_hdr_t;

// VUSB_FRAME_DATA_IN: request IN data from endpoint
typedef struct __attribute__((packed)) {
  uint8_t  ep_addr;
  uint16_t max_len;
} vusb_in_req_t;

// VUSB_FRAME_ACK/NAK/STALL: ep_addr + transfer length
typedef struct __attribute__((packed)) {
  uint8_t  ep_addr;
  uint16_t xferred_len;
} vusb_handshake_t;

// VUSB_FRAME_SOF
typedef struct __attribute__((packed)) {
  uint32_t frame_number;
} vusb_sof_t;

// VUSB_FRAME_SET_ADDR
typedef struct __attribute__((packed)) {
  uint8_t dev_addr;
} vusb_set_addr_t;

// VUSB_FRAME_SPEED
typedef struct __attribute__((packed)) {
  uint8_t speed;  // tusb_speed_t
} vusb_speed_t;

#ifdef __cplusplus
}
#endif

#endif /* VUSB_PROTO_H_ */
