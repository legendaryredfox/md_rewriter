# SWD session: back up firmware 1.0, flash firmware 1.1, test it

Written before the ST-Link is installed. Everything marked "done" was run on the PC without hardware; the rest is the order to follow on the bench. Background: `docs/NEW_FIRMWARE_PLAN.md` steps 2 to 5, `docs/HWTEST.md` (what the unit did with firmware 1.0), `README.md` ("Ligar o ST-Link V2", the pad and pin map).

Nothing in this file runs by itself. `tools/swd/swd.sh flash` and `restore` write to the microcontroller and need `--yes`; every cartridge write or erase in step 6 needs the owner's go-ahead at that moment.

## What the unit already told us (firmware 1.0, 2026-11-13)

- Enumerates as plain CDC `0483:5740`. No USB update path: SWD is the only way in.
- Writes to the S29GL064N cartridge only every other word (no polling). Firmware 1.1 polls, so one pass should do. This is the main thing to prove.
- Drops bytes sent within 100 ms of the `detect` answer (NFW-19, not applied; the app holds the next command 250 ms). 1.1 has the same code, so the same hold applies. The hold is only on the app side: any other host tool that talks to the dumper hits it.
- Reference values to compare against after flashing: `FLASHID:17E` from 1.0 (1.1 prints `017E` and names the chip itself); the Metal Gear image on the S29GL064N cartridge has SHA-256 `0c52b1e269bcd97b92102739336791385a06dfdcea0371d205390821197dc646` for its first 2 MiB, as long as nobody rewrites it.

## Done before the session (PC only)

| What | Result |
|------|--------|
| `tools/swd/test-swd.sh` | 54 checks, 0 failed (includes the 512 KiB chip and the dropping link). Stubs for `st-info` and `st-flash`: probe (no ST-Link, wrong chip id, wrong flash size), backup (equal to the chip, `.sha256`, refuses to overwrite, blank read = read protection, failed read), flash refuses without `--yes`, without a backup, with a missing, unsigned or modified backup, with a tiny image, an image without vector table, a wrong chip, a chip that is not the backed-up unit; detects a read back mismatch; flash, flash again (no-op), restore |
| `tools/swd/swd.sh probe` with no ST-Link plugged | `Found 0 stlink programmers`, exit 1, message names the likely causes. `st-flash` and `st-info` are 1.8.0 |
| `tools/swd/swd.sh build` | two images in `firmware_1.1/build-poll` (`FWVER:1.1.0-test`, 29 572 bytes) and `firmware_1.1/build-nopoll` (`FWVER:1.1.0-nopoll`, `-DFLASH_POLL=0`, the 1.0 write sequence, 29 560 bytes). Initial SP `0x20005000`, reset vector `0x08006A29`. Both fit in the 128 KiB |
| `firmware_1.1/check.sh` | 0 gcc warnings, 0 cppcheck findings, host model 207 and 212 checks, 0 failed |
| `hwtest.py` | new `--expect-fw 1.1` (fails on 1.0, as it should today) and `--rom-size 8M` (skipped with a reason on 1.0) |

What the PC cannot show: whether the real S29GL064N accepts the 1.1 timing, whether `main.c` (not in the host model) behaves, whether the chip is read protected.

## Bring

ST-Link V2 and cable, the soldered header, multimeter, the S29GL064N cartridge with Metal Gear, a USB cable for the dumper. Optional: a logic analyzer (plan step 3) and a cartridge with a mask ROM that may be written (checklist row 6). The Rolling Thunder 2 cartridge must not be written (owner's rule).

## Steps

### 1. Wiring (no power)

Pads and pin map: `README.md`, "Ligar o ST-Link V2" (one vertical row of four pads under C10, top to bottom `3V3`, `SWCLK`, `SWDIO`, `GND`, USB-C at the bottom; the same order seen from the back). Silkscreen has no labels (`docs/HARDWARE_REVIEW.md` M4): find each pad with the multimeter, continuity to `U1` pins 72 (SWDIO, PA13) and 76 (SWCLK, PA14) and to ground. Multimeter check done by the owner on 2026-11-13: the bottom pad has continuity with the USB-C shell (GND) and the top pad reads 3.3 V with USB plugged in (3V3). So, top to bottom:

| Pad | Net | ST-Link V2 wire |
|-----|-----|-----------------|
| 1 (top) | 3V3 | 3.3V |
| 2 | SWCLK (PA14) | SWCLK |
| 3 | SWDIO (PA13) | SWDIO |
| 4 (bottom) | GND | GND |

Two ways to power the board. **Preferred: USB**, with only SWCLK, SWDIO and GND on the ST-Link (3V3 and RST loose): the board has its own supply, the ST-Link only carries signals, and the dumper stays visible on USB. The ST-Link 3V3 wire may be used instead when the board cannot be plugged to USB: then **cartridge slot empty**, no USB cable while that wire is on, because the clone's 3V3 pin is weak. Only one power source at a time. Pads 2 and 3 are inferred from the project file; if `probe` finds nothing, swap them first.

If the link drops now and then (`chipid 0x000`, `Failed to connect`): check **GND at the header first**. On the first unit a loose GND pin caused hours of intermittent links on 2026-11-13; reseating the other wires only helped for minutes. `swd.sh` retries dropped links (`SWD_TRIES`, default 6, `SWD_RETRY_SLEEP`, default 1 s) and still demands the read back to match.

### 2. Probe

```
tools/swd/swd.sh probe
```

Expect `swd: probe ok (0x410, 131072 bytes)`. Stops on no ST-Link, another chip id, or another flash size. If it finds nothing: swap `SWCLK` and `SWDIO` first, then check ground and the 3V3 wire (the board is powered by it).

### 3. Back up firmware 1.0

```
tools/swd/swd.sh backup
```

Reads the flash twice, compares the two reads, refuses a blank read (all `0x00` or `0xFF`: read protection) and a read with no sane vector table, and writes `test-results/swd-<time>/fw-factory.bin` with a `.sha256` file. The folder is git-ignored: **copy the file somewhere else too.** Note the sha256 in `docs/HWTEST.md`.

If it says the read is blank: **stop.** Do not erase, do not flash. The 1.0 binary then cannot be saved. The source is in `legacy_firmware/` and `firmware_1.1/` builds from the same sources, but the owner decides whether to go on without a copy of the factory image (turning the protection off erases the chip).

Optional check against the source: `tools/check-firmware.sh` builds `legacy_firmware`; the used part of the backup should equal that build, except for what the factory changed.

### 4. Flash the control build (1.0 write sequence)

```
tools/swd/swd.sh flash firmware_1.1/build-nopoll/md-rewriter-new.bin --backup test-results/swd-<time>/fw-factory.bin --yes
```

The script re-reads the chip, requires it to hold exactly the backed-up image, writes, reads back, compares, resets. Remove the ST-Link 3V3 wire, plug USB, then:

```
python3 tools/hwtest/hwtest.py nocart --port ttyACM0 --expect-fw 1.1
```

Expect `device says 1.1.0-nopoll` and the same results as the 1.0 run (read of the empty slot identical, all `0xFF`).

With the S29GL064N cartridge in: `hwtest.py cart --rom-size 2M --expect-fw 1.1`. The 2 MiB dump must have the SHA-256 above (the content on the cartridge did not change). Then read `--rom-size 8M` twice (checklist row 8): identical, first 2 MiB equal to the 2M read.

Write test with the control build (owner's go-ahead needed: it erases and writes the cartridge): erase 2M, `write-rom` the Metal Gear. Expected: the same half-written result as on 1.0 (verify fails, words blank, none wrong). That proves the control build behaves like 1.0 and that polling is the difference. Finish with `tools/repair-write-fw10.py` or skip this and go to step 5.

### 5. Flash the polling build

```
tools/swd/swd.sh flash firmware_1.1/build-poll/md-rewriter-new.bin --backup test-results/swd-<time>/fw-factory.bin --yes
```

(After step 4 the chip no longer holds the backed-up image, so the script refuses: step 4's image is the new baseline. Make a second backup with `swd.sh backup` and pass that one, or run `swd.sh restore` first. This is on purpose.)

Then with the cartridge, in this order (writes need the owner's go-ahead):

| # | Test | Expected |
|---|------|----------|
| a | `hwtest.py nocart --expect-fw 1.1`, then `cart --rom-size 2M --expect-fw 1.1` | firmware `1.1.0-test`; Metal Gear dump SHA-256 unchanged |
| b | `mdrewriter-cli detect` | `FLASHID:017E`, `S29GL064N MD FLASH CART` from the firmware itself; the CFI line (NFW-16) if the chip answers |
| c | `erase 2M --yes`, 2M read | all `0xFF`; time against 8 s on 1.0 |
| d | `write-rom` Metal Gear, verify on | **one pass, `verified: true`, no repair tool**; compare time with 57 s on 1.0; dump SHA-256 equal to the file |
| e | `write-rom` again without erasing | `WRITE TIMEOUT` at the first word that needs 0 to 1 (checklist row 5) |
| f | 8M read, twice | identical, 8 388 608 bytes (row 8) |
| g | optional: build `OPT="-O1 -DFLASH_FAST=1"` (write buffer), repeat c and d | verify passes; time it against d (row 3b) |
| h | `read-save` 32K twice, `write-save` then `read-save` | same bytes (row 9). The cartridge RAM content is not needed |
| i | write to a mask-ROM cartridge that may be written | `WRITE TIMEOUT` within seconds, no hang, cartridge untouched (row 6) |
| j | unplug the dumper during a write, reconnect | the app reports it, reconnect works (row 10) |

Rows 1 and 2 of the checklist (old app with 1.1, new app with 1.0) need the old app release and a `restore` of the factory image. Do the restore last.

### 6. Roll back

```
tools/swd/swd.sh restore --backup test-results/swd-<time>/fw-factory.bin --yes
```

Writes the factory image back, reads it back, compares, resets. If the board no longer answers on USB but SWD still sees it, restore works the same way. With no NRST pad the ST-Link cannot connect under reset. If the board ever stops answering on SWD, the way back is the ROM bootloader with BOOT0 held high (`docs/HARDWARE_ADDON.md` M5), which speaks UART only.

## Record afterwards

- `docs/HWTEST.md`: a new "Bench results" section: the backup sha256, probe output, each table row with PASS or FAIL, times, anything unexpected. Same style as the earlier sections.
- `docs/NEW_FIRMWARE_PLAN.md`: the checklist rows done; `docs/GLOBAL_PLAN.md`: one log row.
- Everything in `firmware_1.1` stays "untested on hardware" in the docs until the rows above are recorded.
- NFW-19 (clear the input buffer before the delay) can be tried in the same session as a separate build; it needs its own row and a way to prove it: `detect` followed at once by a write block, from the raw serial script in the HWTEST.md findings.
