#!/bin/bash
#
# launch-0xfx.sh — Launch the installed 0xFX standalone inside the Wine
# prefix for smoke testing.
#
# Usage:
#   ./scripts/wine/launch-0xfx.sh
#
# Runs C:\Program Files\0xFX\0xfx_gui.exe under the dedicated prefix.
# Use this to verify the GUI renders, the cab IRs load, dropdowns work,
# etc. Audio quality under Wine is not representative — do final audio
# testing on a native Windows machine.
#

set -e

export WINEPREFIX="${WINEPREFIX:-$HOME/.local/share/0xfx-wine-test}"
export WINEDLLOVERRIDES="mscoree=;mshtml="

EXE="$WINEPREFIX/drive_c/Program Files/0xFX/0xfx_gui.exe"

if [ ! -f "$EXE" ]; then
    echo "ERROR: $EXE not found." >&2
    echo "  Run ./scripts/wine/install-0xfx.sh first." >&2
    exit 1
fi

echo "=== Launching 0xFX under Wine ==="
echo "  Prefix: $WINEPREFIX"
echo "  Binary: $EXE"
echo ""

# cd into the install dir so relative resources (presets/, resources/ir/)
# resolve correctly.
cd "$WINEPREFIX/drive_c/Program Files/0xFX"
exec wine 0xfx_gui.exe "$@"
