#!/usr/bin/env bash
# Build script for md_rewriter.
# Usage: tools/build.sh [app] [firmware] [tests] [all] [clean]
#   app        Qt app (qmake6 + make) -> build/app/MDRewriter and build/cli/mdrewriter-cli
#   firmware   firmware_1.1 (arm-none-eabi-gcc) -> firmware_1.1/build/md-rewriter-new.{elf,hex,bin}
#   tests      builds and runs the app tests (unit, integration, end to end)
#   all        app + firmware (default when no target is given)
#   clean      removes build/app and firmware_1.1/build
# Variables: JOBS (default: nproc), QMAKE (default: qmake6), FW_VERSION, OPT (passed to the firmware make).
# legacy_firmware is frozen and Keil-only: not built here. Nothing here flashes anything.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
JOBS="${JOBS:-$(nproc)}"
QMAKE="${QMAKE:-qmake6}"

need() {
  command -v "$1" >/dev/null 2>&1 || { echo "error: '$1' not found. $2" >&2; exit 1; }
}

build_app() {
  need "$QMAKE" "sudo apt install build-essential qt6-base-dev qt6-serialport-dev qt6-tools-dev qt6-l10n-tools"
  need make "sudo apt install build-essential"
  echo "==> app"
  mkdir -p "$ROOT/build/app"
  cd "$ROOT/build/app"
  "$QMAKE" "$ROOT/Software/MDRewriter.pro"
  make -j"$JOBS"
  echo "app: $ROOT/build/app/MDRewriter"
  echo "==> cli"
  mkdir -p "$ROOT/build/cli"
  cd "$ROOT/build/cli"
  "$QMAKE" "$ROOT/Software/cli/mdrewriter-cli.pro"
  make -j"$JOBS"
  echo "cli: $ROOT/build/cli/mdrewriter-cli"
}

build_firmware() {
  need arm-none-eabi-gcc "sudo apt install gcc-arm-none-eabi"
  need make "sudo apt install build-essential"
  echo "==> firmware (firmware_1.1)"
  local args=(-j"$JOBS")
  [ -n "${FW_VERSION:-}" ] && args+=("FW_VERSION=$FW_VERSION")
  [ -n "${OPT:-}" ] && args+=("OPT=$OPT")
  make -C "$ROOT/firmware_1.1" "${args[@]}"
  echo "firmware: $ROOT/firmware_1.1/build/md-rewriter-new.{elf,hex,bin}"
}

run_tests() {
  need "$QMAKE" "sudo apt install build-essential qt6-base-dev qt6-serialport-dev qt6-tools-dev qt6-l10n-tools"
  need make "sudo apt install build-essential"
  echo "==> tests"
  mkdir -p "$ROOT/build/tests"
  cd "$ROOT/build/tests"
  "$QMAKE" "$ROOT/Software/tests/tests.pro"
  make -j"$JOBS"
  make check
}

clean() {
  echo "==> clean"
  rm -rf "$ROOT/build/app" "$ROOT/build/cli" "$ROOT/build/tests"
  [ -f "$ROOT/firmware_1.1/Makefile" ] && make -C "$ROOT/firmware_1.1" clean
}

targets=("$@")
[ "${#targets[@]}" -eq 0 ] && targets=(all)

for t in "${targets[@]}"; do
  case "$t" in
    app)      build_app ;;
    firmware) build_firmware ;;
    tests)    run_tests ;;
    all)      build_app; build_firmware ;;
    clean)    clean ;;
    -h|--help|help) sed -n '2,10p' "$0" ;;
    *) echo "unknown target: $t (use: app firmware tests all clean)" >&2; exit 2 ;;
  esac
done
