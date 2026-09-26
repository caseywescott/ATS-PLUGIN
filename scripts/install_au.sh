#!/usr/bin/env bash
# Compatibility entry point. The canonical workflow is deliberately split into
# deterministic build and transactional install scripts.
set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"

if [[ $# -ne 0 ]]; then
    printf 'install_au.sh no longer supports partial/cache-only installs.\n' >&2
    printf 'Use scripts/build_macos_artifacts.sh and scripts/install_logic_au.sh.\n' >&2
    exit 2
fi

"$REPO/scripts/build_macos_artifacts.sh" --arch arm64 --sign -
"$REPO/scripts/install_logic_au.sh" \
    --build-dir "$REPO/build-macos-arm64" \
    --arch arm64
