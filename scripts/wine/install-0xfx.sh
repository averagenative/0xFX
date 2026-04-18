#!/bin/bash
#
# install-0xfx.sh — Run the latest 0xFX NSIS installer inside the Wine
# prefix so we can smoke-test the exact artifact users will download.
#
# Usage:
#   ./scripts/wine/install-0xfx.sh [path/to/0xFX-X.Y.Z-windows-x64-setup.exe]
#
# With no argument, auto-picks the newest matching setup.exe under release/.
# The installer runs silently (NSIS /S flag) and installs into the prefix's
# C:\Program Files\0xFX.
#

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$(dirname "$SCRIPT_DIR")")"
cd "$PROJECT_DIR"

export WINEPREFIX="${WINEPREFIX:-$HOME/.local/share/0xfx-wine-test}"
export WINEDLLOVERRIDES="mscoree=;mshtml="

if [ ! -d "$WINEPREFIX" ]; then
    echo "ERROR: prefix $WINEPREFIX does not exist." >&2
    echo "  Run ./scripts/wine/setup.sh first." >&2
    exit 1
fi

INSTALLER="$1"
if [ -z "$INSTALLER" ]; then
    # Newest x64 setup.exe under release/
    INSTALLER="$(ls -t release/0xFX-*-windows-x64-setup.exe 2>/dev/null | head -1 || true)"
fi

if [ -z "$INSTALLER" ] || [ ! -f "$INSTALLER" ]; then
    echo "ERROR: No setup.exe found." >&2
    echo "  Build first: ./scripts/packaging/package_release.sh <version> --arch x64" >&2
    echo "  Or pass explicitly: $0 path/to/setup.exe" >&2
    exit 1
fi

echo "=== Installing $(basename "$INSTALLER") into $WINEPREFIX ==="

# /S = silent install. /D=install-dir must come last and have NO quotes
# (NSIS limitation — it reads everything after /D= literally, up to EOL).
wine "$INSTALLER" /S
RC=$?

if [ $RC -ne 0 ]; then
    echo "ERROR: installer exited with code $RC" >&2
    exit $RC
fi

INSTALL_DIR="$WINEPREFIX/drive_c/Program Files/0xFX"
if [ ! -f "$INSTALL_DIR/0xfx_gui.exe" ]; then
    echo "ERROR: 0xfx_gui.exe not found at $INSTALL_DIR after install." >&2
    echo "  The NSIS installer may have failed silently — check winetricks deps." >&2
    exit 1
fi

echo ""
echo "=== Installed ==="
echo "  Standalone: C:\\Program Files\\0xFX\\0xfx_gui.exe"
echo "  CLAP:       C:\\Program Files\\Common Files\\CLAP\\0xFX.clap"
echo "  VST3:       C:\\Program Files\\Common Files\\VST3\\0xFX.vst3"
echo ""
echo "Launch with: ./scripts/wine/launch-0xfx.sh"
