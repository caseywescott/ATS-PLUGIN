#!/usr/bin/env bash
# install_logic_au.sh — staged, architecture-aware, rollback-safe AU installer.
# Normative: follows docs/MACOS_BUILD_INSTALL_SPEC.md §9–11.
#
# Usage: ./scripts/install_logic_au.sh --build-dir build-macos-arm64 --arch arm64 [--launch-logic]
set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR=""
ARCH="arm64"
LAUNCH_LOGIC=false
SIGN_IDENTITY="-"

usage() {
    echo "Usage: $0 --build-dir <dir> --arch <arm64|x86_64> [--launch-logic]" >&2
    exit 1
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --build-dir)    BUILD_DIR="$2"; shift 2 ;;
        --arch)         ARCH="$2";      shift 2 ;;
        --launch-logic) LAUNCH_LOGIC=true; shift ;;
        *) usage ;;
    esac
done

[[ -n "$BUILD_DIR" ]] || usage
[[ -d "$BUILD_DIR" ]] || { echo "Build directory not found: $BUILD_DIR" >&2; exit 1; }

log() { printf '\033[1;34m▶ %s\033[0m\n' "$*"; }
ok()  { printf '\033[1;32m✔ %s\033[0m\n' "$*"; }
die() { printf '\033[1;31m✘ %s\033[0m\n' "$*" >&2; exit 1; }

COMPONENTS_DIR="$HOME/Library/Audio/Plug-Ins/Components"
BACKUP_DIR="$HOME/Library/Audio/Plug-Ins/ATS Resynth Backups"
INSTALLED="$COMPONENTS_DIR/ATS Resynth.component"
SOURCE_AU="$BUILD_DIR/src/host/au/ATS Resynth.component"
PROBE="$BUILD_DIR/src/host/au/ats_au_editor_view_probe"
LOCK_DIR="/tmp/ats_au_install.lock.d"
RECEIPT="$COMPONENTS_DIR/.ats_install_receipt"

STAGING=""
BACKUP=""
SWAPPED=false

list_matching_components() {
    local scan_dir candidate plist au_type au_subtype au_manufacturer
    for scan_dir in \
        "$HOME/Library/Audio/Plug-Ins/Components" \
        "/Library/Audio/Plug-Ins/Components" \
        "/Network/Library/Audio/Plug-Ins/Components"
    do
        [[ -d "$scan_dir" ]] || continue
        while IFS= read -r -d '' candidate; do
            plist="$candidate/Contents/Info.plist"
            [[ -f "$plist" ]] || continue
            au_type=$(plutil -extract AudioComponents.0.type raw "$plist" 2>/dev/null || true)
            au_subtype=$(plutil -extract AudioComponents.0.subtype raw "$plist" 2>/dev/null || true)
            au_manufacturer=$(plutil -extract AudioComponents.0.manufacturer raw "$plist" 2>/dev/null || true)
            if [[ "$au_type/$au_subtype/$au_manufacturer" == "aumf/Atr1/AtPl" ]]; then
                printf '%s\n' "$candidate"
            fi
        done < <(find "$scan_dir" -maxdepth 1 -type d -name '*.component' -print0 2>/dev/null)
    done
}

require_unique_installed_identity() {
    local matches count
    matches=$(list_matching_components)
    count=$(printf '%s\n' "$matches" | grep -c . || true)
    [[ "$count" -eq 1 && "$matches" == "$INSTALLED" ]] || \
        die "Expected exactly one installed aumf/Atr1/AtPl component at '$INSTALLED'; found $count:\n${matches:-<none>}"
    ok "Exactly one discoverable aumf/Atr1/AtPl component"
}

require_logic_closed() {
    if pgrep -x "Logic Pro" > /dev/null 2>&1; then
        die "Logic Pro is running. Quit it before installing (⌘Q) — an unsaved project may be open."
    fi
}

cleanup() {
    local exit_code=$?
    rmdir "$LOCK_DIR" 2>/dev/null || true
    # Remove staging if it survived (failed before atomic swap)
    [[ -n "$STAGING" && -d "$STAGING" ]] && rm -rf "$STAGING"
    # Never leave a failed new component active. Restore the previous version
    # when there was one, otherwise remove the failed first installation.
    if [[ "$SWAPPED" == true && $exit_code -ne 0 ]]; then
        rm -rf "$INSTALLED" 2>/dev/null || true
        if [[ -n "$BACKUP" && -d "$BACKUP" ]]; then
            log "Post-install gate failed — restoring previous component"
            mv "$BACKUP" "$INSTALLED" 2>/dev/null || true
            ok "Previous component restored from $(basename "$BACKUP")"
        fi
    fi
    exit $exit_code
}
trap cleanup EXIT

# ── §9 step 1: refuse if Logic is running ────────────────────────────────────
require_logic_closed
ok "Logic Pro is not running"

# ── §9 step 2: installer lock (mkdir is atomic on POSIX) ─────────────────────
if ! mkdir "$LOCK_DIR" 2>/dev/null; then
    die "Another install is in progress ($LOCK_DIR). Wait or remove the lock directory."
fi
ok "Installer lock acquired"

# Refuse an ambiguous starting state. AudioComponentRegistrar may select any
# duplicate bundle with this identity, including a backup ending in .component.
existing_matches=$(list_matching_components)
existing_count=$(printf '%s\n' "$existing_matches" | grep -c . || true)
if [[ "$existing_count" -gt 1 || ( "$existing_count" -eq 1 && "$existing_matches" != "$INSTALLED" ) ]]; then
    die "Noncanonical or duplicate aumf/Atr1/AtPl components found before install:\n$existing_matches\nOnly '$INSTALLED' may be discoverable; move backups outside Audio Unit scan directories."
fi

# ── §9 step 3: re-run build-tree gates ───────────────────────────────────────
log "Running build-tree verification"
"$REPO/scripts/verify_macos_artifacts.sh" --build-dir "$BUILD_DIR" --arch "$ARCH"

# Gather receipt fields
VERSION=$(grep -m1 'project(.*VERSION' "$REPO/CMakeLists.txt" | \
    sed -E 's/.*VERSION[[:space:]]+([0-9]+\.[0-9]+\.[0-9]+).*/\1/')
IFS='.' read -r VERSION_MAJOR VERSION_MINOR VERSION_PATCH <<< "$VERSION"
PACKED_VERSION=$(( (VERSION_MAJOR << 16) | (VERSION_MINOR << 8) | VERSION_PATCH ))
PACKED_VERSION_HEX=$(printf '0x%x' "$PACKED_VERSION")
REVISION=$(git -C "$REPO" rev-parse --short HEAD 2>/dev/null || echo "unknown")
DIRTY=$(git -C "$REPO" status --porcelain 2>/dev/null | wc -l | tr -d ' ')
[[ "$DIRTY" -gt 0 ]] && REVISION="${REVISION}-dirty"

# ── §9 steps 4–7: stage on destination filesystem ────────────────────────────
mkdir -p "$COMPONENTS_DIR" "$BACKUP_DIR"
STAGING="$COMPONENTS_DIR/.ats_staging_$$"
rm -rf "$STAGING"

log "Staging component"
ditto "$SOURCE_AU" "$STAGING"

# §9 step 5: permissions
find "$STAGING" -type d -exec chmod 755 {} \;
find "$STAGING" -type f -exec chmod 644 {} \;
chmod 755 "$STAGING/Contents/MacOS/ATS Resynth"

# §9 step 6: remove AppleDouble sidecars before signing
find "$STAGING" -name '._*' -delete

# §9 step 6: sign inside-out: dylib → executable → outer bundle
log "Signing staged bundle (inside-out)"
codesign --force --sign "$SIGN_IDENTITY" \
    "$STAGING/Contents/Frameworks/libsndfile.1.dylib"
find "$STAGING" -name '._*' -delete
codesign --force --sign "$SIGN_IDENTITY" \
    "$STAGING/Contents/MacOS/ATS Resynth"
find "$STAGING" -name '._*' -delete
codesign --force --sign "$SIGN_IDENTITY" "$STAGING"
find "$STAGING" -name '._*' -delete

# §9 step 7: verify staged artifact
log "Verifying staged bundle"
STAGED_ARCHS=$(lipo -archs "$STAGING/Contents/MacOS/ATS Resynth")
[[ "$STAGED_ARCHS" == "$ARCH" ]] || \
    die "Staged binary arch '$STAGED_ARCHS' != expected '$ARCH'"
STAGED_DYLIB_ARCHS=$(lipo -archs "$STAGING/Contents/Frameworks/libsndfile.1.dylib")
[[ "$STAGED_DYLIB_ARCHS" == "$ARCH" ]] || \
    die "Staged libsndfile arch '$STAGED_DYLIB_ARCHS' != expected '$ARCH'"

codesign --verify --deep --strict --verbose=2 "$STAGING" 2>/dev/null || \
    die "Staged bundle signature check failed"

plutil -lint "$STAGING/Contents/Info.plist" > /dev/null || \
    die "Staged plist invalid"

bad=$(otool -L "$STAGING/Contents/MacOS/ATS Resynth" | \
      grep -E '^[[:space:]]+(/Volumes/|/Users/|/opt/homebrew/|/usr/local/)' | \
      grep -v '@rpath\|@loader_path\|@executable_path' || true)
[[ -z "$bad" ]] || die "Absolute dependency paths in staged binary:\n$bad"

ok "Staged bundle verified ($ARCH, signature clean, no absolute paths)"

# ── §9 step 8: atomic swap — backup then rename ──────────────────────────────
require_logic_closed
if [[ -d "$INSTALLED" ]]; then
    BACKUP="$BACKUP_DIR/ATS Resynth.$$.component.disabled"
    mv "$INSTALLED" "$BACKUP"
    log "Backed up existing component outside Logic's scan path: $BACKUP"
fi

mv "$STAGING" "$INSTALLED"
STAGING=""   # renamed; cleanup() must not rm it
SWAPPED=true

ok "Installed: $INSTALLED"

# Disable rollback for the post-install gates that follow — a failure here
# will still restore BACKUP via cleanup(), so keep BACKUP set until receipt.

# ── §9 step 9: verify installed path ─────────────────────────────────────────
log "Verifying installed artifact"
[[ -d "$INSTALLED" ]] || die "Installed component missing after rename"

INST_ARCHS=$(lipo -archs "$INSTALLED/Contents/MacOS/ATS Resynth")
[[ "$INST_ARCHS" == "$ARCH" ]] || die "Installed binary arch '$INST_ARCHS' != expected '$ARCH'"
ok "Installed arch = $ARCH"

INST_DYLIB_ARCHS=$(lipo -archs "$INSTALLED/Contents/Frameworks/libsndfile.1.dylib")
[[ "$INST_DYLIB_ARCHS" == "$ARCH" ]] || \
    die "Installed libsndfile arch '$INST_DYLIB_ARCHS' != expected '$ARCH'"
ok "Installed libsndfile arch = $ARCH"

codesign --verify --deep --strict --verbose=2 "$INSTALLED" 2>/dev/null || \
    die "Installed signature check failed"
ok "Installed signature valid"

plutil -lint "$INSTALLED/Contents/Info.plist" > /dev/null || die "Installed plist invalid"
ok "Installed plist valid"

INST_BUNDLE_VERSION=$(plutil -extract CFBundleShortVersionString raw \
    "$INSTALLED/Contents/Info.plist" 2>/dev/null || echo "MISSING")
INST_PACKED_VERSION=$(plutil -extract AudioComponents.0.version raw \
    "$INSTALLED/Contents/Info.plist" 2>/dev/null || echo "MISSING")
[[ "$INST_BUNDLE_VERSION" == "$VERSION" ]] || \
    die "Installed bundle version '$INST_BUNDLE_VERSION' != expected '$VERSION'"
[[ "$INST_PACKED_VERSION" == "$PACKED_VERSION" ]] || \
    die "Installed packed AU version '$INST_PACKED_VERSION' != expected '$PACKED_VERSION'"
ok "Installed versions = $VERSION / packed $PACKED_VERSION"

sidecar_count=$(find "$INSTALLED" -name '._*' 2>/dev/null | wc -l | tr -d ' ')
[[ "$sidecar_count" -eq 0 ]] || die "Installed component contains $sidecar_count AppleDouble sidecar(s)"
ok "Installed component has no AppleDouble sidecars"
require_unique_installed_identity

# ── §9 step 10: retain at most one known-good backup ─────────────────────────
old_backups=$(find "$BACKUP_DIR" -maxdepth 1 \
    -name 'ATS Resynth.*.component.disabled' 2>/dev/null | sort || true)
count=$(echo "$old_backups" | grep -c . || true)
if [[ "$count" -gt 1 ]]; then
    oldest=$(echo "$old_backups" | head -n $(( count - 1 )))
    while IFS= read -r b; do
        [[ -n "$b" ]] && rm -rf "$b"
    done <<< "$oldest"
    ok "Pruned $(( count - 1 )) stale backup(s)"
fi

# ── §10: registrar and Logic refresh ─────────────────────────────────────────
require_logic_closed
log "Clearing AU and Logic caches"

rm -f "$HOME/Library/Caches/AudioUnitCache/com.apple.audiounits.cache"

for f in Cache.db Cache.db-shm Cache.db-wal; do
    rm -f "$HOME/Library/Caches/com.apple.logic10/$f"
done

for key in "aufx-Atr1-AtPl" "aumf-Atr1-AtPl" "aumu-Atr2-AtPl"; do
    defaults delete com.apple.logic10 "$key" 2>/dev/null || true
done

for key in \
    "7-'aumf'-'Atr1'-'AtPl'-$PACKED_VERSION_HEX" \
    "7-'aufx'-'Atr1'-'AtPl'-0x10000" \
    "7-'aumf'-'Atr1'-'AtPl'-0x10000" \
    "7-'aumu'-'Atr2'-'AtPl'-0x10000"
do
    defaults delete com.apple.audio.AudioComponentCache "$key" 2>/dev/null || true
done

killall -9 AudioComponentRegistrar 2>/dev/null || true
ok "Caches cleared; AudioComponentRegistrar terminated"

# §10 step 5: auval against installed component
log "Running auval -v aumf Atr1 AtPl"
auval_out=$(auval -v aumf Atr1 AtPl 2>&1) || die "auval failed:\n$auval_out"
echo "$auval_out" | grep -q "AU VALIDATION SUCCEEDED" || \
    die "auval did not report success:\n$auval_out"
echo "$auval_out" | grep -q "ATSPlugin" || \
    die "Manufacturer string 'ATSPlugin' absent from auval output"
echo "$auval_out" | grep -q "ATS Resynth" || \
    die "AU name 'ATS Resynth' absent from auval output"
INPUT_BUS_COUNT=$(printf '%s\n' "$auval_out" | awk '
    /Input Scope Bus Configuration:/ { input_scope=1; next }
    input_scope && /Default Bus Count:/ {
        sub(/^.*Default Bus Count:[[:space:]]*/, "")
        print; exit
    }')
[[ "$INPUT_BUS_COUNT" == "1" ]] || \
    die "auval registered input bus count '$INPUT_BUS_COUNT', expected 1:\n$auval_out"
printf '%s\n' "$auval_out" | grep -Fq '[2, 2]' || \
    die "auval did not report required stereo layout [2, 2]:\n$auval_out"
printf '%s\n' "$auval_out" | grep -Fq '[1, 2]' || \
    die "auval did not report required mono-to-stereo layout [1, 2]:\n$auval_out"
ok "auval: success, one input bus, layouts [2, 2] and [1, 2]"

# §10 step 6: Cocoa editor probe against installed binary
log "Running Cocoa editor probe against installed component"
[[ -x "$PROBE" ]] || die "Editor probe not found or not executable: $PROBE"
"$PROBE" "$INSTALLED/Contents/MacOS/ATS Resynth" || \
    die "Cocoa editor probe failed against installed component"
ok "Cocoa editor probe passed"

# ── Installation complete — clear rollback guard and write receipt ────────────
BACKUP=""
SWAPPED=false

INST_HASH=$(shasum -a 256 "$INSTALLED/Contents/MacOS/ATS Resynth" | awk '{print $1}')
INSTALL_TIME=$(date -u +"%Y-%m-%dT%H:%M:%SZ")

{
    echo "revision=$REVISION"
    echo "version=$VERSION"
    echo "arch=$ARCH"
    echo "sign=$SIGN_IDENTITY"
    echo "artifact=$SOURCE_AU"
    echo "destination=$INSTALLED"
    echo "binary_sha256=$INST_HASH"
    echo "installed_at=$INSTALL_TIME"
    echo "result=success"
} > "$RECEIPT"
ok "Receipt written: $RECEIPT"

echo ""
printf '  %-18s %s\n' "Revision:"  "$REVISION"
printf '  %-18s %s\n' "Version:"   "$VERSION"
printf '  %-18s %s\n' "Arch:"      "$ARCH"
printf '  %-18s %s\n' "Installed:" "$INSTALLED"
printf '  %-18s %s\n' "SHA-256:"   "$INST_HASH"
printf '  %-18s %s\n' "Result:"    "SUCCESS"
echo ""

if $LAUNCH_LOGIC; then
    log "Launching Logic Pro"
    open -a "Logic Pro"
else
    echo "  Open Logic Pro and insert a new ATS Resynth instance."
    echo "  Audio FX → ATSPlugin → ATS Resynth"
    echo "  (If a saved project has a stale instance, remove it and re-insert.)"
fi
