#!/bin/bash
# packaging/mkdeb.sh [version] — genera y520-center_<ver>_amd64.deb
# Uso: ./packaging/mkdeb.sh [0.1.0]   (por defecto: git describe o 0.1.0)
# Requiere: meson ninja-build g++ pkg-config libgtk-4-dev libadwaita-1-dev
#           libglib2.0-dev dpkg-dev
set -euo pipefail
cd "$(dirname "$0")/.."

VERSION="${1:-}"
if [ -z "$VERSION" ]; then
    VERSION="$(git describe --tags --always --dirty 2>/dev/null | sed 's/^v//' || true)"
    VERSION="${VERSION:-0.1.0}"
fi
OUT="packaging/out"
ROOT="$OUT/root/y520-center_${VERSION}_amd64"

echo "==> Versión: $VERSION"
rm -rf "$OUT"
mkdir -p "$ROOT"

echo "==> Configurando (prefix=/usr)"
meson setup "$OUT/build" --prefix=/usr --buildtype=release
echo "==> Compilando"
ninja -C "$OUT/build"
echo "==> Tests"
meson test -C "$OUT/build" --print-errorlogs

echo "==> Instalando en DESTDIR"
DESTDIR="$PWD/$ROOT" meson install -C "$OUT/build"

echo "==> Control Debian"
mkdir -p "$ROOT/DEBIAN"
cat > "$ROOT/DEBIAN/control" <<EOF
Package: y520-center
Version: $VERSION
Section: utils
Priority: optional
Architecture: amd64
Depends: libgtk-4-1 (>= 4.14), libadwaita-1-0 (>= 1.4), libglib2.0-0t64 (>= 2.80) | libglib2.0-0, polkitd | policykit-1, power-profiles-daemon
Maintainer: Y520 Center contributors
Description: GNOME control center for the Lenovo Legion Y520
 Native GTK4/libadwaita app (C++) for the Lenovo Y520-15IKBN (80WK):
 Extreme Cooling fan mode via EC, battery conservation mode, white
 keyboard backlight and live CPU/temperature monitors, through a
 privileged D-Bus system daemon with polkit authorization.
 .
 ADVERTENCIA / WARNING: sin garantía; el modo Extreme Cooling pone los
 ventiladores al 100% — úsalo en periodos cortos.
EOF
cat > "$ROOT/DEBIAN/postinst" <<'EOF'
#!/bin/sh
set -e
[ -d /run/systemd/system ] && systemctl daemon-reload || true
[ -d /run/systemd/system ] && systemctl enable y520d.service >/dev/null || true
udevadm control --reload-rules 2>/dev/null || true
gtk-update-icon-cache -q /usr/share/icons/hicolor 2>/dev/null || true
exit 0
EOF
chmod 755 "$ROOT/DEBIAN/postinst"

echo "==> dpkg-deb"
dpkg-deb --build --root-owner-group "$ROOT" "$OUT/y520-center_${VERSION}_amd64.deb"
echo "==> OK: $OUT/y520-center_${VERSION}_amd64.deb"
ls -l "$OUT"/*.deb
