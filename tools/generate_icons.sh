#!/bin/bash
#
# generate_icons.sh — Rasterize resources/icon/0xfx.svg to all target formats.
#
# Requires:
#   rsvg-convert   (Fedora: dnf install librsvg2-tools; Ubuntu: librsvg2-bin)
#   magick         (ImageMagick)
#
# Outputs:
#   resources/icon/0xfx_{64,128,256,512}.png   — consumed by CMake & Linux packaging
#   resources/icon/0xfx.ico                    — Windows app icon (multi-size)
#
# The macOS .icns is regenerated on a Mac via scripts/packaging/package_macos.sh
# (which requires iconutil). Running this script on Linux does not touch .icns.
#
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
ICON_DIR="${PROJECT_DIR}/resources/icon"
SVG="${ICON_DIR}/0xfx.svg"

if [ ! -f "$SVG" ]; then
    echo "ERROR: $SVG not found"
    exit 1
fi
command -v rsvg-convert >/dev/null || { echo "ERROR: rsvg-convert not installed"; exit 1; }
command -v magick       >/dev/null || { echo "ERROR: magick not installed"; exit 1; }

echo "--- Rasterizing $SVG ---"
for size in 16 32 48 64 128 256 512 1024; do
    OUT="${ICON_DIR}/0xfx_${size}.png"
    rsvg-convert -w "$size" -h "$size" "$SVG" -o "$OUT"
    echo "  -> 0xfx_${size}.png"
done

echo ""
echo "--- Building 0xfx.ico (16/32/48/64/128/256) ---"
magick \
    "${ICON_DIR}/0xfx_16.png"  \
    "${ICON_DIR}/0xfx_32.png"  \
    "${ICON_DIR}/0xfx_48.png"  \
    "${ICON_DIR}/0xfx_64.png"  \
    "${ICON_DIR}/0xfx_128.png" \
    "${ICON_DIR}/0xfx_256.png" \
    "${ICON_DIR}/0xfx.ico"
echo "  -> 0xfx.ico"

# Keep only the sizes other tooling references.
rm -f "${ICON_DIR}/0xfx_16.png" "${ICON_DIR}/0xfx_32.png" "${ICON_DIR}/0xfx_48.png" "${ICON_DIR}/0xfx_1024.png"

echo ""
echo "Done. On macOS, run scripts/packaging/package_macos.sh to regenerate 0xfx.icns."
