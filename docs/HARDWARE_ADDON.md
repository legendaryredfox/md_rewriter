> **Status (2026-11-04): dropped.** The owner chose not to build add-on boards and then also dropped the PCB rework of this project (hardware work moved to the sibling project `oscr_minimal`). Kept as a record of the options that were analysed.

# Can the V1.0 hardware findings be fixed by an add-on instead of a new PCB?

Answer for the owner, 2026-11-04. The V1.0 PCB exposes only: the USB-C receptacle, the cartridge slot (P2), four SWD pads (`3V3`, `SWCLK`, `SWDIO`, `GND`), and the component pads. **No spare MCU pin is routed to any pad or connector**, so an add-on cannot get new signals out of the MCU. Everything below uses what is reachable.

## Via the cartridge slot: an interposer (male edge plug on the bottom, female slot on top)

| Finding | How | Limits |
|---------|-----|--------|
| M3 series resistors (33-100 ohm) on the ~50 cartridge lines | resistors in the interposer | slows edges; software delays (`Delay_nop`) and the cart access time must be re-measured |
| M3 ESD on the cartridge lines | TVS arrays in the interposer | capacitance |
| M6 decoupling at the cart 5 V pins | 100 nF + ~22 uF in the interposer | works, it is on the cart side of the slot |
| L2 `/UWR` | tie `/UWR` to `/LWR` on the interposer: the firmware always writes full 16-bit words, so both strobes together is the real Mega Drive word write | carts that need byte writes to the high half only are not served |
| L2 `/CAS2` | derive it on the interposer from `A21..A23` (and `/AS`): low for `200000h`-`3FFFFFh` | needs a small decoder (74HC138 class) |
| L3 level buffer (74LVC245 class) | buffers on D0-D15 and address lines in the interposer; direction from `/RD` | adds delay on the bidirectional bus; needs 3.3 V and 5 V rails from the slot; mechanically the stack gets taller and may not fit a case. Validate with a logic analyzer first |

## Via the exposed points and small rework (no new board)

| Finding | How |
|---------|-----|
| H1 no Rd on USB-C CC | solder two 5.1 k 0603 from the receptacle's A5 and B5 pads to GND (bodge), **or** a pass-through dongle (C plug to C receptacle with 5.1 k on both CC pins to GND), **or** keep using an A-to-C cable (works today) |
| H3 polyfuse | replace F1 with a 0.5 A hold part |
| M2 tantalum C10 | replace with 16-25 V part |
| M4 SWD | solder a 1x4 header on the four pads; NRST: tap the pad of R7 on the NRST side (R7 pulls NRST to 3V3 through 1.5 k) |
| M5 BOOT0 | tap the BOOT0 side of R3 (10 k to GND) and strap to 3V3 for the ROM bootloader. PA9/PA10 have no pads (LQFP100 pins 68 and 69): fine-pitch wires. Still only UART, no USB DFU |
| L1 ACT LED speed | firmware only |

## Via USB: an inline dongle

| Finding | How |
|---------|-----|
| M3 USB ESD | inline USB-C or USB-A dongle with a USBLC6-2 class TVS and the H1 resistors |
| Anything else | nothing: the USB port carries only the CDC link |

## Needs a respin (no add-on can do it)

H2 and H4 are data fixes in the design files, not physical. A new GPIO for the ACT LED (L1), BOOT0 jumper in place (M5), NRST filter (L5), crystal capacitors (L4), silkscreen (L6), via and hole sizes (L7). Also anything that needs a new MCU signal: I2C/SPI save memories (D3), Master System / Game Gear lines, a cart-detect input.

## Suggestion

Fix H1 with the two resistors or the dongle, solder the SWD header, and build **one interposer** (M3, M6, L2 via `/UWR=/LWR`, optional L3). That covers the V1.0 findings that matter for firmware 1.1 without waiting for the V1.1 PCB. The V1.1 PCB was dropped (see the note at the top); the findings in the list above that only a respin fixes stay open.
