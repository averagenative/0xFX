#!/bin/bash
#
# sign_macos.sh — Codesign, notarize, and staple the macOS release.
#
# Run after package_macos.sh has produced release/0xFX.app, release/Plugins/*,
# the .pkg installer, and the .zip. Re-builds and signs the .pkg with the
# Developer ID Installer cert, notarizes it, and staples the ticket. Re-zips
# with the signed bundles.
#
# Usage:
#   CODESIGN_IDENTITY="Developer ID Application: Your Name (TEAMID)" \
#   INSTALLER_IDENTITY="Developer ID Installer: Your Name (TEAMID)" \
#   NOTARY_PROFILE="0xFX-notary" \
#   ./scripts/packaging/sign_macos.sh 1.3.0
#
# Environment:
#   CODESIGN_IDENTITY   Required. Developer ID *Application* identity — signs
#                       the .app and plugin bundles.
#                       Find via: security find-identity -v -p codesigning
#   INSTALLER_IDENTITY  Required when building the signed .pkg. Developer ID
#                       *Installer* identity — a separate cert from the
#                       Application one. Same TEAMID.
#   NOTARY_PROFILE      Optional. Keychain profile name previously stored via
#                       `xcrun notarytool store-credentials`. If unset, skips
#                       the notarize + staple steps (useful for test signing).
#   ENTITLEMENTS        Optional. Path to entitlements plist.
#                       Default: resources/macos/0xfx.entitlements
#
# See docs/MACOS_SIGNING.md for the full one-time setup walkthrough.
#

set -euo pipefail

VERSION="${1:-}"
if [ -z "$VERSION" ]; then
    echo "Usage: CODESIGN_IDENTITY='Developer ID Application: ...' \\" >&2
    echo "       INSTALLER_IDENTITY='Developer ID Installer: ...' \\" >&2
    echo "       $0 <version>" >&2
    exit 2
fi

if [ -z "${CODESIGN_IDENTITY:-}" ]; then
    echo "ERROR: CODESIGN_IDENTITY is not set." >&2
    echo "  Find yours with: security find-identity -v -p codesigning" >&2
    exit 2
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$(dirname "$SCRIPT_DIR")")"
cd "$PROJECT_DIR"

RELEASE_DIR="release"
APP="${RELEASE_DIR}/0xFX.app"
PLUGINS_DIR="${RELEASE_DIR}/Plugins"
ENTITLEMENTS="${ENTITLEMENTS:-resources/macos/0xfx.entitlements}"
PKG="${RELEASE_DIR}/0xFX-${VERSION}-macos-universal.pkg"
ZIP="${RELEASE_DIR}/0xFX-${VERSION}-macos-universal.zip"

if [ ! -d "$APP" ]; then
    echo "ERROR: $APP not found. Run ./scripts/packaging/package_macos.sh ${VERSION} first." >&2
    exit 1
fi
if [ ! -f "$ENTITLEMENTS" ]; then
    echo "ERROR: Entitlements file not found at $ENTITLEMENTS" >&2
    exit 1
fi

echo "=== Signing 0xFX v${VERSION} ==="
echo "  App/plugin identity: ${CODESIGN_IDENTITY}"
echo "  Installer identity:  ${INSTALLER_IDENTITY:-<unsigned .pkg — set INSTALLER_IDENTITY>}"
echo "  Entitlements:        ${ENTITLEMENTS}"
echo "  Notary:              ${NOTARY_PROFILE:-<skip — no NOTARY_PROFILE set>}"
echo ""

# ── Sign plugin bundles ──
# Plugin bundles are self-contained — no extra dylibs inside — so a single
# codesign pass per bundle is sufficient.
echo "--- Signing plugin bundles ---"
for bundle in "${PLUGINS_DIR}/0xFX.clap" \
              "${PLUGINS_DIR}/0xFX.vst3" \
              "${PLUGINS_DIR}/0xFX.component"; do
    if [ -d "$bundle" ]; then
        codesign --force --options runtime --timestamp \
            --sign "$CODESIGN_IDENTITY" "$bundle"
        echo "  -> signed $bundle"
    fi
done

# ── Sign the standalone app with hardened runtime + entitlements ──
echo ""
echo "--- Signing ${APP} ---"
codesign --force --options runtime --timestamp \
    --entitlements "$ENTITLEMENTS" \
    --sign "$CODESIGN_IDENTITY" "$APP"

# ── Verify ──
echo ""
echo "--- Verifying signatures ---"
codesign --verify --deep --strict --verbose=2 "$APP"
spctl --assess --type execute --verbose "$APP" || {
    echo "  (spctl warning above is expected until notarization completes)"
}

# ── Rebuild the .pkg so it contains the signed bundles, and sign it ──
echo ""
echo "--- Rebuilding signed .pkg ---"
rm -f "$PKG"
if [ -n "${INSTALLER_IDENTITY:-}" ]; then
    INSTALLER_IDENTITY="$INSTALLER_IDENTITY" \
        "${SCRIPT_DIR}/build_pkg_macos.sh" "$VERSION"
else
    echo "  (INSTALLER_IDENTITY unset — building unsigned .pkg)" >&2
    "${SCRIPT_DIR}/build_pkg_macos.sh" "$VERSION"
fi

# ── Notarize + staple ──
if [ -n "${NOTARY_PROFILE:-}" ]; then
    echo ""
    echo "--- Notarizing ${PKG} (this can take 1–5 minutes) ---"
    xcrun notarytool submit "$PKG" \
        --keychain-profile "$NOTARY_PROFILE" \
        --wait

    echo ""
    echo "--- Stapling ---"
    # Staple the .pkg itself (so the download verifies offline) and each
    # bundle the .pkg contains (so installed apps/plugins verify offline
    # once Gatekeeper unpacks them).
    xcrun stapler staple "$PKG"
    xcrun stapler staple "$APP"
    for bundle in "${PLUGINS_DIR}/0xFX.clap" \
                  "${PLUGINS_DIR}/0xFX.vst3" \
                  "${PLUGINS_DIR}/0xFX.component"; do
        [ -d "$bundle" ] && xcrun stapler staple "$bundle" || true
    done

    echo ""
    echo "--- Final Gatekeeper check ---"
    spctl --assess --type execute --verbose "$APP"
    spctl --assess --type install --verbose "$PKG" || true
else
    echo ""
    echo "(skipping notarize/staple — set NOTARY_PROFILE to enable)"
fi

# ── Re-zip with signed (and stapled, if notarized) bundles ──
echo ""
echo "--- Re-packing ${ZIP} ---"
rm -f "$ZIP"
(cd "$RELEASE_DIR" && zip -r "$(basename "$ZIP")" \
    0xFX.app Plugins/ INSTALL.txt -q)
echo "  -> $ZIP"

echo ""
echo "=== Done ==="
ls -lh "$PKG" "$ZIP"
echo ""
echo "Upload with: ./scripts/packaging/upload_release.sh ${VERSION}"
