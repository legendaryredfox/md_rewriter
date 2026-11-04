#!/usr/bin/env python3
"""Bench test suite for the md_rewriter hardware (dumper + firmware).

READ ONLY. This suite never writes or erases a cartridge. Every device command goes through
run_cli(), which refuses anything outside READ_ONLY_COMMANDS and never passes --yes. The
only destructive path in the project (write-rom, write-save, erase) is not reachable from here.

Phases
  host      no hardware: self-check of the guard and of the CLI binary
  nocart    dumper connected, slot EMPTY
  cart      dumper connected, a cartridge inserted (any cartridge; nothing is changed on it)

Usage
  tools/hwtest/hwtest.py host
  tools/hwtest/hwtest.py nocart [--port ttyACM0]
  tools/hwtest/hwtest.py cart   [--port ttyACM0] [--save-size 8K|32K] [--rom-size 4M] [--label "my cart"]

Output: a table on stdout (use a new --out folder each run: the CLI will not overwrite dumps), and a report in test-results/<timestamp>-<phase>/ (report.json, report.md,
the dumps). Exit code 0 only if no check failed. SKIP is not a failure; INFO records a value.
Stdlib only.
"""
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
READ_ONLY_COMMANDS = {"ports", "chips", "rominfo", "compare", "info", "detect", "read-rom", "read-save", "verify"}
FORBIDDEN_ARGS = {"--yes", "-y"}
CLI_CANDIDATES = [ROOT / "build" / "cli" / "mdrewriter-cli", ROOT / "build" / "cli" / "mdrewriter-cli.exe"]
FW_USB_ID = ("0483", "5740")


class Guard(Exception):
    pass


def find_cli():
    env = os.environ.get("MDREWRITER_CLI")
    if env:
        return env
    for c in CLI_CANDIDATES:
        if c.exists():
            return str(c)
    found = shutil.which("mdrewriter-cli")
    if found:
        return found
    raise SystemExit("mdrewriter-cli not found: run tools/build.sh app (or set MDREWRITER_CLI)")


def run_cli(args, timeout=300):
    """Runs the CLI with --json. Refuses anything that could change a cartridge."""
    cmd = [a for a in args if not a.startswith("-")][:1]
    if not cmd or cmd[0] not in READ_ONLY_COMMANDS:
        raise Guard("command not allowed in the read-only suite: %r" % (args,))
    if any(a in FORBIDDEN_ARGS for a in args):
        raise Guard("--yes is not allowed in the read-only suite: %r" % (args,))
    t0 = time.monotonic()
    p = subprocess.run([find_cli()] + args + ["--json", "-q"], capture_output=True, text=True, timeout=timeout)
    dt = time.monotonic() - t0
    try:
        data = json.loads(p.stdout.strip().splitlines()[-1])
    except (ValueError, IndexError):
        data = {"ok": False, "error": "no JSON from CLI", "stderr": p.stderr[-400:]}
    data["_seconds"] = round(dt, 2)
    data["_rc"] = p.returncode
    return data


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


class Report:
    def __init__(self, phase, outdir):
        self.phase, self.outdir, self.rows = phase, outdir, []

    def add(self, status, name, detail=""):
        self.rows.append({"status": status, "name": name, "detail": detail})
        print("%-5s %-44s %s" % (status, name, detail))

    def check(self, cond, name, detail=""):
        self.add("PASS" if cond else "FAIL", name, detail)
        return cond

    def info(self, name, detail):
        self.add("INFO", name, str(detail))

    def skip(self, name, why):
        self.add("SKIP", name, why)

    @property
    def failed(self):
        return any(r["status"] == "FAIL" for r in self.rows)

    def save(self, extra):
        self.outdir.mkdir(parents=True, exist_ok=True)
        data = {"phase": self.phase, "when": time.strftime("%Y-%m-%d %H:%M:%S"), "extra": extra, "rows": self.rows,
                "failed": self.failed}
        (self.outdir / "report.json").write_text(json.dumps(data, indent=2))
        lines = ["# md_rewriter bench test: %s" % self.phase, "", "Date: %s" % data["when"], ""]
        for k, v in extra.items():
            lines.append("- %s: %s" % (k, v))
        lines += ["", "| Status | Check | Detail |", "|---|---|---|"]
        for r in self.rows:
            lines.append("| %s | %s | %s |" % (r["status"], r["name"], r["detail"].replace("|", "/")))
        (self.outdir / "report.md").write_text("\n".join(lines) + "\n")
        print("\nreport: %s" % self.outdir)


# ------------------------------------------------------------------ phases

def phase_host(rep, a):
    for bad in (["write-rom", "x.bin"], ["erase", "all"], ["write-save", "x"], ["read-rom", "--yes"], ["info", "-y"]):
        try:
            run_cli(bad)
            rep.check(False, "guard refuses %s" % bad[0])
        except Guard:
            rep.check(True, "guard refuses %s" % " ".join(bad))
    r = run_cli(["chips"])
    rep.check(r.get("ok") and len(r.get("chips", [])) >= 8, "cli runs, chip table", "%d chips" % len(r.get("chips", [])))
    r = run_cli(["ports"])
    rep.check(r.get("ok"), "cli lists ports", "%d ports" % len(r.get("ports", [])))


def pick_port(rep, a):
    r = run_cli(["ports"])
    ports = r.get("ports", [])
    mine = [p for p in ports if p.get("mdrewriter")]
    rep.check(len(mine) >= 1 or bool(a.port), "dumper enumerates as %s:%s" % FW_USB_ID,
              ", ".join(p["name"] for p in mine) or "none (use --port)")
    return a.port or (mine[0]["location"] if len(mine) == 1 else None)


def common_link(rep, a, port):
    pa = ["--port", port] if port else []
    r = run_cli(["info"] + pa)
    ok = rep.check(r.get("ok"), "connect + banner", r.get("firmware", r.get("error", "")))
    if ok:
        rep.info("firmware version", r.get("firmware"))
    times = []
    fails = 0
    for _ in range(10):
        x = run_cli(["info"] + pa)
        times.append(x["_seconds"])
        fails += 0 if x.get("ok") else 1
    rep.check(fails == 0, "10 reconnects in a row", "max %.2fs, %d failed" % (max(times), fails))
    return pa, r.get("firmware")


def phase_nocart(rep, a):
    port = pick_port(rep, a)
    pa, _fw = common_link(rep, a, port)
    d = run_cli(["detect"] + pa)
    rep.check(d.get("ok"), "detect answers with the slot empty", "flashId=%s" % d.get("flashId"))
    rep.check(not d.get("chip"), "empty slot is not reported as a known chip", d.get("chip") or "no chip")
    h = []
    for i in (1, 2):
        out = rep.outdir / ("empty_%d.bin" % i)
        rep.outdir.mkdir(parents=True, exist_ok=True)
        r = run_cli(["read-rom", "--size", "512K", "--out", str(out)] + pa)
        if not rep.check(r.get("ok"), "read 512K from empty slot #%d" % i, "%.1fs" % r["_seconds"]):
            return
        h.append(sha256(out))
    rep.check(h[0] == h[1], "two reads of the empty slot are identical (bus is stable)", h[0][:16])
    data = (rep.outdir / "empty_1.bin").read_bytes()
    ff = data.count(b"\xff") / len(data)
    rep.info("fraction of 0xFF with no cartridge", "%.3f (pull-ups on D0-D15 expected to give ~1.0)" % ff)
    rep.info("empty-slot sha256", h[0])


def phase_cart(rep, a):
    port = pick_port(rep, a)
    pa, fw = common_link(rep, a, port)
    d = run_cli(["detect"] + pa)
    rep.check(d.get("ok"), "detect", "flashId=%s chip=%s firmwareKnows=%s" % (d.get("flashId"), d.get("chip"), d.get("firmwareKnowsChip")))
    rep.outdir.mkdir(parents=True, exist_ok=True)
    sizes = {"512K": 0x80000, "1M": 0x100000, "2M": 0x200000, "4M": 0x400000}
    sz = a.rom_size
    hashes = []
    for i in (1, 2):
        out = rep.outdir / ("rom_%s_%d.bin" % (sz, i))
        r = run_cli(["read-rom", "--size", sz, "--out", str(out)] + pa, timeout=900)
        if not rep.check(r.get("ok"), "read ROM %s #%d" % (sz, i), "%.1fs" % r["_seconds"]):
            return
        if i == 1:
            rep.info("read speed", "%.0f KiB/s" % (sizes[sz] / 1024 / max(r["_seconds"], 0.01)))
        hashes.append(sha256(out))
    rep.check(hashes[0] == hashes[1], "two ROM reads are identical", hashes[0][:16])
    rep.info("ROM sha256", hashes[0])
    data = (rep.outdir / ("rom_%s_1.bin" % sz)).read_bytes()
    rep.check(len(data) == sizes[sz], "dump has the requested size", "%d bytes" % len(data))
    # Not a pass/fail check: a blank flash cart or a non-MD cart has no header.
    rep.info("header bytes at 0x100", ("SEGA header present: " if data[0x100:0x103] == b"SEG" else "no SEGA header: ")
             + data[0x100:0x110].decode("latin1", "replace"))
    name = data[0x120:0x150].decode("latin1", "replace").strip()
    rep.info("header domestic name", name)
    # A smaller read must be the prefix of a bigger one (same chip, no address-line fault).
    if sz != "512K":
        out = rep.outdir / "rom_512K.bin"
        r = run_cli(["read-rom", "--size", "512K", "--out", str(out)] + pa)
        if rep.check(r.get("ok"), "read 512K for prefix check"):
            rep.check(out.read_bytes() == data[:0x80000], "512K read equals the first 512K of the %s read" % sz)
    # verify uses the normal read path and only reads.
    r = run_cli(["verify", str(rep.outdir / ("rom_%s_1.bin" % sz)), "--size", sz] + pa, timeout=900)
    rep.check(r.get("ok"), "verify against the file just dumped (read only)")
    # Address line check: a long run of identical 32 KiB blocks across the whole dump can hide a stuck line.
    blocks = [data[i:i + 0x8000] for i in range(0, len(data), 0x8000)]
    distinct = len(set(blocks))
    rep.info("distinct 32 KiB blocks", "%d of %d (all equal would mean a stuck address line or a mirror)" % (distinct, len(blocks)))
    if a.save_size:
        hs = []
        for i in (1, 2):
            out = rep.outdir / ("save_%s_%d.srm" % (a.save_size, i))
            r = run_cli(["read-save", "--size", a.save_size, "--out", str(out)] + pa)
            if not rep.check(r.get("ok"), "read save %s #%d" % (a.save_size, i)):
                break
            hs.append(sha256(out))
        if len(hs) == 2:
            rep.check(hs[0] == hs[1], "two save reads are identical", hs[0][:16])
    else:
        rep.skip("save RAM reads", "pass --save-size 8K or 32K for a cartridge with a battery save")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("phase", choices=["host", "nocart", "cart"])
    ap.add_argument("--port")
    ap.add_argument("--rom-size", default="4M", choices=["512K", "1M", "2M", "4M"])
    ap.add_argument("--save-size", choices=["8K", "32K"])
    ap.add_argument("--label", default="")
    ap.add_argument("--out", help="result folder (default test-results/<time>-<phase>)")
    a = ap.parse_args()
    outdir = Path(a.out) if a.out else ROOT / "test-results" / (time.strftime("%Y%m%d-%H%M%S") + "-" + a.phase)
    rep = Report(a.phase, outdir)
    print("md_rewriter bench test, phase %s (read only)\n" % a.phase)
    try:
        {"host": phase_host, "nocart": phase_nocart, "cart": phase_cart}[a.phase](rep, a)
    except Guard as g:
        rep.add("FAIL", "guard", str(g))
    except subprocess.TimeoutExpired as t:
        rep.add("FAIL", "timeout", str(t))
    rep.save({"label": a.label, "cli": find_cli()})
    sys.exit(1 if rep.failed else 0)


if __name__ == "__main__":
    main()
