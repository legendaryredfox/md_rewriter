# Build of the md_rewriter firmware with arm-none-eabi-gcc (no Keil needed).
# Usage:  make            builds build/md-rewriter-new.{elf,hex,bin} and prints the size
#         make clean
#         make check      static checks (-Wall -Wextra -Werror on our own sources, cppcheck)
# Needs:  gcc-arm-none-eabi (and cppcheck for `make check`).
# Nothing here flashes anything: see docs/NEW_FIRMWARE_PLAN.md for the flashing procedure.

TARGET   := md-rewriter-new
BUILD    := build
PREFIX   ?= arm-none-eabi-
CC       := $(PREFIX)gcc
OBJCOPY  := $(PREFIX)objcopy
SIZE     := $(PREFIX)size

# Version string reported in the connect banner log; override with `make FW_VERSION=...`.
FW_VERSION ?= 1.1.0-dev

CPU      := -mcpu=cortex-m3 -mthumb
DEFS     := -DUSE_HAL_DRIVER -DSTM32F103xB -DFW_VERSION=\"$(FW_VERSION)\"
INCS     := -ICore/Inc -IUSB_DEVICE/App -IUSB_DEVICE/Target \
            -IDrivers/STM32F1xx_HAL_Driver/Inc -IDrivers/STM32F1xx_HAL_Driver/Inc/Legacy \
            -IMiddlewares/ST/STM32_USB_Device_Library/Core/Inc \
            -IMiddlewares/ST/STM32_USB_Device_Library/Class/CDC/Inc \
            -IDrivers/CMSIS/Device/ST/STM32F1xx/Include -IDrivers/CMSIS/Include

# Optimisation level 1 and no LTO: the cartridge bus timing in MD.c is a software delay
# (Delay_nop) tuned against the Keil build; keep the code shape predictable. See the spec, NFW-4.
OPT      ?= -O1
CFLAGS   := $(CPU) $(DEFS) $(INCS) $(OPT) -g3 -ffunction-sections -fdata-sections \
            -fno-common -std=gnu11
LDFLAGS  := $(CPU) -TSTM32F103VBTx_FLASH.ld --specs=nano.specs --specs=nosys.specs \
            -Wl,--gc-sections -Wl,-Map=$(BUILD)/$(TARGET).map -Wl,--print-memory-usage

OWN_SRC := $(wildcard Core/Src/*.c) $(wildcard USB_DEVICE/App/*.c) $(wildcard USB_DEVICE/Target/*.c)

HAL_SRC := $(addprefix Drivers/STM32F1xx_HAL_Driver/Src/, \
  stm32f1xx_hal.c stm32f1xx_hal_cortex.c stm32f1xx_hal_dma.c stm32f1xx_hal_exti.c \
  stm32f1xx_hal_flash.c stm32f1xx_hal_flash_ex.c stm32f1xx_hal_gpio.c stm32f1xx_hal_gpio_ex.c \
  stm32f1xx_hal_pcd.c stm32f1xx_hal_pcd_ex.c stm32f1xx_hal_pwr.c stm32f1xx_hal_rcc.c \
  stm32f1xx_hal_rcc_ex.c stm32f1xx_hal_tim.c stm32f1xx_hal_tim_ex.c stm32f1xx_hal_uart.c \
  stm32f1xx_ll_usb.c)

USB_SRC := $(addprefix Middlewares/ST/STM32_USB_Device_Library/, \
  Core/Src/usbd_core.c Core/Src/usbd_ctlreq.c Core/Src/usbd_ioreq.c Class/CDC/Src/usbd_cdc.c)

STARTUP := Drivers/CMSIS/Device/ST/STM32F1xx/Source/Templates/gcc/startup_stm32f103xb.s

SRC  := $(OWN_SRC) $(HAL_SRC) $(USB_SRC)
OBJS := $(addprefix $(BUILD)/,$(SRC:.c=.o)) $(BUILD)/startup.o

.PHONY: all clean check
all: $(BUILD)/$(TARGET).hex $(BUILD)/$(TARGET).bin
	@$(SIZE) $(BUILD)/$(TARGET).elf

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/startup.o: $(STARTUP)
	@mkdir -p $(dir $@)
	$(CC) $(CPU) -x assembler-with-cpp -c $< -o $@

$(BUILD)/$(TARGET).elf: $(OBJS) STM32F103VBTx_FLASH.ld
	$(CC) $(OBJS) $(LDFLAGS) -o $@

%.hex: %.elf
	$(OBJCOPY) -O ihex $< $@
%.bin: %.elf
	$(OBJCOPY) -O binary $< $@

check:
	@./check.sh

clean:
	rm -rf $(BUILD)

-include $(OBJS:.o=.d)
