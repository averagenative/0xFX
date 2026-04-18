#!/bin/bash
#
# install-reaper.sh — Download and install Reaper for Windows into the
# 0xFX Wine prefix so we can test that 0xFX's VST3 and CLAP plugins load
# cleanly in a real DAW.
#
# Usage:
#   ./scripts/wine/install-reaper.sh [version]
#
# Default version is 7.28 (Windows x64). The installer ships a /S silent
# flag. After install, the VST3 plugin will be picked up automatically
# from C:\Program Files\Common Files\VST3\. The CLAP plugin is discovered
# from C:\Program Files\Common Files\CLAP\ by recent Reaper builds.
#

set -e

REAPER_VERSION="${1:-7.28}"
REAPER_URL="https://www.reaper.fm/files/${REAPER_VERSION%%.*}.x/reaper${REAPER_VERSION//./}_x64-install.exe"

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

CACHE_DIR="$HOME/.cache/0xfx-wine"
mkdir -p "$CACHE_DIR"
INSTALLER="$CACHE_DIR/reaper${REAPER_VERSION//./}_x64-install.exe"

if [ ! -f "$INSTALLER" ]; then
    echo "=== Downloading Reaper ${REAPER_VERSION} ==="
    echo "  URL: $REAPER_URL"
    wget -O "$INSTALLER" "$REAPER_URL"
    echo ""
fi

REAPER_EXE="$WINEPREFIX/drive_c/Program Files/REAPER (x64)/reaper.exe"
if [ -f "$REAPER_EXE" ]; then
    echo "=== Reaper already installed at: ==="
    echo "  $REAPER_EXE"
    echo ""
    echo "Re-install anyway? (Ctrl-C to abort, Enter to continue)"
    read -r
fi

echo "=== Installing Reaper ${REAPER_VERSION} into $WINEPREFIX ==="
# /S = silent install (Reaper uses NSIS too)
wine "$INSTALLER" /S

if [ ! -f "$REAPER_EXE" ]; then
    echo "ERROR: Reaper install failed — $REAPER_EXE not found." >&2
    exit 1
fi

echo ""
echo "=== Reaper installed ==="
echo "  $REAPER_EXE"
echo ""
echo "Launch Reaper to test 0xFX plugin scanning:"
echo "  WINEPREFIX='$WINEPREFIX' wine '$REAPER_EXE'"
echo ""
echo "Tips:"
echo "  - On first launch, Reaper will scan for plugins. Watch for"
echo "    '0xFX' in both VST3 and CLAP lists (Options > Preferences >"
echo "    Plug-ins > VST  /  CLAP)."
echo "  - If a plugin crashes the scan, the crash happens in a sandbox"
echo "    subprocess — Reaper will blacklist it and continue. Check"
echo "    Reaper's plugin scan log for the exception."
