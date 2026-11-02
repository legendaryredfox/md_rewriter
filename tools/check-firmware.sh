#!/usr/bin/env bash
# Static check of the application firmware sources without Keil.
# Usage: tools/check-firmware.sh
# Needs: arm-none-eabi-gcc. Uses cppcheck when available.
set -u
cd "$(dirname "$0")/../legacy_firmware"

INC=(
  -ICore/Inc -IUSB_DEVICE/App -IUSB_DEVICE/Target
  -IDrivers/STM32F1xx_HAL_Driver/Inc
  -IDrivers/CMSIS/Device/ST/STM32F1xx/Include
  -IDrivers/CMSIS/Core/Include
  -IMiddlewares/ST/STM32_USB_Device_Library/Core/Inc
  -IMiddlewares/ST/STM32_USB_Device_Library/Class/CDC/Inc
)
DEFS=(-DUSE_HAL_DRIVER -DSTM32F103xB -D__GNUC__=13)
FILES=(Core/Src/main.c Core/Src/MD.c USB_DEVICE/App/usbd_cdc_if.c)

status=0
total=0
for f in "${FILES[@]}"; do
  out=$(arm-none-eabi-gcc -mcpu=cortex-m3 -mthumb "${DEFS[@]}" "${INC[@]}" \
        -Wall -Wextra -fsyntax-only "$f" 2>&1)
  rc=$?
  n=$(printf '%s\n' "$out" | grep -c -E 'warning:|error:')
  total=$((total + n))
  if [ "$rc" -ne 0 ] || [ "$n" -ne 0 ]; then
    printf '%s\n' "$out" | grep -E 'warning:|error:'
    status=1
  fi
done
echo "gcc: $total warning/error line(s)"

if command -v cppcheck >/dev/null 2>&1; then
  cppcheck --quiet --error-exitcode=1 --enable=warning,portability \
    --suppress=missingIncludeSystem --inline-suppr \
    "${DEFS[@]}" "${INC[@]}" "${FILES[@]}" || status=1
else
  echo "cppcheck not installed, skipped"
fi
exit $status
