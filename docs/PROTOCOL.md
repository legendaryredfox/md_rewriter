# Wire protocol (firmware 1.0, and what firmware 1.1 adds)

The USB serial protocol between the app (or `mdrewriter-cli`) and the dumper. The firmware 1.0 part below was observed on the factory firmware and stays valid in both directions: the app works with 1.0 and 1.1, and firmware 1.1 only adds to it (`docs/NEW_FIRMWARE_SPEC.md`, section 3.2). The code that implements it on the app side is `Software/protocol.cpp`.

Command packet from the PC: `[cmd] AA 55 AA BB [arg...]`.

| Command | Meaning |
|---------|---------|
| `0A` | dump ROM (arg: size `01`, `02`, `03`, `04`; firmware 1.1 also `05` = 8 MiB) |
| `1A` | dump save RAM (arg: `01` = 32K, else 8K) |
| `0B` | write a ROM block (args: block index in the bank, bank) |
| `1B` | write a save RAM block (same args) |
| `0C` | connect (firmware 1.1 follows the banner with `FWVER:<version>`) |
| `0D` | detect: prints `FLASHID:` and the chip line (1.1: `FLASHID:%02X%02X`, and `CHIPINFO:` for the S29GL064N) |
| `0E` | erase all (chip and save RAM) |
| `1E` | erase by size (arg `01` to `06`, or `00` plus a 3-byte address) |
| `2E` | erase one sector |
| `0F` | clear the buffers |
| `3A` | firmware 1.1 only: dump through the SSF2 mapper (arg = number of 512 KiB pages) |

ROM or save write block: the PC sends 1024 raw bytes, then the 7-byte `0B` / `1B` command. The firmware answers `ADD:0x... WRITE OK\r\n` (ROM) or `... WRITE GK\r\n` (save). Firmware 1.1 answers `ADD:0x<word address> WRITE TIMEOUT` when a word is not programmed.

Dump: the firmware sends text `... DUMP START!!!\r\n`, then N raw blocks of 1024 bytes (N = 512, 1024, 2048, 4096 for ROM; 32 or 8 for save RAM; 8192 for the 8 MiB dump of 1.1), then `DUMPER ROM FINISH!!!\r\n` or `DUMPER RAM FINISH!!!\r\n`.

Known quirk (firmware 1.0 and 1.1): after answering `detect` the firmware waits 100 ms and only then clears its input buffer, so bytes sent right after the answer can be lost. The app holds the next command back for 250 ms (`docs/CLI.md`).
