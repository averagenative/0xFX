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
cp -r build/0xFX.vst3          "${LINUX_DIR}/" 2>/dev/null || echo "  (no VST3 bundle)"
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
  VST3: cp -r 0xFX.vst3 ~/.vst3/

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

# ── Locate appimagetool ──
APPIMAGETOOL=""
if command -v appimagetool &>/dev/null; then
    APPIMAGETOOL="appimagetool"
elif [ -x "$HOME/tools/appimagetool" ]; then
    APPIMAGETOOL="$HOME/tools/appimagetool"
else
    echo "ERROR: appimagetool not found — AppImage is required for every release."
    echo "Install:"
    echo "  mkdir -p ~/tools && \\"
    echo "  wget https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage -O ~/tools/appimagetool && \\"
    echo "  chmod +x ~/tools/appimagetool"
    exit 1
fi

# ── Fuse3 runtimes (download once to ~/tools/, reuse on subsequent runs) ──
# Uses the type2-runtime which supports both FUSE 2 and FUSE 3, unlike the old
# AppImageKit runtime which required libfuse.so.2 (missing on Fedora 34+, Ubuntu 22.04+).
TYPE2_RUNTIME_BASE="https://github.com/AppImage/type2-runtime/releases/download/continuous"
mkdir -p "$HOME/tools"

RUNTIME_X86_64="$HOME/tools/runtime-fuse3-x86_64"
if [ ! -f "$RUNTIME_X86_64" ]; then
    echo "  Downloading type2-runtime for x86_64..."
    wget -q "${TYPE2_RUNTIME_BASE}/runtime-x86_64" -O "$RUNTIME_X86_64"
    chmod +x "$RUNTIME_X86_64"
    echo "  -> $RUNTIME_X86_64"
else
    echo "  Using cached type2-runtime: $RUNTIME_X86_64"
fi

RUNTIME_AARCH64="$HOME/tools/runtime-fuse3-aarch64"
if [ ! -f "$RUNTIME_AARCH64" ]; then
    echo "  Downloading type2-runtime for aarch64..."
    wget -q "${TYPE2_RUNTIME_BASE}/runtime-aarch64" -O "$RUNTIME_AARCH64"
    chmod +x "$RUNTIME_AARCH64"
    echo "  -> $RUNTIME_AARCH64"
else
    echo "  Using cached type2-runtime: $RUNTIME_AARCH64"
fi

# ── Helper: populate an AppDir ──
populate_appdir() {
    local APPDIR="$1"
    local BINARY="$2"

    mkdir -p "${APPDIR}/usr/bin"
    mkdir -p "${APPDIR}/usr/bin/presets"
    mkdir -p "${APPDIR}/usr/share/applications"
    mkdir -p "${APPDIR}/usr/share/icons/hicolor/256x256/apps"

    cp "$BINARY" "${APPDIR}/usr/bin/0xfx_gui"
    cp -r presets/factory "${APPDIR}/usr/bin/presets/"
    mkdir -p "${APPDIR}/usr/bin/resources/ir"
    cp -r resources/ir/bundled "${APPDIR}/usr/bin/resources/ir/"

    cp resources/icon/0xfx_256.png "${APPDIR}/0xfx.png"
    cp resources/icon/0xfx_256.png "${APPDIR}/usr/share/icons/hicolor/256x256/apps/0xfx.png"
    ln -sf 0xfx.png "${APPDIR}/.DirIcon"

    cp resources/0xFX.desktop "${APPDIR}/0xfx.desktop"
    sed -i 's/^Exec=.*/Exec=0xfx_gui/' "${APPDIR}/0xfx.desktop"
    cp "${APPDIR}/0xfx.desktop" "${APPDIR}/usr/share/applications/"

    cat > "${APPDIR}/AppRun" << 'APPRUN_EOF'
#!/bin/bash
SELF="$(readlink -f "$0")"
HERE="${SELF%/*}"
export LD_LIBRARY_PATH="${HERE}/usr/lib:${LD_LIBRARY_PATH}"
exec "${HERE}/usr/bin/0xfx_gui" "$@"
APPRUN_EOF
    chmod +x "${APPDIR}/AppRun"
}

# ── x86_64 AppImage ──
APPDIR="release/0xFX.AppDir"
rm -rf "${APPDIR}"
populate_appdir "${APPDIR}" "build/0xfx_gui"

ARCH=x86_64 "$APPIMAGETOOL" \
    --runtime-file "$RUNTIME_X86_64" \
    "${APPDIR}" "release/0xFX-${VERSION}-x86_64.AppImage"
echo "  -> release/0xFX-${VERSION}-x86_64.AppImage"

# ── aarch64 AppImage (cross-compiled binary from build_linux_arm64/) ──
echo ""
echo "--- Packaging Linux aarch64 AppImage ---"
if [ -x "build_linux_arm64/0xfx_gui" ]; then
    APPDIR_ARM64="release/0xFX-aarch64.AppDir"
    rm -rf "${APPDIR_ARM64}"
    populate_appdir "${APPDIR_ARM64}" "build_linux_arm64/0xfx_gui"

    ARCH=aarch64 "$APPIMAGETOOL" \
        --runtime-file "$RUNTIME_AARCH64" \
        "${APPDIR_ARM64}" "release/0xFX-${VERSION}-aarch64.AppImage"
    echo "  -> release/0xFX-${VERSION}-aarch64.AppImage"
else
    echo "  WARN: build_linux_arm64/0xfx_gui not found — skipping aarch64 AppImage."
    echo "  To build: cmake -B build_linux_arm64 -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-linux-gnu.cmake -DCMAKE_BUILD_TYPE=Release && cmake --build build_linux_arm64 -j\$(nproc)"
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
cp -r build_win/0xFX.vst3      "${WIN_DIR}/" 2>/dev/null || echo "  (no VST3 bundle)"
cp -r presets/factory           "${WIN_DIR}/presets/"
mkdir -p                        "${WIN_DIR}/resources/ir"
cp -r resources/ir/bundled      "${WIN_DIR}/resources/ir/"
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
echo "Upload to GitHub release:"
echo "  gh release upload v${VERSION} --clobber release/0xFX-${VERSION}-x86_64.AppImage release/0xFX-${VERSION}-aarch64.AppImage 2>/dev/null || \\"
echo "  gh release upload v${VERSION} --clobber release/0xFX-${VERSION}-x86_64.AppImage"
echo ""
echo "To create a GitHub release:"
echo "  git tag v${VERSION}"
echo "  git push origin v${VERSION}"
echo "  gh release create v${VERSION} release/0xFX-${VERSION}-* --title 'v${VERSION}' --generate-notes"
