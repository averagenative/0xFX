#!/bin/bash
#
# package_macos.sh — Build and package 0xFX for macOS.
#
# Run this on a Mac with Homebrew, SDL2, and CMake installed:
#   brew install sdl2 cmake
#   ./scripts/packaging/package_macos.sh [version]
#
# Outputs:
#   release/0xFX-{version}-macos-x64.dmg
#   release/0xFX-{version}-macos-x64.zip
#
# Lessons learned from 0x808/0xSYNTH releases:
#   - App must work from /Applications (no hardcoded paths relative to build dir)
#   - Presets bundled inside .app/Contents/Resources/ so drag-to-Applications works
#   - SDL2 linked from Homebrew — users need: brew install sdl2
#   - Unsigned app requires xattr -cr to clear Gatekeeper quarantine
#   - VST3 macOS bundle uses Contents/MacOS/ (not x86_64-linux)
#   - CLAP is a single .clap file (no bundle structure needed)
#

set -e

VERSION="${1:-1.0.0}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$(dirname "$SCRIPT_DIR")")"
APP_NAME="0xFX"
RELEASE_DIR="release"
APP_DIR="${RELEASE_DIR}/${APP_NAME}.app"

cd "$PROJECT_DIR"

echo "=== 0xFX macOS Release Packaging v${VERSION} ==="
echo ""

# ── Icon generation ──
echo "--- Generating .icns icon ---"
if [ ! -f resources/icon/0xfx.icns ]; then
    ICONSET="resources/icon/0xfx.iconset"
    mkdir -p "${ICONSET}"
    sips -z 16 16   resources/icon/0xfx_64.png  --out "${ICONSET}/icon_16x16.png"
    sips -z 32 32   resources/icon/0xfx_64.png  --out "${ICONSET}/icon_16x16@2x.png"
    sips -z 32 32   resources/icon/0xfx_64.png  --out "${ICONSET}/icon_32x32.png"
    sips -z 64 64   resources/icon/0xfx_64.png  --out "${ICONSET}/icon_32x32@2x.png"
    sips -z 128 128 resources/icon/0xfx_128.png --out "${ICONSET}/icon_128x128.png"
    sips -z 256 256 resources/icon/0xfx_256.png --out "${ICONSET}/icon_128x128@2x.png"
    sips -z 256 256 resources/icon/0xfx_256.png --out "${ICONSET}/icon_256x256.png"
    sips -z 512 512 resources/icon/0xfx_512.png --out "${ICONSET}/icon_256x256@2x.png"
    sips -z 512 512 resources/icon/0xfx_512.png --out "${ICONSET}/icon_512x512.png"
    sips -z 512 512 resources/icon/0xfx_512.png --out "${ICONSET}/icon_512x512@2x.png"
    iconutil -c icns "${ICONSET}" -o resources/icon/0xfx.icns
    rm -rf "${ICONSET}"
    echo "  -> Generated resources/icon/0xfx.icns"
else
    echo "  -> Using existing resources/icon/0xfx.icns"
fi

# ── Build ──
echo ""
echo "--- Building macOS ---"
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(sysctl -n hw.ncpu)

# ── Run tests ──
echo ""
echo "--- Running tests ---"
cd build && ctest --output-on-failure 2>&1 | tail -5 && cd "$PROJECT_DIR"

# ── Create .app bundle ──
echo ""
echo "--- Creating .app bundle ---"
rm -rf "${RELEASE_DIR}"
mkdir -p "${APP_DIR}/Contents/MacOS"
mkdir -p "${APP_DIR}/Contents/Resources"

# Binary — use GUI app if available, otherwise standalone
if [ -f build/0xfx_gui ]; then
    cp build/0xfx_gui "${APP_DIR}/Contents/MacOS/${APP_NAME}"
    echo "  -> Binary: 0xfx_gui"
elif [ -f build/0xfx_standalone ]; then
    cp build/0xfx_standalone "${APP_DIR}/Contents/MacOS/${APP_NAME}"
    echo "  -> Binary: 0xfx_standalone (no GUI build)"
else
    echo "ERROR: No binary found. Build failed?"
    exit 1
fi

# Presets — bundled inside .app so it works from /Applications
# (Lesson from 0x808/0xSYNTH: paths relative to build dir break after install)
if [ -d presets ]; then
    cp -r presets "${APP_DIR}/Contents/Resources/"
    echo "  -> Presets bundled in Resources/"
fi

# Icon
cp resources/icon/0xfx.icns "${APP_DIR}/Contents/Resources/"
echo "  -> Icon: 0xfx.icns"

# Info.plist
cat > "${APP_DIR}/Contents/Info.plist" << PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleExecutable</key>
    <string>${APP_NAME}</string>
    <key>CFBundleIdentifier</key>
    <string>com.dcmichael.0xfx</string>
    <key>CFBundleName</key>
    <string>0xFX</string>
    <key>CFBundleDisplayName</key>
    <string>0xFX</string>
    <key>CFBundleIconFile</key>
    <string>0xfx</string>
    <key>CFBundleVersion</key>
    <string>${VERSION}</string>
    <key>CFBundleShortVersionString</key>
    <string>${VERSION}</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>NSHighResolutionCapable</key>
    <true/>
    <key>LSMinimumSystemVersion</key>
    <string>11.0</string>
    <key>CFBundleDocumentTypes</key>
    <array>
        <dict>
            <key>CFBundleTypeExtensions</key>
            <array>
                <string>0xfx</string>
            </array>
            <key>CFBundleTypeName</key>
            <string>0xFX Preset</string>
            <key>CFBundleTypeRole</key>
            <string>Editor</string>
        </dict>
    </array>
    <key>NSMicrophoneUsageDescription</key>
    <string>0xFX needs microphone access for live guitar input.</string>
</dict>
</plist>
PLIST

echo "  -> ${APP_DIR}"

# ── Prepare plugins ──
echo ""
echo "--- Preparing plugins ---"
PLUGIN_DIR="${RELEASE_DIR}/Plugins"
mkdir -p "${PLUGIN_DIR}"

# CLAP — single file
if [ -f build/0xFX.clap ]; then
    cp build/0xFX.clap "${PLUGIN_DIR}/"
    echo "  -> CLAP: ${PLUGIN_DIR}/0xFX.clap"
else
    echo "  (no CLAP plugin found)"
fi

# VST3 — macOS bundle structure (Contents/MacOS/, not x86_64-linux)
if [ -f build/0xFX.vst3 ]; then
    VST3_BUNDLE="${PLUGIN_DIR}/0xFX.vst3"
    mkdir -p "${VST3_BUNDLE}/Contents/MacOS"
    cp build/0xFX.vst3 "${VST3_BUNDLE}/Contents/MacOS/0xFX"

    cat > "${VST3_BUNDLE}/Contents/Info.plist" << VST3PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleExecutable</key>
    <string>0xFX</string>
    <key>CFBundleIdentifier</key>
    <string>com.dcmichael.0xfx.vst3</string>
    <key>CFBundleName</key>
    <string>0xFX</string>
    <key>CFBundleVersion</key>
    <string>${VERSION}</string>
    <key>CFBundlePackageType</key>
    <string>BNDL</string>
</dict>
</plist>
VST3PLIST
    echo "  -> VST3: ${VST3_BUNDLE}"
elif [ -d build/0xFX.vst3.bundle ]; then
    # Use pre-built bundle from CMake
    cp -r build/0xFX.vst3.bundle "${PLUGIN_DIR}/0xFX.vst3"
    echo "  -> VST3: ${PLUGIN_DIR}/0xFX.vst3 (from bundle)"
else
    echo "  (no VST3 plugin found)"
fi

# Plugin install instructions
cat > "${PLUGIN_DIR}/INSTALL_PLUGINS.txt" << 'PLUGINTXT'
0xFX — Plugin Installation (macOS)
====================================

Copy the plugins to these locations so your DAW can find them:

  VST3:  ~/Library/Audio/Plug-Ins/VST3/0xFX.vst3
  CLAP:  ~/Library/Audio/Plug-Ins/CLAP/0xFX.clap

Quick install (run in Terminal from this folder):

  mkdir -p ~/Library/Audio/Plug-Ins/VST3
  mkdir -p ~/Library/Audio/Plug-Ins/CLAP
  cp -r 0xFX.vst3 ~/Library/Audio/Plug-Ins/VST3/
  cp 0xFX.clap    ~/Library/Audio/Plug-Ins/CLAP/

Then clear Gatekeeper quarantine (unsigned plugins):

  xattr -cr ~/Library/Audio/Plug-Ins/CLAP/0xFX.clap
  xattr -cr ~/Library/Audio/Plug-Ins/VST3/0xFX.vst3

Restart your DAW and rescan plugins.
PLUGINTXT
echo "  -> ${PLUGIN_DIR}/INSTALL_PLUGINS.txt"

# ── macOS INSTALL.txt ──
cat > "${RELEASE_DIR}/INSTALL.txt" << 'EOF'
0xFX — macOS Installation
==============================

STANDALONE:
  Drag 0xFX.app to /Applications.

PLUGINS:
  See Plugins/INSTALL_PLUGINS.txt for DAW plugin installation.

GATEKEEPER (unsigned app):
  If macOS blocks the app with "cannot be opened because the developer
  cannot be verified", run in Terminal:

  xattr -cr /Applications/0xFX.app
  xattr -cr ~/Library/Audio/Plug-Ins/CLAP/0xFX.clap
  xattr -cr ~/Library/Audio/Plug-Ins/VST3/0xFX.vst3

  Or: System Settings > Privacy & Security > "Allow Anyway"

PRESETS:
  Factory presets are bundled inside the .app.
  User presets save to ~/Library/Application Support/0xFX/presets/

REQUIREMENTS:
  SDL2: brew install sdl2
  macOS 11.0 (Big Sur) or later
EOF

# ── Create .dmg ──
echo ""
echo "--- Creating .dmg ---"
DMG_NAME="0xFX-${VERSION}-macos-x64.dmg"
DMG_STAGE="${RELEASE_DIR}/dmg_stage"
mkdir -p "${DMG_STAGE}"

cp -r "${APP_DIR}" "${DMG_STAGE}/"
cp README.md "${DMG_STAGE}/" 2>/dev/null || true
cp LICENSE "${DMG_STAGE}/" 2>/dev/null || true
cp "${RELEASE_DIR}/INSTALL.txt" "${DMG_STAGE}/"
ln -s /Applications "${DMG_STAGE}/Applications"

hdiutil create -volname "0xFX v${VERSION}" \
    -srcfolder "${DMG_STAGE}" \
    -ov -format UDZO \
    "${RELEASE_DIR}/${DMG_NAME}" 2>/dev/null && {
    echo "  -> ${RELEASE_DIR}/${DMG_NAME}"
} || {
    echo "  hdiutil failed — check Console.app for details"
    echo "  Falling back to zip-only distribution"
}

rm -rf "${DMG_STAGE}"

# ── Create zip (app + plugins) ──
echo ""
echo "--- Creating zip ---"
cd "${RELEASE_DIR}"
zip -r "0xFX-${VERSION}-macos-x64.zip" \
    "${APP_NAME}.app" Plugins/ INSTALL.txt -q 2>/dev/null && {
    echo "  -> release/0xFX-${VERSION}-macos-x64.zip"
} || {
    echo "  zip creation failed"
}
cd "$PROJECT_DIR"

# ── Summary ──
echo ""
echo "=== macOS Release Artifacts ==="
ls -lh "${RELEASE_DIR}"/*.dmg "${RELEASE_DIR}"/*.zip 2>/dev/null
echo ""
echo "To upload to GitHub release:"
echo "  gh release upload v${VERSION} ${RELEASE_DIR}/*.dmg ${RELEASE_DIR}/*.zip"
echo ""
echo "To test locally:"
echo "  open ${RELEASE_DIR}/${APP_NAME}.app"
echo "  # Or mount DMG: open ${RELEASE_DIR}/${DMG_NAME}"
