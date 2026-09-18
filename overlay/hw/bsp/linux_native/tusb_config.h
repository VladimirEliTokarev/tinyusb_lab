/*
 * TinyUSB config for linux_native BSP.
 * This is used as a fallback if the example doesn't provide its own.
 */

#ifndef TUSB_CONFIG_H_
#define TUSB_CONFIG_H_

#define CFG_TUSB_OS_LINUX_NATIVE  1
#define CFG_TUSB_MCU              OPT_MCU_NONE
#define CFG_TUSB_OS               OPT_OS_NONE

// Enable device stack
#define CFG_TUD_ENABLED           1
#define CFG_TUD_MAX_SPEED         OPT_MODE_FULL_SPEED
#define CFG_TUD_ENDPOINT0_SIZE    64

// CDC + MSC classes
#define CFG_TUD_CDC               1
#define CFG_TUD_MSC               1
#define CFG_TUD_HID               0
#define CFG_TUD_MIDI              0
#define CFG_TUD_VENDOR            0

// CDC FIFO size
#define CFG_TUD_CDC_RX_BUFSIZE    256
#define CFG_TUD_CDC_TX_BUFSIZE    256

// MSC buffer size
#define CFG_TUD_MSC_EP_BUFSIZE    512

// Debug
#define CFG_TUSB_DEBUG            2

#endif /* TUSB_CONFIG_H_ */
