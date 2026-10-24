#!/usr/bin/env bash
# Notarizes and staples the signed bundles in the given directory.
# Needs NOTARY_APPLE_ID, NOTARY_PASSWORD (app-specific password) and NOTARY_TEAM_ID.
set -euo pipefail

DIST="${1:?usage: macos_notarize.sh <dist-dir>}"
ZIP="$RUNNER_TEMP/draco-notarize.zip"

shopt -s nullglob
bundles=("$DIST"/*.vst3 "$DIST"/*.component "$DIST"/*.app)
rm -f "$ZIP"
(cd "$DIST" && zip -qry "$ZIP" ./*.vst3 ./*.component ./*.app)

xcrun notarytool submit "$ZIP" \
  --apple-id "$NOTARY_APPLE_ID" \
  --password "$NOTARY_PASSWORD" \
  --team-id "$NOTARY_TEAM_ID" \
  --wait

for bundle in "${bundles[@]}"; do
  xcrun stapler staple "$bundle"
done
