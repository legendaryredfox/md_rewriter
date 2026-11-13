# Bench tests for when the hardware arrives

`tools/hwtest/hwtest.py` (Python 3, stdlib only) drives `mdrewriter-cli`. **It is read only.** It never writes or erases: every device command passes through a guard that allows only `ports chips rominfo compare info detect read-rom read-save verify` and refuses `--yes`. The cartridge in the slot is never changed. (`read-save` writes the cartridge's RAM-enable register, as every dump does, not its contents.)

Build the CLI first: `tools/build.sh app`.

| Phase | Setup | Command |
|-------|-------|---------|
| `host` | no hardware | `tools/hwtest/hwtest.py host` (guard test, CLI runs) |
| `nocart` | dumper plugged in, slot empty | `tools/hwtest/hwtest.py nocart` |
| `cart` | dumper plugged in, any cartridge | `tools/hwtest/hwtest.py cart --rom-size 4M [--save-size 8K\|32K] [--label "name"]` |

`--expect-fw 1.1` (any phase with a device) fails unless the banner version starts with it; `--rom-size 8M` is skipped with a reason on firmware 1.0. The SWD session that installs 1.1 is in `docs/SWD_SESSION.md`.

Checks, `nocart`: dumper enumerates as `0483:5740`; connect and banner; 10 reconnects; detect answers and does not name a chip; two 512K reads of the empty slot are identical; fraction of `0xFF` (pull-ups).

Checks, `cart`: the same link checks; detect (ID, chip name, whether the firmware knows it); two ROM reads identical, size, `SEGA` header, header name; header ROM size, mirror (a read larger than the cartridge repeats it) and header checksum (information, not a failure); 512K read equals the prefix of the big read; `verify` against the dump just taken; number of distinct 32 KiB blocks of the real image (a stuck address line gives repeats); two save reads identical; read speed.

Results: `test-results/<time>-<phase>/report.md` and `report.json`, plus the dumps (folder is git-ignored). Exit code 0 only when nothing failed. Keep the `report.md` files: they are the evidence for the checklist in `docs/NEW_FIRMWARE_PLAN.md` step 5 (rows 3, 8, 9).

What it does not do on purpose: writing, erasing, timing with a logic analyzer (plan step 3), flashing the firmware (plan steps 2 and 4). Those are manual, on a sacrificial cartridge.

## Bench results

### 2026-11-13, first unit, factory firmware 1.0, slot empty, USB only

Unit: STM32F103VB, firmware 1.0 as shipped (banner `1.0`, no `FWVER:` line). Host: Linux, `ttyACM0`, `mdrewriter-cli` 1.2.0. Nothing was written, erased or flashed.

| Check | Result |
|-------|--------|
| USB enumeration | `0483:5740`, "STM32 Virtual ComPort", bcdDevice 2.00, two interfaces (CDC control and CDC data), no DFU interface |
| Connect and banner | pass, firmware `1.0` |
| 10 reconnects in a row | pass, slowest 0.5 s |
| `detect`, slot empty | `FLASHID:FFFF`, not reported as a chip, warning text correct |
| 512K read of the empty slot, twice | identical, SHA-256 `043e238a765f7cfbc62596a50e53c8ffb6b188a99357b0ebede251725d67589f`, all bytes `0xFF` (pull-ups work on D0-D15) |
| Read time, 512K | 3.1 s |

Not covered, needs a cartridge: header, address-line check, 4M read, save RAM, write, erase, chip IDs marked "not confirmed" in `ChipDb`.

Findings:

1. **Connect right after `detect` fails on firmware 1.0.** `detect` then `read-rom` or `info` as two processes in a row gave `no answer to connect` after the 3 s connect timeout. The next invocation worked. A wait of 0.3 s between the processes was enough; the failure needs the next process to open the port at once. Reproduced several times. Cause not known (firmware 1.0 source was not instrumented); firmware 1.1 has the same receive path, so it may be there too. Fixes: `mdrewriter-cli` retries the connect once (test `connectRetriesOnceWhenFirstConnectIsIgnored`, fake firmware option `ignoreConnects`); `hwtest.py` waits 0.3 s between CLI calls. Verified on the unit: `detect` followed at once by `read-rom`, 3 times, all read 512K with the same hash. The GUI is not affected in normal use (the connect and the next command are seconds apart); not tested.
2. **The firmware cannot be updated over USB.** The unit enumerates as a plain CDC device. There is no DFU interface, and the STM32F103 ROM bootloader has no USB DFU (it listens on USART1 only, entered with BOOT0 high). Firmware 1.0 has no "enter bootloader" command. So firmware 1.1 has to go in through SWD (ST-Link V2, see the README), as `docs/NEW_FIRMWARE_PLAN.md` already says. A USB update (plan D1) needs a bootloader flashed once through SWD, and that was not done. Not checked: whether the unit has read-out protection set (needs an SWD session).
3. `hwtest.py nocart` passes 11 of 11 rows with the 0.3 s pause. Without it, the first run failed on the first read (finding 1).

### 2026-11-13, same unit, firmware 1.0, original cartridge: Rolling Thunder 2 (Namco, T-14133-00, 1991)

Mask-ROM game cartridge, no battery. **Read only.** The owner ruled out any write or erase on this cartridge; the suite has no path to one, and no `write-*`, `erase` or `--yes` command was run. Checklist row 6 of `docs/NEW_FIRMWARE_PLAN.md` (write to a mask-ROM cartridge) must use another cartridge.

`hwtest.py cart --rom-size 1M` and `--rom-size 2M`: every PASS row passed.

| Check | Result |
|-------|--------|
| `detect` | `FLASHID:00`, no chip named, firmware does not know the chip: correct for a mask ROM (the app text says reading works with original cartridges) |
| Two reads in a row, 1M and 2M | identical each time |
| 1M read | 5.3 s, 190 KiB/s, SHA-256 `f1f35b18755d36571b8e3e879a93fd999c4387d6b5641cf00b11a0aae66debbb` |
| 2M read | 10 s, 205 KiB/s, SHA-256 `c7f0929bbb7c7f125f95fae7d0234008aacc82b242b8ee5f29164f6d6a5b8df5` |
| 512K read against the prefix of the 1M and the 2M read | equal; the 1M read equals both halves of the 2M read |
| `verify` against the dump just taken | pass |
| Header | `SEGA MEGA DRIVE`, `(C)T-14 1991.AUG`, `ROLLING THUNDER 2`, `GM T-14133 -00`, ROM size 0x000000-0x0FFFFF (1 MiB), no RAM |
| Data bits D0-D15 | each bit is 1 in 30 to 39 percent of the words: none stuck |
| Address lines A18, A19 | no repeated blocks. The image has 31 distinct 32 KiB blocks of 32; the one repeat is two all-zero blocks (padding) |
| Content | data ends at 0xFC9CF, 0xFF to the end; one zero run of 79 139 bytes (0x1CADE to 0x30000) between two assets, other shorter zero runs end on 0x4000 or 0x8000 boundaries: looks like padding, not a fault |

Findings:

1. **A read larger than the cartridge is a mirror.** The 2M read is the 1 MiB image twice (the upper address lines are not decoded). The old suite called this "31 of 64 distinct blocks" and left it to the reader. `hwtest.py` now compares the mirror with the header ROM size (`mirror matches the header ROM size`), counts blocks on the real image, and reports uniform blocks. The analysis is `analyze_rom()`, checked on synthetic images in the `host` phase (9 rows now). The app has no cartridge size detection: the user picks the size, and a read bigger than the cartridge repeats it.
2. **The header checksum does not match.** Header word 0xBEB6, sum of words 0x200 to 0xFFFFF is 0x11B1 (the app's `rominfo` and an independent script agree). Two identical reads, healthy data bits and no address aliasing speak against a read fault, but a deterministic fault on a single line cannot be ruled out without a reference. **Open:** compare with a known-good dump of the same revision (`mdrewriter-cli compare`, or a No-Intro hash). Nothing in the app was changed for this; its warning text ("hacks and translations often have a wrong checksum") is written for the other case, and a read-only original can reach it too.
3. Not covered by this cartridge: save RAM (none), flash IDs, write and erase, 4M and 8M reads, SSF2 mapper.

### 2026-11-13, same unit, firmware 1.0, flash cartridge: S29GL064N90TFI03 (PCB Ver1.4, battery, SRAM and FRAM)

Cartridge supplied for writing; its old content was not backed up (the owner said it did not matter). The owner authorised erasing and writing **this** cartridge and the Metal Gear image `Metal Gear (World) (v1.2) (Aftermarket) (Unl) [fixed].bin` (2 097 152 bytes, the `.gen` next to it is byte-identical; neither is committed). Only the first 2 MiB of the chip was erased and written.

Read only, before the write (`hwtest.py cart --rom-size 4M --save-size 32K`): all PASS. `FLASHID:17E`, named S29GL064N by the app. Two 4M reads identical, 214 KiB/s, two 32K save reads identical.

Write, with the tools as they were:

| Step | Result |
|------|--------|
| `erase 2M --yes` | 8 s; a 2M read after it is all `0xFF` |
| `write-rom` (first try) | **failed**: `The device did not confirm block 1 of 2048` after the 120 s timeout. Chip untouched |
| raw serial, one block alone | `ADD:0x0 WRITE OK` |
| raw serial, `detect` then a block at once / after 0.1 s | no answer / `WRITE OK` |
| `write-rom` (after the 250 ms hold) | all 2048 blocks confirmed in 57 s, then `Verification failed: ... 1070253 bytes differ` |
| read back, analysed | 503 441 of 1 029 582 programmable words right, 545 135 still `0xFFFF`, **0 wrong**; the right ones alternate word by word. Two read backs identical |
| `tools/repair-write-fw10.py` | 8 passes: words left 545 135, 296 070, 155 621, 75 740, 25 732, 5 668, 1 133, 21, 0 (the loop was the same logic, run from a scratch script) |
| final dump, twice | both SHA-256 `0c52b1e269bcd97b92102739336791385a06dfdcea0371d205390821197dc646`, equal to the file; `mdrewriter-cli compare`: 0 differences; `cmp`: identical; `verify`: identical |
| `hwtest.py cart --rom-size 2M --save-size 32K` after the write | all PASS; the game header checksum 0xEE3C matches the data (header ROM size 1 825 280 bytes) |

Findings:

1. **The firmware drops bytes sent right after `detect`.** `HAL_Delay(100)` and then `cmdclear()` run after the answer is out. The first write block is lost and the app waits for an acknowledgement that never comes. This is also the cause of finding 1 of the first bench run (a connect right after `detect`). Fix in the app: the session holds the next command 250 ms after `detect` finishes (`kDetectSettleMs`; `tst_session` `nothingStartsInTheSettleWindowAfterDetect`, `tst_cli` `writeRightAfterDetectSurvivesDeafFirmware`, with the fake firmware option `deafAfterDetectMs`). The test fails with the hold at 0 ms. The CLI's one connect retry stays. The firmware fix is written up as NFW-19, **not applied**.
2. **Firmware 1.0 cannot program the S29GL064N in one pass.** It does not wait for the chip, and after each programmed word the next one is lost. No word is ever wrong, only blank. Firmware 1.1 polls the chip (NFW-10, NFW-12) and should write in one pass; **not flashed, so not tested on this cartridge**. Workaround for 1.0: `tools/repair-write-fw10.py`.
3. The first CLI error text blames the cartridge ("may not have been erased first, may not be a flash cartridge, or the chip may be too slow for the firmware"). For this chip the last reason was the right one. The text was left as it is.
4. The chip ID `17E` is now seen on hardware (table comment in `Software/chipdb.cpp`). The second ID cycle is still not read by the firmware, so the other GL-N chips with the same low byte are not excluded.
5. Not covered: write of the save RAM, 4M and 8M writes, the rest of the chip beyond 2 MiB, erase by the `all` command, firmware 1.1.
