BOARD_TYPE          := 0x09
BOARD_REVISION      := 0x03
BOOTLOADER_VERSION  := 0x88
HW_TYPE             := 0x00

CHIP                := STM32F405RGT
BOARD               := STM32F4xx_RM
MODEL               := HD
MODEL_SUFFIX        :=

USB_VEND            := "OpenPilot"
USB_PROD            := "Revolution"

include $(MAKE_INC_DIR)/firmware-arches.mk

# Note: These must match the values in link_$(BOARD)_memory.ld
BL_BANK_BASE        := 0x08000000  # Start of bootloader flash
BL_BANK_SIZE        := 0x00008000  # Should include BD_INFO region (32kb)

# Leave the remaining 16KB and 64KB sectors for other uses

FW_BANK_BASE        := 0x08020000  # Start of firmware flash
FW_BANK_SIZE        := 0x00060000  # Should include FW_DESC_SIZE (384kb)

FW_DESC_SIZE        := 0x00000064

EE_BANK_BASE        := 0x00000000
EE_BANK_SIZE        := 0x00000000

EF_BANK_BASE        := 0x08000000  # Start of entire flash image (usually start of bootloader as well)
EF_BANK_SIZE        := 0x00080000  # Size of the entire flash image (from bootloader until end of firmware)

OSCILLATOR_FREQ     :=   8000000
SYSCLK_FREQ         := 168000000
