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
| `verify F [--size S]` | yes | no | read the cartridge back and compare with F |
| `write-rom F --yes [--no-verify]` | yes | **yes** | write a flash cartridge, read back by default |
| `write-save F --yes` | yes | **yes** | overwrite save RAM |
| `erase all\|512K\|1M\|2M\|4M\|8M --yes` | yes | **yes** | erase |

Options: `--port NAME` (default: the only port with the dumper USB id, or `MD_REWRITER_EXTRA_PORT`), `--json`, `--yes`, `--dry-run`, `--timeout S`, `--quiet`.

Safety:
- A command that changes the cartridge refuses without `--yes` (exit 4). `--dry-run` checks arguments and files and never opens the port.
- `read-rom` and `read-save` do not overwrite an existing output file without `--yes`; the file is written atomically.
- Files are checked before the port is opened.

Output: messages and progress on stderr; with `--json` one JSON object on stdout (`ok`, `exit`, `command`, `error`, `messages`, plus command fields such as `flashId`, `chip`, `firmware`, `bytes`, `verified`, `identical`).

Exit codes: 0 ok, 1 operation failed (device error, verify mismatch, files differ), 2 usage, 3 no device, 4 needs `--yes`, 5 file error.

Example for an agent: `mdrewriter-cli detect --json`, then `mdrewriter-cli read-rom --size 4M --out game.bin --json`, then `mdrewriter-cli write-rom game.bin --yes --json` only after the user agreed.

Tests: `Software/tests/tst_cli` (fake firmware).
