#!/usr/bin/env bash
# Static checks for the new firmware sources (our own code only, not the vendor drivers).
#  1. arm-none-eabi-gcc -Wall -Wextra -Wformat=2 on every own source, in both FLASH_POLL modes
#  2. cppcheck, when installed
# Exit status 0 only when there is nothing to report.
set -u
cd "$(dirname "$0")"

INC=(
  -ICore/Inc -IUSB_DEVICE/App -IUSB_DEVICE/Target
  -IDrivers/STM32F1xx_HAL_Driver/Inc
  -IDrivers/CMSIS/Device/ST/STM32F1xx/Include
  -IDrivers/CMSIS/Include
  -IMiddlewares/ST/STM32_USB_Device_Library/Core/Inc
  -IMiddlewares/ST/STM32_USB_Device_Library/Class/CDC/Inc
)
FILES=(Core/Src/main.c Core/Src/MD.c Core/Src/flashid.c Core/Src/gpio.c Core/Src/usart.c Core/Src/stm32f1xx_it.c
       USB_DEVICE/App/usbd_cdc_if.c USB_DEVICE/App/usbd_desc.c USB_DEVICE/App/usb_device.c)
# USB_DEVICE/Target/usbd_conf.c is CubeMX-generated (three unused-parameter warnings), left out.

status=0
total=0
for poll in 1 0; do
  for f in "${FILES[@]}"; do
    out=$(arm-none-eabi-gcc -mcpu=cortex-m3 -mthumb -DUSE_HAL_DRIVER -DSTM32F103xB -DFLASH_POLL=$poll \
          "${INC[@]}" -std=gnu11 -Wall -Wextra -Wformat=2 -fsyntax-only "$f" 2>&1)
    rc=$?
    n=$(printf '%s\n' "$out" | grep -c -E 'warning:|error:')
    total=$((total + n))
    if [ "$rc" -ne 0 ] || [ "$n" -ne 0 ]; then
      printf '[FLASH_POLL=%s] %s\n' "$poll" "$f"
      printf '%s\n' "$out" | grep -E 'warning:|error:'
      status=1
    fi
  done
done
echo "gcc: $total warning/error line(s)"

if command -v cppcheck >/dev/null 2>&1; then
  out=$(cppcheck --quiet --enable=warning,portability --inline-suppr \
        -DUSE_HAL_DRIVER -DSTM32F103xB -D__GNUC__=13 "${INC[@]}" \
        Core/Src/main.c Core/Src/MD.c Core/Src/flashid.c USB_DEVICE/App/usbd_cdc_if.c 2>&1 | grep -v -E "^$")
  if [ -n "$out" ]; then
    printf '%s\n' "$out"
    status=1
  fi
  echo "cppcheck: $(printf '%s' "$out" | grep -c .) finding(s)"
else
  echo "cppcheck: not installed, skipped"
fi

# host tests (no hardware needed): chip ID table, and MD.c against the flash chip model
if command -v gcc >/dev/null 2>&1; then
  tmp=$(mktemp -d)
  if gcc -Wall -Wextra -Wformat=2 -ICore/Inc tests/test_flashid.c Core/Src/flashid.c -o "$tmp/t1" && "$tmp/t1" >"$tmp/out1"; then
    echo "host test flashid: $(tail -1 "$tmp/out1")"
  else
    cat "$tmp/out1" 2>/dev/null; echo "host test flashid: FAILED"; status=1
  fi
  if gcc -Wall -Wextra -Wformat=2 -DMD_HOST_TEST -DFLASH_POLL=1 -Itests/host -ICore/Inc \
       tests/host/test_md_flash.c tests/host/host_hw.c Core/Src/MD.c Core/Src/flashid.c -o "$tmp/t2" && "$tmp/t2" >"$tmp/out2"; then
    echo "host test MD.c vs chip model: $(tail -1 "$tmp/out2")"
  else
    cat "$tmp/out2" 2>/dev/null; echo "host test MD.c vs chip model: FAILED"; status=1
  fi
  rm -rf "$tmp"
else
  echo "host tests: gcc not installed, skipped"
fi
exit $status
