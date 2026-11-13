#!/usr/bin/env python3
"""Finish a ROM write that firmware 1.0 left half done, on a chip it programs too fast for.

Bench, 2026-11-13 (docs/HWTEST.md): firmware 1.0 does not wait for the chip to finish each word. On an
S29GL064N it programmed every other word; the words it missed stayed blank (0xFFFF), none came out wrong.
Firmware 1.1 polls the chip and does not need this tool.

What it does, per pass: read the cartridge back, build a patch file in which every word that is already
right (or blank in the file) is 0xFFFF, which the firmware skips, and write the patch without erasing. The
missing words get programmed. It repeats until the read back equals the file.

It writes to the cartridge, so it needs --yes, and it stops without writing if it finds a word that is
neither blank nor equal to the file (then the cartridge needs an erase, which this tool never does).
Stdlib only. Needs mdrewriter-cli (tools/build.sh app).

  tools/repair-write-fw10.py game.bin --port ttyACM0 --size 2M --yes
  tools/repair-write-fw10.py --selftest
"""
import argparse
import os
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SIZES = {"512K": 0x80000, "1M": 0x100000, "2M": 0x200000, "4M": 0x400000}


def plan(src, cart):
    """Returns (patch, todo, foreign). patch has 0xFFFF everywhere except the words still to program.
    foreign counts words that are not blank and differ from the file: a patch cannot fix those."""
    patch = bytearray(b"\xff" * len(src))
    todo = foreign = 0
    for i in range(0, len(src), 2):
        want, have = src[i:i + 2], cart[i:i + 2]
        if have == want:
            continue
        if have != b"\xff\xff":
            foreign += 1
        elif want != b"\xff\xff":
            patch[i:i + 2] = want
            todo += 1
    return bytes(patch), todo, foreign


def selftest():
    src = bytes.fromhex("1234 5678 ffff 9abc deef 0001".replace(" ", ""))
    cart = bytes.fromhex("1234 ffff ffff ffff deef ffff".replace(" ", ""))
    patch, todo, foreign = plan(src, cart)
    assert todo == 3 and foreign == 0, (todo, foreign)
    assert patch == bytes.fromhex("ffff 5678 ffff 9abc ffff 0001".replace(" ", "")), patch.hex()
    bad = bytes.fromhex("1234 0000 ffff ffff deef ffff".replace(" ", ""))
    assert plan(src, bad)[2] == 1
    assert plan(src, src) == (b"\xff" * len(src), 0, 0)
    print("selftest ok")


def find_cli():
    for c in (os.environ.get("MDREWRITER_CLI"), str(ROOT / "build" / "cli" / "mdrewriter-cli")):
        if c and Path(c).exists():
            return c
    sys.exit("mdrewriter-cli not found: run tools/build.sh app (or set MDREWRITER_CLI)")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("file", nargs="?")
    ap.add_argument("--port", default=None)
    ap.add_argument("--size", choices=sorted(SIZES), help="read back this much (at least the file size)")
    ap.add_argument("--yes", action="store_true", help="allow writing to the cartridge")
    ap.add_argument("--max-passes", type=int, default=40)
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    if not a.file or not a.size:
        ap.error("file and --size are required")
    if not a.yes:
        sys.exit("refusing: this writes to the cartridge, pass --yes")
    src = Path(a.file).read_bytes()
    if len(src) % 2 or len(src) > SIZES[a.size]:
        sys.exit("the file must have an even size and fit in --size")
    cli = find_cli()
    pa = ["--port", a.port] if a.port else []

    def run(*args):
        r = subprocess.run([cli, *pa, "--json", "-q", "--timeout", "120", *args], capture_output=True, text=True)
        time.sleep(1)   # firmware 1.0 can ignore a connect that follows at once
        return r

    with tempfile.TemporaryDirectory() as tmp:
        rb, pf = os.path.join(tmp, "readback.bin"), os.path.join(tmp, "patch.bin")
        for n in range(1, a.max_passes + 1):
            r = run("read-rom", "--size", a.size, "--out", rb)
            if r.returncode:
                sys.exit("read failed: " + r.stdout[-300:])
            patch, todo, foreign = plan(src, Path(rb).read_bytes()[:len(src)])
            print("pass %d: %d words to program, %d foreign" % (n, todo, foreign), flush=True)
            if foreign:
                sys.exit("stopped: the cartridge holds words that are neither blank nor the file. Erase it first.")
            if todo == 0:
                print("done: the cartridge equals the file")
                return
            Path(pf).write_bytes(patch)
            r = run("write-rom", pf, "--yes", "--no-verify")
            if r.returncode:
                sys.exit("write failed: " + r.stdout[-500:])
        sys.exit("not converged after %d passes" % a.max_passes)


if __name__ == "__main__":
    main()
