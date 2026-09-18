# Native Linux x86 build for TinyUSB with Raw Gadget DCD / libusb HCD.
#
# Device build (default):
#   make -f hw/bsp/linux_native/Makefile EXAMPLE=device/cdc_msc
#   sudo ./build_native/cdc_msc
#
# Host build:
#   make -f hw/bsp/linux_native/Makefile EXAMPLE=host/cdc_msc_hid ROLE=host
#   sudo ./build_native/cdc_msc_hid
#
# Prerequisites:
#   sudo modprobe dummy_hcd
#   sudo modprobe raw_gadget
#   sudo apt install libusb-1.0-0-dev   (for host build)

TOP ?= $(shell cd ../../.. && pwd)
EXAMPLE ?= device/cdc_msc
EXAMPLE_NAME := $(notdir $(EXAMPLE))
BUILD := $(TOP)/build_native
ROLE ?= device

CC := gcc
CFLAGS := -Wall -Wno-error -Wno-implicit-function-declaration -Wno-unused-function -g -O0 \
          -DCFG_TUSB_OS_LINUX_NATIVE=1 \
          -DCFG_TUSB_MCU=OPT_MCU_NONE \
          -DCFG_TUSB_DEBUG=2 \
          -DBOARD_TUD_RHPORT=0 \
          -DBOARD_TUH_RHPORT=0

LDFLAGS := -lpthread

# TinyUSB core sources (always needed)
SRC := \
  $(TOP)/src/tusb.c \
  $(TOP)/src/common/tusb_fifo.c

# Device stack sources
SRC_DEVICE := \
  $(TOP)/src/device/usbd.c \
  $(TOP)/src/class/audio/audio_device.c \
  $(TOP)/src/class/cdc/cdc_device.c \
  $(TOP)/src/class/dfu/dfu_device.c \
  $(TOP)/src/class/dfu/dfu_rt_device.c \
  $(TOP)/src/class/hid/hid_device.c \
  $(TOP)/src/class/midi/midi_device.c \
  $(TOP)/src/class/midi/midi2_device.c \
  $(TOP)/src/class/msc/msc_device.c \
  $(TOP)/src/class/mtp/mtp_device.c \
  $(TOP)/src/class/net/ecm_rndis_device.c \
  $(TOP)/src/class/net/ncm_device.c \
  $(TOP)/src/class/printer/printer_device.c \
  $(TOP)/src/class/usbtmc/usbtmc_device.c \
  $(TOP)/src/class/vendor/vendor_device.c \
  $(TOP)/src/class/video/video_device.c \
  $(TOP)/src/portable/linux/dcd_rawgadget.c

# Host stack sources
SRC_HOST := \
  $(TOP)/src/host/usbh.c \
  $(TOP)/src/host/hub.c \
  $(TOP)/src/class/cdc/cdc_host.c \
  $(TOP)/src/class/hid/hid_host.c \
  $(TOP)/src/class/msc/msc_host.c \
  $(TOP)/src/class/audio/audio_host.c \
  $(TOP)/src/class/midi/midi_host.c \
  $(TOP)/src/class/midi/midi2_host.c \
  $(TOP)/src/typec/usbc.c \
  $(TOP)/src/portable/linux/hcd_libusb.c

ifeq ($(ROLE),host)
  SRC += $(SRC_HOST)
  LDFLAGS += -lusb-1.0
else
  SRC += $(SRC_DEVICE)
endif

# BSP
SRC += $(TOP)/hw/bsp/linux_native/family.c \
       $(TOP)/hw/bsp/board.c

# Example sources
SRC += $(wildcard $(TOP)/examples/$(EXAMPLE)/src/*.c)

# Includes
INC := \
  -I$(TOP)/src \
  -I$(TOP)/hw/bsp/linux_native \
  -I$(TOP)/hw/bsp \
  -I$(TOP)/hw \
  -I$(TOP)/examples/$(EXAMPLE)/src

OBJ := $(patsubst $(TOP)/%.c, $(BUILD)/%.o, $(SRC))

.PHONY: all clean

all: $(BUILD)/$(EXAMPLE_NAME)

$(BUILD)/$(EXAMPLE_NAME): $(OBJ)
	@mkdir -p $(dir $@)
	$(CC) $(OBJ) $(LDFLAGS) -o $@
	@echo ""
	@echo "Built: $@"
	@echo "Run:   sudo $@"

$(BUILD)/%.o: $(TOP)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INC) -c $< -o $@

clean:
	rm -rf $(BUILD)
