#!/bin/bash
#
# build_pkg_macos.sh — Build a macOS .pkg installer from an already-built
# release/0xFX.app + release/Plugins/.
#
# The installer is component-based so users can uncheck plugin formats they
# don't need. Payload goes to the standard system plugin paths so every DAW
# picks the plugins up automatically on next rescan.
#
# Usage:
#   ./scripts/packaging/build_pkg_macos.sh <version>
#
# Environment:
#   INSTALLER_IDENTITY  Optional. Developer ID Installer cert for signing.
#                       Example: "Developer ID Installer: Your Name (TEAMID)"
#                       When unset the .pkg is unsigned (still works but
#                       Gatekeeper will warn on first open).
#
# Output:
#   release/0xFX-<version>-macos-universal.pkg
#

set -euo pipefail

VERSION="${1:-}"
if [ -z "$VERSION" ]; then
    echo "Usage: $0 <version>" >&2
    exit 2
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$(dirname "$SCRIPT_DIR")")"
cd "$PROJECT_DIR"

RELEASE_DIR="release"
APP="${RELEASE_DIR}/0xFX.app"
PLUGINS_DIR="${RELEASE_DIR}/Plugins"
PKG_BUILD_DIR="${RELEASE_DIR}/pkg_build"
BUNDLE_ID_BASE="com.averagenative.0xfx"
FINAL_PKG="${RELEASE_DIR}/0xFX-${VERSION}-macos-universal.pkg"

if [ ! -d "$APP" ]; then
    echo "ERROR: ${APP} not found. Run package_macos.sh first." >&2
    exit 1
fi
for b in 0xFX.clap 0xFX.vst3 0xFX.component; do
    if [ ! -d "${PLUGINS_DIR}/${b}" ]; then
        echo "ERROR: ${PLUGINS_DIR}/${b} missing — plugin build didn't produce a bundle." >&2
        exit 1
    fi
done

echo "=== Building macOS .pkg installer v${VERSION} ==="
if [ -n "${INSTALLER_IDENTITY:-}" ]; then
    echo "  Installer identity: ${INSTALLER_IDENTITY}"
else
    echo "  (unsigned — set INSTALLER_IDENTITY to sign with 'Developer ID Installer: ...')"
fi
echo ""

rm -rf "$PKG_BUILD_DIR"
mkdir -p "$PKG_BUILD_DIR/components" "$PKG_BUILD_DIR/resources"

# ── Component package helper ──
# Stages one payload under the final install root and builds a flat pkg.
build_component() {
    local stage="$1"         # staging dir
    local install_path="$2"  # path inside stage that mirrors the final install root
    local payload_src="$3"   # source .app / .clap / .vst3 / .component
    local pkg_id="$4"
    local out_pkg="$5"

    mkdir -p "${stage}${install_path}"
    cp -R "$payload_src" "${stage}${install_path}/"

    pkgbuild \
        --root "$stage" \
        --identifier "$pkg_id" \
        --version "$VERSION" \
        --install-location "/" \
        "$out_pkg"
}

build_component \
    "$PKG_BUILD_DIR/stage_app" \
    "/Applications" \
    "$APP" \
    "${BUNDLE_ID_BASE}.app.pkg" \
    "$PKG_BUILD_DIR/components/0xFX-app.pkg"

build_component \
    "$PKG_BUILD_DIR/stage_clap" \
    "/Library/Audio/Plug-Ins/CLAP" \
    "${PLUGINS_DIR}/0xFX.clap" \
    "${BUNDLE_ID_BASE}.clap.pkg" \
    "$PKG_BUILD_DIR/components/0xFX-clap.pkg"

build_component \
    "$PKG_BUILD_DIR/stage_vst3" \
    "/Library/Audio/Plug-Ins/VST3" \
    "${PLUGINS_DIR}/0xFX.vst3" \
    "${BUNDLE_ID_BASE}.vst3.pkg" \
    "$PKG_BUILD_DIR/components/0xFX-vst3.pkg"

build_component \
    "$PKG_BUILD_DIR/stage_au" \
    "/Library/Audio/Plug-Ins/Components" \
    "${PLUGINS_DIR}/0xFX.component" \
    "${BUNDLE_ID_BASE}.au.pkg" \
    "$PKG_BUILD_DIR/components/0xFX-au.pkg"

# ── Distribution XML ──
cat > "$PKG_BUILD_DIR/distribution.xml" <<XML
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>0xFX ${VERSION}</title>
    <organization>com.averagenative</organization>
    <domains enable_localSystem="true"/>
    <options customize="allow" require-scripts="false"
             rootVolumeOnly="true" hostArchitectures="x86_64,arm64"/>
    <volume-check>
        <allowed-os-versions>
            <os-version min="11.0"/>
        </allowed-os-versions>
    </volume-check>
    <welcome file="welcome.html"/>
    <license file="LICENSE.txt"/>
    <conclusion file="conclusion.html"/>
    <choices-outline>
        <line choice="app"/>
        <line choice="clap"/>
        <line choice="vst3"/>
        <line choice="au"/>
    </choices-outline>
    <choice id="app" title="0xFX Standalone App"
            description="Install 0xFX.app into /Applications.">
        <pkg-ref id="${BUNDLE_ID_BASE}.app.pkg"/>
    </choice>
    <choice id="clap" title="CLAP Plugin"
            description="Install 0xFX.clap for CLAP-compatible DAWs (Bitwig, Reaper, Qtractor).">
        <pkg-ref id="${BUNDLE_ID_BASE}.clap.pkg"/>
    </choice>
    <choice id="vst3" title="VST3 Plugin"
            description="Install 0xFX.vst3 for VST3-compatible DAWs (Reaper, Cubase, Studio One, FL Studio).">
        <pkg-ref id="${BUNDLE_ID_BASE}.vst3.pkg"/>
    </choice>
    <choice id="au" title="Audio Unit"
            description="Install 0xFX.component for AU-compatible DAWs (Logic Pro, GarageBand, MainStage).">
        <pkg-ref id="${BUNDLE_ID_BASE}.au.pkg"/>
    </choice>
    <pkg-ref id="${BUNDLE_ID_BASE}.app.pkg"  version="${VERSION}" auth="Root">0xFX-app.pkg</pkg-ref>
    <pkg-ref id="${BUNDLE_ID_BASE}.clap.pkg" version="${VERSION}" auth="Root">0xFX-clap.pkg</pkg-ref>
    <pkg-ref id="${BUNDLE_ID_BASE}.vst3.pkg" version="${VERSION}" auth="Root">0xFX-vst3.pkg</pkg-ref>
    <pkg-ref id="${BUNDLE_ID_BASE}.au.pkg"   version="${VERSION}" auth="Root">0xFX-au.pkg</pkg-ref>
</installer-gui-script>
XML

# ── Installer UI resources ──
cat > "$PKG_BUILD_DIR/resources/welcome.html" <<HTML
<!DOCTYPE html>
<html><body style="font-family:-apple-system,sans-serif;padding:20px;font-size:13px;">
<h2 style="margin-top:0;">0xFX ${VERSION}</h2>
<p>This installer places the 0xFX standalone app and audio plug-ins onto your Mac.</p>
<p>By default all components install. Click <b>Customize</b> to pick only the plug-in formats your DAW uses.</p>
<h3>Install locations</h3>
<ul style="line-height:1.6em;">
    <li><b>App:</b> /Applications/0xFX.app</li>
    <li><b>CLAP:</b> /Library/Audio/Plug-Ins/CLAP/0xFX.clap</li>
    <li><b>VST3:</b> /Library/Audio/Plug-Ins/VST3/0xFX.vst3</li>
    <li><b>Audio Unit:</b> /Library/Audio/Plug-Ins/Components/0xFX.component</li>
</ul>
<p style="color:#666;"><small>This build is not Apple-notarized yet. On first launch you may need to approve it under <i>System Settings &rarr; Privacy &amp; Security &rarr; Open Anyway</i>.</small></p>
</body></html>
HTML

cat > "$PKG_BUILD_DIR/resources/conclusion.html" <<HTML
<!DOCTYPE html>
<html><body style="font-family:-apple-system,sans-serif;padding:20px;font-size:13px;">
<h2 style="margin-top:0;">Installation complete</h2>
<p>0xFX is ready to use.</p>
<ul style="line-height:1.7em;">
    <li><b>Standalone:</b> open from Launchpad or /Applications/0xFX.app.</li>
    <li><b>Plug-ins:</b> restart your DAW and rescan plug-ins.</li>
</ul>
<p>If macOS blocks the app with <i>&ldquo;cannot be opened because the developer cannot be verified&rdquo;</i>, open <b>System Settings &rarr; Privacy &amp; Security</b> and click <b>Open Anyway</b>.</p>
</body></html>
HTML

if [ -f LICENSE ]; then
    cp LICENSE "$PKG_BUILD_DIR/resources/LICENSE.txt"
else
    # productbuild requires the license file exist when referenced from
    # Distribution.xml — synthesize a placeholder if none is checked in.
    echo "0xFX ${VERSION}" > "$PKG_BUILD_DIR/resources/LICENSE.txt"
fi

# ── Product archive ──
echo "--- Building product archive ---"
if [ -n "${INSTALLER_IDENTITY:-}" ]; then
    productbuild \
        --distribution "$PKG_BUILD_DIR/distribution.xml" \
        --package-path "$PKG_BUILD_DIR/components" \
        --resources "$PKG_BUILD_DIR/resources" \
        --sign "$INSTALLER_IDENTITY" \
        --timestamp \
        "$FINAL_PKG"
else
    productbuild \
        --distribution "$PKG_BUILD_DIR/distribution.xml" \
        --package-path "$PKG_BUILD_DIR/components" \
        --resources "$PKG_BUILD_DIR/resources" \
        "$FINAL_PKG"
fi

# Cleanup intermediate staging
rm -rf "$PKG_BUILD_DIR"

echo ""
echo "=== Done ==="
ls -lh "$FINAL_PKG"
