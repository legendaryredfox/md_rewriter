# md_rewriter: global specification

Status: living document, started 2026-11-04. It ties the other specs together and says what the whole project (hardware + firmware + app) must do. Details stay in the specs it points to; if two documents disagree, fix the detail document and this one.

## 1. Goal

One dumper that reads and rewrites as many Mega Drive / Genesis cartridge types as possible: original game carts, rewritable carts of many flash chips (including the KY Technology / FlashKit MD family), save RAM. Firmware is developed first; PCB changes come later, in a new folder, and only when needed.

## 2. State of the parts

| Part | Folder | State | Detail document |
|------|--------|-------|-----------------|
| Legacy firmware | `legacy_firmware/` | frozen | `docs/SPEC.md` |
| Firmware 1.1 (current PCB) | `firmware_1.1/` | builds, host-tested against chip models, **never run on hardware** | `docs/NEW_FIRMWARE_SPEC.md`, `_PLAN.md` |
| Desktop app | `Software/` | GUI done; CLI `mdrewriter-cli` added | `docs/SPEC.md`, `docs/CLI.md` |
| Bench tests | `tools/hwtest/` | read-only suite written | `docs/HWTEST.md` |
| KY / FlashKit support | firmware + app tables | analysis done, 12 chips and 2 new command sets in the model | `docs/KY_SUPPORT.md` |
| Hardware PCB V1.0 | `Hardware/` | reviewed, 4 high, 6 medium, 8 low findings, nothing changed | `docs/HARDWARE_REVIEW.md` |
| Firmware 1.2 (revised PCB) | `firmware_1.2/` | empty placeholder | `docs/GLOBAL_PLAN.md` phase D |
| Hardware PCB V1.1 | none (`hardware_new/` was deleted on 2026-11-04) | **cancelled**: the owner moved the hardware work to `oscr_minimal` (Arduino Mega shield, board `16_bits` v. 1.2) | this file, section 5 |
| References | `references/` (git-ignored) | KY research, FlashKit source, cloned open-source programmers | `references/ky_technology/*.md` |

## 3. Constraints (all of them hold for every step)

- G1. Wire protocol stays compatible both ways between app and firmware (`NEW_FIRMWARE_SPEC.md` N-C1).
- G2. Nothing is flashed or written to a cartridge by automation. Flashing, SWD and destructive commands are manual steps.
- G3. Until hardware is tested, every firmware change is labelled "untested on hardware", has a switch or a rollback, and a host test of the real `MD.c`.
- G4. `legacy_firmware/` and `Hardware/` are never edited. PCB changes would go in a new folder and only when a change is needed (none planned: see section 5).
- G5. The bench suite never changes a cartridge.
- G6. Commits, PRs and docs carry the user's authorship only (global rule).
- G7. Linux first-class, Windows and macOS keep building.

## 4. Firmware / app requirements still open

| ID | Requirement | State |
|----|-------------|-------|
| K1 | Name KY / FlashKit chips | done (NFW-14), IDs unconfirmed |
| K2 | AMD 5555h command set (MX29L3211) | done in the model (NFW-15) |
| K3 | Intel 28F400 command set | done in the model (NFW-15) |
| K4 | Page program (MX29L3211, 128 words), unlock bypass | not started, speed only |
| K5 | Every cartridge type of the reference repositories (`docs/CART_SUPPORT.md`): mappers, remaining flash chips, serial saves | goal of the project; matrix written, mostly not started |
| K6 | Confirm every unconfirmed chip ID on a real chip | needs hardware and carts |
| F1 | Timing against the 1.0 hex, GCC `-O1` vs `-O3` | needs hardware, logic analyzer (NFW-4) |
| F2 | USB update (bootloader) | deferred, needs BOOT0 on the PCB (D1) |
| F3 | EEPROM saves (I2C) | deferred, needs `/UWR` and `/CAS2` (D3, D5) |
| A1 | CLI | done |
| A2 | App text: README (4 languages) and help mention the CLI and the new chips | open |

## 5. Hardware V1.1 scope (decided 2026-11-04, work deferred to another session)

**Cancelled on 2026-11-04** (the text below is the scope that was decided before). Format was to be **KiCad**, schematic and PCB recreated from the netlist in `docs/HARDWARE_REVIEW.md` section 5, in a `hardware_new/` folder. The original `Hardware/` stays as is. Scope: H1 to H4 (must), M2 to M6, L2 (`/UWR`, `/CAS2` to free GPIOs), L3 (74LVC245-class buffer on the cartridge lines, needs timing validation against the firmware delays). Firmware pin map must change together with L2/L3 (`MD.h`, `gpio.c`), so the PCB spec has to list every pin change before layout.

KY / FlashKit support itself needs **no** PCB change (`docs/KY_SUPPORT.md` section 4).

## 6. Acceptance of the whole project

1. `tools/build.sh all` and `tools/build.sh tests` pass; `firmware_1.1/check.sh` has no warnings.
2. With hardware: plan steps 2 to 7 of `NEW_FIRMWARE_PLAN.md` pass on at least two cartridges; `hwtest.py nocart` and `cart` pass and their reports are kept.
3. Every chip in the tables is marked confirmed or removed.
4. Only then is a firmware `.hex` published.
