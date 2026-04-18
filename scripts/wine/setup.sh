#!/bin/bash
#
# setup.sh — Initialize a dedicated Wine prefix for smoke-testing 0xFX
# Windows builds from Linux.
#
# Usage:
#   ./scripts/wine/setup.sh
#
# Creates a 64-bit prefix at ~/.local/share/0xfx-wine-test/ and (optionally,
# if winetricks is installed) pulls down the common Windows runtimes that
# Reaper and other DAWs expect. Idempotent — re-running is safe and fast.
#
# Why a dedicated prefix:
#   - Don't pollute the user's default ~/.wine.
#   - Reproducible test surface: other 0xFX developers can run the same
#     steps and get the same result.
#   - Easy to nuke and start over: `rm -rf ~/.local/share/0xfx-wine-test`
#
# Wine caveats:
#   - Audio under Wine uses winepulse/winealsa. It works for smoke
#     tests (GUI + plugin scan) but is not suitable for latency-sensitive
#     real-world playtesting. Use a native Windows machine for that.
#   - MIDI under Wine is limited. MIDI learn won't be representative.
#   - VST3/CLAP scanning in a Windows DAW running inside Wine is the
#     point of this setup — catches load/crash bugs without rebooting.
#

set -e

if ! command -v wine &>/dev/null; then
    echo "ERROR: wine not installed." >&2
    echo "  Fedora: sudo dnf install wine wine-core wine-desktop" >&2
    echo "  Ubuntu: sudo apt install wine winetricks" >&2
    exit 1
fi

export WINEPREFIX="${WINEPREFIX:-$HOME/.local/share/0xfx-wine-test}"
export WINEARCH="win64"
# Suppress the mono/gecko "install now?" dialog — we don't need .NET / IE
# rendering for 0xFX or Reaper smoke tests.
export WINEDLLOVERRIDES="mscoree=;mshtml="

echo "=== 0xFX Wine prefix setup ==="
echo "  WINEPREFIX: $WINEPREFIX"
echo "  Wine: $(wine --version)"
echo ""

# ── 1. Bootstrap the prefix ────────────────────────────────────────────
if [ ! -d "$WINEPREFIX" ]; then
    echo "--- Creating prefix ---"
    mkdir -p "$(dirname "$WINEPREFIX")"
    # Wine bootstraps the prefix on first invocation of any command.
    # `wineboot --init` is the explicit, non-interactive way. First run
    # takes ~15s.
    wine wineboot --init
    echo "  -> $WINEPREFIX"
else
    echo "--- Prefix already exists, skipping wineboot ---"
fi
echo ""

# ── 2. Optional winetricks dependencies ────────────────────────────────
# Reaper and most DAWs run on a bare wine64 prefix, but vcrun2019 covers
# edge cases (MSVC-built plugins, some Reaper UI features). corefonts
# gives us Arial/Tahoma etc. for Windows-native UI rendering.
if command -v winetricks &>/dev/null; then
    echo "--- Installing winetricks deps (corefonts, vcrun2019) ---"
    # -q = unattended. Both are idempotent; skipped if already present.
    winetricks -q corefonts vcrun2019 2>&1 | tail -5 || {
        echo "  WARN: winetricks returned non-zero — some deps may not have"
        echo "  installed. Reaper/0xFX will likely still work for smoke tests."
    }
else
    echo "--- winetricks not installed (optional) ---"
    echo "  Install to get corefonts + vcrun2019 pulled in automatically:"
    echo "    Fedora: sudo dnf install winetricks"
    echo "    Ubuntu: sudo apt install winetricks"
    echo "  Smoke tests will likely still work without these."
fi
echo ""

echo "=== Prefix ready ==="
echo ""
echo "Next steps:"
echo "  1. Build the release: ./scripts/packaging/package_release.sh 1.1.0 --arch x64"
echo "  2. Install 0xFX:       ./scripts/wine/install-0xfx.sh"
echo "  3. Launch 0xFX:        ./scripts/wine/launch-0xfx.sh"
echo "  4. (Optional) Reaper:  ./scripts/wine/install-reaper.sh"
