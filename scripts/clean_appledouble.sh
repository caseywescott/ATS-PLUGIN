#!/usr/bin/env bash
# Remove macOS AppleDouble (._*) metadata files that exFAT volumes accumulate.
# Idempotent; never touches .git internals. Usage: scripts/clean_appledouble.sh [root]
set -euo pipefail
ROOT="${1:-$(cd "$(dirname "$0")/.." && pwd)}"
count=$(find "$ROOT" -path "$ROOT/.git" -prune -o -name '._*' -type f -print | wc -l | tr -d ' ')
find "$ROOT" -path "$ROOT/.git" -prune -o -name '._*' -type f -exec rm -f {} +
echo "Removed $count AppleDouble file(s) under $ROOT"
