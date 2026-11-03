# md_rewriter: new firmware plan

Implements `docs/NEW_FIRMWARE_SPEC.md`. Steps 0 and 1 are done on this machine. Steps 2 to 7 need the hardware (the dumper with a cartridge, and the ST-Link V2) and are written as a checklist to follow when it arrives. Nothing in this plan is run automatically.

## Step 0: Scaffold (done)

1. `git mv Firmware legacy_firmware`; all references fixed (`README.md` in four languages, `docs/`, `tools/check-firmware.sh`, `.gitignore`).
2. `new_firmware/` created from the legacy application code, trimmed vendor code, `.ioc`.
3. Added `Makefile`, `STM32F103VBTx_FLASH.ld`, `check.sh`.

Verify: `cd new_firmware && make && ./check.sh` (exit 0, 26 072 B flash, 9 200 B RAM at `-O1`). `tools/check-firmware.sh` still passes on `legacy_firmware`.

## Step 1: Firmware changes (done, NFW-1, 2, 2a, 3, 5, 6, 7, 8)

Files: `Core/Src/MD.c`, `Core/Src/main.c`, `Core/Inc/MD.h`, nothing else. One commit when the owner asks for it.

Verify without hardware: `make`, `make clean && make OPT=-O3` (the Makefile does not rebuild when only `OPT` changes), `make test`, `./check.sh`, `diff -ru legacy_firmware/Core new_firmware/Core` (ten hunks, all listed in the spec), app tests (`Software/tests`) with the fake firmware.

## Step 2: Back up the original firmware (hardware, before anything else)

Tools installed: `st-flash`, `openocd`, `gdb-multiarch`, `dfu-util`.

1. Wire the ST-Link V2 to the four SWD pads of the dumper PCB (`3V3`, `SWCLK` = PA14, `SWDIO` = PA13, `GND`; no NRST pad). Identify each pad with a multimeter first (silkscreen has no labels, `docs/HARDWARE_REVIEW.md` M4). Power the board from **one** source only: either USB, with the ST-Link `3V3` wire disconnected, or the ST-Link `3V3` with no USB.
2. `st-info --probe` must show `STM32F1xx medium-density`, 128 KiB flash (`0x20000`).
3. Read the flash back: `st-flash read backup_fw_1.0.bin 0x08000000 0x20000`.
4. Check it: the file is 131 072 bytes, is not all `0x00` or `0xFF`, and the first word (initial SP) is `0x20005000`-ish (`xxd backup_fw_1.0.bin | head -1`). If the read comes back empty, read protection is on: stop, do not erase, ask the author for the `.hex`.
5. Keep two copies of the backup and of the released `md-rewriter-firmware-1.0.hex`. Compare `sha256sum` of both: they may differ in the unused tail; compare the used part.

This step is the rollback for everything below. **Do not go on without it.**

## Step 3: Timing baseline (NFW-4)

Needs a logic analyzer (8 channels, at least 24 MHz) or an oscilloscope. Probe `/WR` (PB14), `/RD` (PB4), `/CS` (PB6), one data line (PE0) and one address line (PA0 side or PD0).

Measure on the **1.0 firmware** first (original behaviour), with a cartridge in the slot, during a ROM read and a ROM write:

| Quantity | 1.0 hex | new, `-O1` | new, `-O3` | Needed |
|----------|---------|-----------|-----------|--------|
| `/RD` low time in a read | | | | cartridge access time (a 150-200 ns mask ROM is the usual worst case) |
| `/CS` to data valid | | | | |
| `/WR` low time in a program cycle | | | | above the chip's tWP (35 ns on MX29LV640E; check the datasheet of the chip in use) |
| Time per word in a write | | | | |
| Time for a 512 KiB read | | | | |

Host-model hint (not a measurement): with the model's assumed 55 ns per `Delay_nop` iteration, the 1.0 write sequence starts the next unlock cycle about 6 us after a word program starts. A chip that ignores commands while it programs would then lose words with `FLASH_POLL=0`; the model does that for 9 us and 15 us chips. Measure the real gap (row "Time per word") before judging the control build.

Rules: the new build must not be shorter than 1.0 on `/RD` low and `/WR` low. If `-O1` is shorter, use `-O3` or raise the `Delay_nop` counts in `MD.c` (a measured change, recorded here). Choose `OPT` from this table.

## Step 4: First run of the new build

1. `make FW_VERSION=1.1.0-test` (and, as a control, `make FLASH_POLL=0 FW_VERSION=1.1.0-nopoll`).
2. Flash the control build first (no polling, closest to 1.0): `st-flash --format ihex write build/md-rewriter-new.hex` or `openocd -f interface/stlink.cfg -f target/stm32f1x.cfg -c "program build/md-rewriter-new.hex verify reset exit"`.
3. App: Connect (expect `Device identified.` and, in the log, the `FWVER:` line), Detect, Read ROM of a game you own twice. The two dumps must be identical and equal to a dump taken with the 1.0 firmware (`sha256sum`).
4. Flash the polling build. Repeat. Then on a **flash cartridge**: Erase all, write a 256 KiB ROM with Verify on, read it back, compare with the file.
5. Time the write (plan step 3 table row "Time per word"): polling must stay under 2x the control build.

Pass: dumps identical, write verifies, no `WRITE TIMEOUT` on a healthy cartridge.

## Step 5: Compatibility checklist

| # | Test | Expected |
|---|------|----------|
| 1 | Old app (the original release) with the new firmware: connect, detect, read, write, erase | works; extra `FWVER:` line harmless |
| 2 | New app with firmware 1.0 (reflash the backup) | works; no `WRITE TIMEOUT` line ever; verify still works |
| 3 | Detect with the Macronix cartridge | `FLASHID:C2CB`, `MX29LV640EB MD FLASH CART` |
| 3a | Detect with the Spansion cartridge (S29GL064N90TFI03, PCB Ver1.4) | `FLASHID:017E`, `S29GL064N MD FLASH CART` (needs ID cycle 2 = `0C`; if it prints `NO FIND NGP CARD`, record the real cycle 2/3 values and fix `flashid.c`) |
| 4 | Detect with the ST-chip cartridge (the one bought) | `FLASHID:20xx`; name from the table or `NO FIND NGP CARD`; note the real ID in the table and in `Software/chipdb.cpp`, mark confirmed |
| 5 | Write without erasing first, over existing data | `WRITE TIMEOUT` at the first word that needs 0 to 1 (the polling working as intended), not silent corruption |
| 6 | Write to a mask-ROM game cartridge (not flash) | `WRITE TIMEOUT` within seconds, not a hang; the cartridge is untouched |
| 7 | Erase by size, 512K / 1M / 2M / 4M, on the ST chip | completes; on a top-boot or uniform chip (`DT`, S29GL064N model 03) the firmware now erases the first 64 KiB sector once (NFW-10); on the Macronix chip it still steps by 8 KiB. Time both and compare with the 1.0 firmware |
| 8 | Read 8 MiB (the app offers it after connecting to this firmware: size list, "8 MiB") | `8M ROM DUMP START!!!`, 8 388 608 bytes, `DUMPER ROM FINISH!!!` |
| 9 | Save read/write (SRAM) on a battery cartridge | same bytes as with 1.0 |
| 10 | Unplug during a write | the app reports it; reconnect works |

Any failure: reflash `backup_fw_1.0.bin` (`st-flash write backup_fw_1.0.bin 0x08000000`), record the failure in this file.

## Step 6: Rollback and field policy

- Rollback = reflash the backup over SWD. There is no USB update (D1), so users cannot do it themselves: the new firmware is **not** published as a release until steps 3 to 5 pass on at least two cartridges.
- If polling misbehaves on some chip, ship a build with `FLASH_POLL=0`; the app's read-back verify (default on) still catches bad writes.

## Step 7: Results and release

1. Fill the tables above, mark confirmed chip IDs in `new_firmware/Core/Src/MD.c` (`flash_chips[]`, the `not confirmed` comments) and `Software/chipdb.cpp`.
2. D6 is already in the app (8 MiB offered when `FWVER:` is present). If the 8 MiB read fails on hardware (checklist 8), remove it: either drop `FWVER:`/code 5 from the firmware or the `extendedRomSize()` entry in `Software/protocol.cpp`.
3. Tag and publish `md-rewriter-firmware-1.1.hex` with the Makefile output, the `sha256sum`, and the backup/rollback instructions in the release notes.

## Future: USB update (D1), design sketch only

- A small bootloader (CDC or DFU; `dapboot`-class, about 4 to 8 KiB) at `0x08000000`; the application moves to `0x08002000` (linker script `ORIGIN`, `SCB->VTOR` in `SystemInit`, `FLASH LENGTH` reduced).
- Entry: the application writes a magic word to a backup register and resets; the bootloader sees it and waits for a new image. A `BOOT` pad (the PCB needs one, M5) as the recovery path.
- The app needs a new opcode for "enter bootloader", which is allowed **only** in a firmware that also has the bootloader, because firmware 1.0 cannot be updated this way: those units still need one SWD session.
- Risk: a failed bootloader flash bricks the unit without SWD. Needs a hardware test plan of its own.

## Order of work

Firmware changes and the app improvements are done and compile. Everything that needs hardware is blocked until the dumper arrives, in this order: step 2 (backup), 3 (timing), 4 (first run), 5 (checklist), 6 and 7.
