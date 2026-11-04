# Bench tests for when the hardware arrives

`tools/hwtest/hwtest.py` (Python 3, stdlib only) drives `mdrewriter-cli`. **It is read only.** It never writes or erases: every device command passes through a guard that allows only `ports chips rominfo compare info detect read-rom read-save verify` and refuses `--yes`. The cartridge in the slot is never changed. (`read-save` writes the cartridge's RAM-enable register, as every dump does, not its contents.)

Build the CLI first: `tools/build.sh app`.

| Phase | Setup | Command |
|-------|-------|---------|
| `host` | no hardware | `tools/hwtest/hwtest.py host` (guard test, CLI runs) |
| `nocart` | dumper plugged in, slot empty | `tools/hwtest/hwtest.py nocart` |
| `cart` | dumper plugged in, any cartridge | `tools/hwtest/hwtest.py cart --rom-size 4M [--save-size 8K\|32K] [--label "name"]` |

Checks, `nocart`: dumper enumerates as `0483:5740`; connect and banner; 10 reconnects; detect answers and does not name a chip; two 512K reads of the empty slot are identical; fraction of `0xFF` (pull-ups).

Checks, `cart`: the same link checks; detect (ID, chip name, whether the firmware knows it); two ROM reads identical, size, `SEGA` header, header name; 512K read equals the prefix of the big read; `verify` against the dump just taken; number of distinct 32 KiB blocks (a stuck address line gives repeats); two save reads identical; read speed.

Results: `test-results/<time>-<phase>/report.md` and `report.json`, plus the dumps (folder is git-ignored). Exit code 0 only when nothing failed. Keep the `report.md` files: they are the evidence for the checklist in `docs/NEW_FIRMWARE_PLAN.md` step 5 (rows 3, 8, 9).

What it does not do on purpose: writing, erasing, timing with a logic analyzer (plan step 3), flashing the firmware (plan steps 2 and 4). Those are manual, on a sacrificial cartridge.
