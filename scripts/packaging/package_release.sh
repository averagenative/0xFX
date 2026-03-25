#!/bin/bash
#
# package_release.sh — Build and package 0xFX for release.
#
# Usage:
#   ./scripts/packaging/package_release.sh [version]
#
# Builds:
#   - Linux:   tar.gz (standalone + plugins + presets)
#   - Linux:   AppImage (standalone only)
#   - Windows: zip (standalone + plugins + presets)
#   - Windows: NSIS installer (.exe)
#
# Outputs to release/ directory.
#

set -e

VERSION="${1:-0.1.0}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$(dirname "$SCRIPT_DIR")")"

cd "$PROJECT_DIR"

echo "=== 0xFX Release Packaging v${VERSION} ==="
echo ""

# ── Clean ──
rm -rf release
mkdir -p release

# ══════════════════════════════════════════════════════════════════════
# LINUX BUILD
# ══════════════════════════════════════════════════════════════════════
echo "--- Building Linux (native) ---"
cmake -B build -DCMAKE_BUILD_TYPE=Release 2>&1 | tail -3
cmake --build build -j$(nproc) 2>&1 | tail -5

# Run tests
echo "--- Running tests ---"
cd build && ctest --output-on-failure 2>&1 | tail -3 && cd "$PROJECT_DIR"

# ── Linux tar.gz ──
echo ""
echo "--- Packaging Linux tar.gz ---"
LINUX_DIR="release/0xFX-${VERSION}-linux-x64"
mkdir -p "${LINUX_DIR}/presets"

cp build/0xfx_gui              "${LINUX_DIR}/" 2>/dev/null || echo "  (no standalone GUI binary)"
cp build/0xfx_standalone       "${LINUX_DIR}/" 2>/dev/null || echo "  (no standalone console binary)"
cp build/0xFX.clap             "${LINUX_DIR}/" 2>/dev/null || echo "  (no CLAP)"
cp build/0xFX.vst3             "${LINUX_DIR}/" 2>/dev/null || echo "  (no VST3)"
cp -r presets/factory           "${LINUX_DIR}/presets/"
cp README.md LICENSE            "${LINUX_DIR}/"

cat > "${LINUX_DIR}/INSTALL.txt" << 'EOF'
0xFX — Linux Installation
==============================

Standalone:
  chmod +x 0xfx_gui
  ./0xfx_gui

Plugins:
  CLAP: cp 0xFX.clap ~/.clap/
  VST3: mkdir -p ~/.vst3/0xFX.vst3/Contents/x86_64-linux && \
        cp 0xFX.vst3 ~/.vst3/0xFX.vst3/Contents/x86_64-linux/

Presets should be alongside the binary in presets/factory/
or in ~/.local/share/0xFX/presets/
EOF

cd release
tar czf "0xFX-${VERSION}-linux-x64.tar.gz" "0xFX-${VERSION}-linux-x64"
cd "$PROJECT_DIR"
echo "  -> release/0xFX-${VERSION}-linux-x64.tar.gz"

# ── Linux AppImage ──
echo ""
echo "--- Packaging Linux AppImage ---"
APPDIR="release/0xFX.AppDir"
mkdir -p "${APPDIR}/usr/bin"
mkdir -p "${APPDIR}/usr/bin/presets"
mkdir -p "${APPDIR}/usr/share/applications"
mkdir -p "${APPDIR}/usr/share/icons/hicolor/256x256/apps"

cp build/0xfx_gui "${APPDIR}/usr/bin/" 2>/dev/null || true
cp -r presets/factory "${APPDIR}/usr/bin/presets/"

# Icon
cp resources/icon/0xfx_256.png "${APPDIR}/0xfx.png"
cp resources/icon/0xfx_256.png "${APPDIR}/usr/share/icons/hicolor/256x256/apps/0xfx.png"
ln -sf 0xfx.png "${APPDIR}/.DirIcon"

# Desktop entry
cat > "${APPDIR}/0xfx.desktop" << EOF
[Desktop Entry]
Name=0xFX
Exec=0xfx_gui
Icon=0xfx
Type=Application
Categories=AudioVideo;Audio;
Comment=Guitar Amp Simulator & Effects Pedalboard
EOF
cp "${APPDIR}/0xfx.desktop" "${APPDIR}/usr/share/applications/"

# AppRun
cat > "${APPDIR}/AppRun" << 'EOF'
#!/bin/bash
SELF="$(readlink -f "$0")"
HERE="${SELF%/*}"
export LD_LIBRARY_PATH="${HERE}/usr/lib:${LD_LIBRARY_PATH}"
exec "${HERE}/usr/bin/0xfx_gui" "$@"
EOF
chmod +x "${APPDIR}/AppRun"

# Build AppImage if tool available
if command -v appimagetool &>/dev/null; then
    ARCH=x86_64 appimagetool "${APPDIR}" "release/0xFX-${VERSION}-x86_64.AppImage" 2>/dev/null
    echo "  -> release/0xFX-${VERSION}-x86_64.AppImage"
else
    echo "  appimagetool not found — AppDir created at ${APPDIR}"
    echo "  Install: wget https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-x86_64.AppImage"
fi

# ══════════════════════════════════════════════════════════════════════
# WINDOWS BUILD (cross-compile)
# ══════════════════════════════════════════════════════════════════════
echo ""
echo "--- Building Windows (MinGW cross-compile) ---"
cmake -B build_win -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64.cmake -DCMAKE_BUILD_TYPE=Release 2>&1 | tail -3
cmake --build build_win -j$(nproc) 2>&1 | tail -5

# ── Windows zip ──
echo ""
echo "--- Packaging Windows zip ---"
WIN_DIR="release/0xFX-${VERSION}-windows-x64"
mkdir -p "${WIN_DIR}/presets"

cp build_win/0xfx_gui.exe      "${WIN_DIR}/" 2>/dev/null || echo "  (no standalone GUI)"
cp resources/icon/0xfx.ico     "${WIN_DIR}/" 2>/dev/null || true
cp build_win/0xFX.clap         "${WIN_DIR}/" 2>/dev/null || echo "  (no CLAP)"
cp build_win/0xFX.vst3         "${WIN_DIR}/" 2>/dev/null || echo "  (no VST3)"
cp -r presets/factory           "${WIN_DIR}/presets/"
cp README.md LICENSE            "${WIN_DIR}/"

cat > "${WIN_DIR}/INSTALL.txt" << 'EOF'
0xFX — Windows Installation
================================

Standalone:
  Double-click 0xfx_gui.exe to run.
  Presets are in the presets\factory\ folder.

Plugins (manual install):
  CLAP: Copy 0xFX.clap to C:\Program Files\Common Files\CLAP\
  VST3: Copy 0xFX.vst3 to C:\Program Files\Common Files\VST3\0xFX.vst3\Contents\x86_64-win\

Or use the installer (0xFX-setup.exe) which handles everything automatically.

Recordings save to a "recordings" folder next to 0xfx_gui.exe.
EOF

cd release
zip -r "0xFX-${VERSION}-windows-x64.zip" "0xFX-${VERSION}-windows-x64" -q
cd "$PROJECT_DIR"
echo "  -> release/0xFX-${VERSION}-windows-x64.zip"

# ── Windows NSIS installer ──
echo ""
echo "--- Building Windows installer (NSIS) ---"
if command -v makensis &>/dev/null; then
    # Update version in .nsi
    sed -i "s/!define VER \".*\"/!define VER \"${VERSION}\"/" scripts/packaging/0xfx_installer.nsi
    sed -i "s/!define VERFULL \".*\"/!define VERFULL \"${VERSION}.0\"/" scripts/packaging/0xfx_installer.nsi

    makensis scripts/packaging/0xfx_installer.nsi 2>&1 | tail -5
    echo "  -> release/0xFX-${VERSION}-windows-x64-setup.exe"
else
    echo "  makensis not found — skipping installer"
    echo "  Install: sudo apt install nsis"
fi

# ══════════════════════════════════════════════════════════════════════
# SUMMARY
# ══════════════════════════════════════════════════════════════════════
echo ""
echo "=== Release artifacts ==="
ls -lh release/*.tar.gz release/*.zip release/*.exe release/*.AppImage 2>/dev/null
echo ""
echo "To create a GitHub release:"
echo "  git tag v${VERSION}"
echo "  git push origin v${VERSION}"
echo "  gh release create v${VERSION} release/0xFX-${VERSION}-* --title 'v${VERSION}' --generate-notes"
