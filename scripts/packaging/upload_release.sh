#!/bin/bash
#
# upload_release.sh — Upload all release artifacts for a given version
# to the matching GitHub release (v<version>).
#
# Usage:
#   ./scripts/packaging/upload_release.sh <version>
#
# Uploads every file under release/ whose name starts with "0xFX-<version>-"
# using `gh release upload --clobber`. Missing artifacts are fine — we just
# upload whatever the local packaging step produced.
#
# Expects the tag/release to already exist:
#   git tag v<version> && git push origin v<version>
#   gh release create v<version> --title "v<version>" --generate-notes
#

set -e

VERSION="$1"
if [ -z "$VERSION" ]; then
    echo "Usage: $0 <version>" >&2
    exit 2
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$(dirname "$SCRIPT_DIR")")"
cd "$PROJECT_DIR"

if ! command -v gh &>/dev/null; then
    echo "ERROR: gh CLI not found. Install from https://cli.github.com/" >&2
    exit 1
fi

if ! gh release view "v${VERSION}" &>/dev/null; then
    echo "ERROR: GitHub release v${VERSION} does not exist."
    echo "  Create it first:"
    echo "    git tag v${VERSION} && git push origin v${VERSION}"
    echo "    gh release create v${VERSION} --title 'v${VERSION}' --generate-notes"
    exit 1
fi

# Collect every artifact matching the naming convention.
shopt -s nullglob
ARTIFACTS=(release/"0xFX-${VERSION}"-*.tar.gz
           release/"0xFX-${VERSION}"-*.zip
           release/"0xFX-${VERSION}"-*.exe
           release/"0xFX-${VERSION}"-*.AppImage
           release/"0xFX-${VERSION}"-*.dmg
           release/"0xFX-${VERSION}"-*.pkg)

if [ ${#ARTIFACTS[@]} -eq 0 ]; then
    echo "ERROR: No artifacts found matching release/0xFX-${VERSION}-*" >&2
    echo "  Run: ./scripts/packaging/package_release.sh ${VERSION}"
    exit 1
fi

echo "=== Uploading artifacts for v${VERSION} ==="
for f in "${ARTIFACTS[@]}"; do
    echo "  -> $(basename "$f") ($(du -h "$f" | cut -f1))"
done
echo ""

gh release upload "v${VERSION}" --clobber "${ARTIFACTS[@]}"

echo ""
echo "Done. View the release:"
echo "  gh release view v${VERSION} --web"
