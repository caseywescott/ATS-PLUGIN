#!/usr/bin/env bash
# Create unsigned zip archives of built plug-in artefacts (Phase 5 helper).
# Usage: from repo root after Release build:
#   ./scripts/package_release_archives.sh [build-dir]
# Default build-dir: build

set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BDIR="${1:-$ROOT/build}"
ART="$BDIR/src/plugin/AtsResynth_artefacts/Release"
OUT="$ROOT/dist"
VER="$(grep -E '^project\(ats_plugin' "$ROOT/CMakeLists.txt" | sed -n 's/.*VERSION \([0-9.]*\).*/\1/p')"
TAG="ats-resynth-${VER:-0.0.0}-unsigned-$(uname -s | tr '[:upper:]' '[:lower:]')-$(uname -m)"

mkdir -p "$OUT"
TMP="$OUT/$TAG"
rm -rf "$TMP"
mkdir -p "$TMP"

if [[ -d "$ART/VST3/ATS Resynth.vst3" ]]; then
  cp -R "$ART/VST3/ATS Resynth.vst3" "$TMP/"
fi
if [[ -d "$ART/Standalone/ATS Resynth.app" ]]; then
  cp -R "$ART/Standalone/ATS Resynth.app" "$TMP/"
fi
if [[ -d "$ART/AU/ATS Resynth.component" ]]; then
  cp -R "$ART/AU/ATS Resynth.component" "$TMP/"
fi

cp "$ROOT/THIRD_PARTY.md" "$TMP/" 2>/dev/null || true
cp "$ROOT/docs/PERFORMANCE.md" "$TMP/" 2>/dev/null || true
cp "$ROOT/docs/RELEASE.md" "$TMP/" 2>/dev/null || true

(
  cd "$OUT"
  rm -f "$TAG.zip"
  zip -qr "$TAG.zip" "$TAG"
)
rm -rf "$TMP"
echo "Wrote $OUT/$TAG.zip"
