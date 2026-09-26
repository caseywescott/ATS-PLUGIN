#!/usr/bin/env bash
# build_macos_artifacts.sh — configure and build AU + Standalone for macOS.
# Normative: follows docs/MACOS_BUILD_INSTALL_SPEC.md §6.
#
# Usage: ./scripts/build_macos_artifacts.sh --arch arm64 --sign -
set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
ARCH="arm64"
SIGN_IDENTITY="-"
JOBS=8

usage() {
    echo "Usage: $0 --arch <arm64|x86_64> --sign <identity> [--jobs N] [--reconfigure]" >&2
    exit 1
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --arch)        ARCH="$2";            shift 2 ;;
        --sign)        SIGN_IDENTITY="$2";   shift 2 ;;
        --jobs)        JOBS="$2";            shift 2 ;;
        --reconfigure) shift ;; # compatibility; configuration is always refreshed
        *) usage ;;
    esac
done

[[ "$ARCH" == "arm64" || "$ARCH" == "x86_64" ]] || \
    { echo "Unsupported arch: $ARCH" >&2; exit 1; }

BUILD_DIR="$REPO/build-macos-$ARCH"
VENDOR_SNDFILE="$REPO/vendor/sndfile-$ARCH"

log() { printf '\033[1;34m▶ %s\033[0m\n' "$*"; }
ok()  { printf '\033[1;32m✔ %s\033[0m\n' "$*"; }
die() { printf '\033[1;31m✘ %s\033[0m\n' "$*" >&2; exit 1; }

# Version from root CMakeLists.txt
VERSION=$(grep -m1 'project(.*VERSION' "$REPO/CMakeLists.txt" | \
    sed -E 's/.*VERSION[[:space:]]+([0-9]+\.[0-9]+\.[0-9]+).*/\1/')
[[ -n "$VERSION" ]] || die "Cannot extract version from CMakeLists.txt"

REVISION=$(git -C "$REPO" rev-parse --short HEAD 2>/dev/null || echo "unknown")
DIRTY=$(git -C "$REPO" status --porcelain 2>/dev/null | wc -l | tr -d ' ')
DIRTY_FLAG=""
[[ "$DIRTY" -gt 0 ]] && DIRTY_FLAG=" (dirty: $DIRTY files)"

log "ATS Resynth $VERSION | rev=$REVISION$DIRTY_FLAG | arch=$ARCH | sign='$SIGN_IDENTITY'"
log "Build directory: $BUILD_DIR"

# Vendor sndfile must exist for this arch
[[ -f "$VENDOR_SNDFILE/lib/libsndfile.dylib" ]] || \
    die "Vendor libsndfile not found: $VENDOR_SNDFILE/lib/libsndfile.dylib"
[[ -f "$VENDOR_SNDFILE/include/sndfile.h" ]] || \
    die "Vendor sndfile headers not found: $VENDOR_SNDFILE/include/sndfile.h"

# Always configure with the requested architecture and signing inputs. An
# unchecked cache can otherwise reuse an artifact built for another host.
log "Configuring cmake"
cmake -S "$REPO" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    "-DCMAKE_OSX_ARCHITECTURES=$ARCH" \
    -DBUILD_PLUGIN=OFF \
    -DATS_BUILD_NATIVE_AU=ON \
    -DATS_BUILD_NATIVE_STANDALONE=ON \
    -DATS_BUILD_NATIVE_VST3=OFF \
    "-DATS_CODESIGN_IDENTITY=$SIGN_IDENTITY" \
    "-DSNDFILE_INCLUDE_DIR=$VENDOR_SNDFILE/include" \
    "-DSNDFILE_LIBRARY=$VENDOR_SNDFILE/lib/libsndfile.dylib"
ok "cmake configured"

log "Building artifacts and test binaries"
cmake --build "$BUILD_DIR" --config Release \
    --target ats_resynth_standalone ats_resynth_au \
            ats_resynth_standalone_smoke ats_au_artifact_test ats_au_editor_view_probe \
    -j "$JOBS"

APP="$BUILD_DIR/src/host/standalone/ATS Resynth.app"
AU="$BUILD_DIR/src/host/au/ATS Resynth.component"

[[ -d "$APP" ]] || die "Standalone not produced: $APP"
[[ -d "$AU"  ]] || die "AU not produced: $AU"

# POST_BUILD commands do not run when a target is already up to date. Always
# normalize and re-sign both final bundles here so stale metadata or a changed
# signing identity cannot survive a nominally successful incremental build.
normalize_and_sign_bundle() {
    local bundle="$1"
    local executable="$2"
    local embedded_sndfile="$bundle/Contents/Frameworks/libsndfile.1.dylib"

    [[ -f "$embedded_sndfile" ]] || die "Embedded libsndfile missing: $embedded_sndfile"
    [[ -f "$executable" ]] || die "Bundle executable missing: $executable"

    find "$bundle" -name '._*' -delete
    find "$bundle" -type d -exec chmod 755 {} \;
    find "$bundle" -type f -exec chmod 644 {} \;
    chmod 755 "$executable"

    codesign --force --sign "$SIGN_IDENTITY" "$embedded_sndfile"
    find "$bundle" -name '._*' -delete
    codesign --force --sign "$SIGN_IDENTITY" "$executable"
    find "$bundle" -name '._*' -delete
    codesign --force --sign "$SIGN_IDENTITY" "$bundle"
    find "$bundle" -name '._*' -delete
}

log "Normalizing and signing final build-tree bundles"
normalize_and_sign_bundle "$APP" "$APP/Contents/MacOS/ATS Resynth"
normalize_and_sign_bundle "$AU" "$AU/Contents/MacOS/ATS Resynth"

ok "Build complete"
printf '  %-18s %s\n' "Revision:" "$REVISION$DIRTY_FLAG"
printf '  %-18s %s\n' "Version:"  "$VERSION"
printf '  %-18s %s\n' "Arch:"     "$ARCH"
printf '  %-18s %s\n' "Sign:"     "$SIGN_IDENTITY"
printf '  %-18s %s\n' "Standalone:" "$APP"
printf '  %-18s %s\n' "Audio Unit:" "$AU"
