# firmware_1.1

Firmware for the md_rewriter STM32F103VB with the compatibility changes described in `../docs/NEW_FIRMWARE_SPEC.md`. **Not tested on hardware.** Do not flash it before reading `../docs/NEW_FIRMWARE_PLAN.md`, steps 2 and 3 (back up the original firmware first).

## Build

```bash
sudo apt install gcc-arm-none-eabi make cppcheck        # once
cd firmware_1.1
make                                  # build/md-rewriter-new.{elf,hex,bin}
make OPT=-O3                          # other optimisation level (timing, see spec NFW-4)
make OPT="-O1 -DFLASH_POLL=0"        # without DQ7 polling (the 1.0 write sequence)
make FW_VERSION=1.1.0-test
make test                             # host tests: ID table, and MD.c against a flash chip model
./check.sh                            # warnings, cppcheck, host test; both polling modes
make clean
```

## Flash (manual, with an ST-Link V2 on the SWD pads)

```bash
st-info --probe
st-flash read backup_fw_1.0.bin 0x08000000 0x20000          # first, always
st-flash --format ihex write build/md-rewriter-new.hex
# or
openocd -f interface/stlink.cfg -f target/stm32f1x.cfg \
        -c "program build/md-rewriter-new.hex verify reset exit"
```

## Layout

| Path | What |
|------|------|
| `Core/`, `USB_DEVICE/` | Application code (from `legacy_firmware`, plus the changes in the spec) |
| `Drivers/`, `Middlewares/` | Vendor code: STM32F1 HAL, CMSIS headers, GCC startup file, USB device library (unchanged, not the full CubeMX tree) |
| `STM32F103VBTx_FLASH.ld` | Linker script: 128 KiB flash, 20 KiB RAM, no bootloader |
| `tests/` | PC tests: `test_flashid.c` (ID table) and `host/` (the real `MD.c` against a flash chip model); run with `make test` |
| `GBVBT6.ioc` | CubeMX project, kept for reference; regenerating code from it would overwrite the changes |
