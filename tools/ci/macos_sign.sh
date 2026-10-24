#!/usr/bin/env bash
# Signs every bundle in the given directory.
# With MACOS_CERTIFICATE_P12 (base64 .p12), MACOS_CERTIFICATE_PASSWORD and
# MACOS_SIGNING_IDENTITY ("Developer ID Application: Name (TEAMID)") set, the
# bundles are signed with a Developer ID and hardened runtime so they can be
# notarized. Otherwise they are ad-hoc signed (enough to load locally and on
# Apple Silicon, but Gatekeeper will warn on other machines).
set -euo pipefail

DIST="${1:?usage: macos_sign.sh <dist-dir>}"
ENTITLEMENTS="$(cd "$(dirname "$0")" && pwd)/entitlements.plist"

if [[ -n "${MACOS_CERTIFICATE_P12:-}" ]]; then
  KEYCHAIN="$RUNNER_TEMP/draco-signing.keychain-db"
  KEYCHAIN_PASSWORD="$(openssl rand -base64 24)"
  echo "$MACOS_CERTIFICATE_P12" | base64 --decode > "$RUNNER_TEMP/cert.p12"
  security create-keychain -p "$KEYCHAIN_PASSWORD" "$KEYCHAIN"
  security set-keychain-settings -lut 21600 "$KEYCHAIN"
  security unlock-keychain -p "$KEYCHAIN_PASSWORD" "$KEYCHAIN"
  security import "$RUNNER_TEMP/cert.p12" -P "$MACOS_CERTIFICATE_PASSWORD" -A -t cert -f pkcs12 -k "$KEYCHAIN"
  security set-key-partition-list -S apple-tool:,apple: -k "$KEYCHAIN_PASSWORD" "$KEYCHAIN"
  security list-keychains -d user -s "$KEYCHAIN" $(security list-keychains -d user | tr -d '"')
  rm -f "$RUNNER_TEMP/cert.p12"
  IDENTITY="$MACOS_SIGNING_IDENTITY"
  SIGN_ARGS=(--force --timestamp --options runtime --entitlements "$ENTITLEMENTS" --sign "$IDENTITY")
  echo "Signing with Developer ID: $IDENTITY"
else
  SIGN_ARGS=(--force --sign -)
  echo "No signing certificate configured; ad-hoc signing."
fi

shopt -s nullglob
for bundle in "$DIST"/*.vst3 "$DIST"/*.component "$DIST"/*.app; do
  echo "codesign $bundle"
  codesign "${SIGN_ARGS[@]}" "$bundle"
  codesign --verify --deep --strict --verbose=2 "$bundle"
done
