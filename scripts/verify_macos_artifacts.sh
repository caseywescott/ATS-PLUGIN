#!/usr/bin/env bash
# verify_macos_artifacts.sh — read-only build-tree gate checks.
# Normative: follows docs/MACOS_BUILD_INSTALL_SPEC.md §7.
#
# Usage: ./scripts/verify_macos_artifacts.sh --build-dir build-macos-arm64 --arch arm64
set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR=""
ARCH="arm64"

usage() {
    echo "Usage: $0 --build-dir <dir> --arch <arm64|x86_64>" >&2
    exit 1
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --arch)      ARCH="$2";      shift 2 ;;
        *) usage ;;
    esac
done

[[ -n "$BUILD_DIR" ]] || usage
[[ -d "$BUILD_DIR" ]] || { echo "Build directory not found: $BUILD_DIR" >&2; exit 1; }

log() { printf '\033[1;34m▶ %s\033[0m\n' "$*"; }
ok()  { printf '\033[1;32m✔ %s\033[0m\n' "$*"; }
die() { printf '\033[1;31m✘ %s\033[0m\n' "$*" >&2; exit 1; }

APP="$BUILD_DIR/src/host/standalone/ATS Resynth.app"
AU="$BUILD_DIR/src/host/au/ATS Resynth.component"
APP_BIN="$APP/Contents/MacOS/ATS Resynth"
APP_DYLIB="$APP/Contents/Frameworks/libsndfile.1.dylib"
AU_BIN="$AU/Contents/MacOS/ATS Resynth"
AU_DYLIB="$AU/Contents/Frameworks/libsndfile.1.dylib"

log "Verifying build-tree artifacts (build-dir=$(basename "$BUILD_DIR") arch=$ARCH)"

# Artifact existence
[[ -d "$APP" ]] || die "Standalone not found: $APP"
ok "Standalone exists"
[[ -d "$AU" ]]  || die "AU not found: $AU"
ok "AU exists"

# Architecture — use lipo -archs, not file(1)
APP_ARCHS=$(lipo -archs "$APP_BIN")
[[ "$APP_ARCHS" == "$ARCH" ]] || die "Standalone arch '$APP_ARCHS' != expected '$ARCH'"
ok "Standalone arch = $ARCH"

AU_ARCHS=$(lipo -archs "$AU_BIN")
[[ "$AU_ARCHS" == "$ARCH" ]] || die "AU arch '$AU_ARCHS' != expected '$ARCH'"
ok "AU arch = $ARCH"

DYLIB_ARCHS=$(lipo -archs "$AU_DYLIB")
[[ "$DYLIB_ARCHS" == "$ARCH" ]] || die "libsndfile arch '$DYLIB_ARCHS' != expected '$ARCH'"
ok "AU libsndfile arch = $ARCH"

APP_DYLIB_ARCHS=$(lipo -archs "$APP_DYLIB")
[[ "$APP_DYLIB_ARCHS" == "$ARCH" ]] || \
    die "Standalone libsndfile arch '$APP_DYLIB_ARCHS' != expected '$ARCH'"
ok "Standalone libsndfile arch = $ARCH"

# Plist integrity
plutil -lint "$APP/Contents/Info.plist" > /dev/null || die "Standalone plist invalid"
ok "Standalone plist valid"
plutil -lint "$AU/Contents/Info.plist" > /dev/null || die "AU plist invalid"
ok "AU plist valid"

# Identity is a host compatibility contract, not just plist syntax.
AU_TYPE=$(plutil -extract AudioComponents.0.type raw "$AU/Contents/Info.plist" 2>/dev/null || echo "MISSING")
AU_SUBTYPE=$(plutil -extract AudioComponents.0.subtype raw "$AU/Contents/Info.plist" 2>/dev/null || echo "MISSING")
AU_MANUFACTURER=$(plutil -extract AudioComponents.0.manufacturer raw "$AU/Contents/Info.plist" 2>/dev/null || echo "MISSING")
[[ "$AU_TYPE" == "aumf" ]] || die "AU type '$AU_TYPE' != expected 'aumf'"
[[ "$AU_SUBTYPE" == "Atr1" ]] || die "AU subtype '$AU_SUBTYPE' != expected 'Atr1'"
[[ "$AU_MANUFACTURER" == "AtPl" ]] || die "AU manufacturer '$AU_MANUFACTURER' != expected 'AtPl'"
ok "AU identity = aumf/Atr1/AtPl"

# Codesign
codesign --verify --deep --strict --verbose=2 "$APP" 2>/dev/null || die "Standalone signature invalid"
ok "Standalone signature valid"
codesign --verify --deep --strict --verbose=2 "$AU" 2>/dev/null || die "AU signature invalid"
ok "AU signature valid"

# No AppleDouble sidecars
count=$(find "$AU" -name '._*' 2>/dev/null | wc -l | tr -d ' ')
[[ "$count" -eq 0 ]] || die "AppleDouble files in AU bundle ($count found)"
ok "No AppleDouble files in AU"

count=$(find "$APP" -name '._*' 2>/dev/null | wc -l | tr -d ' ')
[[ "$count" -eq 0 ]] || die "AppleDouble files in Standalone ($count found)"
ok "No AppleDouble files in Standalone"

# No absolute build/vendor/Homebrew dependency paths
bad=$(otool -L "$AU_BIN" | grep -E '^[[:space:]]+(/Volumes/|/Users/|/opt/homebrew/|/usr/local/)' | \
      grep -v '@rpath\|@loader_path\|@executable_path' || true)
[[ -z "$bad" ]] || die "Absolute dependency paths in AU binary:\n$bad"
ok "No absolute dependency paths in AU binary"

bad=$(otool -L "$APP_BIN" | grep -E '^[[:space:]]+(/Volumes/|/Users/|/opt/homebrew/|/usr/local/)' | \
      grep -v '@rpath\|@loader_path\|@executable_path' || true)
[[ -z "$bad" ]] || die "Absolute dependency paths in Standalone binary:\n$bad"
ok "No absolute dependency paths in Standalone binary"

# Version: bundle string and packed AU version must match cmake project
VERSION=$(grep -m1 'project(.*VERSION' "$REPO/CMakeLists.txt" | \
    sed -E 's/.*VERSION[[:space:]]+([0-9]+\.[0-9]+\.[0-9]+).*/\1/')
[[ -n "$VERSION" ]] || die "Cannot extract version from CMakeLists.txt"

IFS='.' read -r MAJOR MINOR PATCH <<< "$VERSION"
PACKED=$(( (MAJOR << 16) | (MINOR << 8) | PATCH ))

BUNDLE_VER=$(plutil -extract CFBundleShortVersionString raw "$AU/Contents/Info.plist" 2>/dev/null || echo "MISSING")
APP_BUNDLE_VER=$(plutil -extract CFBundleShortVersionString raw "$APP/Contents/Info.plist" 2>/dev/null || echo "MISSING")
AU_VER=$(plutil -extract AudioComponents.0.version raw "$AU/Contents/Info.plist" 2>/dev/null || echo "MISSING")

[[ "$BUNDLE_VER" == "$VERSION" ]] || \
    die "AU CFBundleShortVersionString='$BUNDLE_VER', expected '$VERSION'"
ok "AU bundle version = $BUNDLE_VER"

[[ "$APP_BUNDLE_VER" == "$VERSION" ]] || \
    die "Standalone CFBundleShortVersionString='$APP_BUNDLE_VER', expected '$VERSION'"
ok "Standalone bundle version = $APP_BUNDLE_VER"

[[ "$AU_VER" == "$PACKED" ]] || \
    die "AU packed AudioComponents version=$AU_VER, expected $PACKED (${VERSION})"
ok "AU packed version = $AU_VER (${VERSION})"

# Focused ctest: smoke, artifact, editor probe
log "Running focused ctest"
ctest --test-dir "$BUILD_DIR" --output-on-failure \
    -R 'ats_resynth_standalone_smoke|ats_au_artifact_test|ats_au_editor_view_probe'

ok "All build-tree gates passed for $ARCH build in $(basename "$BUILD_DIR")"
