# md_rewriter: cartridge support matrix

Goal (owner, 2026-11-04): in the future the project supports **every cartridge type handled by the reference repositories** in `references/repos/` (MD_Dumper, FlashKit-MD-Plus, flashkit-md-py, fkmd, MD-Flash-Cart-4MB, open-ed) and by the KY FlashKit family (`docs/KY_SUPPORT.md`). This file lists what exists and what is missing. Update it whenever a row changes.

Firmware lines: **1.1** runs on the current PCB (V1.0), folder `firmware_1.1/`. There is no 1.2: no revised PCB exists (the placeholder folder was removed on 2026-11-13).

States: **Yes** = in the code and checked on hardware. **Model** = in the code, passes the host tests against a chip model, **never run on a chip** (today this is everything except the 1.0 chip). **Planned** = not in the code. **HW** = needs a hardware change first.

## 1. Flash chips (rewriting)

| Chip | ID (mfr dev) | Size | Command set | Seen in | 1.1 | Missing / note |
|------|--------------|------|-------------|---------|-----|----------------|
| MX29LV640EB | C2 CB | 64M | AMD 555h | 1.0 firmware, MD_Dumper | Yes (1.0 chip) | none |
| MX29LV640ET / MX29LV640 | C2 C9 | 64M | AMD 555h | MD_Dumper | Model | layout unknown |
| M29W640DB / DT | 20 DF / DE | 64M | AMD 555h | ST datasheet | Model | |
| M29W640G | 20 7E | 64M | AMD 555h | | Model | layout unknown |
| M29W640D (listed as `20ED`) | 20 ED | 64M | AMD 555h | MD_Dumper `flashlist.csv` | Planned | ID differs from the DB/DT rows above: check on a chip |
| S29GL064N | 01 7E | 64M | AMD 555h, write buffer | MD_Dumper, user cart (PCB Ver1.4) | Model (word program); write buffer is off by default | |
| SST39VF6401B / 6402B | BF 6D / 6C | 64M | AMD 555h, `30h` = 32 KWord block | datasheet | Model | |
| SST38VF6401 | BF 6B | 64M | | MD_Dumper | Planned | other sector commands |
| M29W320DT/DB, M29W320ET/EB | 20 CA/CB/56/57 | 32M | AMD 555h | KY carts, MD_Dumper | Model | top boot: last 64 KiB erased as eight sectors |
| MX29LV320T / B | C2 A7 / A8 | 32M | AMD 555h | FlashKit-MD-Plus, MD-Flash-Cart-4MB | Model | |
| MX29L3211 | C2 F9 | 32M | AMD, unlock 5555h/2AAAh | FlashKit-MD-Plus, MD-Flash-Cart-4MB | Model | no page program (128 words): slower; layout unknown |
| SST39VF3201 / 3202 | BF 5B / 5A | 32M | AMD 555h but `50h` for the block | MD_Dumper | Model | firmware sends `50h` per 32 KWord block (ported from oscr_minimal D86) |
| SST36VF3204 | BF 53 | 32M | | MD_Dumper | Planned | |
| SST39SF040 | BF B7 | 4M | 5 V part | MD_Dumper | Planned | 5 V chip, byte-wide: check the cart wiring |
| Intel PA28F400 T/B | 89 70 / 71 | 4M | Intel/Sharp | FlashKit-MD-Plus (code cracker) | Model | block map and status bits from memory |
| Sharp LH28F320BJE | B0 E3 | 32M | Intel/Sharp family | MD_Dumper | Planned | same family as the Intel set; block map differs. Looked up 2026-11-10: 8 blocks of 4 KWord plus 63 of 32 KWord, two-cycle block erase; the `20h`/`D0h` codes and the top/bottom position for `B0E3` were not confirmed. Not implemented on purpose |
| Toshiba TC58FVT160F | 98 C2 | 16M | AMD-like | MD_Dumper | Planned | top boot, JEDEC-compatible commands per the datasheet summary; ID and unlock addresses not confirmed (2026-11-10). Not implemented on purpose |
| MX26L6420 | C2 FC | 64M (8 MByte) | | FlashKit-MD-Plus, MD_Dumper | Planned | cart `/CE` above 4 MiB, bank commands |
| Any CFI chip | | | detected by CFI query | flashkit-md-py | Model (size only) | an unknown chip that answers `QRY` is reported with its size and command set (NFW-16); it is not written with the CFI data, the chip stays on AMD 555h. Block map and unlock addresses still come from the table |

## 2. Cartridge types and mappers (reading and rewriting)

| Type | Seen in | 1.1 | Missing |
|------|---------|-----|---------|
| Classic ROM up to 4 MiB (mask ROM and FlashKit style) | all | Model (reads 512K..4M; unchanged from 1.0 read path) | hardware check |
| ROM 8 MiB (whole 64 Mbit flash, `/CE0` held) | | Model (dump code 05) | hardware check |
| Sega Virtua Racing (SVP) | MD_Dumper `gameslist.csv` | Planned | probably reads as a classic 2 MiB ROM: verify |
| SSF2 mapper (`A130F3h`..`A130FFh`, 512 KB windows) | MD_Dumper | Model (read only) | `mdrewriter-cli read-rom --mapper ssf2 --size 5M` (firmware command `3A`, NFW-17); never run on a cartridge, the `/TIME` + `/LWR` cycle is new on this PCB; no GUI yet |
| Extended SSF2 (Titan Overdrive 2, Demons of Asterborg) | MD_Dumper | Model (read only) | same command, up to 64 pages (32 MiB); the page register is assumed to be 6 bits wide, not checked against these cartridges |
| Sega Lock-on (Sonic & Knuckles) | MD_Dumper | Planned | |
| WaterMelon T-5740 / T-5741 (Pier Solar, Paprium) | MD_Dumper | Planned | mapper and serial save |
| Open EverDrive (`A130E0h` init, CTRL_REG `A13000h`, 4 MiB + menu in an 8 MiB flash) | open-ed, FlashKit-MD-Plus | Planned | write `0404h` to `A130E0h`; the programmer needs `/VRES` handling (open-ed README) |
| EverDrive-MD v2 mapper | FlashKit-MD-Plus | Planned | |
| Magistr-16 | FlashKit-MD-Plus | Planned | |
| MegaWiFi carts (mw-prog) | gitlab doragasu | Planned | not cloned; separate programmer design |
| Master System / Mark III, Game Gear | MD_Dumper | Planned, HW | needs a pin adapter (different connector and bus) |
| KY FlashKit MD cart (`MD PCB Ver 2.0`) | KY | Model for the chips above | chip under the ST logo unknown |

## 3. Save memory

| Type | Seen in | 1.1 | Missing |
|------|---------|-----|---------|
| Parallel SRAM 8 KB / 32 KB (enable via `A13000h`) | 1.0, FlashKit | Yes (1.0 code, unchanged) | |
| Larger parallel SRAM, bankswitch SRAM | MD_Dumper | Planned | |
| Serial I2C EEPROM (24Cxx) | MD_Dumper `-backup ... serial_i2c` | Planned, HW | `/UWR`, `/CAS2` not wired (D3, D5) |
| Serial SPI memory | MD_Dumper, WaterMelon | Planned, HW | |
| Write RAM on FlashKit MD cart | manual says unsupported by the cart | not applicable | |

## 4. Order of work

1. Everything marked Model: run it on hardware (`docs/NEW_FIRMWARE_PLAN.md` step 5), mark chip IDs confirmed.
2. CFI query: size and command set reported for unknown chips (done in the model 2026-11-10, NFW-16). Using the CFI block map to erase and write unknown chips is not done: needs a chip and the unlock address question (555h or 5555h) answered.
3. Missing flash chips (section 1, Planned), cheapest first: SST `50h` block erase, Sharp/Intel family, Toshiba.
4. Mappers that only read: SSF2 and Extended SSF2 done in the model 2026-11-10 (NFW-17, CLI only). Lock-on and the SVP check are still open.
5. Open EverDrive (`A130E0h`), EverDrive v2, Magistr-16, MX26L6420.
6. Needs the revised PCB (firmware 1.2): I2C/SPI saves, Master System / Game Gear adapter.
