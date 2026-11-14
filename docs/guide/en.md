# MD Rewriter: step-by-step guide, from the box to your first game

**Languages:** [English](en.md) · [Português (Brasil)](pt-BR.md) · [Русский](ru.md) · [简体中文](zh-Hans.md) · [हिन्दी](hi.md) · [اردو](ur.md)

For people who just received the MD Rewriter board and know little about electronics. Everything here was done on a real unit on 2026-11-13; the results are in section 9. If something does not match what you see, stop and read section 10 before you go on.

**Short version.** The board works out of the box with the factory firmware 1.0 (no soldering needed). Firmware 1.1 is optional: it writes flash cartridges correctly in one pass and names more chips, but installing it needs a small header soldered to the board, an ST-Link V2 programmer and about an hour. **Flash cartridges of the KY / FlashKit family hold at most 2 MiB of game (section 8).**

---

## 0. Safety rules

1. **Never write to, erase, or "test" an original game cartridge** (the ones sold with games). They are mask ROMs: they cannot be written, and the attempt can only do harm. The program reads them safely; that is all you do with them.
2. **Only one power source at a time.** Either the USB cable of the board, or the 3.3 V wire of the ST-Link, never both.
3. **Cartridges are inserted and removed only with the USB cable unplugged.**
4. **Make a backup before you erase or overwrite anything** (a flash cartridge that holds a game, and the firmware of the board).
5. A soldering iron burns (about 350 °C). Work in a ventilated place, wash your hands after soldering (solder can contain lead), and let the iron cool before you put it away.
6. Touch a grounded metal object before you handle the board, and do not carry it by the cartridge slot.

---

## 1. What you need

| Item | For what | Needed? |
|------|----------|---------|
| The MD Rewriter board (V1.0) | the device itself | yes |
| A USB cable **with data wires**, USB-A to USB-C preferred (see 3.2) | connects the board to the computer | yes |
| A computer with Linux (tested) or Windows | runs the program | yes |
| A flash cartridge (S29GL064N, KY / FlashKit type) | to write games | to write |
| A Mega Drive game cartridge | to back it up | to read |
| ST-Link V2 programmer (original or the common USB "dongle" clone) | install firmware 1.1 | optional |
| 1×4 pin header, 2.54 mm | solder it to the SWD pads | with the ST-Link |
| 4 Dupont jumper wires (female, to connect the ST-Link to the header) | wiring | with the ST-Link |
| Soldering iron 25 to 40 W with a fine tip, solder 0.6 to 0.8 mm with flux core | soldering the header | with the ST-Link |
| Desoldering wick, flux pen, isopropyl alcohol, a small brush | cleaning and fixing joints | helps a lot |
| Multimeter with a continuity buzzer | checks of the joints and the pads | with the ST-Link |
| Tape or a "third hand" holder | keeps the board still | helps |

---

## 2. Know your board

![Top of the board](../images/board-top.png)

| What you see | What it is |
|--------------|------------|
| Two rows of 32 holes at the top, "CART FRONT" and an arrow | the **cartridge slot**. The arrow shows the side of the cartridge that faces front: the label of the cartridge looks the same way as the arrow |
| USB-C connector at the bottom right | connects to the computer and powers the board |
| Button marked RESET | restarts the microcontroller (not needed for normal use) |
| LEDs POWER and ACT | POWER is on when the board has power; ACT is the activity LED |
| Chip U1 (the big square one) | the microcontroller (STM32F103) |
| Four round holes in a column, right of U1 and above the USB-C connector, under the capacitor C10 | the **SWD pads**, used only to install firmware. No labels on the board (section 3.1) |
| Four holes in the corners | M3 mounting holes. Use spacers or rubber feet so the bottom of the board does not touch the table |

![Bottom of the board](../images/board-bottom.png)

---

## 3. Soldering

You solder only if you will install firmware 1.1 (section 6). If you keep the factory firmware, skip to section 4.

### 3.1 The SWD header (4 pins)

The four pads, from top to bottom (USB-C connector at the bottom of the board):

```
   ●  3V3
   ●  SWCLK
   ●  SWDIO
   ●  GND
 [USB-C]
```

**Before soldering.** Unplug everything. Confirm the pads with the multimeter (the board is not powered):

1. Buzzer mode: one probe on the bottom pad (GND), the other on the metal shell of the USB-C connector. It must beep. If it does not, you have the wrong pad or the board is upside down.
2. Later, with the USB cable plugged in, the top pad shows 3.3 V against GND.

**Soldering, step by step:**

1. Fix the board on the table with tape, top side up. Use the top-side photo above to find the four holes.
2. Push the 1×4 header into the holes from the top, short pins down, plastic resting on the board. The four pins must be straight and flat.
3. Turn the board over. Heat the iron to about 330 to 350 °C and clean the tip on a damp sponge.
4. Touch the tip to a pin and the pad at the same time (a second), then feed solder to the joint (not to the tip) for 1 to 2 seconds, remove the solder, then the iron. Do one pin only first.
5. Look at it: the joint must be shiny and shaped like a small volcano, covering the pad and the pin. If it is dull, grey, a ball, or leaves a gap, heat it again with a little fresh solder, or remove it with the wick and redo it.
6. Check that the header is still straight and flat. Fix it by reheating the first joint while you push it. Then solder the other three pins (about 3 seconds each, never longer: too much heat lifts the pads).
7. Clean the board with isopropyl alcohol and a brush. Inspect the four joints with a magnifier or the phone camera: no solder bridges between neighbours, no bare gaps.
8. **Multimeter check, board unplugged:**
   - GND pad: buzzer to the USB-C shell (as before).
   - Between each pair of neighbour pins (3V3 to SWCLK, SWCLK to SWDIO, SWDIO to GND): **no** beep. A beep means a solder bridge: remove it with the wick.
   - Move each pin with your fingernail: none can wobble. **A loose GND pin cost us hours on the first unit** (the connection to the ST-Link dropped every few minutes and it looked like a software fault). If anything wobbles, reheat that pin.

### 3.2 USB-C: the cable

On board V1.0 the USB-C connector has no 5.1 kΩ resistors on its CC lines (finding H1 of `docs/HARDWARE_REVIEW.md`). A USB-C to USB-C cable plugged into a charger or a recent laptop may then give **no power**. Use a **USB-A to USB-C cable with data wires**; it always works. Advanced users can fix it by soldering two 5.1 kΩ 0603 resistors from the A5 and B5 pads of the connector to GND; if you do not know how to solder 0603 parts, use the A-to-C cable instead.

---

## 4. Assembly and disassembly

### 4.1 Assembly (everything unplugged)

For the **normal use** (no firmware work): plug the USB cable into the board and the computer, then insert the cartridge (4.3).

For **installing firmware** (section 6): connect the ST-Link first, with the USB cable of the board **unplugged** and the cartridge slot **empty**.

1. Put the ST-Link USB dongle into a USB port of the computer only after the wires are connected.
2. Connect the Dupont wires, always by the name printed on the ST-Link, never by colour or position (clone models differ):

| Header pin (top to bottom) | Net | ST-Link wire |
|----------------------------|-----|--------------|
| 1 | 3V3 | `3.3V` (see the power note below) |
| 2 | SWCLK | `SWCLK` |
| 3 | SWDIO | `SWDIO` (some clones print `SWIO`) |
| 4 | GND | `GND` |
| none | NRST | leave `RST` loose: the board has no NRST pad |

The ST-Link `5V` pin stays unconnected.

**Power note.** The preferred way, which was stable on the bench: wires `GND`, `SWCLK` and `SWDIO` only, **without** the `3.3V` wire, and the board powered by its own USB cable. If you cannot plug the board to the computer, use the `3.3V` wire instead, with the USB cable unplugged and the cartridge slot empty; the clone's 3.3 V output is weak, so keep the tests short.

3. Look once more at the four connections. Press each Dupont connector with a finger: a loose one is the most common problem.

### 4.2 Disassembly after the work

1. Close the program. 2. Unplug the USB cable of the board. 3. Wait a few seconds for the LEDs to go out. 4. Remove the cartridge (4.3). 5. Pull the Dupont connectors off the header **by the plastic, straight out**, never by the wire. 6. Unplug the ST-Link from the computer.

The header can stay on the board; it does no harm. If you want it off: heat one pin, pull it out with pliers; or cut the plastic with side cutters and remove the pins one by one; clean the holes with the wick. Never heat a pad for more than a few seconds: it lifts. Keep the board in an anti-static bag or a box, away from metal.

### 4.3 Inserting and removing a cartridge

- With the **USB cable unplugged**.
- Hold the cartridge by the plastic with the **label facing the arrow marked "CART FRONT"** on the board. Push it straight down into the slot until it stops, evenly, without rocking or twisting.
- To remove it, pull it straight up, again with the USB unplugged. Do not pull by the board or by the USB cable.
- Do not hot-plug cartridges while the board has power: the cartridge and the board share the same lines and can be damaged.

---

## 5. Install the program

Follow "Installing the application" in the README (Linux: a `.deb` or a tar.gz from the releases page, or build from source; Windows: build from source). On Linux, give your user access to the serial port: `sudo usermod -aG dialout $USER` and log in again, or install the udev rule `Software/linux/99-md-rewriter.rules`.

Check that the program talks to the board: plug the board in (no cartridge), run the program, press **Refresh**, then **Connect**. The log shows `Device identified.`. On the command line:

```
mdrewriter-cli ports        # the board appears as 0483:5740
mdrewriter-cli info         # prints the firmware version
```

If it says "permission denied", fix the serial permission (above). If the board does not appear, use another cable (one with data wires).

---

## 6. Install firmware 1.1 (optional, about one hour)

Why: with firmware 1.0 a write to the S29GL064N flash cartridge leaves half of the words blank (it programs every other word) and needs eight repair passes. Firmware 1.1 writes and verifies a game in one pass and names the chip. Firmware 1.1 is the same program with the programming wait added; it was tested on the first unit (section 9).

You need the ST-Link V2, the soldered header (section 3) and the programs `st-info` and `st-flash` (Linux: `sudo apt install stlink-tools`). The helper script `tools/swd/swd.sh` needs Linux or macOS with `bash`. On Windows use the same commands by hand (6.5).

### 6.1 Wire it

Section 4.1: wires `GND`, `SWCLK`, `SWDIO`, USB cable of the board plugged in, cartridge slot empty.

### 6.2 Check the connection

```
tools/swd/swd.sh probe
```

Expected: `swd: probe ok (0x414, 524288 bytes)` (the units we have use an STM32F103xE with 512 KiB; `0x410` and 131072 bytes also pass). `chip id 0x0` or "no ST-Link found" means a bad connection: check **GND at the header first**, then SWCLK and SWDIO (swap them if the connection never works), then the cable. The script retries a dropped connection by itself.

### 6.3 Back up the factory firmware

```
tools/swd/swd.sh backup --out ~/md_rewriter_backups/fw-factory
```

It reads the chip twice, compares the two reads and writes `fw-factory.bin` with a `.sha256` file. **If it says the read is blank, stop**: read protection is on and the factory firmware cannot be saved; do not erase or flash. Copy the backup to a second place (another disk or a USB stick).

### 6.4 Write firmware 1.1

Download `md-rewriter-firmware-1.1.0.bin` (and its `.sha256`) from the releases page of this repository, check it with `sha256sum -c`, and:

```
tools/swd/swd.sh flash md-rewriter-firmware-1.1.0.bin --backup ~/md_rewriter_backups/fw-factory/fw-factory.bin --yes
```

The script checks that the chip still holds the backed up firmware, writes, reads back and compares. It ends with `written and verified`. Then **unplug and plug the board USB cable again** (the soft reset does not always make the board enumerate on USB). Test: `mdrewriter-cli info` must print `firmware 1.1.0`.

**To go back to the factory firmware:** `tools/swd/swd.sh restore --backup ~/md_rewriter_backups/fw-factory/fw-factory.bin --yes`.

### 6.5 Windows (by hand)

Install the ST-Link tools (the `stlink` release with `st-info.exe` and `st-flash.exe`, or STM32CubeProgrammer). The equivalent commands: `st-info --probe`; `st-flash read backup.bin 0x08000000 0x80000` (use `0x20000` if `st-info` says 128 KiB); `st-flash write md-rewriter-firmware-1.1.0.bin 0x08000000`; read it back and compare the first bytes with the file. Keep the backup safe.

---

## 7. Test your unit, step by step

Every step can be done in the graphical program (GUI) or on the command line (CLI); results are the same. In the CLI examples the port is found automatically; if you have more than one serial device add `--port /dev/ttyACM0` (Linux) or `--port COM3` (Windows). The button names are in English (the program has English and Portuguese).

Do the tests in this order. If a step fails, stop and read section 10.

| # | Test | GUI | CLI | What you should see |
|---|------|-----|-----|---------------------|
| 1 | Version (no cartridge) | **Connect** | `mdrewriter-cli info` | `Device identified.` and firmware `1.1.0` (or `1.0` with the factory firmware) |
| 2 | Empty slot | **Detect cartridge** | `mdrewriter-cli detect` | no flash chip found; a read of the empty slot gives only `0xFF` |
| 3 | Original game, read only | insert the cartridge, **Detect cartridge**, **Cartridge size** 2 MiB (or the size of the game), **Read ROM…** | `mdrewriter-cli read-rom --size 2M --out game1.bin` | the dump is made (about 12.5 s per 2 MiB). For a mask ROM `detect` shows flash id `0000` or "unknown chip": this is **not** a flash cartridge, never write to it |
| 4 | Read twice, compare | read the same cartridge again to `game2.bin`, **Compare files…** | `mdrewriter-cli compare game1.bin game2.bin` | no differences. Two different results mean a bad contact: clean the cartridge pins, try again |
| 5 | Flash cartridge: detect | insert the flash cartridge, **Detect cartridge** | `mdrewriter-cli detect` | `S29GL064N ... Supported`, and a line `Chip codes 017E1001 (or 017E1000), CFI sector words ...` |
| 6 | **Back up what is on the flash cartridge** | **Read ROM…** 2 MiB, and **Read save…** | `read-rom --size 2M --out old.bin`, `read-save --size 32K --out old.srm` | the two files exist. Keep them if the cartridge holds something you want |
| 7 | Erase 2 MiB | **Erase by size…** with **Cartridge size** 2 MiB, confirm | `mdrewriter-cli erase 2M --yes` | `Erase complete.` in 6 to 9 seconds; a new read gives only `0xFF` |
| 8 | Write a ROM (up to 2 MiB) | **Write ROM…**, choose a `.bin`/`.md`/`.gen`/`.smd`, leave **Verify after writing** on, confirm | `mdrewriter-cli write-rom game.bin --yes` | `Game written`, then `Verified: the … bytes read back match the file.` About 90 s for 2 MiB |
| 9 | Verify again | **Read ROM…** and **Compare files…** with the original | `mdrewriter-cli verify game.bin` | identical |
| 10 | Save data | **Read save…** twice, compare | `read-save --size 32K --out s1.srm`, again `s2.srm`, `compare s1.srm s2.srm` | identical files |

**Do not do** the erase and write steps on a cartridge that is not a flash cartridge, on a cartridge that holds a game you do not have a copy of, or while anything else is plugged in.

### The test suite we used (optional, read only)

The bench suite reads the board and the cartridge and **never writes or erases** (it has no `--yes`). It needs Python 3:

```
python3 tools/hwtest/hwtest.py nocart --port ttyACM0 --expect-fw 1.1          # slot empty
python3 tools/hwtest/hwtest.py cart   --port ttyACM0 --expect-fw 1.1 --rom-size 2M   # with a cartridge
```

It prints PASS / FAIL / INFO / SKIP per check and saves a report in `test-results/`. Use `--rom-size 8M` to also read 8 MiB (on the flash cartridges the second 4 MiB repeats the first: normal, section 8).

---

## 8. The 2 MiB limit of flash cartridges

**The flash cartridges of the KY / FlashKit family hold at most 2 MiB of game, whatever the size printed on the chip (the S29GL064N is 8 MiB).** We tried two of them (a KY cartridge and a second S29GL064N cartridge) and both behave the same:

- Erasing or writing above 2 MiB fails: `erase 4M` stops with `A sector did not finish erasing in time` after about 23 seconds, and a game larger than 2 MiB cannot be written.
- A read above 2 MiB shows the **save RAM** (every other byte `0xFF`) and not flash: the cartridge board selects the flash only for the lower 2 MiB and the RAM for the upper 2 MiB. Reading 8 MiB repeats the same 4 MiB twice.

This is how the cartridge board is built (its address decoder, `docs/HWTEST.md`), not a fault of the MD Rewriter, of the firmware, or of the program. No setting changes it; we tried the mapper registers of the original programmer.

**What fits:** almost every Mega Drive game (most are 512 KiB to 2 MiB; Metal Gear, 2 MiB, was written and verified on both cartridges). **What does not fit:** the games larger than 2 MiB, for example Super Street Fighter II (4 MiB, with a bank mapper). For those you need a different cartridge: a plain 4 MiB flash cartridge without that decoder (for example the open design `MD-Flash-Cart-4MB`, MX29LV320 or MX29L3211) or a cartridge with a bank mapper.

---

## 9. What was tested on the first unit

First unit, 2026-11-13. Firmware 1.1.0-test (the same program as 1.1.0, only the version text differs).

| Check | Result |
|-------|--------|
| Board | STM32F103xE microcontroller, 512 KiB of flash (the design files say a 128 KiB part); firmware 1.1 fits |
| Factory firmware backup | read twice, identical, no read protection. (The `restore` command is tested on the PC only, with simulated tools; it was not run on the unit) |
| Firmware 1.1 flashed over SWD | written and verified; the board enumerates as `0483:5740` and talks to the program |
| Empty slot | two reads identical, only `0xFF` |
| Original cartridge (Rolling Thunder 2), read only | 2 MiB read twice in 12.4 s, identical, same as firmware 1.0 gave. Never written |
| Flash cartridge S29GL064N, firmware 1.0 | writes only every other word: needs repair passes |
| Flash cartridge S29GL064N and a KY cartridge, firmware 1.1 | named `S29GL064N`; erase 2 MiB in 6 to 9 s; the 2 MiB Metal Gear written and verified in **one pass** (about 90 s with the read back); the dump equals the file |
| The written cartridges on a real console | the owner tested the cartridges written with this firmware on a real Mega Drive (reported 2026-11-13) |
| Write over data without erasing | `WRITE TIMEOUT` in 0.4 s, the board keeps answering: the safe failure |
| The two flash cartridges have different sector layouts under the same chip ID | the firmware therefore always erases in small steps, which works on both |
| Writing or erasing above 2 MiB | not possible on these two cartridges (section 8) |

Not tested yet: the SSF2 bank mapper read, a write over a power cut, the write buffer build, and flash cartridges of other brands.

---

## 10. Troubleshooting

| Symptom | What to do |
|---------|------------|
| The board does not appear on the computer | Use a USB-A to USB-C cable with data wires. Try another USB port. The POWER LED must be on |
| "permission denied" (Linux) | `sudo usermod -aG dialout $USER`, log in again, or install the udev rule |
| `probe` says `chip id 0x0` or finds nothing | Check **GND at the header** first (a loose pin is the usual cause), then SWCLK and SWDIO, then press the Dupont connectors. The script retries by itself |
| `swd.sh` says the chip does not hold the backed up firmware | Another unit, or a changed one. Make a new backup first; never flash without a backup |
| After flashing the board does not show on USB | Unplug and plug the board USB cable again |
| `detect` says unknown chip | Not a flash cartridge (an original one), or a chip not in the table: do not write |
| "did not finish erasing" | The cartridge is not flash, is damaged, or you asked for more than 2 MiB (section 8) |
| `WRITE TIMEOUT` | The area was not erased first (erase, then write), or the chip is damaged |
| Verification fails | Clean the cartridge contacts, use another USB cable, repeat the erase and the write |
| Two reads of the same cartridge differ | Bad contact: clean the pins with isopropyl alcohol, insert it again |
| A written game does not start on the console | Check that the file is a valid ROM (the program warns about header problems), that it is at most 2 MiB, and that the verification passed |

If you find something not covered here, open an issue with the log of the program and the output of `mdrewriter-cli detect`.
