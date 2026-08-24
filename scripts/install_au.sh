#!/usr/bin/env bash
# install_au.sh — build, install, and clear all caches so Logic recognises the AU.
#
# Usage:
#   ./scripts/install_au.sh            # build + install + clear caches
#   ./scripts/install_au.sh --caches   # clear caches only (no build)
set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$REPO/build-arm"
COMPONENT_DST="$HOME/Library/Audio/Plug-Ins/Components/ATS Resynth.component"

# ── helpers ──────────────────────────────────────────────────────────────────

log() { printf '\033[1;34m▶ %s\033[0m\n' "$*"; }
ok()  { printf '\033[1;32m✔ %s\033[0m\n' "$*"; }
die() { printf '\033[1;31m✘ %s\033[0m\n' "$*" >&2; exit 1; }

clear_caches() {
    log "Clearing AU and Logic caches"

    rm -f "$HOME/Library/Caches/AudioUnitCache/com.apple.audiounits.cache"

    for f in Cache.db Cache.db-shm Cache.db-wal; do
        rm -f "$HOME/Library/Caches/com.apple.logic10/$f"
    done

    # Remove all known ATS Resynth entries from Logic's plugin database.
    for key in "aufx-Atr1-AtPl" "aumf-Atr1-AtPl" "aumu-Atr2-AtPl"; do
        defaults delete com.apple.logic10 "$key" 2>/dev/null || true
    done

    # Remove from the system-wide AudioComponent cache.
    for key in \
        "7-'aufx'-'Atr1'-'AtPl'-0x10000" \
        "7-'aumf'-'Atr1'-'AtPl'-0x10000" \
        "7-'aumu'-'Atr2'-'AtPl'-0x10000"
    do
        defaults delete com.apple.audio.AudioComponentCache "$key" 2>/dev/null || true
    done

    ok "Caches cleared"
}

build_and_install() {
    log "Building arm64 AU (build-arm)"
    [[ -d "$BUILD_DIR" ]] || die "Build directory not found: $BUILD_DIR  (run cmake configure first)"

    cmake --build "$BUILD_DIR" --config Release 2>&1 \
        | grep -E "(error:|Building.*AtsAudioUnit|Linking.*component|Installing|signing)" \
        || true

    # Verify the binary landed and is arm64.
    local binary="$COMPONENT_DST/Contents/MacOS/ATS Resynth"
    [[ -f "$binary" ]] || die "Component binary not found after build"

    local arch
    arch=$(file "$binary" | grep -o "arm64\|x86_64" | head -1)
    [[ "$arch" == "arm64" ]] || die "Installed binary is $arch, expected arm64"

    ok "Installed arm64 component  ($binary)"
}

verify() {
    log "Verifying system registration"
    if auval -a 2>/dev/null | grep -q "Atr1.*AtPl"; then
        ok "auval -a sees  aumf Atr1 AtPl — ATS Resynth"
    else
        die "auval -a does not see the component — check plist and codesign"
    fi

    log "Plist sanity check"
    local plist="$COMPONENT_DST/Contents/Info.plist"
    local type sandbox
    type=$(plutil -extract AudioComponents.0.type raw "$plist" 2>/dev/null || echo "?")
    sandbox=$(plutil -extract AudioComponents.0.sandboxSafe raw "$plist" 2>/dev/null || echo "?")
    [[ "$type" == "aumf" ]]   || die "AU type is '$type', expected 'aumf'"
    [[ "$sandbox" == "true" || "$sandbox" == "1" ]] \
        || die "sandboxSafe is '$sandbox', expected true"
    ok "type=aumf  sandboxSafe=true"
}

# ── main ─────────────────────────────────────────────────────────────────────

if [[ "${1:-}" == "--caches" ]]; then
    clear_caches
else
    build_and_install
    clear_caches
    verify
fi

echo ""
echo "  Quit Logic Pro (⌘Q) and reopen it."
echo "  The plugin will appear under:  Audio FX → ATSPlugin → ATS Resynth"
