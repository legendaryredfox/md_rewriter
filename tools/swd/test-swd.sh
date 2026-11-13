#!/usr/bin/env bash
# Tests for swd.sh with stub st-info/st-flash: no ST-Link, no hardware, nothing is flashed.
#   tools/swd/test-swd.sh
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SWD="$HERE/swd.sh"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
FAILS=0; N=0

# --- stubs. FAKE_FLASH is the "chip"; FAKE_PROBE=none|wrongchip|smallflash; FAKE_RDP=1 reads zeros;
#     FAKE_CORRUPT=1 flips a byte on write; FAKE_READFAIL=1 makes read fail.
mkdir -p "$T/bin"
cat > "$T/bin/st-info" <<'STUB'
#!/usr/bin/env bash
case "${FAKE_PROBE:-ok}" in
  none) echo "Found 0 stlink programmers" ;;
  wrongchip) printf 'Found 1 stlink programmers\n  flash: 131072 (pagesize: 1024)\n  chipid: 0x0414\n' ;;
  smallflash) printf 'Found 1 stlink programmers\n  flash: 65536 (pagesize: 1024)\n  chipid: 0x0410\n' ;;
  *) printf 'Found 1 stlink programmers\n  version: V2J37S7\n  flash: 131072 (pagesize: 1024)\n  sram: 20480\n  chipid: 0x0410\n  descr: F1 medium-density device\n' ;;
esac
STUB
cat > "$T/bin/st-flash" <<'STUB'
#!/usr/bin/env bash
args=(); for a in "$@"; do [ "$a" = "--reset" ] || args+=("$a"); done
case "${args[0]}" in
  read)
    [ "${FAKE_READFAIL:-0}" = 1 ] && exit 1
    if [ "${FAKE_RDP:-0}" = 1 ]; then head -c "${args[3]}" /dev/zero > "${args[1]}"
    else head -c "${args[3]}" "$FAKE_FLASH" > "${args[1]}"; fi ;;
  write)
    { cat "${args[1]}"; head -c 131072 /dev/zero | tr '\0' '\377'; } | head -c 131072 > "$FAKE_FLASH.new"
    if [ "${FAKE_CORRUPT:-0}" = 1 ]; then printf '\125' | dd of="$FAKE_FLASH.new" bs=1 seek=5000 conv=notrunc 2>/dev/null; fi
    mv "$FAKE_FLASH.new" "$FAKE_FLASH" ;;
esac
STUB
chmod +x "$T/bin/"*
export ST_FLASH="$T/bin/st-flash" ST_INFO="$T/bin/st-info" FAKE_FLASH="$T/chip.bin"

mkimg() {  # file size seed: sane vector table (SP 0x20005000, reset 0x08000101) then pseudo-random bytes
    { printf '\000\120\000\040\001\001\000\010'; head -c $(( $2 - 8 )) < <(openssl enc -aes-128-ctr -pass "pass:$3" -nosalt </dev/zero 2>/dev/null); } > "$1"
}
mkimg "$T/factory.bin" 29000 factory; { cat "$T/factory.bin"; head -c $((131072-29000)) /dev/zero | tr '\0' '\377'; } > "$T/factory128.bin"
mkimg "$T/new.bin" 29500 new

t() {  # name expected-exit-code command...
    local name="$1" want="$2"; shift 2
    local out rc
    out="$("$@" 2>&1)"; rc=$?
    N=$((N+1))
    if [ "$rc" -eq "$want" ]; then echo "PASS  $name"; else echo "FAIL  $name (exit $rc, wanted $want)"; echo "$out" | sed 's/^/      /'; FAILS=$((FAILS+1)); fi
    LAST="$out"
}
has() { N=$((N+1)); if grep -q -- "$2" <<< "$LAST"; then echo "PASS  $1"; else echo "FAIL  $1 (no \"$2\" in output)"; FAILS=$((FAILS+1)); fi; }

cp "$T/factory128.bin" "$FAKE_FLASH"
t "probe ok" 0 "$SWD" probe
FAKE_PROBE=none t "probe: no ST-Link" 1 "$SWD" probe; has "  says no ST-Link" "no ST-Link"
FAKE_PROBE=wrongchip t "probe: wrong chip id" 1 "$SWD" probe; has "  says chip id" "chip id"
FAKE_PROBE=smallflash t "probe: wrong flash size" 1 "$SWD" probe

t "backup ok" 0 "$SWD" backup --out "$T/b1"
cmp -s "$T/b1/fw-factory.bin" "$T/factory128.bin" && { N=$((N+1)); echo "PASS  backup equals the chip"; } || { N=$((N+1)); echo "FAIL  backup equals the chip"; FAILS=$((FAILS+1)); }
( cd "$T/b1" && sha256sum -c fw-factory.bin.sha256 >/dev/null 2>&1 ) && { N=$((N+1)); echo "PASS  backup .sha256 file is valid"; } || { N=$((N+1)); echo "FAIL  backup .sha256"; FAILS=$((FAILS+1)); }
t "backup refuses to overwrite" 1 "$SWD" backup --out "$T/b1"
FAKE_RDP=1 t "backup: read protected chip (zeros)" 1 "$SWD" backup --out "$T/b2"; has "  warns about read protection" "read protection"
FAKE_READFAIL=1 t "backup: read fails" 1 "$SWD" backup --out "$T/b3"
FAKE_PROBE=none t "backup: no ST-Link" 1 "$SWD" backup --out "$T/b4"

B="$T/b1/fw-factory.bin"
t "flash refuses without --yes" 1 "$SWD" flash "$T/new.bin" --backup "$B"; has "  says pass --yes" "pass --yes"
t "flash refuses without --backup" 1 "$SWD" flash "$T/new.bin" --yes
t "flash refuses a missing backup" 1 "$SWD" flash "$T/new.bin" --backup "$T/none.bin" --yes
cp "$B" "$T/nosha.bin"; t "flash refuses a backup without .sha256" 1 "$SWD" flash "$T/new.bin" --backup "$T/nosha.bin" --yes
cp "$B" "$T/mod.bin"; cp "$B.sha256" "$T/mod.bin.sha256"; printf 'x' | dd of="$T/mod.bin" bs=1 seek=100 conv=notrunc 2>/dev/null
t "flash refuses a modified backup" 1 "$SWD" flash "$T/new.bin" --backup "$T/mod.bin" --yes; has "  says changed" "changed"
head -c 4096 /dev/zero > "$T/tiny.bin"; t "flash refuses an implausible image" 1 "$SWD" flash "$T/tiny.bin" --backup "$B" --yes
head -c 20000 /dev/urandom > "$T/novec.bin"; t "flash refuses an image without vector table" 1 "$SWD" flash "$T/novec.bin" --backup "$B" --yes
FAKE_PROBE=wrongchip t "flash refuses a wrong chip" 1 "$SWD" flash "$T/new.bin" --backup "$B" --yes
cp "$T/factory128.bin" "$FAKE_FLASH"; printf 'Z' | dd of="$FAKE_FLASH" bs=1 seek=777 conv=notrunc 2>/dev/null
t "flash refuses when the chip is not the backed up unit" 1 "$SWD" flash "$T/new.bin" --backup "$B" --yes; has "  says another unit" "another unit"
cmp -s "$FAKE_FLASH" "$T/factory128.bin" && { N=$((N+1)); echo "FAIL  the refused flash left the chip unchanged?"; FAILS=$((FAILS+1)); } || { N=$((N+1)); echo "PASS  refused flashes did not touch the chip"; }

cp "$T/factory128.bin" "$FAKE_FLASH"
FAKE_CORRUPT=1 t "flash: read back mismatch is detected" 1 "$SWD" flash "$T/new.bin" --backup "$B" --yes; has "  points to restore" "restore"
cp "$T/factory128.bin" "$FAKE_FLASH"
t "flash ok" 0 "$SWD" flash "$T/new.bin" --backup "$B" --yes; has "  says verified" "written and verified"
cmp -s -n 29500 "$FAKE_FLASH" "$T/new.bin" && { N=$((N+1)); echo "PASS  chip holds the new image"; } || { N=$((N+1)); echo "FAIL  chip holds the new image"; FAILS=$((FAILS+1)); }
t "flash again with the same image is a no-op" 0 "$SWD" flash "$T/new.bin" --backup "$B" --yes; has "  says already holds" "already holds"
t "restore refuses without --yes" 1 "$SWD" restore --backup "$B"
t "restore ok" 0 "$SWD" restore --backup "$B" --yes
cmp -s "$FAKE_FLASH" "$T/factory128.bin" && { N=$((N+1)); echo "PASS  restore brought back the factory image"; } || { N=$((N+1)); echo "FAIL  restore brought back the factory image"; FAILS=$((FAILS+1)); }

echo; echo "$N checks, $FAILS failed"
exit $(( FAILS > 0 ))
