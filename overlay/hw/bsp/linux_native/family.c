/*
 * The MIT License (MIT)
 * Copyright (c) 2024 TinyUSB contributors
 *
 * Native Linux BSP for TinyUSB.
 * Implements the board_api.h functions for a regular Linux process.
 * No hardware, no cross-compilation — just gcc on x86.
 */

#include "bsp/board_api.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

//--------------------------------------------------------------------+
// Board API
//--------------------------------------------------------------------+

void board_init(void) {
  printf("[board] linux_native board_init\n");
}

void board_led_write(bool state) {
  (void)state;
}

uint32_t board_button_read(void) {
  return 0;
}

int board_uart_read(uint8_t* buf, int len) {
  (void)buf;
  (void)len;
  return 0;
}

int board_uart_write(void const* buf, int len) {
  return (int)fwrite(buf, 1, (size_t)len, stdout);
}

//--------------------------------------------------------------------+
// Time
//--------------------------------------------------------------------+

uint32_t tusb_time_millis_api(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

// Stub for new CDC API not yet in all examples
void tud_cdc_notify_uart_state(void* p) { (void)p; }
