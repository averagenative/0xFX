#!/bin/bash
#
# package_release.sh — Build and package 0xFX for release.
#
# Usage:
#   ./scripts/packaging/package_release.sh [version] [--arch <x64|arm64|all>]
#
# Default arch is "all" — builds every architecture for which a toolchain
# is available on this machine. Missing toolchains are skipped with a warning;
# they never fail the script.
#
# Produces (into release/):
#   Linux x64:     0xFX-<ver>-linux-x64.tar.gz        + 0xFX-<ver>-x86_64.AppImage
#   Linux arm64:   0xFX-<ver>-linux-arm64.tar.gz      + 0xFX-<ver>-aarch64.AppImage
#   Windows x64:   0xFX-<ver>-windows-x64.zip         + 0xFX-<ver>-windows-x64-setup.exe
#   Windows arm64: 0xFX-<ver>-windows-arm64.zip       + 0xFX-<ver>-windows-arm64-setup.exe
#
# macOS is produced by package_macos.sh (must run on a Mac).
#

set -e

# ── Parse args ─────────────────────────────────────────────────────────
VERSION=""
ARCH_FILTER="all"
while [ $# -gt 0 ]; do
    case "$1" in
        --arch)
            ARCH_FILTER="$2"
            shift 2
            ;;
        --arch=*)
            ARCH_FILTER="${1#*=}"
            shift
            ;;
        -h|--help)
            sed -n '2,15p' "$0" | sed 's/^# \{0,1\}//'
            exit 0
            ;;
        *)
            if [ -z "$VERSION" ]; then
                VERSION="$1"
            else
                echo "Unknown arg: $1" >&2
                exit 2
            fi
            shift
            ;;
    esac
done
VERSION="${VERSION:-0.1.0}"

case "$ARCH_FILTER" in
    x64|arm64|all) ;;
    *) echo "ERROR: --arch must be one of: x64, arm64, all (got '$ARCH_FILTER')" >&2; exit 2 ;;
esac

BUILD_X64=false
BUILD_ARM64=false
case "$ARCH_FILTER" in
    all)   BUILD_X64=true; BUILD_ARM64=true ;;
    x64)   BUILD_X64=true ;;
    arm64) BUILD_ARM64=true ;;
esac

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$(dirname "$SCRIPT_DIR")")"
cd "$PROJECT_DIR"

echo "=== 0xFX Release Packaging v${VERSION} (arch=${ARCH_FILTER}) ==="
echo ""

# ── Toolchain detection ────────────────────────────────────────────────
detect_llvm_mingw() {
    if [ -n "$LLVM_MINGW_PREFIX" ] && [ -x "$LLVM_MINGW_PREFIX/bin/aarch64-w64-mingw32-clang" ]; then
        echo "$LLVM_MINGW_PREFIX"
        return 0
    fi
    for candidate in /opt/llvm-mingw "$HOME"/tools/llvm-mingw-* "$HOME"/.local/llvm-mingw-*; do
        if [ -x "$candidate/bin/aarch64-w64-mingw32-clang" ]; then
            echo "$candidate"
            return 0
        fi
    done
    return 1
}

HAVE_MINGW_X64=false
HAVE_LINUX_ARM64=false
LLVM_MINGW_FOUND=""
command -v x86_64-w64-mingw32-gcc &>/dev/null && HAVE_MINGW_X64=true
command -v aarch64-linux-gnu-gcc  &>/dev/null && HAVE_LINUX_ARM64=true
LLVM_MINGW_FOUND="$(detect_llvm_mingw 2>/dev/null || true)"

echo "Toolchains:"
echo "  Linux x64   (native):              yes"
$HAVE_LINUX_ARM64 && echo "  Linux arm64 (aarch64-linux-gnu):   yes" || echo "  Linux arm64:                       no (install gcc-aarch64-linux-gnu to enable)"
$HAVE_MINGW_X64   && echo "  Windows x64 (mingw-w64):           yes" || echo "  Windows x64:                       no (install mingw-w64 to enable)"
[ -n "$LLVM_MINGW_FOUND" ] && echo "  Windows arm64 (llvm-mingw):        yes ($LLVM_MINGW_FOUND)" || echo "  Windows arm64:                     no (install llvm-mingw to enable)"
echo ""

# ── Clean ──
rm -rf release
mkdir -p release

have_appimagetool() {
    APPIMAGETOOL=""
    if command -v appimagetool &>/dev/null; then
        APPIMAGETOOL="appimagetool"; return 0
    elif [ -x "$HOME/tools/appimagetool" ]; then
        APPIMAGETOOL="$HOME/tools/appimagetool"; return 0
    fi
    return 1
}

ensure_type2_runtime() {
    local arch="$1"
    local runtime="$HOME/tools/runtime-fuse3-${arch}"
    mkdir -p "$HOME/tools"
    if [ ! -f "$runtime" ]; then
        echo "  Downloading type2-runtime for ${arch}..."
        wget -q "https://github.com/AppImage/type2-runtime/releases/download/continuous/runtime-${arch}" -O "$runtime"
        chmod +x "$runtime"
    fi
    echo "$runtime"
}

populate_appdir() {
    local APPDIR="$1"
    local BINARY="$2"

    mkdir -p "${APPDIR}/usr/bin/presets"
    mkdir -p "${APPDIR}/usr/bin/resources/ir"
    mkdir -p "${APPDIR}/usr/share/applications"
    mkdir -p "${APPDIR}/usr/share/icons/hicolor/256x256/apps"

    cp "$BINARY" "${APPDIR}/usr/bin/0xfx_gui"
    cp -r presets/factory            "${APPDIR}/usr/bin/presets/"
    cp -r resources/ir/bundled       "${APPDIR}/usr/bin/resources/ir/"

    cp resources/icon/0xfx_256.png   "${APPDIR}/0xfx.png"
    cp resources/icon/0xfx_256.png   "${APPDIR}/usr/share/icons/hicolor/256x256/apps/0xfx.png"
    ln -sf 0xfx.png                  "${APPDIR}/.DirIcon"

    cp resources/0xFX.desktop        "${APPDIR}/0xfx.desktop"
    sed -i 's/^Exec=.*/Exec=0xfx_gui/' "${APPDIR}/0xfx.desktop"
    cp "${APPDIR}/0xfx.desktop"      "${APPDIR}/usr/share/applications/"

    cat > "${APPDIR}/AppRun" << 'APPRUN_EOF'
#!/bin/bash
SELF="$(readlink -f "$0")"
HERE="${SELF%/*}"
export LD_LIBRARY_PATH="${HERE}/usr/lib:${LD_LIBRARY_PATH}"
exec "${HERE}/usr/bin/0xfx_gui" "$@"
APPRUN_EOF
    chmod +x "${APPDIR}/AppRun"
}

populate_linux_tarball_dir() {
    local DIR="$1"
    local BUILD="$2"
    mkdir -p "${DIR}/presets" "${DIR}/resources/ir"
    cp    "${BUILD}/0xfx_gui"          "${DIR}/" 2>/dev/null || echo "  (no standalone GUI binary)"
    cp    "${BUILD}/0xfx_standalone"   "${DIR}/" 2>/dev/null || echo "  (no standalone console binary)"
    cp    "${BUILD}/0xFX.clap"         "${DIR}/" 2>/dev/null || echo "  (no CLAP)"
    cp -r "${BUILD}/0xFX.vst3"         "${DIR}/" 2>/dev/null || echo "  (no VST3 bundle)"
    cp -r presets/factory              "${DIR}/presets/"
    cp -r resources/ir/bundled         "${DIR}/resources/ir/"
    cp    README.md LICENSE            "${DIR}/"
    cat > "${DIR}/INSTALL.txt" << 'EOF'
0xFX — Linux Installation
==============================

Standalone:
  chmod +x 0xfx_gui
  ./0xfx_gui

Plugins:
  CLAP: cp 0xFX.clap ~/.clap/
  VST3: cp -r 0xFX.vst3 ~/.vst3/

Presets are alongside the binary in presets/factory/
or install to ~/.local/share/0xFX/presets/
EOF
}

populate_windows_zip_dir() {
    local DIR="$1"
    local BUILD="$2"
    local ARCH_PATH="$3"   # x86_64-win or arm64-win (for INSTALL.txt)
    mkdir -p "${DIR}/presets" "${DIR}/resources/ir"
    cp    "${BUILD}/0xfx_gui.exe"      "${DIR}/" 2>/dev/null || echo "  (no standalone GUI)"
    cp    resources/icon/0xfx.ico      "${DIR}/" 2>/dev/null || true
    cp    "${BUILD}/0xFX.clap"         "${DIR}/" 2>/dev/null || echo "  (no CLAP)"
    cp -r "${BUILD}/0xFX.vst3"         "${DIR}/" 2>/dev/null || echo "  (no VST3 bundle)"
    cp -r presets/factory              "${DIR}/presets/"
    cp -r resources/ir/bundled         "${DIR}/resources/ir/"
    cp    README.md LICENSE            "${DIR}/"
    cat > "${DIR}/INSTALL.txt" << EOF
0xFX — Windows Installation
================================

Standalone:
  Double-click 0xfx_gui.exe to run.
  Presets are in the presets\\factory\\ folder.

Plugins (manual install):
  CLAP: Copy 0xFX.clap to C:\\Program Files\\Common Files\\CLAP\\
  VST3: Copy 0xFX.vst3 to C:\\Program Files\\Common Files\\VST3\\0xFX.vst3\\Contents\\${ARCH_PATH}\\

Or use the installer (0xFX-*-setup.exe) which handles everything automatically.

Recordings save to a "recordings" folder next to 0xfx_gui.exe.
EOF
}

# ══════════════════════════════════════════════════════════════════════
# LINUX x64
# ══════════════════════════════════════════════════════════════════════
if $BUILD_X64; then
    echo "--- Building Linux x64 (native) ---"
    cmake -B build -DCMAKE_BUILD_TYPE=Release 2>&1 | tail -3
    cmake --build build -j$(nproc) 2>&1 | tail -5

    echo "--- Running tests ---"
    (cd build && ctest --output-on-failure 2>&1 | tail -3)

    LINUX_DIR="release/0xFX-${VERSION}-linux-x64"
    populate_linux_tarball_dir "${LINUX_DIR}" "build"
    (cd release && tar czf "0xFX-${VERSION}-linux-x64.tar.gz" "0xFX-${VERSION}-linux-x64")
    echo "  -> release/0xFX-${VERSION}-linux-x64.tar.gz"

    if have_appimagetool; then
        RUNTIME_X86_64="$(ensure_type2_runtime x86_64)"
        APPDIR="release/0xFX.AppDir"
        rm -rf "${APPDIR}"
        populate_appdir "${APPDIR}" "build/0xfx_gui"
        ARCH=x86_64 "$APPIMAGETOOL" \
            --runtime-file "$RUNTIME_X86_64" \
            "${APPDIR}" "release/0xFX-${VERSION}-x86_64.AppImage"
        echo "  -> release/0xFX-${VERSION}-x86_64.AppImage"
    else
        echo "  WARN: appimagetool not found — skipping x86_64 AppImage"
    fi
    echo ""
fi

# ══════════════════════════════════════════════════════════════════════
# LINUX arm64
# ══════════════════════════════════════════════════════════════════════
if $BUILD_ARM64; then
    if $HAVE_LINUX_ARM64; then
        echo "--- Building Linux arm64 (aarch64-linux-gnu cross-compile) ---"
        cmake -B build_linux_arm64 \
            -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-linux-gnu.cmake \
            -DCMAKE_BUILD_TYPE=Release 2>&1 | tail -3
        cmake --build build_linux_arm64 -j$(nproc) 2>&1 | tail -5

        LINUX_ARM64_DIR="release/0xFX-${VERSION}-linux-arm64"
        populate_linux_tarball_dir "${LINUX_ARM64_DIR}" "build_linux_arm64"
        (cd release && tar czf "0xFX-${VERSION}-linux-arm64.tar.gz" "0xFX-${VERSION}-linux-arm64")
        echo "  -> release/0xFX-${VERSION}-linux-arm64.tar.gz"

        if have_appimagetool; then
            RUNTIME_AARCH64="$(ensure_type2_runtime aarch64)"
            APPDIR_ARM64="release/0xFX-aarch64.AppDir"
            rm -rf "${APPDIR_ARM64}"
            populate_appdir "${APPDIR_ARM64}" "build_linux_arm64/0xfx_gui"
            ARCH=aarch64 "$APPIMAGETOOL" \
                --runtime-file "$RUNTIME_AARCH64" \
                "${APPDIR_ARM64}" "release/0xFX-${VERSION}-aarch64.AppImage"
            echo "  -> release/0xFX-${VERSION}-aarch64.AppImage"
        fi
        echo ""
    else
        echo "--- Skipping Linux arm64 (no aarch64-linux-gnu toolchain) ---"
        echo "  Install on Fedora: sudo dnf install gcc-aarch64-linux-gnu gcc-c++-aarch64-linux-gnu"
        echo "  Install on Ubuntu: sudo apt install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu"
        echo ""
    fi
fi

# ══════════════════════════════════════════════════════════════════════
# WINDOWS x64
# ══════════════════════════════════════════════════════════════════════
if $BUILD_X64; then
    if $HAVE_MINGW_X64; then
        echo "--- Building Windows x64 (MinGW cross-compile) ---"
        cmake -B build_win -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64.cmake -DCMAKE_BUILD_TYPE=Release 2>&1 | tail -3
        cmake --build build_win -j$(nproc) 2>&1 | tail -5

        WIN_DIR="release/0xFX-${VERSION}-windows-x64"
        populate_windows_zip_dir "${WIN_DIR}" "build_win" "x86_64-win"
        (cd release && zip -r "0xFX-${VERSION}-windows-x64.zip" "0xFX-${VERSION}-windows-x64" -q)
        echo "  -> release/0xFX-${VERSION}-windows-x64.zip"

        if command -v makensis &>/dev/null; then
            sed -i "s/!define VER \".*\"/!define VER \"${VERSION}\"/"   scripts/packaging/0xfx_installer.nsi
            sed -i "s/!define VERFULL \".*\"/!define VERFULL \"${VERSION}.0\"/" scripts/packaging/0xfx_installer.nsi
            makensis scripts/packaging/0xfx_installer.nsi 2>&1 | tail -5
            echo "  -> release/0xFX-${VERSION}-windows-x64-setup.exe"
        else
            echo "  WARN: makensis not found — skipping Windows x64 installer"
            echo "  Install on Fedora: sudo dnf install mingw32-nsis  (or nsis)"
        fi
        echo ""
    else
        echo "--- Skipping Windows x64 (no mingw-w64 toolchain) ---"
        echo "  Install on Fedora: sudo dnf install mingw64-gcc mingw64-gcc-c++ mingw64-SDL2"
        echo "  Install on Ubuntu: sudo apt install mingw-w64 libsdl2-dev"
        echo ""
    fi
fi

# ══════════════════════════════════════════════════════════════════════
# WINDOWS arm64
# ══════════════════════════════════════════════════════════════════════
if $BUILD_ARM64; then
    if [ -n "$LLVM_MINGW_FOUND" ]; then
        echo "--- Building Windows arm64 (llvm-mingw cross-compile) ---"
        LLVM_MINGW_PREFIX="$LLVM_MINGW_FOUND" cmake -B build_win_arm64 \
            -DCMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw-arm64.cmake \
            -DCMAKE_BUILD_TYPE=Release 2>&1 | tail -3
        cmake --build build_win_arm64 -j$(nproc) 2>&1 | tail -5

        WIN_ARM64_DIR="release/0xFX-${VERSION}-windows-arm64"
        populate_windows_zip_dir "${WIN_ARM64_DIR}" "build_win_arm64" "arm64-win"
        (cd release && zip -r "0xFX-${VERSION}-windows-arm64.zip" "0xFX-${VERSION}-windows-arm64" -q)
        echo "  -> release/0xFX-${VERSION}-windows-arm64.zip"

        if command -v makensis &>/dev/null; then
            sed -i "s/!define VER \".*\"/!define VER \"${VERSION}\"/"   scripts/packaging/0xfx_installer_arm64.nsi
            sed -i "s/!define VERFULL \".*\"/!define VERFULL \"${VERSION}.0\"/" scripts/packaging/0xfx_installer_arm64.nsi
            makensis scripts/packaging/0xfx_installer_arm64.nsi 2>&1 | tail -5
            echo "  -> release/0xFX-${VERSION}-windows-arm64-setup.exe"
        fi
        echo ""
    else
        echo "--- Skipping Windows arm64 (no llvm-mingw toolchain) ---"
        echo "  Download from: https://github.com/mstorsjo/llvm-mingw/releases"
        echo "  Extract to ~/tools/ or /opt/ and re-run, or set LLVM_MINGW_PREFIX"
        echo ""
    fi
fi

# ══════════════════════════════════════════════════════════════════════
# SUMMARY
# ══════════════════════════════════════════════════════════════════════
echo "=== Release artifacts ==="
ls -lh release/*.tar.gz release/*.zip release/*.exe release/*.AppImage 2>/dev/null || echo "  (none)"
echo ""
echo "Upload all to the v${VERSION} GitHub release:"
echo "  ./scripts/packaging/upload_release.sh ${VERSION}"
echo ""
echo "Or create + upload the release in one shot:"
echo "  git tag v${VERSION} && git push origin v${VERSION}"
echo "  gh release create v${VERSION} release/0xFX-${VERSION}-* --title 'v${VERSION}' --generate-notes"
