# md_rewriter: new firmware specification

Status: draft, implemented and compiled, **not tested on hardware**. Written 2026-11-02.

## 1. Context

| Folder | Role |
|--------|------|
| `legacy_firmware/` | The firmware as it was after the 2026-10 review fixes (FW-1 to FW-10 of `docs/SPEC.md`). Frozen: no more edits. Keil project kept. |
| `new_firmware/` | Copy of the legacy application code plus the compatibility changes below. Builds with `make` and `arm-none-eabi-gcc`, no Keil. Vendor code is a trimmed copy (HAL, CMSIS headers, USB device library; no CMSIS-DSP/NN/RTOS), 18 MB instead of 69 MB. |
| `Software/` | Desktop app. Already improved to work with both firmwares (see 3.3). |

Facts checked on this machine:

- MCU is **STM32F103VB** (`legacy_firmware/GBVBT6.ioc`: `STM32F103VBTx`, LQFP100): 128 KiB flash (`0x08000000`..`0x0801FFFF`), 20 KiB RAM. (`docs/HARDWARE_REVIEW.md` M5 first said "xE"; corrected.)
- New build, `-O1`: 26 072 B flash (19.9 %), 9 200 B RAM (44.9 %).
- `legacy_firmware` is **not byte-identical to the released 1.0 hex**: it already contains the review fixes, among them FW-8 (a bus-pin change in the SRAM dump). Treat the released `md-rewriter-firmware-1.0.hex` as the only "original" and back it up from the device before flashing anything (plan, step 1).
- The released 1.0 was built with Keil ARM Compiler 5 at `-O3` (`MDK-ARM/GBVBT6.uvprojx`: `uAC6 = 0`, `Optim = 4`). The new build uses GCC. Bus timing is done with software delays (`Delay_nop`), so it can differ. This is the main risk (NFW-4).

Goals, from the owner: more compatibility with rewritable cartridges (other chips, safer writes), same hardware, same wire protocol as far as old software is concerned.

## 2. Hard constraints

- **N-C1. Wire compatibility in both directions.** New firmware must work with the old app and with the app in `Software/`; the app in `Software/` must keep working with firmware 1.0. No new opcodes, framing or checksums. Allowed: extra text lines after existing lines; new values of an existing argument that old firmware ignores or maps to the old behaviour.
- **N-C2. No hardware.** Every change here is verified by compiling and static analysis only. Each one is listed as "untested on hardware" and has a switch or a rollback (plan, step 6).
- **N-C3. Nothing is flashed from automation.** Flashing, reading flash back and any SWD access are manual steps in the plan.
- **N-C4. `legacy_firmware/` is frozen.** Differences between the two folders are exactly the ones listed in section 4 (acceptance NFW-8).
- **N-C5. Cartridge bus order and delay values stay as in legacy**, except where a requirement says otherwise.

## 3. Wire protocol

### 3.1 Unchanged

Commands `0A 0B 0C 0D 0E 0F 1A 1B 1E 2E`, header `AA 55 AA BB`, 1 KiB write blocks with the 7-byte command behind them, `ADD:0x... WRITE OK` / `WRITE GK`, dump framing, erase texts.

### 3.2 Additions (all backward compatible)

| Where | Old firmware | New firmware | Old app sees | New app does |
|-------|--------------|--------------|--------------|--------------|
| `0D` detect, ID line | `FLASHID:%X%X` | `FLASHID:%02X%02X` | text | parses 4 hex digits (also still parses the short form) |
| `0D` detect, chip line | `MX29LV640EB MD FLASH CART` or `NO FIND NGP CARD` | `<name> MD FLASH CART` for every chip in the table, else `NO FIND NGP CARD`. The Macronix line is byte-identical to 1.0. | text | names the chip from its own table too |
| `0B` ROM block failed | (none, silent) | `ADD:0x<word addr> WRITE TIMEOUT` instead of `WRITE OK` | waits, idle timeout | stops at once and says why |
| `0C` connect | banner | banner, then `FWVER:<version>` | extra text after the banner | waits up to 400 ms for it after the banner, enables 8 MiB reads |
| `0A` dump, arg `05` | treated as 512K (`else` branch) | `8M ROM DUMP START!!!` and 8192 blocks | not sent | offered only after `FWVER:` (D6) |

### 3.3 App side (done in `Software/`, works with both firmwares)

ROM of any even size up to 4 MiB (padded by the protocol with `0xFF`), `.smd` conversion, verify-after-write by reading back with the normal dump command, chip table keyed on `FLASHID:`, plain-language detect messages, header warnings, `WRITE TIMEOUT` handling. No firmware dependency except the last item, which only reacts to a new line.

## 4. Requirements

| ID | Requirement | Acceptance |
|----|-------------|------------|
| NFW-1 | `checkid()` prints `FLASHID:%02X%02X`, and looks the ID up in a table (`Core/Src/flashid.c`, `flash_chips[]` and `identify_s29gl064n()`: MX29LV640EB `C2CB`, MX29LV640ET `C2C9`*, M29W640DB `20DF`, M29W640DT `20DE`, M29W640G `207E`*, S29GL064N `017E`, accepted only when ID cycle 2 (word `0x0E`) is `0C` or `10`; * = not confirmed against a datasheet). A hit prints `<name> MD FLASH CART`; a miss prints `NO FIND NGP CARD`. The `C2CB` output is byte-identical to 1.0 except for the zero padding of the ID. | Code review; `FLASHID:` line cannot be ambiguous; builds. |
| NFW-2 | After every programmed word (`0B`), `flash_wait_program()` does DQ7 data polling on the word address with the low data byte, handles DQ5, is bounded by `FLASH_PROGRAM_POLL_MAX` polls, sends the chip a reset (`0xF0`) on failure, and leaves the bus in write mode. On failure the block stops and the firmware answers `ADD:0x.. WRITE TIMEOUT`. `-DFLASH_POLL=0` removes all of it and gives exactly the legacy write sequence. | Code review; builds in both modes; `check.sh` clean in both modes. |
| NFW-2a | `bus_input_fast()` / `bus_output_fast()` give the same pin state as `read_mode()` / `write_mode()` (pull-up inputs, 50 MHz push-pull outputs, ODR cleared/set as the HAL does) with register writes. They are used only by the polling loop. | Code review against `HAL_GPIO_Init`; hardware check in plan step 4. |
| NFW-3 | Dump argument `05` reads 8 MiB (`wsize = 8192`, banner `8M ROM DUMP START!!!`). Arguments `01..04` behave as before. | Code review. |
| NFW-4 | Timing review. `Delay_nop` call sites and counts are unchanged. Default `OPT=-O1`; `OPT=-O3` is selectable (`make OPT=-O3`). The release candidate must be chosen by measuring, not by default (plan, step 3): per-iteration cost of `Delay_nop`, `/WR` low time (must exceed the chip's tWP), `/RD` access time, against the 1.0 hex. | Measurements recorded in the plan's results table. |
| NFW-5 | `0C` connect sends `FWVER:` + `FW_VERSION` as a second line after the unchanged banner. Version comes from the Makefile (`FW_VERSION`, default `1.1.0-dev`). | Code review. |
| NFW-6 | `make` builds `build/md-rewriter-new.{elf,hex,bin}` with `arm-none-eabi-gcc` and prints the memory use. A linker script and the CMSIS GCC startup file are in the tree. | `make` exits 0; flash and RAM below 50 % each. |
| NFW-7 | `new_firmware/check.sh`: zero `-Wall -Wextra -Wformat=2` warnings on the own sources in both `FLASH_POLL` modes, zero cppcheck findings. | Script exits 0. |
| NFW-8 | Nothing else differs from `legacy_firmware`: `diff -ru legacy_firmware/Core new_firmware/Core` and the same for `USB_DEVICE/` show only the hunks of NFW-1, 2, 2a, 3, 5, 10 (plus the new `flashid.c/.h`), the new `MD.h` macros, and the global `disbuff` growing from 30 to 40 bytes (room for `<name> MD FLASH CART`). | Diff review. |
| NFW-9 | The Keil project is not carried over. A user without Keil can build and flash with the commands in `new_firmware/README.md`. | README review. |
| NFW-10 | `flash_autoselect()` reads ID words `0x00`, `0x01`, `0x0E`, `0x0F` in one autoselect session (same bus sequence and delays as 1.0) and `flashid.c` classifies them. Erase by size (`1E`, sizes 1..4 and the single-sector form) steps through eight 8 KiB sectors at the start of the chip only when `flash_has_bottom_boot_sectors()` is 1: it is 0 only for a chip positively known to be top boot or uniform (M29W640DT, S29GL064N model 03 or uniform), so those no longer erase their first 64 KiB sector eight times. The firmware still prints eight dots for that sector, because the app counts dots for its progress bar (`Protocol::eraseSteps`) and assumes the 1.0 count. Unknown chips and unreadable IDs keep the 1.0 steps. The wire output is unchanged. For testability the word program cycle moved from `main.c` into `flash_program_word()` and the erase-by-size loop into `erase_range()` (`MD.c`), with the same statements and delays; `main.c` calls them. | `cd new_firmware && make test`, `./check.sh`; hardware check: plan step 5, row 7. |
| NFW-11 | Host tests: `tests/host/` compiles the real `MD.c` on the PC (`-DMD_HOST_TEST`: pin accessors, `Delay_nop` and the HAL calls come from `tests/host/main.h` and `host_hw.c`) against a model of an AMD x16 chip (autoselect, word program with DQ7/DQ6/DQ5, sector and chip erase, bottom/top/uniform layouts, a mask ROM, a stuck chip). It fails on bus contention, writes with the data bus as input, and commands sent while the chip is busy. Covers detect output for 6 chips, the layout probe, program (typical and slow chip, 0-to-1 failure), erase by size 512K..4M on every layout, chip erase, read back. | `make test` and `./check.sh` pass (77 checks). The model comes from the datasheets and assumes 55 ns per `Delay_nop` iteration: it does not replace plan steps 3 to 5. |

## 5. Deferred (not in this version)

| ID | Item | Why not now |
|----|------|-------------|
| D1 | Update over USB (bootloader). | Needs a first SWD flash and a way to force the bootloader; `BOOT0` is tied to GND on the PCB (M5). Sketch in the plan, section "Future: USB update". |
| D2 | Other command sets (SST39VF `0x5555/0x2AAA`, Intel/J3). | The ID cannot even be read with the AMD sequence; each family needs its own detect. Needs a real cartridge to design against. |
| D3 | EEPROM saves (I2C 24Cxx cartridges). | New protocol. |
| D4 | SSF2 bank-switch mapper and ROMs above 8 MiB. | Hardware/mapper work. |
| D5 | `/UWR`, `/CAS2` not wired on the PCB. | Hardware (`docs/HARDWARE_REVIEW.md` L2). |
| D6 | ~~App support for the 8 MiB read and `FWVER`~~ **Done in the app (2026-11-02):** the size list gains 8 MiB only after a `FWVER:` line; firmware 1.0 never sends it, so 8 MiB is never offered there (`readRom` refuses it). Reading 8 MiB itself still needs the hardware check of NFW-3. Erase by size 8M is code 5, a full chip erase, on any firmware. Writing stays limited to 4 MiB. | — |
| D7 | Faster programming with the chip's write-buffer or unlock-bypass commands. | Timing and chip differences; measure first. |

## 6. Risks

| Risk | Mitigation |
|------|-----------|
| GCC build has different `Delay_nop` timing than the Keil `-O3` release, so the bus breaks or is marginal. | NFW-4 measurements before trusting it; `OPT` selectable; the backed-up 1.0 hex is the rollback. |
| `bus_*_fast` differs from `HAL_GPIO_Init` in some pin detail. | Used only in the polling loop; `FLASH_POLL=0` removes it; checked on hardware in step 4. |
| Polling flags healthy writes as failures (a chip variant that reports DQ7 differently). | Bounded, not endless; `WRITE TIMEOUT` stops the write instead of corrupting; `FLASH_POLL=0` is the fallback; the app's read-back verify is independent of it. |
| Polling makes writes slower. | Measured in plan step 4; budget is under 2x the legacy write time. |
| Unconfirmed chip IDs (`C2C9`, `207E`) match a different chip. | Marked in code and in the app; a wrong match can only name a chip, never change commands. |
| Firmware and app drift apart. | `Software/tests/fakefirmware.cpp` mirrors `main.c`; the new lines are covered by app tests. |
