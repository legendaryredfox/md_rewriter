#!/usr/bin/env bash
# Builds a .deb of the desktop app and the command line tool (release build, installs under /usr).
# Usage: tools/package-deb.sh            -> build/dist/mdrewriter_<version>_<arch>.deb (+ .sha256)
# Needs: qmake6 make g++ qt6-base-dev qt6-serialport-dev qt6-tools-dev qt6-l10n-tools dpkg-dev fakeroot
# Variables: MAINTAINER ("Name <mail>", default: from git config), JOBS (default: nproc).
# Nothing here installs anything on this machine.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
JOBS="${JOBS:-$(nproc)}"
VERSION="$(sed -n 's/^VERSION *= *//p' "$ROOT/Software/MDRewriter.pro")"
ARCH="$(dpkg --print-architecture)"
MAINTAINER="${MAINTAINER:-$(git -C "$ROOT" config user.name) <$(git -C "$ROOT" config user.email)>}"

for tool in qmake6 make dpkg-deb fakeroot strip; do
  command -v "$tool" >/dev/null 2>&1 || { echo "error: '$tool' not found (see the header of this script)" >&2; exit 1; }
done
[ -n "$VERSION" ] || { echo "error: no VERSION in Software/MDRewriter.pro" >&2; exit 1; }

WORK="$ROOT/build/deb"
OUT="$ROOT/build/dist"
PKG="$WORK/pkg"
rm -rf "$WORK"
mkdir -p "$WORK/b" "$PKG" "$OUT"

echo "==> build $VERSION (release, PREFIX=/usr)"
(cd "$WORK/b" && qmake6 CONFIG+=release PREFIX=/usr "$ROOT/Software/MDRewriter.pro" && make -j"$JOBS" && make install INSTALL_ROOT="$PKG")
strip "$PKG/usr/bin/MDRewriter"

echo "==> build the command line tool"
mkdir -p "$WORK/c"
(cd "$WORK/c" && qmake6 CONFIG+=release "$ROOT/Software/cli/mdrewriter-cli.pro" && make -j"$JOBS")
install -Dm755 "$WORK/c/mdrewriter-cli" "$PKG/usr/bin/mdrewriter-cli"
strip "$PKG/usr/bin/mdrewriter-cli"

mkdir -p "$PKG/DEBIAN" "$PKG/usr/share/doc/mdrewriter"
install -m644 "$ROOT/LICENSE" "$PKG/usr/share/doc/mdrewriter/copyright"
(cd "$PKG" && find usr -type f -exec md5sum {} + > DEBIAN/md5sums)

# Qt 6.4 or newer. Ubuntu 24.04 / Mint 22 call the packages libqt6*6t64, Debian 12 libqt6*6.
cat > "$PKG/DEBIAN/control" <<CONTROL
Package: mdrewriter
Version: $VERSION
Architecture: $ARCH
Section: utils
Priority: optional
Installed-Size: $(du -sk "$PKG/usr" | cut -f1)
Depends: libc6 (>= 2.34), libgcc-s1 (>= 3.0), libstdc++6 (>= 5), libqt6core6t64 (>= 6.4.0) | libqt6core6 (>= 6.4.0), libqt6gui6t64 (>= 6.4.0) | libqt6gui6 (>= 6.4.0), libqt6widgets6t64 (>= 6.4.0) | libqt6widgets6 (>= 6.4.0), libqt6serialport6 (>= 6.4.0)
Maintainer: $MAINTAINER
Homepage: https://github.com/legendaryredfox/md_rewriter
Description: Read and write Sega Mega Drive cartridges
 MD Rewriter backs up Mega Drive game cartridges and their saves, writes
 rewritable flash cartridges (with read-back verification) and compares files.
 It talks to the MD Rewriter dumper over a USB virtual serial port.
 Includes a udev rule that gives the logged-in user access to the dumper.
CONTROL

# Reload the udev rules after installing or removing the package.
for script in postinst postrm; do
  cat > "$PKG/DEBIAN/$script" <<'SCRIPT'
#!/bin/sh
set -e
if command -v udevadm >/dev/null 2>&1; then
    udevadm control --reload-rules 2>/dev/null || true
    udevadm trigger --subsystem-match=tty 2>/dev/null || true
fi
exit 0
SCRIPT
  chmod 755 "$PKG/DEBIAN/$script"
done
find "$PKG" -type d -exec chmod 755 {} +

DEB="$OUT/mdrewriter_${VERSION}_${ARCH}.deb"
fakeroot dpkg-deb --root-owner-group -Zxz -b "$PKG" "$DEB"
(cd "$OUT" && sha256sum "$(basename "$DEB")" > "$(basename "$DEB").sha256")
echo "package: $DEB"
