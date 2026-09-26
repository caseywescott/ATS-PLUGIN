#!/bin/zsh
set -euo pipefail

root_dir="$(cd "$(dirname "$0")/.." && pwd)"
output_dir="${1:-$root_dir/build/spectrum-parity/reference}"
reference_exporter="${SPECTRUM_REFERENCE_EXPORTER:-}"

if [[ -z "$reference_exporter" || ! -x "$reference_exporter" ]]; then
  print -u2 "SPECTRUM_REFERENCE_EXPORTER must point to the exporter built from oracle commit 65341d64c5317f465dd5cfc8f5a95981512516ad"
  exit 2
fi

mkdir -p "$output_dir"
exec "$reference_exporter" "$output_dir"
