# mdrewriter-cli

Command line front end of md_rewriter. Same `DeviceSession` as the GUI, so the bytes on the wire are identical. Meant for scripts and AI agents.

Build: `tools/build.sh app` (also builds the GUI) gives `build/cli/mdrewriter-cli`. Install with `make install` in `build/cli`.

```
mdrewriter-cli [options] <command> [args]
```

| Command | Needs device | Changes the cartridge | What it does |
|---------|:-:|:-:|---|
| `ports` | no | no | lists serial ports; the dumper is `0483:5740` |
| `chips` | no | no | chip table (`FLASHID` -> name, confirmed or not) |
| `rominfo <file>` | no | no | what would be written: size, SMD conversion, header warnings |
| `compare <a> <b>` | no | no | byte compare |
| `info` | yes | no | connect, firmware version |
| `detect` | yes | no | flash ID and chip name |
| `read-rom --size 512K\|1M\|2M\|4M\|8M --out F` | yes | no | dump ROM (8M needs firmware 1.1) |
| `read-save --size 8K\|32K --out F` | yes | no | dump save RAM |
| `read-rom --mapper ssf2 --size 512K..32M --out F` | yes | no | dump an SSF2 bank-switched cartridge page by page (size in 512K steps, firmware 1.1+, not yet run on a cartridge) |
| `verify F [--size S]` | yes | no | read the cartridge back and compare with F |
| `write-rom F --yes [--no-verify]` | yes | **yes** | write a flash cartridge, read back by default |
| `write-save F --yes` | yes | **yes** | overwrite save RAM |
| `erase all\|512K\|1M\|2M\|4M\|8M --yes` | yes | **yes** | erase |

Options: `--port NAME` (default: the only port with the dumper USB id, or `MD_REWRITER_EXTRA_PORT`), `--json`, `--yes`, `--dry-run`, `--timeout S`, `--quiet`.

Safety:
- A command that changes the cartridge refuses without `--yes` (exit 4). `--dry-run` checks arguments and files and never opens the port.
- `read-rom` and `read-save` do not overwrite an existing output file without `--yes`; the file is written atomically.
- Files are checked before the port is opened.

`write-rom` checks the chip first (connect, then `detect`, then write). It refuses, exit 1, when the firmware printed no chip ID, when the ROM is larger than the chip (`ChipDb` capacity), when the ROM is over 4 MiB and the firmware is 1.0, or when the chip needs a command set that only firmware 1.1+ sends. A chip with a readable ID that is not in the table is allowed (`--yes` is the override); the JSON field `preflight` says `compatible` or `uncertain`. The GUI is more lenient in one case: with no readable ID it asks the user instead of refusing.

Output: messages and progress on stderr; with `--json` one JSON object on stdout (`ok`, `exit`, `command`, `error`, `messages`, plus command fields such as `flashId`, `chip`, `firmware`, `bytes`, `verified`, `identical`).

Connect retry: firmware 1.0 can ignore the connect that arrives right after the port was reopened (bench, 2026-11-13: `detect` followed at once by `read-rom` as two processes failed with exit 3). The CLI tries the connect a second time before it reports exit 3; the retry adds a `warning` message. A silent device therefore takes two idle timeouts to report exit 3.

After `detect` the session holds the next command back for 250 ms. Firmware 1.0 (and the 1.1 source) answers `detect`, waits 100 ms and only then clears its input buffer, so a write block sent at once is lost: the first bench write failed with `The device did not confirm block 1 of 2048`. The GUI has the same hold. `tst_session` and `tst_cli` model the firmware that drops those bytes.

Firmware 1.0 and fast chips: on the S29GL064N the firmware programs every other word and leaves the rest blank, so `write-rom` ends with `Verification failed` (bench, 2026-11-13). Nothing is wrong in the words that were written. `tools/repair-write-fw10.py game.bin --size 2M --yes` reads the cartridge back, writes only the missing words and repeats until the read back equals the file (8 passes for a 2 MiB game, a little over 2 minutes). It never erases and stops if the cartridge holds a word that is neither blank nor the file's. Firmware 1.1 does not need it.

Exit codes: 0 ok, 1 operation failed (device error, verify mismatch, files differ), 2 usage, 3 no device, 4 needs `--yes`, 5 file error.

Example for an agent: `mdrewriter-cli detect --json`, then `mdrewriter-cli read-rom --size 4M --out game.bin --json`, then `mdrewriter-cli write-rom game.bin --yes --json` only after the user agreed.

Tests: `Software/tests/tst_cli` (fake firmware).
