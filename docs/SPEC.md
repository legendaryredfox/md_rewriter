# md_rewriter: fix specification

Status: draft for implementation. Derived from the full-repo code review and the UX/UI review of 2026-10-31.

## 1. Context

md_rewriter is a Sega Mega Drive cartridge reader/writer. Three parts:

| Part | Tech | Role |
|------|------|------|
| `legacy_firmware/` | STM32F103 (HAL, USB CDC), Keil project | Drives the cartridge bus, talks to the PC over a USB serial port |
| `Software/` | Qt (Widgets + SerialPort), qmake | Desktop GUI: read/write ROM and save, erase, detect, compare |
| `Hardware/` | PCB/BOM/Gerber | Out of scope |

Baseline facts verified on this machine (Linux Mint 22.3, Qt 6.4.2, arm-none-eabi-gcc 13.2.1):

- The Qt app builds and starts on Linux without changes.
- Firmware sources pass `-fsyntax-only` with six `%X`/`%d` format warnings and three unused-parameter warnings.
- There are no tests, no `.gitignore`, no firmware build file other than a Keil project.

## 2. Hard constraints

- **C1. Wire-protocol compatibility.** Released firmware 1.0 (`md-rewriter-firmware-1.0.hex`) is already in users' hands and the author cannot be asked to reflash. The new app must work with the old firmware, and the new firmware must work with the old app. No new framing, checksums or opcodes on the wire.
- **C2. No hardware available.** Nothing here can be tested against a real cartridge or a real STM32. Firmware changes are verified by compilation, static analysis and reasoning only, and must be minimal and local. Every firmware change is listed in the final report as "untested on hardware".
- **C3. Linux is a first-class target.** Everything must build and run on Linux with distro Qt 6 packages. Windows must keep working.
- **C4. Preserve behaviour for valid flows.** Reading, writing, erasing and detecting with a healthy device must produce the same bytes on the wire as before (apart from items explicitly listed below).
- **C5. Do not delete vendor code or history.** Vendor trees (`Drivers/`, `Middlewares/`) stay. Untracking of personal/generated files uses `git rm --cached` only.

## 3. Wire protocol (as observed, for reference)

Command packet from PC: `[cmd] AA 55 AA BB [arg...]`. Commands: `0A` dump ROM (arg: size 01/02/03/04), `1A` dump SRAM (arg: 01 = 32K, else 8K), `0B` write ROM block (args: block index in bank, bank), `1B` write SRAM block (same args), `0C` connect, `0D` detect, `0E` erase all (chip + SRAM), `1E` erase by size (arg 01..06 or 00 + 3-byte address), `2E` erase one sector, `0F` clear buffers.

ROM/SRAM write block: PC sends 1024 raw bytes, then the 7-byte `0B`/`1B` command. Firmware answers `ADD:0x... WRITE OK\r\n` (ROM) or `... WRITE GK\r\n` (SRAM).

Dump: firmware sends text `... DUMP START!!!\r\n`, then N raw 1024-byte blocks (N = 512, 1024, 2048, 4096 for ROM; 32 or 8 for SRAM), then `DUMPER ROM FINISH!!!\r\n` / `DUMPER RAM FINISH!!!\r\n`.

## 4. Requirements

IDs are referenced from the plan and the commit messages.

### 4.1 Firmware

| ID | Requirement | Acceptance |
|----|-------------|------------|
| FW-1 | `CDC_Receive_FS` must not read past the received length. A packet is a command only if `Len >= 5`, `Len <= 16` and the 4 header bytes match. | Code review + compile. |
| FW-2 | `buffcnt` must never index outside `receiveBuffer[16]`. Excess data packets are dropped, not stored. Command packets must not advance `buffcnt`. | Code review + compile. |
| FW-3 | Buffers shared between the USB interrupt and the main loop (`cmdbuff`, `receiveBuffer`, `buffcnt`) are `volatile`-qualified or accessed through a minimal critical section. | Compile with `-Wall -Wextra`, no new warnings. |
| FW-4 | `memclear()` clears all 16 rows. | Code review. |
| FW-5 | `checkid()` has no stack overflow: remove the undersized `chipid` buffer. | Code review. |
| FW-6 | `CDC_Transmit` gives up after a bounded time (host gone) instead of spinning forever. | Code review. |
| FW-7 | `erase_sector` and `eraseFLASH` have bounded waits. On timeout they stop, report `ERASE TIMEOUT` text, and restore bus mode. Timeouts are generous (sector 15 s, chip 300 s). | Code review. |
| FW-8 | SRAM dump releases `RD` (`MD_RD = 1`) as the comments intend, instead of writing `MD_CS = 1` twice. | Code review; flagged untested on hardware. |
| FW-10 | ROM/SRAM dump never drops or overwrites a block: wait until the previous USB transmit has left the device before refilling `transmitBuffer`, and stop the dump if the host stops reading. | Code review. |
| FW-9 | Zero compiler warnings from `-Wall -Wextra` on the three application files: portable `printf` formats, unused parameters marked, `&` vs `&&` fixed, stale comments corrected. | Warning count = 0. |

Known limitation accepted under C1: a raw data packet that happens to match the header and is shorter than 17 bytes could still be taken for a command. Data packets are always 64 bytes, so after FW-1 this is not reachable in normal flows.

### 4.2 Desktop app: correctness

| ID | Requirement | Acceptance |
|----|-------------|------------|
| APP-1 | Serial port is an owned member (`QSerialPort`, parent `this`). No global, no leak. | Build, no `new` without owner. |
| APP-2 | `readyRead` is connected exactly once. | Unit/behaviour test: two refreshes, one handler invocation per byte. |
| APP-3 | Incoming bytes are parsed by a stream parser that is independent of how the OS fragments reads. During a dump, exactly `N * 1024` bytes after the start line are data, written to the file; everything else is text. | Unit tests feed the same stream split at every byte boundary and at random boundaries; output file identical. |
| APP-4 | Write acknowledgements (`WRITE OK`, `WRITE GK`) are matched on complete lines, one ack advances exactly one block. | Unit test with split and coalesced acks. |
| APP-5 | Writing handles files whose size is not a multiple of 1024 by padding the last block with `0xFF` (the value firmware skips when programming). Reads that fail abort the operation with an error. | Unit test of the block reader. |
| APP-6 | Supported sizes come from one table: read and erase-by-size use 512K, 1M, 2M, 4M (what the firmware dump/erase accept); write accepts file sizes 512K, 1M, 2M, 3M, 4M (as the old app did); save read uses 8K/32K, save write accepts 8K/32K files. The dead 128K/256K branches are removed. Combos, packets, progress and size validation all use the table; anything else is rejected before a single byte is sent. | Unit test on the table. |
| APP-7 | All state members are initialised. Disconnecting or a port error mid-operation aborts it, closes and removes a partial file only if it was created by that operation and says so in the log. | Test with a fake device that drops. |
| APP-8 | Command packets are built by one helper. The ~25 copies of the header disappear. Bytes on the wire are identical to before (C4). | Golden-bytes unit test per command. |
| APP-9 | File compare is O(n) in memory, counts differences in 64-bit, lists the first 20, never overflows a counter, and runs without blocking the UI for large files. | Unit test with 300 differences. |
| APP-10 | No `QThread::msleep` on the GUI thread. | grep. |
| APP-11 | Progress is computed from bytes done / bytes expected, clamped to 0..100. Erase shows an indeterminate (busy) bar. | Unit test on the progress function. |

### 4.3 Desktop app: UX/UI

| ID | Requirement | Acceptance |
|----|-------------|------------|
| UX-1 | Layout-based form (no absolute geometry). Window resizes, text is not clipped at 125% to 200% scaling or with a different font. | Screenshots at several sizes and `QT_SCALE_FACTOR` values. |
| UX-2 | Follow the system palette and style. Remove the forced green-on-grey stylesheet and the `AdLib WGL4 BT` font. Keep the brand name in the title only. | Screenshot light and dark. |
| UX-3 | Actions grouped by task: Connection, Cartridge (detect), ROM (read, write), Save (read, write), Erase (separate group, visually marked as destructive). | Screenshot. |
| UX-4 | Destructive and overwriting actions (erase flash, erase by size, write ROM, write save, overwrite on read) ask for confirmation naming what will be lost. | Test with a scripted dialog. |
| UX-5 | Buttons are enabled by state: nothing but Refresh/Connect while disconnected; only Cancel while an operation runs. A Cancel button aborts the current operation (host side). Closing the window mid-operation asks first. | Offscreen test of enabled states. |
| UX-6 | One labelled size selector per purpose: "Cartridge ROM size" is used by read and erase-by-size; write uses the file size and says so. SRAM size likewise. Tooltips explain each. | Screenshot + tooltips present. |
| UX-7 | Log has severity (info, success, warning, error) shown with colour and an icon/prefix, no double blank lines, and a Clear button with an English label. Status bar shows connection and current operation. | Screenshot. |
| UX-8 | Port list shows port name plus description and VID:PID, refreshes on demand and when the dialog opens, and pre-selects the only device matching the firmware. | Screenshot with a fake list where possible. |
| UX-9 | All user-visible strings go through `tr()`. Translation files are in `TRANSLATIONS`, `.qm` generated at build time, loaded from the resource system according to the system locale. English is the source language; a pt_BR file is added (Russian and Chinese exist only in the README). | Build produces `.qm`; `QT_LANGUAGE` check. |
| UX-10 | Permission problems on Linux (`dialout` group) and busy ports are explained in the error text with the exact fix command. | Unit test of the message mapper. |
| UX-12 | Built-in user guide (Help > User guide, F1): quick start, window reference, use-case recipes, log message reference, troubleshooting, safety, shortcuts, technical notes; English and pt_BR; searchable with a table of contents; follows the system theme. Every message and access key it quotes exists in the program (checked by a test). | `tst_help`, e2e F1 test, screenshots in light and dark. |
| UX-11 | Mnemonics, tab order, accessible names. | Reviewed in `.ui`. |

### 4.4 Repository and build

| ID | Requirement | Acceptance |
|----|-------------|------------|
| REPO-1 | Add `.gitignore`; untrack `*.pro.user*`, `*.uvguix.*`, generated `.lst`, generated `.qm`. | `git status` clean after build. |
| REPO-2 | `Software/MDRewriter.pro` builds with Qt 6 (tested with 6.4.2; Qt 5.15 is intended to work but could not be tested here because its SerialPort package is not installed), enables C++17, uses `lrelease` for translations, has a `tests` subproject and Linux install rules including `.desktop` file and PNG icon. | Clean build in a fresh directory. |
| REPO-3 | Linux support: udev rule file (`99-md-rewriter.rules`) that sets permissions and tells ModemManager to ignore the device; README section. | File exists, VID:PID read from the firmware USB descriptor. |
| REPO-4 | README: remove "Windows only", add Linux build/run, add test instructions. | Review. |
| REPO-5 | `tools/check-firmware.sh` runs the `-fsyntax-only -Wall -Wextra` check and `cppcheck` on the three application files, so the firmware can be validated without Keil. | Script exits 0. |
| REPO-6 | MIT `LICENSE` added at the maintainer's request (copyright line "md_rewriter contributors"). Vendor trees untouched and keep their own licenses (C5). | — |

## 5. Out of scope

- Changing the wire protocol, adding checksums, reflashing tooling.
- PCB/hardware changes.
- Trimming the 14 MB CMSIS-DSP vendor tree (suggested to the maintainer, not done).
- Replacing the Keil project with another build system.
- Real-hardware validation (C2).

## 6. Risks

| Risk | Mitigation |
|------|-----------|
| Stream parser misclassifies bytes on a real device | Parser is purely driven by lengths announced by the protocol; unit-tested with exhaustive splits; logs unexpected bytes instead of dropping them silently. |
| Firmware edits change bus timing | No change to `Delay_nop` values or the order of bus writes, except FW-8, which is called out. |
| UI rewrite regresses a flow | Golden-bytes tests per command (APP-8) and a fake-device integration test drive every flow headless. |
| Qt 5/6 drift | Qt 6 only was built and tested; the code avoids Qt 6-only APIs where practical. Stated in the README. |
