# md_rewriter: KY Technology / FlashKit MD cartridge support

Status: analysis and first software step done 2026-11-04. **Nothing tested on hardware.**
Sources: `references/ky_technology/REPORT.md`, `RESEARCH.md` (read them first), the FlashKit C# source in `references/ky_technology/flashkit/`, and clones in `references/repos/` (FlashKit-MD-Plus, flashkit-md-py, fkmd, MD_Dumper, MD-Flash-Cart-4MB, open-ed). Not in git (`references` is ignored).

## 1. What "KY" is

KY Technology sells the Krikzz FlashKit MD programmer and cart (PCB marked `MD PCB Ver 2.0`). No source shows a KY-only protocol. "KY support" for this project therefore means: **write and read the flash chips and mapper arrangements found on FlashKit/KY carts, with our own dumper**. The KY programmer itself (FT245R + EPM240 CPLD) is a different device and is out of scope.

## 2. What the FlashKit cart needs from a programmer

Facts from `Cart.cs`, `Device.cs`, `Form1.cs` (stock v1.0.0.0) and `cart-md-v1.2.pcb`:

| Item | FlashKit | md_rewriter |
|------|----------|-------------|
| Flash command set | AMD `AAh@555h, 55h@2AAh`, word addresses, unlock bypass `20h`, program `A0h`, sector erase `30h` in 8 KiB steps | same sequence on the bus; no unlock bypass (one unlock per word, slower, same result) |
| Erase | 8 x `30h` at 4096-word steps per 64 KiB | one `30h` per sector, 8 KiB steps below 64 KiB on bottom-boot chips (NFW-10) |
| Status | RY wait in the CPLD | DQ7 polling (NFW-2) |
| ID | none in the stock app | autoselect, 4 bytes (NFW-10) |
| ROM size | alias detection up to 4 MiB | user picks 512K..4M (8M on firmware 1.1) |
| SRAM | `A13000h` bit writes enable RAM at `200000h`; "Write RAM" not supported by the cart | save read/write by firmware commands `1A`/`1B` |
| Open-ED image | writes `0404h` to `A130E0h` before flashing | **not supported** (D4) |

Conclusion: a plain FlashKit cart with an AMD 29W part works on the existing protocol. The old firmware printed `NO FIND NGP CARD` for every chip but one, which looks like "unsupported" although writing never depended on the ID.

## 3. Chip support matrix

IDs: low byte of manufacturer/device word, as the firmware reads them. "Cmd" = command set.

| Chip | ID | Bits | Cmd | State |
|------|----|------|-----|-------|
| MX29LV640EB/ET, M29W640D*/G, S29GL064N, SST39VF640x | see `flashid.c` | 64M | AMD 555h | supported (before 2026-11-04) |
| M29W320DT/DB, M29W320ET/EB | 20 CA/CB/56/57 | 32M | AMD 555h | **named now**, layout set (T = top boot: last 64 KiB is eight 8 KiB sectors, erased one by one; B = bottom boot), IDs not confirmed on a chip |
| MX29LV320T/B | C2 A7/A8 | 32M | AMD 555h | **named now**, same caveat |
| SST39VF3201/3202 | BF 5B/5A | 32M | AMD 555h, but `30h` erases a 2 KWord sector, the 32 KWord block erase is `50h` (as in the VF160 family) | **not listed**: erase-by-size would leave most of each block unerased. Needs a `50h` block erase in `erase_sector` |
| MX29L3211 | C2 F9 | 32M | AMD but unlock at **5555h/2AAAh**, page program 128 words, no bypass | **supported in the model (NFW-15)**: word program and sector/chip erase with 5555h/2AAAh. No page program (slower, same result). Layout unknown, so erase keeps the 1.0 8 KiB steps |
| Intel PA28F400 (B/T) | 89 / 4470 (T), 4471 (B) | 4M | Intel/Sharp: `FFh` reset, `90h` ID, `40h` program, `20h`+`D0h` block erase, `70h`/`50h` status | **supported in the model (NFW-15)**: program, block erase over the 16/8/8/96/128x3 KB map, chip erase as seven block erases. The block map and the status masks are from memory and the host model uses the same table, so the tests cannot catch a wrong map: check them in the Intel datasheet before the first erase |
| MX26L6420 (8 MByte) | | 64M | | needs `/CE` above 4 MiB, see hardware note |
| Magistr-16, EverDrive-MD v2 mapper, Open-ED `A130E0h` | | | mapper | not supported (K5) |

The app (`Software/chipdb.cpp`) and firmware (`flashid.c`) tables agree. A wrong ID can only name a chip wrongly: commands never depend on the name except the erase step layout (unknown layout keeps the 1.0 behaviour).

## 4. Hardware

No hardware change is inevitable for the matrix above.

- All FlashKit cart address/data/control lines used (`A1..A23`, `D0..D15`, `/CE0`, `/LWR`, `/CAS0`, `/TIME`) are wired on our PCB (`docs/HARDWARE_REVIEW.md`, pin map).
- `/UWR` is not wired (L2). FlashKit carts decode only `/LWR` for word writes, so no impact for them; carts that decode `/UWR` only need the L2 fix.
- 3.3 V flash on carts is powered by the cart (AMS1117 on the cart); the dumper drives 5 V levels from 3.3 V MCU pins (L3). Unchanged risk, same as 1.0.
- Reading above 4 MiB (MX26L6420, S29GL064N 8 MiB) depends on cart `/CE` decoding; `/CE0` is asserted by the MCU for the whole 8 MiB.
- Open-ED needs a 10 k pull-up on `/VRES` of the **programmer** (open-ed README). Our `/VRES` is MCU-driven (`PB15`): no hardware change, firmware must hold it as an input/high. Untested.

## 5. What was done in software

- 8 chips (AMD 555h) plus MX29L3211 and PA28F400T/B added to `firmware_1.1/Core/Src/flashid.c` and `Software/chipdb.cpp`.
- `flash_probe()` chooses the command set (AMD 555h, AMD 5555h, Intel); `MD.c` programs and erases with it (`docs/NEW_FIRMWARE_SPEC.md` NFW-14, NFW-15).
- The host chip model (`tests/host/host_hw.c`) has the three families; 171 checks (176 with `FLASH_FAST=1`).
- App: chips carry a command set; with firmware that lacks it the app says writing needs firmware 1.1 or newer.
- CLI (`Software/cli`) and read-only bench suite (`tools/hwtest`) for when the hardware arrives.

## 6. Still not done

- Write buffer / page program for MX29L3211 (128 words) and unlock bypass: speed only.
- Open-ED `A130E0h` init, Magistr-16, EverDrive-MD v2 mapper, MX26L6420 8 MByte: not started.
- Nothing here has touched a chip. The Intel and 5555h routines follow FlashKit-MD-Plus and datasheets as remembered: read the datasheet of the chip on the cartridge before the first write.

## 7. Open questions that need a real cart

1. Which chip is under the KY cart (photo shows an ST logo: likely M29W320 or M29W160).
2. Whether KY carts use the stock mapper (`A13000h` RAM enable) or a variant.
3. Boot-sector layout of the chip: decides the erase loop.
