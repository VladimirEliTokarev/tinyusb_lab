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
 * This file is part of the TinyUSB stack.
 *
 * QEMU raspi0 BSP - runs bare-metal on qemu-system-arm -M raspi0.
 * Hardcodes clocks to avoid vcmailbox queries that QEMU may not implement.
 */

/* metadata:
   manufacturer: QEMU
*/

#include "bsp/board_api.h"
#include "board.h"

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#pragma GCC diagnostic ignored "-Wredundant-decls"
#endif

#include "broadcom/cpu.h"
#include "broadcom/gpio.h"
#include "broadcom/interrupts.h"
#include "broadcom/mmu.h"
#include "broadcom/caches.h"
#include "broadcom/vcmailbox.h"

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

//--------------------------------------------------------------------+
// PL011 UART registers (primary UART on BCM2835)
// QEMU maps serial_hd(0) to PL011 at 0x20201000
// Used as the virtual USB transport for the device-role board
//--------------------------------------------------------------------+
#define PL011_BASE    0x20201000
#define PL011_DR      (*(volatile uint32_t*)(PL011_BASE + 0x00))
#define PL011_FR      (*(volatile uint32_t*)(PL011_BASE + 0x18))
#define PL011_IBRD    (*(volatile uint32_t*)(PL011_BASE + 0x24))
#define PL011_FBRD    (*(volatile uint32_t*)(PL011_BASE + 0x28))
#define PL011_LCRH    (*(volatile uint32_t*)(PL011_BASE + 0x2C))
#define PL011_CR      (*(volatile uint32_t*)(PL011_BASE + 0x30))
#define PL011_IMSC    (*(volatile uint32_t*)(PL011_BASE + 0x38))
#define PL011_ICR     (*(volatile uint32_t*)(PL011_BASE + 0x44))

#define PL011_FR_RXFE (1 << 4)  // RX FIFO empty
#define PL011_FR_TXFF (1 << 5)  // TX FIFO full

// AUX mini-UART registers (secondary UART, console output)
#define LED_PIN               18
#define LED_STATE_ON          1
#define UART_TX_PIN           14

// BCM2835 core clock is 250 MHz in QEMU
#define QEMU_CORE_CLOCK_HZ   250000000

//--------------------------------------------------------------------+
// PL011 transport for virtual USB (used by dcd_vusb.c)
//--------------------------------------------------------------------+
void pl011_init(void) {
  PL011_CR = 0;          // disable UART
  PL011_ICR = 0x7FF;     // clear all interrupts

  // 115200 baud at 3 MHz UART clock (QEMU default for PL011)
  // Divider = 3000000 / (16 * 115200) = 1.627
  // IBRD = 1, FBRD = round(0.627 * 64) = 40
  PL011_IBRD = 1;
  PL011_FBRD = 40;

  PL011_LCRH = (3 << 5); // 8-N-1, FIFO disabled
  PL011_CR = (1 << 0) | (1 << 8) | (1 << 9); // UART enable, TX enable, RX enable
}

void pl011_write(const uint8_t* buf, uint32_t len) {
  for (uint32_t i = 0; i < len; i++) {
    while (PL011_FR & PL011_FR_TXFF) {}
    PL011_DR = buf[i];
  }
}

uint32_t pl011_read(uint8_t* buf, uint32_t max_len) {
  uint32_t count = 0;
  while (count < max_len && !(PL011_FR & PL011_FR_RXFE)) {
    buf[count++] = (uint8_t)(PL011_DR & 0xFF);
  }
  return count;
}

bool pl011_readable(void) {
  return !(PL011_FR & PL011_FR_RXFE);
}

//--------------------------------------------------------------------+
// Forward USB interrupt events to TinyUSB IRQ Handler
//--------------------------------------------------------------------+
void USB_IRQHandler(void) {
#if defined(QEMU_ROLE_HOST) || !defined(QEMU_ROLE_DEVICE)
  tud_int_handler(0);
#endif
}

//--------------------------------------------------------------------+
// Board porting API
//--------------------------------------------------------------------+
void board_init(void) {
  setup_mmu_flat_map();
  init_caches();

  // LED
  gpio_set_function(LED_PIN, GPIO_FUNCTION_OUTPUT);
  gpio_set_pull(LED_PIN, BP_PULL_NONE);
  board_led_write(true);

  // Console UART (AUX mini-UART on serial_hd(1))
  // Hardcode baud divider instead of querying vcmailbox
  COMPLETE_MEMORY_READS;
  AUX->ENABLES_b.UART_1 = true;

  UART1->IER = 0;
  UART1->CNTL = 0;
  UART1->LCR_b.DATA_SIZE = UART1_LCR_DATA_SIZE_MODE_8BIT;
  UART1->MCR = 0;
  UART1->IER = 0;

  // Hardcoded baud divider for 115200 at 250 MHz core clock
  // Formula: divider = (core_clock / (8 * baud)) - 1
  // = (250000000 / (8 * 115200)) - 1 = 270
  UART1->BAUD = 270;
  UART1->CNTL |= UART1_CNTL_TX_ENABLE_Msk;
  COMPLETE_MEMORY_READS;

  gpio_set_function(UART_TX_PIN, GPIO_FUNCTION_ALT5);

#if defined(QEMU_ROLE_DEVICE)
  // Initialize PL011 for virtual USB transport
  pl011_init();
#else
  // Turn on USB peripheral (host role uses real DWC2 controller)
  vcmailbox_set_power_state(VCMAILBOX_DEVICE_USB_HCD, true);
#endif

  // Timer 1/1024 second tick
  SYSTMR->CS_b.M1 = 1;
  SYSTMR->C1 = SYSTMR->CLO + 977;
  BP_EnableIRQ(TIMER_1_IRQn);

#if !defined(QEMU_ROLE_DEVICE)
  BP_SetPriority(USB_IRQn, 0x00);
  BP_ClearPendingIRQ(USB_IRQn);
  BP_EnableIRQ(USB_IRQn);
#endif

  BP_EnableIRQs();
}

void board_led_write(bool state) {
  gpio_set_value(LED_PIN, state ? LED_STATE_ON : (1 - LED_STATE_ON));
}

uint32_t board_button_read(void) {
  return 0;
}

int board_uart_read(uint8_t* buf, int len) {
  (void) buf;
  (void) len;
  return 0;
}

int board_uart_write(void const* buf, int len) {
  const uint8_t* p = (const uint8_t*) buf;
  int count = 0;
  while (count < len) {
    if (!UART1->STAT_b.TX_READY) {
      break;
    }
    UART1->IO = p[count];
    count++;
  }
  return count;
}

#if CFG_TUSB_OS == OPT_OS_NONE
volatile uint32_t system_ticks = 0;

void TIMER_1_IRQHandler(void) {
  system_ticks++;
  SYSTMR->C1 += 977;
  SYSTMR->CS_b.M1 = 1;
}

uint32_t tusb_time_millis_api(void) {
  return system_ticks;
}
#endif

void HardFault_Handler(void) {
}

void _init(void) {
}
