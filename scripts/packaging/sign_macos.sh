#!/bin/bash
#
# sign_macos.sh — Codesign, notarize, and staple the macOS release.
#
# Run after package_macos.sh has produced release/0xFX.app, release/Plugins/*,
# and the .dmg/.zip artifacts. Re-packs the .dmg and .zip after signing so the
# distributed archives contain the signed + stapled bundles.
#
# Usage:
#   CODESIGN_IDENTITY="Developer ID Application: Your Name (TEAMID)" \
#   NOTARY_PROFILE="0xFX-notary" \
#   ./scripts/packaging/sign_macos.sh 1.3.0
#
# Environment:
#   CODESIGN_IDENTITY  Required. Full Developer ID Application identity string.
#                      Find via: security find-identity -v -p codesigning
#   NOTARY_PROFILE     Optional. Keychain profile name previously stored via
#                      `xcrun notarytool store-credentials`. If unset, skips
#                      the notarize + staple steps (useful for test signing).
#   ENTITLEMENTS       Optional. Path to entitlements plist.
#                      Default: resources/macos/0xfx.entitlements
#
# See docs/MACOS_SIGNING.md for the full one-time setup walkthrough.
#

set -euo pipefail

VERSION="${1:-}"
if [ -z "$VERSION" ]; then
    echo "Usage: CODESIGN_IDENTITY='Developer ID Application: ...' $0 <version>" >&2
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
DMG="${RELEASE_DIR}/0xFX-${VERSION}-macos-universal.dmg"
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
echo "  Identity:     ${CODESIGN_IDENTITY}"
echo "  Entitlements: ${ENTITLEMENTS}"
echo "  Notary:       ${NOTARY_PROFILE:-<skip — no NOTARY_PROFILE set>}"
echo ""

# ── Sign plugins (inside-out) ──
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

# ── Re-pack .dmg so it contains the signed app ──
echo ""
echo "--- Re-packing ${DMG} ---"
DMG_STAGE="${RELEASE_DIR}/dmg_stage"
rm -rf "$DMG_STAGE"
mkdir -p "$DMG_STAGE"
cp -R "$APP" "$DMG_STAGE/"
# Include signed plugin bundles so the DMG can be a complete install
# (standalone via Applications symlink, plugins via Plugins/).
[ -d "$PLUGINS_DIR" ] && cp -R "$PLUGINS_DIR" "$DMG_STAGE/"
[ -f README.md ] && cp README.md "$DMG_STAGE/"
[ -f LICENSE ] && cp LICENSE "$DMG_STAGE/"
[ -f "${RELEASE_DIR}/INSTALL.txt" ] && cp "${RELEASE_DIR}/INSTALL.txt" "$DMG_STAGE/"
ln -s /Applications "${DMG_STAGE}/Applications"

rm -f "$DMG"
hdiutil create -volname "0xFX v${VERSION}" \
    -srcfolder "$DMG_STAGE" \
    -ov -format UDZO \
    "$DMG" >/dev/null
rm -rf "$DMG_STAGE"
echo "  -> $DMG"

# The DMG itself should be signed too so Gatekeeper accepts the download.
codesign --force --timestamp --sign "$CODESIGN_IDENTITY" "$DMG"

# ── Notarize + staple ──
if [ -n "${NOTARY_PROFILE:-}" ]; then
    echo ""
    echo "--- Notarizing ${DMG} (this can take 1–5 minutes) ---"
    xcrun notarytool submit "$DMG" \
        --keychain-profile "$NOTARY_PROFILE" \
        --wait

    echo ""
    echo "--- Stapling ---"
    xcrun stapler staple "$DMG"
    xcrun stapler staple "$APP"
    # Staple each plugin bundle so they verify offline too.
    for bundle in "${PLUGINS_DIR}/0xFX.clap" \
                  "${PLUGINS_DIR}/0xFX.vst3" \
                  "${PLUGINS_DIR}/0xFX.component"; do
        [ -d "$bundle" ] && xcrun stapler staple "$bundle" || true
    done

    echo ""
    echo "--- Final Gatekeeper check ---"
    spctl --assess --type execute --verbose "$APP"
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
ls -lh "$DMG" "$ZIP"
echo ""
echo "Upload with: ./scripts/packaging/upload_release.sh ${VERSION}"
