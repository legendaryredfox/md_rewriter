#!/usr/bin/env bash
# SWD session helper for the md_rewriter dumper (STM32F103VB, 128 KiB flash) with an ST-Link V2.
# Runbook: docs/SWD_SESSION.md. Nothing here runs on its own: `flash` and `restore` write to the
# microcontroller and need --yes, a good backup, and a probe that shows the expected chip.
#
#   swd.sh build                              build both firmware 1.1 images (poll, nopoll)
#   swd.sh probe                              ST-Link and chip check, reads nothing from the flash
#   swd.sh backup [--out DIR]                 read the flash twice, check it, save it with a sha256 file
#   swd.sh flash IMAGE.bin --backup FILE --yes   write IMAGE at 0x08000000, read back, compare, reset
#   swd.sh restore --backup FILE --yes        write the backup back, read back, compare, reset
#
# Environment: ST_FLASH, ST_INFO (default st-flash, st-info), used by the tests to stub the tools.
set -u
export LC_ALL=C

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
ST_FLASH="${ST_FLASH:-st-flash}"
ST_INFO="${ST_INFO:-st-info}"
FLASH_BASE=0x08000000
# Accepted chips: id 0x410 = STM32F103xB, 128 KiB (what the PCB design says); id 0x414 = STM32F103xE,
# 512 KiB (what the first unit on the bench really has, 2026-11-13). FLASH_SIZE is set by probe.
FLASH_SIZE=131072
CHIPID=0x410
# The SWD link on the bench unit drops now and then (chip id 0x0, "Failed to connect"). Connection steps are
# retried this many times, RETRY_SLEEP seconds apart. A retried write is safe: it is read back and compared.
TRIES="${SWD_TRIES:-6}"
RETRY_SLEEP="${SWD_RETRY_SLEEP:-1}"

die() { echo "swd: $*" >&2; exit 1; }

sha() { sha256sum "$1" | cut -d' ' -f1; }

# True when the file is all 0x00 or all 0xFF (what a read-protected or an erased chip returns).
blank() {
    local f="$1"
    [ "$(tr -d '\377' < "$f" | wc -c)" -eq 0 ] && return 0
    [ "$(tr -d '\000' < "$f" | wc -c)" -eq 0 ] && return 0
    return 1
}

# First word (initial SP) must point into the RAM (20 or 64 KiB), the reset vector into the flash.
vectors_ok() {
    local f="$1" sp rv
    sp=$(od -An -tx4 -j0 -N4 "$f" | tr -d ' ')
    rv=$(od -An -tx4 -j4 -N4 "$f" | tr -d ' ')
    [[ "$sp" =~ ^200[01][0-9a-f]{4}$ ]] || return 1
    [[ "$rv" =~ ^0800[0-9a-f]{4}$ ]] || return 1
    return 0
}

probe() {
    local out id size n
    for n in $(seq 1 "$TRIES"); do
        out="$("$ST_INFO" --probe 2>&1)" || true
        grep -q "Found [1-9]" <<< "$out" || { echo "$out" >&2; die "no ST-Link found (cable, USB port, or the clone needs a replug)"; }
        id=$(grep -iE "chipid:" <<< "$out" | head -1 | sed -E 's/.*chipid: *0x0*([0-9a-fA-F]+).*/\1/')
        # chip id 0 with no flash: the link dropped, not a different chip. Try again.
        if [ -n "$id" ] && [ "$id" != "0" ]; then break; fi
        [ "$n" -lt "$TRIES" ] && sleep "$RETRY_SLEEP"
    done
    echo "$out"
    size=$(grep -E "flash:" <<< "$out" | head -1 | sed -E 's/.*flash: *([0-9]+).*/\1/')
    case "$id:$size" in
        410:131072) CHIPID=0x410; FLASH_SIZE=131072 ;;
        414:524288) CHIPID=0x414; FLASH_SIZE=524288 ;;
        *) die "chip id 0x$id with $size bytes of flash is not a known STM32F103 (0x410/128 KiB or 0x414/512 KiB): wrong target or wiring" ;;
    esac
    echo "swd: probe ok ($CHIPID, $FLASH_SIZE bytes)"
}

read_flash() {  # file
    local n
    for n in $(seq 1 "$TRIES"); do
        rm -f "$1"
        if "$ST_FLASH" read "$1" "$FLASH_BASE" "$FLASH_SIZE" >/dev/null 2>&1 && [ "$(stat -c %s "$1")" -eq "$FLASH_SIZE" ]; then return 0; fi
        [ "$n" -lt "$TRIES" ] && sleep "$RETRY_SLEEP"
    done
    return 1
}

check_backup() {  # file: size, sidecar hash, not blank, vectors
    local f="$1"
    [ -f "$f" ] || die "backup file not found: $f"
    [ "$(stat -c %s "$f")" -eq "$FLASH_SIZE" ] || die "backup is not $FLASH_SIZE bytes"
    [ -f "$f.sha256" ] || die "backup has no $f.sha256 (made by 'swd.sh backup'?)"
    [ "$(cut -d' ' -f1 < "$f.sha256")" = "$(sha "$f")" ] || die "backup does not match its .sha256: it was changed"
    blank "$f" && die "backup is blank: the chip was read protected or the read failed"
    vectors_ok "$f" || die "backup has no sane vector table"
}

cmd_build() {
    ( cd "$ROOT/firmware_1.1" || exit 1
      make -s BUILD=build-poll FW_VERSION=1.1.0-test OPT=-O1 || exit 1
      make -s BUILD=build-nopoll FW_VERSION=1.1.0-nopoll OPT="-O1 -DFLASH_POLL=0" || exit 1 ) || die "build failed"
    for d in build-poll build-nopoll; do
        f="$ROOT/firmware_1.1/$d/md-rewriter-new.bin"
        echo "$d: $(stat -c %s "$f") bytes, sha256 $(sha "$f")"
    done
}

cmd_backup() {
    local out="$ROOT/test-results/swd-$(date +%Y%m%d-%H%M%S)"
    while [ $# -gt 0 ]; do
        case "$1" in
            --out) out="$2"; shift 2 ;;
            *) die "backup: unknown option $1" ;;
        esac
    done
    probe >/dev/null || exit 1
    mkdir -p "$out"
    local a="$out/fw-factory.bin" b="$out/fw-factory.second-read.bin"
    [ -e "$a" ] && die "$a exists, pick another --out"
    read_flash "$a" || die "reading the flash failed"
    read_flash "$b" || die "second read failed"
    cmp -s "$a" "$b" || die "the two reads differ: bad wiring or power, do not flash"
    blank "$a" && { rm -f "$b"; die "the read is blank: read protection is probably on. Do NOT erase or flash; see docs/SWD_SESSION.md"; }
    vectors_ok "$a" || die "no sane vector table in the read ($(od -An -tx4 -N8 "$a")): do not flash"
    rm -f "$b"
    ( cd "$out" && sha256sum fw-factory.bin > fw-factory.bin.sha256 )
    local last
    last=$(tr -d '\377' < "$a" | wc -c)
    echo "swd: backup ok: $a"
    echo "swd: sha256 $(sha "$a"), about $last non-0xFF bytes, first words: $(od -An -tx4 -N8 "$a")"
    echo "swd: copy it somewhere else too (docs/SWD_SESSION.md, step 3)"
}

write_and_verify() {  # image
    local img="$1" tmp rb
    tmp="$(mktemp -d)"; rb="$tmp/readback.bin"
    local n w
    for n in $(seq 1 "$TRIES"); do
        w="$("$ST_FLASH" --reset write "$img" "$FLASH_BASE" 2>&1)"; echo "$w" | tail -3
        grep -q "Failed to connect\|Failed to enter" <<< "$w" || break
        echo "swd: link dropped before the write ($n/$TRIES), trying again"; sleep "$RETRY_SLEEP"
    done
    if ! read_flash "$rb"; then rm -rf "$tmp"; die "read back failed after writing: the chip may be in an unknown state, run 'swd.sh restore'"; fi
    local n; n=$(stat -c %s "$img")
    if ! cmp -s -n "$n" "$img" "$rb"; then rm -rf "$tmp"; die "read back differs from the image: run 'swd.sh restore --backup ... --yes'"; fi
    rm -rf "$tmp"
    echo "swd: written and verified ($n bytes), reset sent"
}

cmd_flash() {
    local img="" backup="" yes=0
    while [ $# -gt 0 ]; do
        case "$1" in
            --backup) backup="$2"; shift 2 ;;
            --yes) yes=1; shift ;;
            -*) die "flash: unknown option $1" ;;
            *) img="$1"; shift ;;
        esac
    done
    [ -n "$img" ] && [ -n "$backup" ] || die "usage: swd.sh flash IMAGE.bin --backup FILE --yes"
    [ "$yes" -eq 1 ] || die "refusing: this overwrites the firmware, pass --yes"
    [ -f "$img" ] || die "image not found: $img"
    local sz; sz=$(stat -c %s "$img")
    [ "$sz" -gt 4096 ] && [ "$sz" -le "$FLASH_SIZE" ] || die "image size $sz is not plausible (4 KiB to $FLASH_SIZE)"
    vectors_ok "$img" || die "the image has no sane vector table"
    probe >/dev/null || exit 1
    check_backup "$backup"
    # The chip must still hold the firmware that was backed up. A different content means the backup is of another unit.
    local now; now="$(mktemp)"
    read_flash "$now" || { rm -f "$now"; die "reading the chip before flashing failed"; }
    if ! cmp -s "$now" "$backup"; then
        if cmp -s -n "$sz" "$now" "$img"; then echo "swd: the chip already holds this image"; rm -f "$now"; exit 0; fi
        rm -f "$now"; die "the chip does not hold the backed up firmware: it is another unit or a changed one. Make a new backup first"
    fi
    rm -f "$now"
    echo "swd: flashing $img (sha256 $(sha "$img"))"
    write_and_verify "$img"
}

cmd_restore() {
    local backup="" yes=0
    while [ $# -gt 0 ]; do
        case "$1" in
            --backup) backup="$2"; shift 2 ;;
            --yes) yes=1; shift ;;
            *) die "restore: unknown option $1" ;;
        esac
    done
    [ -n "$backup" ] || die "usage: swd.sh restore --backup FILE --yes"
    [ "$yes" -eq 1 ] || die "refusing: this overwrites the firmware, pass --yes"
    probe >/dev/null || exit 1
    check_backup "$backup"
    echo "swd: restoring $backup"
    write_and_verify "$backup"
}

case "${1:-}" in
    build) shift; cmd_build "$@" ;;
    probe) shift; probe ;;
    backup) shift; cmd_backup "$@" ;;
    flash) shift; cmd_flash "$@" ;;
    restore) shift; cmd_restore "$@" ;;
    *) sed -n '2,13p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 2 ;;
esac
