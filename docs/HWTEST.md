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

### SWD session 1 (2026-11-13, ST-Link V2 clone, board powered from the ST-Link 3V3, slot empty)

| Step | Result |
|------|--------|
| wiring | pad order from the README confirmed by the owner for 3V3 (top, 3.3 V with USB) and GND (bottom, continuity with the USB-C shell); SWCLK and SWDIO connected as documented and the ST-Link found the chip |
| `swd.sh probe` | `chipid 0x414`, `F1xx_HD`, 512 KiB flash, 64 KiB SRAM. Flash size register (`0x1FFFF7E0`) = `0x0200` (512 KB), DBGMCU_IDCODE `0x10036414`. **The chip on this unit is an STM32F103xE (high density), not the F103VB of the design files.** Firmware 1.1 is linked for 128 KiB flash and 20 KiB RAM and fits in the larger part. `swd.sh` now accepts both (0x410 with 128 KiB, 0x414 with 512 KiB) and backs up the whole flash |
| `swd.sh backup` | two reads identical, not blank (no read protection), 524 288 bytes, about 18 177 non-0xFF bytes, initial SP `0x20003C30`, reset vector `0x08000105`, SHA-256 `7202283475c58c85b0cc85da1ce40f37bfeca97057207c72595e9b9e7dbc42a1`. Copy kept outside the repository |
| `swd.sh flash build-poll` (1.1.0-test, 29 572 bytes, SHA-256 `7f7f6606…bd42`) | written, read back identical, reset via AIRCR (no NRST pad) |
| USB, `hwtest.py nocart --expect-fw 1.1` (slot empty) | 8 PASS, 0 FAIL: enumerates `0483:5740`, banner `1.1.0-test`, 10 reconnects (max 0.11 s), `detect` `flashId=FFFF` (no chip), two 512K reads of the empty slot identical in 3.5 s, all `0xFF`, same SHA-256 `043e238a…589f` as with 1.0. **Firmware 1.1 runs on the F103xE and talks to the app** |

The factory image is 18 KiB, so the first unit's 1.0 firmware was built for this part or a small subset of it; the size of the `legacy_firmware` build was not compared.

Firmware 1.1.0-test, Rolling Thunder 2 (mask ROM, read only; never written, per the owner's rule), same session:

| Step | Result |
|------|--------|
| `hwtest.py cart --rom-size 2M --save-size 32K --expect-fw 1.1` | all PASS. `detect` `flashId=0000` (not a flash chip; 1.0 printed `00`). 2M read twice, 12.4 s and 12.3 s (166 KiB/s), identical, SHA-256 `c7f0929b…8df5`: **the same dump as firmware 1.0 gave**. Mirror of the 1 MiB image, header checksum mismatch as before (0xBEB6 vs 0x11B1, still needs a reference dump), 512K read equals the first 512K, two 32K save reads identical (`663179ea…`) |
| `hwtest.py cart --rom-size 8M --save-size 8K --expect-fw 1.1` (checklist 8) | all PASS. 8 388 608 bytes twice, 48.3 s and 48.2 s (170 KiB/s), identical, SHA-256 `a0d62447…9adf`. It is the 1 MiB image repeated eight times; the first MiB has SHA-256 `f1f35b18…debbb`, equal to the 1M dump taken with 1.0. So the 8 MiB path of 1.1 works and the app's 8 MiB option is justified |
| `mdrewriter-cli detect` | `flash id: 0000  chip: unknown` and the warning that this may be an original cartridge |

Not run on this cartridge on purpose: any write or erase (owner's rule), and the SSF2 mapper read (it writes bank registers to the cartridge bus; the owner said not to write to this cartridge).

Firmware 1.1.0-test (polling build), S29GL064N cartridge with Metal Gear, same session. The owner gave the go-ahead for erase and write on this cartridge; the detect said `FLASHID:017E` before every destructive step. Files in `test-results/fw11-write/` (git-ignored).

| Step | Result |
|------|--------|
| 2M read before | SHA-256 `0c52b1e2…c646`, the Metal Gear image left by the 1.0 session |
| `erase 2M --yes` | 6.3 s (1.0: 8 s). 2M read after: all `0xFF` |
| `write-rom` Metal Gear, verify on (checklist 3a, test d) | **one pass**: `Game written: 2048 blocks`, `Verified: the 2097152 bytes read back match the file`. 90.5 s with the verify read (12 s). No repair tool. With 1.0 the same write left 545 135 words blank and needed 8 repair passes |
| two independent 2M dumps, `cmp` with the file | both SHA-256 `0c52b1e2…c646`, `cmp` identical |
| `write-rom` of the same file again without erasing (`--no-verify`) | completes in the same way: identical data needs no 0 to 1 change, so there is nothing to time out on. Not a test of checklist 5 |
| `write-rom` of the first 512K inverted bitwise, no erase, `--no-verify` (checklist 5) | `The cartridge did not finish programming a word ("ADD:0x3 WRITE TIMEOUT")` after 0.4 s, exit 1. The device answered `info` right after: no hang |
| erase 2M and write Metal Gear again, no verify | 78.9 s for the write alone (1.0: 57 s, but it skipped every other word; the 1.0 control build with the 1.0 sequence was not run on the cartridge, so the "under 2x the control" criterion of plan step 4 is met against 1.0's 57 s: 1.4x). `verify` against the file: identical; 2M dump SHA-256 `0c52b1e2…c646` again |
| `hwtest.py cart --rom-size 8M --save-size 32K --expect-fw 1.1` | all PASS except one check that was too strict (see below). `detect` names `S29GL064N` itself (`firmwareKnows=True`). 8M read twice 48.6 s each, identical; first 2 MiB SHA-256 `0c52b1e2…c646`; header checksum 0xEE3C matches; two 32K save reads identical |

Findings:

1. **Firmware 1.1 with polling programs the S29GL064N correctly in one pass.** This was the main open question of the plan. 78.9 s for 2 MiB, 6.3 s to erase 2M.
2. **Checklist 5 is proven with different data, not with the same data.** Writing identical data over written flash is not an error and does not time out.
3. **The 8 MiB read of this cartridge repeats a 4 MiB image** (second half equals the first). Either the cartridge does not decode the top address line (`A22`) or something on the path does not. The chip is 64 Mbit (8 MiB); the mask-ROM cartridge cannot tell the two apart. Open: check with a logic analyzer or a second S29GL064N cartridge. It does not affect games up to 4 MiB; the SSF2 mapper and 8 MiB images are not covered.
4. **`hwtest.py` check "mirror matches the header ROM size" failed on this cartridge and was wrong.** A flash cartridge holds more than the game (here 4 MiB of image, the game is 1 825 280 bytes: old data beyond what was erased). The check now fails only when the mirror period is shorter than the game's header size, and reports a longer one as INFO.
5. Not run in this session: build `-DFLASH_POLL=0` (control) and `-DFLASH_FAST=1` on the cartridge (both need a new SWD flash with the ST-Link 3V3 wire back on and USB off), `write-save` on the flash cartridge (it has no battery RAM, and on an 8 MiB flash the save window may overlap flash space: a write there would be bus cycles into the chip; not worth the risk), unplug during a write (checklist 10, needs a hand on the cable), checklists 1, 2 and 6, NFW-19, 4M and 8M writes.

Firmware 1.1.0-test, third cartridge: the owner's "KY technology" cartridge, same session. Files in `test-results/ky/` and `test-results/20261113-153524-cart/` (git-ignored); the 8M dump of the original content is also in `~/md_rewriter_backups/ky-cart-ragnacenty-8M-20261113.bin` (SHA-256 `f27ad242…43a9`). The owner asked for the Metal Gear to be written to it.

| Step | Result |
|------|--------|
| `detect` | `FLASHID:017E`, `S29GL064N MD FLASH CART`, the **same answer as the first S29GL064N cartridge**. No KY / FlashKit ID (`20xx`, `C2xx`) appeared, so the KY-specific rows of the plan (3d, 4) are not covered by this cartridge |
| `hwtest.py cart --rom-size 8M --save-size 32K` before the write | all PASS (after the check fix below). Game `RAGNACENTY`, header ROM size 2 097 152 bytes, header checksum 0x123E matches. 8M read twice, identical, 48.6 s, SHA-256 `f27ad242…43a9`; again a 4 MiB image repeated twice; 32K save reads identical |
| `erase 2M --yes` | 7.5 s, "Erase complete", **but 56 544 bytes were still programmed**: sectors of 8 KiB at bytes `0x2000` to `0xFFFF` (the first 8 KiB were blank) |
| `write-rom` Metal Gear | `WRITE TIMEOUT` (`ADD:0x1003`) after 0.8 s, exit 1: the first 8 KiB had been programmed, then the first word needing 0 to 1 in an unerased sector. Correct behaviour of 1.1; the cause was the erase |
| diagnosis | the erase left exactly the 7 small sectors behind the first one: the chip has eight 8 KiB boot sectors at the bottom, the firmware believed it had none (NFW-20). The first S29GL064N cartridge does not have them |
| workaround, no new firmware | raw serial `0x2E` (erase sector, word address): sectors `0x1000` to `0x7000` (words), then sector `0x0` again (it had been programmed by the failed write). Each `SECTORADD:0x… ERASE OK!`. 2M read: all `0xFF` |
| `write-rom` Metal Gear, verify on | `Game written: 2048 blocks`, `Verified`, 93.2 s with the verify read. **One pass again** |
| `hwtest.py cart --rom-size 2M` after | all PASS: two 2M reads identical, SHA-256 `0c52b1e2…c646` (the file), header checksum 0xEE3C matches, `verify` identical |

Findings:

1. **Two cartridges with the same autoselect answer (`01 7E`, name `S29GL064N`) have different erase layouts.** The codes in the datasheet table cannot be trusted to choose the erase steps. Fix NFW-20: layout unknown for this chip, which gives the 1.0 behaviour (8 KiB steps). The host model now has chips that lie about their layout and the 4 MiB erase test fails without the fix. Cycles 3 and 4 of the autoselect are not visible to the app, so the real codes of this cartridge are still unknown; showing them in `detect` would settle which rule was wrong.
2. **The fix is only in the source.** The unit still runs the build without it (`FWVER:1.1.0-test`). Rebuilt images: polling `79d9369a…cb6aa` (29 520 B), control `facc44d3…30fc5` (29 508 B). Flashing needs the ST-Link wire again and the owner's go-ahead; the rows to repeat afterwards are listed in `docs/SWD_SESSION.md`. The cost on uniform chips is extra time on every erase (the first 64 KiB sector is erased up to eight times; on 1.0 an erase of 2M took 8 s).
3. **The 8M read repeats a 4 MiB image on both flash cartridges** (this one and the first one): the second half equals the first. With two different flash cartridges doing it, a cartridge fault is less likely than before and a dumper or firmware limit on the top address line more likely (`A22`, `PA5`?). Still open; it needs a logic analyzer or a look at the address pins of `U1` and the cartridge slot.
4. The pre-write dump shows this cartridge held a complete game (`RAGNACENTY`, checksum good) in its first 2 MiB; it was overwritten by the owner's request. The dump above can restore it: erase 4M with the sector workaround and `write-rom` the first 2 MiB (`head -c 2097152`).

### Final firmware 1.1.0-test (2026-11-13, evening): uniform cartridge, CFI, 2 MiB erase limit, flash of the final build

Cartridge "previous Metal Gear" (the first S29GL064N one, codes `017E1001`) and the KY cartridge (`017E1000`), firmware polling build before the last change (30 172 B, layout taken from the CFI words).

| Step | Result |
|------|--------|
| one raw `0x2E` at word `0x1000` on the `017E1001` cartridge | the whole first 64 KiB went blank: **a truly uniform first sector** (the KY one needs eight erases for the same range) |
| CFI sector words (`2C..34h`) on both | `02070020007F000001` on both cartridges, although the layouts differ. The CFI table does not tell them apart. The four autoselect codes differ on these two cartridges (`017E1001` and `017E1000`), but the firmware's NFW-10 rule read the KY codes as a uniform chip and was wrong, and one sample of each is too little to trust a rule: neither source is safe |
| firmware that trusted the CFI layout | erase 2M 8.5 s, all `0xFF`, write and verify 90.8 s, Metal Gear verified. Functionally the same as always stepping by 8 KiB, so the final build does that (NFW-20) and shows the codes (NFW-21) |
| `erase 4M --yes`, both cartridges | `A sector did not finish erasing in time` after about 23 s. Raw `0x2E` at word `0xF8000` is fine; at `0x100000`, `0x108000`, `0x180000`, `0x1F8000` (byte 2 MiB and up) it gives `ERASE TIMEOUT!`. Reads above 2 MiB work. Cause unknown: the cartridge decoding of the write cycle, or the dumper (`A21`/`A22`, `PA4`..`PA6`?). Same limit as the 8M read repeating 4 MiB: the upper address lines may not reach the chip on this path. Needs a logic analyzer or the schematic |
| 4M write after the failed erase | one bit read wrong once (byte `0xAAB`, `0x04` read as `0x00`); the verify caught it and two clean 1 MiB repeats followed. Not reproduced |

Final build flashed over SWD (`swd.sh flash build-poll`, 30 116 B, SHA-256 `12be6c8f968516c10d8134227f4c50add2d853c8f6641d0f5f1efee67a1347d6`, `FWVER:1.1.0-test`, always 8 KiB steps below 64 KiB plus `CHIPINFO`). Backup of the image it replaced: SHA-256 `38399e3f822866338741fc71c9ecf0deff7285541b85393308a829a1ae666a1e` (`test-results/swd-baseline-cfi-layout/`, copy in `~/md_rewriter_backups/`). Written and read back identical, reset by AIRCR.

The SWD link problem of this session: the link dropped every few minutes (`chipid 0x000`, `Failed to connect`), also with the board powered by USB and only SWCLK, SWDIO and GND on the ST-Link. The cause was **the GND pin loose in the header**. Until then the link worked for a short time after reseating. `swd.sh` now retries the probe, the read and the write when the link drops (`SWD_TRIES`, `SWD_RETRY_SLEEP`); the read back stays mandatory.

With the final build, owner's go-ahead for each cartridge, files in `test-results/20261113-164*-cart/` (git-ignored):

| Cartridge | `detect` | erase 2M | `write-rom` Metal Gear + verify | dump |
|-----------|----------|----------|---------------------------------|------|
| previous Metal Gear (`017E1001`) | `S29GL064N`, info line `Chip codes 017E1001, CFI sector words 02070020007E000001 …` | ok (cartridge was blank before) | `Game written: 2048 blocks`, `Verified`, 91.2 s total, one pass | verified equal to the file |
| KY (`017E1000`) | same name, `Chip codes 017E1000, CFI sector words 02070020007E000001 …` | 7.7 s | `Verified`, 93.3 s total, one pass | `hwtest.py cart --rom-size 2M` before the erase: all PASS, SHA-256 `0c52b1e2…c646`; after the write `verify` equal to the file |

So **both cartridge types are detected and written with the same firmware and no choice by the user**, which was the requirement. The CFI words changed from `…7F…` to `…7E…` between two sessions on both cartridges (power state or temperature of the chip?): one more reason not to read anything into them.

`hwtest.py` fix found here: a blank (all `0xFF`) 2M read was reported as "repeats a 512 KiB image"; an erased image now never counts as a mirror.

Still open after this session: erase and write above 2 MiB on the flash cartridges, the 8M read repeating 4 MiB, header checksum of Rolling Thunder 2 (needs a reference dump), checklist 1, 2, 3b (write buffer build), 6 (mask ROM write, sacrificial cartridge), 10 (unplug during a write), NFW-19 (not applied), `write-save` and `read-save` on the flash cartridges, `FLASH_POLL=0` control build.

### Why nothing works above 2 MiB: the reference designs (2026-11-13, evening; read from `references/`, **not yet tested on hardware**)

Sources: the original FlashKit cartridge PCB (`references/ky_technology/flashkit/pcb/cart-md-v1.2.pcb`, P-CAD netlist), its programmer source (`flashkit-md/flashkit-md/Cart.cs`), the dumper firmware (`enableSram_MD`) and a dump of the KY cartridge taken before it was overwritten (`~/md_rewriter_backups/ky-cart-ragnacenty-8M-20261113.bin`).

| Fact | Where |
|------|-------|
| On the FlashKit cartridge the flash `CE` comes from a 74HC139: `CE = !( !CE_LO and !A20 )`. The flash is selected only while the bus line named `A20` (the 2 MiB line) is low; with it high the same decoder selects the SRAM (`RAM_CE`). The flash chip's own `A21` pin is tied to GND, and the connector lines `a21` and `a22` go nowhere | `cart-md-v1.2.pcb` nets `CE`, `RAM_CE`, `A20`, `A21`, `A22`, `GND` |
| The programmer writes `0xA13000 = 0xFFFF` to put SRAM at `0x200000` and `0x0000` to put ROM there, and reads the upper 2 MiB as ROM only after writing `0x0000`. It only counts "extra ROM" when the bytes at `0x200000` change with that register | `Cart.cs`: `getRomSize`, `ramAvailable`, `checkRomSize` |
| The dumper's `enableSram_MD(1/0)` pulses the cartridge `RESET`, puts D0 on the bus and strobes `/TIME`: the same register. It is called around `read-save` and `write-save`, never before a ROM read or erase | `firmware_1.1/Core/Src/MD.c`, `main.c` |
| The upper 2 MiB of the KY dump (bytes `0x200000` to `0x3FFFFF`) is **not flash**: every other byte is `0xFF` and the odd bytes spell `RAGNACENTY` (`ff 00 ff 52 ff 41 ff 47 ff 4e`...). It is the 8-bit save RAM on D0-D7, repeated over the window, holding the game's save | the dump above |

What this explains, if it holds on the owner's cartridges:

1. **Erase and write at or above 2 MiB time out because the commands go to the SRAM**: the flash is not selected there, so the sector erase never reaches it (`ERASE TIMEOUT!` on every sector from `0x200000`; sectors below work). Reads above 2 MiB "worked" because they read the SRAM.
2. **The 8M read repeats 4 MiB because the cartridge has no `A22` to the flash** (the connector line is open on the reference design): 4 MiB is the most these cartridges can show.
3. The flash chip is a 64 Mbit part used as 2 MiB of ROM (plus RAM); the cartridge hardware, not the firmware, sets that limit unless the register maps ROM over the RAM window. With `0xA13000 = 0` the upper 2 MiB may come from the flash (the programmer's "extra ROM" case, 4 MiB total).

The test, all read only (nothing is erased or written to flash; one register write like `read-save` already does): read 4M, run `read-save`, read 4M again. If the upper half changes from the `FF 00` SRAM pattern to `0xFF` (blank flash) or to real data, the register switches the window and the dumper must write `0xA13000` before any access above 2 MiB (detect the flash chip first; never on a mask ROM cartridge). Then erase 4M and a 4 MiB write become testable. If it does not change, the SRAM is not switchable on these boards and 2 MiB is the limit, which the app should say.

**Result of that test (KY cartridge, firmware 1.1.0-test final build, same evening):** `read-rom 4M`, `read-save 32K`, `read-rom 4M`: the lower 2 MiB identical, the upper 2 MiB **unchanged** by the `read-save` (still `ff 30 ff 00 ff 52 ff 41 ff 47 ff 4e`..., the SRAM pattern; 1 242 016 bytes of `0xFF`, 196 864 of `0x00`). So `enableSram_MD(0)` does **not** switch the window: it strobes `/TIME` with D0 and does not drive the address bus to `$A13000` (it also pulses the cartridge `RESET`), unlike the programmer, which writes the word at `$A13000`. Whether a write at `$A13000` switches it is therefore still open. (The first byte of the window read `00` in the dump taken before the Metal Gear write and `30` now: it really is RAM.)

Next experiment, needs a firmware change and an SWD flash (owner's go-ahead): a protocol command that does one mapper register write cycle at a given `$A130xx` address and value, using the existing `mapper_write()` (the SSF2 command `3A` already uses it for `$A130F3`), tried with `$A13000 = 0` on the KY cartridge, then `read-rom 4M`. Only on a cartridge where the flash chip answers `detect`, never on a mask ROM. `docs/KY_SUPPORT.md` open question 2 (does the KY cartridge use the stock `A13000h` mapper) stays open until then.

**Result of the register experiment (KY cartridge, firmware with the experimental command `3B`, removed again afterwards (NFW-22), same evening, SWD flash of a 30 324 B build, SHA-256 `8e6d9e3d…7188`; backup of the image it replaced, SHA-256 `4fcd4b75…618e`: `test-results/swd-baseline-final-1.1.0-test/` and `~/md_rewriter_backups/`).** One mapper write cycle (`/TIME` and `/LWR` low, `/CE0` high, D0 to D7) at `$A13000`, value `0x00`, sent after a `detect` that showed `FLASHID:017E`; then `read-rom 4M` and compare with the read before. Tried also `$A130F1 = 0x00`, `$A130F1 = 0x01` and `$A13000 = 0x01`. In every case the upper 2 MiB stayed the SRAM pattern (`ff 00 ff 52 ff 41 ff 47 ff 4e`..., 1 242 016 bytes of `0xFF`). The only change was the first RAM byte, `0x30` to `0x00` (32 mirrored bytes, offset 1 of each 64 KiB), which is what a write of the value `0x00` into the RAM would do (the RAM was written by earlier erase and write attempts above 2 MiB). **So on the KY cartridge none of these register writes maps the flash over the RAM window. The 2 MiB limit stands for this cartridge**, unless the cartridge needs a word write with `/UWR` as well (see the netlist below) or a register address nobody has documented.

Dumper side, from the EasyEDA project in `Hardware/PCB` (a SQLite file; decoded with a few lines of Python, no tool needed): `A17` to `A23` go from `U1` pins 23, 24, 25, 26, 29, 30 and 31 (`PA0` to `PA6`) to the slot (`P2` pins 49, 6, 7, 8, 9, 10, 11). **`A21`, `A22` and `A23` reach the slot**, so the missing `A22` of the 8M read is on the cartridge. `/LWR` (`P2` 28) is driven from `U1` pin 53, `/TIME` (31) from pin 51, `/ASEL` (26) from pin 91, `/CE0` (17) from pin 92. **`/UWR` (`P2` 29) and `CAS2` (21) are not connected to the microcontroller at all** (the finding L2 of `docs/HARDWARE_REVIEW.md`): the dumper never drives `/UWR`. The flash programming works anyway (the S29GL064N takes its 16 data bits on D0-D15 and its `/WE` from `/LWR`), and so do the mapper writes of the SSF2 command; a cartridge or register that wants `/UWR` low would never see a valid write from this dumper.

**Conclusion after re-reading the reference repositories (same evening).** The programmer sources agree with the netlist: `references/repos/fkmd/cart/cart.go` and `Cart.cs` (`getRomSize`) limit the ROM to **2 MiB whenever the cartridge has RAM and the `$A13000` write does not change what the window at `0x200000` shows** (`max_rom_size = ram != 0 && extra_rom == 0 ? 0x200000 : 0x400000`), and `cart-md-v1.2.pcb` has **no register chip at all**: only the 74HC139, so the flash `CE` is `!( !CE_LO and !A20 )` and the upper 2 MiB can never reach the flash, with or without a RAM chip fitted. The KY cartridge (and the first S29GL064N cartridge, which behaves the same way) are therefore **2 MiB ROM + 32 KiB save RAM cartridges by board design**, whatever the size of the flash chip. The failed `0xA13000`/`0xA130F1` writes agree (nothing to switch). For games above 2 MiB a different cartridge is needed: the simple 4 MiB cartridge of `references/repos/MD-Flash-Cart-4MB` (no decoder, flash `CE` straight from `/CE`, MX29L3211 or MX29LV320, compatible with the FlashKit programmer, CERN OHL), or a cartridge with a bank mapper (Open EverDrive, SSF2-type). A board with the S29GL064N wired for 8 MiB would also need `A22` on the flash.

What the software can do about it: when the detected chip is a flash cartridge, a write larger than 2 MiB should say that the cartridges of this family cannot hold more, instead of failing with `ERASE TIMEOUT!` after 23 s. The KY/FlashKit cartridge is recognisable by the SRAM window (the 4 MiB read shows the SRAM pattern above 2 MiB). Not done yet.

**Written cartridges on a real console.** The owner reported on 2026-11-13 that the cartridges written during these sessions were tested on a real Mega Drive. The details (which of the two cartridges, and what was seen) were not recorded; this line only records the report. The Metal Gear image on both flash cartridges is the file whose SHA-256 is `0c52b1e2…c646`, verified byte for byte against the dump.

### Release firmware 1.1.0 (2026-11-13, 17:28)

The release build (`FW_VERSION=1.1.0`, 30 112 bytes, SHA-256 `9149da46ebe86c119e4bf3fdcedb9dd25def95111f44da201f8007201a641004`, `md-rewriter-firmware-1.1.0.bin`) is not the binary that was tested before (`1.1.0-test`, 30 116 bytes): the version string is shorter and the rest of the image moves. It was flashed over SWD (written and read back identical; backup of the experimental image it replaced, SHA-256 `db88a984…85ba`, in `test-results/swd-baseline-3b-build/` and `~/md_rewriter_backups/`) and the regression was run on both flash cartridges with the owner's go-ahead.

| Cartridge | `info` | `detect` | `hwtest.py cart --rom-size 2M --expect-fw 1.1` | erase 2M | `write-rom` Metal Gear + verify | `verify` |
|-----------|--------|----------|------------------------------------------------|----------|-------------------------------|----------|
| KY (`017E1000`) | `firmware 1.1.0` | `S29GL064N`, `Chip codes 017E1000 …` | all PASS before the erase, dump SHA-256 `0c52b1e2…c646` | 7.9 s | `Verified`, 1:46 in total | identical, exit 0 |
| first S29GL064N (`017E1001`) | `firmware 1.1.0` | `S29GL064N`, `Chip codes 017E1001 …` | all PASS before the erase, same SHA-256 | 9.0 s | `Verified`, 1:43 in total | identical, exit 0 |

The write takes a little longer than the 90 s of the earlier runs; no other difference. App 1.3.0 and firmware 1.1.0 are released together (GitHub release 1.3.0).
