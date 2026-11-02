# md_rewriter: hardware review (PCB V1.0, 2025-06)

Status: review only. **No hardware file was changed.** Findings are open until marked otherwise in section 4.
Reviewed on 2026-11-02 against the files in `Hardware/` and the firmware in `legacy_firmware/Core`.

## 1. Scope and method

Inputs: `Hardware/PCB/md2.PcbDoc-V1.0-20250619.eprj`, `Hardware/BOM/*.xlsx`, `Hardware/GerBer/*.zip`, `Hardware/patch diagram/*.csv` (pick-and-place), the two renders in `Hardware/*.png`, and the firmware.

Limits, so a later session knows what was **not** done:

- The project has **no schematic** (`schematics` table in the `.eprj` is empty). The netlist was read from the PCB (`PAD_NET` records), so schematic intent was not reviewed and no ERC was possible.
- No DRC was run. Track width, via size and drill data come from the file records and the Gerber drill files only.
- Nothing was tested on a board. LCSC/manufacturer part numbers were not checked against a catalogue.
- STM32F103VET6 LQFP100 pin names come from memory of the datasheet. The firmware (`MD.h`, `gpio.c`) agrees with the netlist for every pin it uses.

Board: 2 layers, 108.4 x 56.5 mm, GND pour on both faces, EasyEDA Pro 2.2.32.3 (the `.eprj` is a SQLite database; layout data is base64+gzip text in `documents.dataStr`).

## 2. Findings

Severity: **H** breaks a use case or the build data, **M** robustness/maintainability, **L** polish.

### High

| ID | Finding | Evidence | Proposed fix |
|----|---------|----------|--------------|
| H1 | USB-C has no Rd (5.1 k) on CC1/CC2, so a C-to-C cable or a USB-C-only charger will not supply VBUS. | `USB1.A5` and `USB1.B5` are in the unnamed (no-connect) net with the NC MCU pins. No 5.1 k part in the BOM. | Add 2x 5.1 k 0603, CC1 and CC2 to GND. Works only with A-to-C cables today. |
| H2 | Part metadata in the `.eprj` is wrong. | C1-C8 all carry `CL10B105KO8NNNC` / C59782 (a 1 uF part) although C1/C2 are 22 pF and C3-C8 are 100 nF. R3-R7 carry `QR0603J100RP05Z` / `Value=100Ω` although they are 10 k, 22, 22, 1.5 k, 1.5 k. R1/R2 (220 Ω) carry `SY0603JN1KP`, which looks like 1 k: verify. U1 and U11 have empty part numbers. | Fix Supplier/Manufacturer Part and Value on every component. Anything generated from the `.eprj` (SMT order, BOM export) is wrong today. 1 uF on the crystal would stop it oscillating. |
| H3 | Polyfuse F1 differs between files. | `.eprj`: `PTC1206SMD1206P010TF0.1A/60V` (0.1 A hold). BOM: `1206L050WR` (0.5 A). | Confirm what is on the assembled boards. Standardise on 0.5 A hold: MCU at 72 MHz + LDO + a flash cartridge can exceed 100 mA. |
| H4 | BOM is stale. | BOM title `md2.PcbDoc-V1.0-20250530`; PCB, Gerbers and pick-and-place are `20250619`. | Regenerate the BOM from the final PCB. |

### Medium

| ID | Finding | Proposed fix |
|----|---------|--------------|
| M1 | No schematic in the repo. | Add the schematic project or at least a PDF export. |
| M2 | C10 (tantalum 10 uF / 10 V) sits directly on the 5 V rail after the PTC; 2:1 margin only, hot-plug overshoot possible. | 16-25 V part, or ceramic/polymer. |
| M3 | No ESD/TVS on USB (D+, D-, VBUS) or on the cartridge connector. About 50 cartridge lines go straight to MCU pins with no series resistor. | USBLC6-2 (or similar) on USB. 33-100 Ω series resistors on cartridge lines (ESD, ringing, bus contention). Check timing against the firmware delays first. |
| M4 | SWD header is 4 loose pads (`3V3, SWCLK(PA14), SWDIO(PA13), GND`), not in the BOM, no silk labels, no NRST. | Label on silk, add a 1x4/1x5 header to the BOM, add an NRST pad. |
| M5 | Firmware can only be flashed by SWD. No BOOT0 jumper/pad, and the STM32F103VB (xB, 128 KiB flash) ROM bootloader is UART (USART1), not USB DFU; PA9/PA10 are NC. | BOOT0 pad or jumper, expose PA9/PA10. |
| M6 | No local decoupling at the cartridge 5 V pins (`P2.34`, `P2.63`); the only bulk cap (C10) is at the USB corner. | 100 nF + about 22 uF near P2. |

### Low

| ID | Finding | Note |
|----|---------|------|
| L1 | ACT LED is on PC13. The datasheet says PC13-15 must not source LED current (3 mA, 2 MHz). BOM LED is blue + 220 Ω, about 1 mA: works, dim. Firmware sets PC13 to `GPIO_SPEED_FREQ_HIGH`. | Firmware: use `LOW` speed. Next rev: move the LED to a free GPIO. |
| L2 | Cartridge signals not connected: `/UWR`, `/CAS2`, `/DTACK`, `EDCLK`, `YS`, `HSYNC`, `VSYNC`, `SL1`, `SR1`. About 22 MCU pins are free. | Connect `/UWR` (and maybe `/CAS2`) to support carts that decode only the high byte. |
| L3 | MCU is 3.3 V, cartridge is 5 V, no level shifter. Works with TTL-threshold inputs and FT data pins (`PE0-15`). `PA0-6` (A17-A23) are not 5 V tolerant but are outputs only. | Optional 74LVC245-class buffer for CMOS-threshold carts. |
| L4 | Crystal: 22 pF with a 20 pF crystal gives about 14-16 pF effective load, so it runs slightly fast. Crystal is about 8 mm from the MCU. | About 33 pF if exact frequency matters. USB tolerance is not at risk. |
| L5 | NRST: 1.5 k pull-up (unusual), no 100 nF. VDDA/VREF+ tied straight to 3V3. | 10 k or none, add 100 nF. VDDA filter only if the ADC is ever used. |
| L6 | Silk prints "U2" (static text) but designator/BOM say U11. LEDs are named `POWER`/`ACT` instead of `D1`/`D2`. 3D model shows green LEDs, BOM says red and blue. No pin 1/64 marks on P2. | Cosmetic. |
| L7 | DFM: vias are 0.254 mm drill (10/23 mil), below the 0.3 mm standard of cheap 2-layer fabs (possible surcharge). Corner holes are 2.0 mm, fine for M1.6 but not M2 (needs about 2.2 mm). | Check with the fab before the next order. |
| L8 | Repo: `Hardware/wechat_*.png` need real names (`pcb-top.png`, `pcb-bottom.png`) and README use. `.eprj` is not diffable. Silk credits Epicjoy: make authorship and hardware licence explicit. | |

## 3. Verified facts (reuse, do not re-derive)

### Pin map (PCB netlist, confirmed by firmware where noted)

| MCU pins | Signal | Cartridge pins (P2) |
|----------|--------|---------------------|
| `PE0..PE15` (U1: 97, 98, 1-5, 38-46) | D0..D15 (input, pull-up in `gpio.c`) | 45, 42, 39, 36, 37, 40, 43, 46, 44, 41, 38, 35, 25, 24, 23, 22 |
| `PD0..PD7` | A1..A8 | 48, 50, 52, 54, 56, 58, 60, 62 |
| `PD8..PD15` | A9..A16 | 4, 5, 61, 59, 57, 55, 53, 51 |
| `PA0..PA6` | A17..A23 | 49, 6, 7, 8, 9, 10, 11 |
| `PB4` | `/CAS0` = `MD_RD` | 16 |
| `PB6` | `/CE0` = `MD_CS` | 17 |
| `PB14` | `/LWR` = `MD_WR` | 28 |
| `PB15` | `/VRES` = `MD_RESET` | 27 |
| `PB12`, `PB13` | `/TIME`, `M3` | 31, 30 |
| `PB5`, `PB7`, `PB8`, `PB9`, `PB11` | `/ASEL`, `/AS`, `VCLK`, `/MRES`, `/CART` | 26, 18, 19, 2, 32 |
| `PA11`, `PA12` | USB D-, D+ (22 Ω series) | USB1 |
| `PA13`, `PA14` | SWDIO, SWCLK | 4-pad header |
| `PC13` | ACT LED | |
| `BOOT0` | 10 k to GND (R3) | |

P2 GND: 33, 47, 64. P2 5 V: 34, 63. Unused MCU pins (NC): `PC0-5`, `PC6-9`, `PC10-12`, `PC14-15`, `PB0-3`, `PB10`, `PA7-10`, `PA15`. `PB10` and `PA7` are still driven as outputs by `gpio.c`.

### Power and USB chain

`VBUS` -> `F1` (PTC) -> `5VOUT` -> `C10`, `U11` (AMS1117, pin 3 in), P2 5 V pins. `U11` out -> `3V3` -> MCU (VDD x5, VDDA, VREF+, VBAT), `C3-C9`, R6, R7, POWER LED. `R6` 1.5 k = D+ pull-up. `R7` 1.5 k = NRST pull-up. Clock: 8 MHz HSE x9 = 72 MHz, USB from the PLL.

### Track and via data

Signals 9 mil, NRST/crystal/USB 9-10 mil, 3V3 16 mil, 5V/VBUS 20 mil. Rules: 6 mil clearance and width minimum. Vias: 117 x 10/23 mil, a few 12/24, 22/37 and 27/49 mil.

## 4. Status

All items open. Update this table as work lands.

| Group | Status |
|-------|--------|
| H1-H4 | open |
| M1-M6 | open |
| L1-L8 | open |

Suggested order for a V1.1: H1 first (only functional defect), then H2-H4 (data hygiene), then M2-M6, then L1 and L2.

## 5. Reproducing the netlist extraction

Work on a copy, never on the repo file. Python 3, standard library only:

```python
import sqlite3, base64, gzip, json, collections
db = sqlite3.connect("copy-of.eprj")
t, d = db.execute("select title, dataStr from documents").fetchone()
text = gzip.decompress(base64.b64decode(d[len("base64"):])).decode()
recs = [json.loads(l) for l in text.splitlines() if l.strip()]
# ["ATTR", id, 0, comp_id, layer, x, y, key, value, ...]  -> Designator, Name, Value, Supplier Part
# ["PAD_NET", comp_id, pad, net, ...]                      -> empty net name = not connected
# ["LINE", id, 0, net, layer, x1, y1, x2, y2, width_mil, 0], ["VIA", id, 0, net, "", x, y, hole_mil, dia_mil, ...]
# ["PAD", ...] with no component = loose pads (the SWD header)
# ["RULE", ...] design rules, in mil
```

Component footprints and symbols live in the `components` table, same encoding.
