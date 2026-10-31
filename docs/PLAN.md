# md_rewriter: implementation plan

Implements `docs/SPEC.md`. Each step lists the requirement IDs it closes, the verification that must pass before moving on, and a commit boundary. Work happens on a branch `fix/review-2026-10`, one commit per step, never on `main`.

## Verification toolbox (all already installed)

- Qt app: `qmake6 && make` in a scratch build directory; Qt tests with `QT_QPA_PLATFORM=offscreen`; screenshots with `xvfb-run` + ImageMagick `import`.
- Firmware: `tools/check-firmware.sh` (arm-none-eabi-gcc `-fsyntax-only -Wall -Wextra`, cppcheck).
- Static analysis: `cppcheck`, `clang-tidy` on `Software/`.
- Runtime checks: `valgrind` or ASan build for the tests.

## Step 0: Baseline (done)

- Baseline build of the unmodified app succeeds on Qt 6.4.2.
- Baseline screenshot saved (`before.png`) shows overlapping group titles and low-contrast borders.
- Firmware baseline: 6 format warnings, 3 unused-parameter warnings.

## Step 1: Repo hygiene and firmware check script (REPO-1, REPO-5)

1. Create branch `fix/review-2026-10`.
2. Add `.gitignore`; `git rm --cached` the `*.pro.user*`, `*.uvguix.*`, `startup_stm32f103xb.lst`, `*.qm` files.
3. Add `tools/check-firmware.sh`; run it to record the baseline.

Verify: script reproduces the known warnings; `git status` shows only the intended index changes.

## Step 2: Firmware fixes (FW-1 to FW-9)

Files: `Firmware/USB_DEVICE/App/usbd_cdc_if.c`, `Firmware/Core/Src/main.c`, `Firmware/Core/Src/MD.c`, `Firmware/Core/Inc/MD.h`.

Order, smallest blast radius first:

1. FW-9 mechanical: `%lX`/`%lu` with casts, `(void)` unused parameters, `&&`, comment fixes. No behaviour change.
2. FW-4, FW-5: `memclear` bound, drop `chipid`.
3. FW-1, FW-2, FW-3: rewrite `CDC_Receive_FS`; declare shared buffers `volatile` in one place (a header, `extern` users updated).
4. FW-6: bounded `CDC_Transmit` using `HAL_GetTick`.
5. FW-7: bounded waits in `erase_sector` / `eraseFLASH`; callers report timeout.
6. FW-8: `MD_RD = 1`.

Verify after each sub-step: `tools/check-firmware.sh` has no new warnings; final count 0. Re-read the diff against C2 (no timing or bus-order change except FW-8). Every change tagged in the commit message "untested on hardware".

## Step 3: Protocol layer in the app (APP-3, APP-4, APP-5, APP-6, APP-8, APP-9, APP-11, UX-10)

New files, no Qt GUI dependency, so they are unit-testable:

- `Software/protocol.h/.cpp`: command enum, `buildCommand(cmd, args)`, size table (ROM 128K..4M, SRAM 8K/32K, packet size codes), `Progress` helper, `describeSerialError()`.
- `Software/streamparser.h/.cpp`: incremental parser. States: `Idle` (text lines), `Dumping(expectedBytes)` (counts exactly N bytes of data), with line assembly for text and ack detection. Emits `textLine`, `dataChunk`, `ackRom`, `ackSram`, `dumpFinished`, `unexpected` signals. No dependence on read sizes.
- `Software/blockreader.h/.cpp`: reads 1024-byte blocks from a file, pads the last with `0xFF`, reports read errors.
- `Software/filecompare.h/.cpp`: streaming compare, 64-bit counters, first 20 differences.

Tests (`Software/tests/`, QtTest, one binary per unit, registered in `tests.pro`):

- Golden bytes per command against the sequences the old code produced (extracted from the old source by reading it, recorded as literals).
- Parser: the same synthetic firmware session split at every byte, at random boundaries (fixed seeds), and fully coalesced; resulting file and text equal in all cases. Includes a data block containing `OK\r\n` and `AA 55 AA BB` inside dump data.
- Block reader: sizes 0, 1, 1023, 1024, 1025, 524288.
- Compare: 0, 1, 20, 21, 300, 70 000 differences; different lengths.
- Size table and progress clamping.

Verify: all tests pass; same tests under ASan; `cppcheck`/`clang-tidy` clean on new files.

## Step 4: MainWindow rewrite on the protocol layer (APP-1, 2, 7, 10; UX-4, 5, 7, 8, 10)

1. `MainWindow` owns `QSerialPort serial_` and a small `Operation` state machine: `Idle`, `Connecting`, `Detect`, `ReadRom`, `WriteRom`, `ReadSave`, `WriteSave`, `EraseChip`, `EraseBySize`, `Compare`. One place decides which buttons are enabled for each state.
2. `readyRead` connected once in the constructor; bytes go into the parser; parser signals drive the state machine.
3. Write flows send a block then wait for the matching ack signal; Cancel and port errors tear down the operation, close the file, and remove partial output only if the operation created it.
4. Confirmation dialogs for erase, write, overwrite.
5. Compare runs on a worker (`QtConcurrent`) with progress and cancel.
6. Remove `delay()`, the global `COM`, `hexStringToByteArray`, dead branches.

Verify: a fake-device test (Step 6) drives each flow; all enabled-state combinations asserted; valgrind clean on the test run.

## Step 5: UI rewrite (UX-1, 2, 3, 6, 7, 9, 11)

1. Rewrite `mainwindow.ui` with layouts: Connection (port combo with description, Refresh, Connect), Cartridge (Detect), ROM (size combo, Read, Write), Save (size combo, Read, Write), Erase (Erase by size, Erase all, separated and marked), log, progress bar, status bar.
2. Remove the stylesheet and font override; destructive buttons get a restrained warning style that works in light and dark.
3. `tr()` on every string; `MDRewriter_en.ts` and new `MDRewriter_pt_BR.ts`; translations compiled by `lrelease` at build time and loaded from resources; remove the committed `.qm` and the stale `zh_CN`.
4. Mnemonics, tab order, tooltips, accessible names.

Verify: screenshots under Xvfb at default, narrow, wide, `QT_SCALE_FACTOR=1.5` and `2`, with light and dark palettes; compare with `before.png`; check no clipped text by eye and by `sizeHint` assertions in an offscreen test.

## Step 6: Fake-device integration test (APP-2, APP-7 end to end)

A test helper creates a pty pair (`openpty`) and runs a small firmware emulator that implements the protocol of Section 3 exactly (reading the real command bytes, replying with the real strings, fragmenting output randomly). `QSerialPort` opens the slave. Scripts: connect, detect, read ROM 512K and 1M and compare with the emulator's image, write ROM, read/write save, erase, abrupt disconnect mid-read, double refresh.

Verify: all flows pass with several fragmentation seeds; the emulator's expectations are derived from firmware source, so protocol drift shows up as a test failure.

## Step 7: Build system, Linux integration, docs (REPO-2, 3, 4, 6; UX-9)

1. `MDRewriter.pro`: C++17, Qt 5.15/6 compatible, `lrelease` hook, `SUBDIRS` with tests, Linux install of binary, `.desktop`, PNG icon (generated from `md.ico`), udev rule.
2. `99-md-rewriter.rules` using the VID:PID from `Firmware/USB_DEVICE/App/usbd_desc.c`.
3. README: Linux build/run, `dialout` or udev instructions, test commands; remove "Windows only".
4. Run the whole pipeline in a fresh clone in the scratchpad: configure, build, test, install to a temp prefix, launch the installed binary under Xvfb.

Verify: fresh clone builds and tests pass; `git status` clean afterwards.

## Step 8: Final review and report

1. `/code-review` style pass over the full diff plus a re-run of `cppcheck` and `clang-tidy`.
2. Before/after screenshots.
3. Report: what changed per requirement ID, what was verified how, what is untested on hardware (all firmware edits, FW-8 in particular), what remains (LICENSE, vendor trimming, real-device smoke test checklist).

## Commits (as made)

1. `chore: add .gitignore, untrack generated files, add firmware check script`
2. `fix(firmware): bounds, volatile, bounded waits and warnings in the USB/flash paths`
3. `feat(app): protocol layer, device session, layout-based UI, translations, Linux files and tests`
4. `docs: spec, plan and README in pt-BR, ru, zh-CN, en`

Pushed to the branch `fix/review-2026-10` only. No PR was opened.

## Order-of-work rationale

Firmware first because it is small and independent. Protocol layer before the window rewrite so the riskiest logic (stream parsing, the data-corruption bug) is covered by tests before any GUI code depends on it. UI last, once behaviour is pinned by tests.

## Results (verification actually run)

| Check | Result |
|-------|--------|
| `tools/check-firmware.sh` (gcc `-Wall -Wextra`, cppcheck) | 0 warnings (baseline: 10 gcc warnings, 1 cppcheck error) |
| `tst_protocol` (unit) | 59 passed |
| `tst_session` (integration, fake firmware, fragmented replies, faults, cancel) | 35 passed |
| `tst_e2e` (real window, real `QSerialPort`, pty, fake firmware) | 20 passed |
| All three under ASan + UBSan + LeakSanitizer | clean |
| Mutation check (off-by-one in the parser data window, wrong bank math) | caught by 5 unit and 8+ integration tests; reverted |
| cppcheck / clang-tidy on the app sources | one real finding (duplicate branch), fixed; remaining cppcheck notes are Qt `emit`/`Q_ENUM` macro false positives |
| Screenshots (before/after, light theme, pt_BR) | `docs/images/ui-main.png` and the review notes |

Findings of the tests that were then fixed: a pty bridge deadlock in the test itself, the difference list printed in upper case, and a fixed minimum window height that clipped the Refresh button.

Not verified (no hardware, no Qt 5): every firmware change (FW-1 to FW-10, FW-8 in particular), Windows build, Qt 5.15 build, a real USB serial port.
