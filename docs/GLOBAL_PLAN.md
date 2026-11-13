# md_rewriter: global plan

Linear order of work. Implements `docs/GLOBAL_SPEC.md`. Each step names its detail document. Mark a step done with the date; do not reorder without a reason written in the log.

## Phase A: software before hardware (this machine)

| # | Step | Detail | State |
|---|------|--------|-------|
| A1 | Review docs, KY research, existing firmware/app | `references/ky_technology/*.md` | done 2026-11-04 |
| A2 | KY gap analysis, chip tables | `docs/KY_SUPPORT.md` | done 2026-11-04 |
| A3 | CLI with tests | `docs/CLI.md`, `Software/tests/tst_cli` | done 2026-11-04 |
| A4 | Read-only bench suite | `docs/HWTEST.md` | done 2026-11-04 |
| A5 | Command sets AMD 5555h and Intel, probe, host model | `NEW_FIRMWARE_SPEC.md` NFW-15 | done 2026-11-04 |
| A6 | Code review of everything above | 10 findings, 8 fixed (top-boot erase, SST 3201/3202 removed, probe helper, dead code in `checkid`, app wording, CLI detect/JSON, hwtest SEGA check, `.gitignore` anchor), 1 checked and rejected (SST 6401B `30h`), 1 documented (Intel map is only checked against itself) | done 2026-11-04 |
| A7 | README (4 languages) and in-app help: CLI, new chips, bench suite | README features and tests sections in pt/ru/zh/en (CLI, KY chips, 8 MiB, `tst_cli`, `hwtest`); help section `cli` and KY chip note in en/pt_BR; all app tests pass (protocol 77, session 59, help 19, e2e 30, cli 10) | done 2026-11-10 |
| A8 | Commit in logical pieces, only when the owner asks | `874e7e6`, `d2e0b96`, `2b56488`, `5edd50a` pushed | done 2026-11-10 |

## Phase B: hardware arrives (needs the dumper, ST-Link, a logic analyzer)

Follow `docs/NEW_FIRMWARE_PLAN.md` steps 2 to 7 in order. Between steps run `tools/hwtest/hwtest.py nocart` and `cart` and keep the `report.md`. Order: backup of the 1.0 flash (step 2), timing (3), first run (4), checklist (5, rows 3d to 3f cover the KY chips), results (7). After every unconfirmed chip ID is seen on a real chip: fix the tables, mark confirmed.

## Phase C: firmware features that need carts

Goal: every cartridge handled by the reference repositories. The list of what exists and what is missing is `docs/CART_SUPPORT.md`; its section 4 is the order. One feature per commit, each with a host test first.

Folders: `firmware_1.1/` is the firmware for the current PCB (version string 1.1.0 always in the code); `firmware_1.2/` is reserved for the revised PCB. A hardware-independent fix goes in 1.1 and is copied to 1.2 when 1.2 exists.

## Phase D: hardware V1.1 (cancelled 2026-11-04, kept as a record)

Before a respin, `docs/HARDWARE_ADDON.md` lists what an interposer, small rework and an inline USB dongle can fix on V1.0.

1. ~~Create `hardware_new/` with a README that points to this plan.~~ **Cancelled 2026-11-04**: the owner dropped the PCB rework of this project; the scaffold (`hardware_new/`, empty KiCad project and the V1.0 netlist extraction) was deleted. Hardware ideas for Mega Drive carts moved to the sibling project `oscr_minimal` (`docs/melhorias-megadrive.md`, `docs/melhorias-megadrive-hardware.md`, board `16_bits` v. 1.2).
2. Spec for V1.1: one table per finding (H1 to H4, M2 to M6, L2, L3) with the exact part, value, net and footprint; pin map changes for L2/L3 listed against `MD.h`/`gpio.c`. File: `docs/HARDWARE_SPEC_V1.1.md`.
3. Recreate the schematic in KiCad (MCP tools) from the netlist, run ERC, then the PCB, run DRC, compare the netlist with V1.0 except for the planned changes.
4. Firmware pin map change in `firmware_1.1/` for L2/L3, host tests updated.
5. Gerbers, BOM and pick-and-place from KiCad; fab check (via size, 2 mm holes: L7).
6. Update `docs/HARDWARE_REVIEW.md` section 4 as items close.

Questions to answer at the start of phase D: which regulator and USB protection parts the owner prefers; whether the level buffer (L3) is wanted after the timing measurements of phase B; PCB fab constraints.

## Phase E: release

Publish app and firmware only after phase B passes (GLOBAL_SPEC section 6).

## Progress log

| Date | What |
|------|------|
| 2026-11-13 (bench, flash cartridge) | First write on hardware, S29GL064N cartridge, firmware 1.0: Metal Gear 2 MiB written and read back identical (SHA-256, `compare`, `verify`). Two findings fixed: the firmware drops bytes sent right after `detect` (app holds 250 ms; NFW-19 written, not applied) and firmware 1.0 programs only every other word on this chip (`tools/repair-write-fw10.py`, 8 passes). Firmware 1.1 still not flashed. Details in `docs/HWTEST.md`. |
| 2026-11-13 (bench, cartridge) | Original Rolling Thunder 2, read only (no writes on this cartridge, owner's rule): 1M and 2M reads identical, mirror confirmed, header checksum mismatch left open pending a reference dump. `hwtest.py` gained `analyze_rom` (mirror, checksum, uniform blocks), `host` phase 9/9. Details in `docs/HWTEST.md`. |
| 2026-11-13 (bench) | First unit on the bench, factory firmware 1.0, USB only, slot empty: `hwtest.py host` 7/7, `nocart` 11/11. Finding: a connect right after `detect` is ignored by firmware 1.0 (CLI now retries once; `hwtest.py` pauses 0.3 s). Finding: no USB firmware update possible (CDC only, F103 ROM bootloader is UART only), so firmware 1.1 needs SWD. Details in `docs/HWTEST.md`, "Bench results". Firmware 1.1 still not flashed. |
| 2026-11-13 | Full review of code and docs. Fixed: chip capacity moved into the chip table, GUI pre-flash message showed the raw `FLASHID:` line, `--timeout` error text, `1E` erase code above 5 (NFW-18), pt_BR strings of the pre-flash check, README 4 MiB text and the missing ru/zh sentence, stale lines in GLOBAL_SPEC and HARDWARE_ADDON. |
| 2026-11-10 (later still) | `CART_SUPPORT.md` section 4 item 4: SSF2 and Extended SSF2 read through `3A` (NFW-17), CLI only. Sharp LH28F320BJE and Toshiba TC58FVT160F looked up and **not** implemented (owner decision). Lock-on and SVP check still open. |
| 2026-11-10 (later) | Phase C, `CART_SUPPORT.md` section 4 item 2: CFI query for unknown chips in `firmware_1.1` (NFW-16), host checks 190/195, app message, `tst_session` 62. Information only. |
| 2026-11-10 | A7 done: README and in-app help describe the CLI, KY/FlashKit chips, 8 MiB writes and the read-only bench suite. Phase B and C still wait for hardware; phase D cancelled. |
| 2026-11-04 (later) | Code review done and applied (10 findings: 8 fixed, 1 rejected, 1 documented); firmware 1.1 in `firmware_1.1/`, 1.2 reserved in `firmware_1.2/`; pushed to `origin/main` (`23eb863`). PCB rework **cancelled**, `hardware_new/` deleted; Mega Drive hardware ideas moved to `oscr_minimal` (board `16_bits` v. 1.2, D87 there). Still untested on hardware: chip IDs, the Intel 28F400 block map, the 5555h flow; the host model uses the same tables. |
| 2026-11-04 | Phase A1 to A6 done (host checks 174/179 after the review fixes). Owner decisions: KiCad for V1.1, scope H1-H4, M2-M6, L2, L3; hardware work postponed; firmware KY command sets are the top priority. |
