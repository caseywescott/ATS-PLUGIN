#!/usr/bin/env bash
# Regenerate perf-stack and ifft patch files from milestone snapshots in /tmp/atsa_m/.
# Run scripts/bootstrap_qatsh_milestones.sh first (requires a matching final ATSA tree).
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
m_dir="${ATS_MILESTONE_DIR:-/tmp/atsa_m}"
patch_dir="$repo_root/patches"

write_diff() {
  local out="$1" before="$2" after="$3"
  shift 3
  local files=("$@")
  : > "$out"
  local any=0
  for f in "${files[@]}"; do
    if [[ ! -f "$before/$f" || ! -f "$after/$f" ]]; then
      echo "missing $f in milestone ($before or $after)" >&2
      exit 1
    fi
    if diff -q "$before/$f" "$after/$f" >/dev/null 2>&1; then
      continue
    fi
    any=1
    diff -u "$before/$f" "$after/$f" \
      | sed "1s|--- .*|--- a/src/atsa/$f|;2s|\+\+\+ .*|+++ b/src/atsa/$f|" >> "$out" || true
  done
  if [[ "$any" -eq 0 ]]; then
    echo "no diff for $out" >&2
    exit 1
  fi
}

filter_atsa_h() {
  local mode="$1" in="$2" out="$3"
  python3 - <<'PY' "$mode" "$in" "$out"
import sys
mode, path_in, path_out = sys.argv[1:4]
lines = open(path_in).read().splitlines(keepends=True)

def hunk_matches(hunk, mode):
    text = "".join(hunk)
    if mode == "ifft":
        return ("use_ifft" in text or "do_synthesis_ifft" in text) and "scratch_mags" not in text
    if mode == "scratch":
        return "peak_detection" in text and "scratch_mags" in text
    return True

header, hunks, cur = [], [], None
for line in lines:
    if line.startswith(("--- ", "+++ ")):
        header.append(line)
    elif line.startswith("@@"):
        if cur is not None:
            hunks.append(cur)
        cur = [line]
    elif cur is not None:
        cur.append(line)
if cur is not None:
    hunks.append(cur)

out = header[:]
for h in hunks:
    if hunk_matches(h, mode):
        out.extend(h)
open(path_out, "w").writelines(out)
PY
}

write_ifft_patch() {
  local tmp="$patch_dir/.qatsh-atsa-ifft.patch.tmp"
  write_diff "$tmp" "$m_dir/post_multires" "$m_dir/pre_ifft_srand" atsa.h
  : > "$patch_dir/qatsh-atsa-ifft.patch"
  diff -u "$m_dir/post_multires/synthesis.c" "$m_dir/pre_ifft_srand/synthesis.c" \
    | sed '1s|--- .*|--- a/src/atsa/synthesis.c|;2s|+++ .*|+++ b/src/atsa/synthesis.c|' \
    >> "$patch_dir/qatsh-atsa-ifft.patch" || true
  filter_atsa_h ifft "$tmp" "$patch_dir/.qatsh-atsa-ifft.atsa.h.tmp"
  if [[ -s "$patch_dir/.qatsh-atsa-ifft.atsa.h.tmp" ]]; then
    cat "$patch_dir/.qatsh-atsa-ifft.atsa.h.tmp" >> "$patch_dir/qatsh-atsa-ifft.patch"
  fi
  rm -f "$tmp" "$patch_dir/.qatsh-atsa-ifft.atsa.h.tmp"
  echo "wrote $patch_dir/qatsh-atsa-ifft.patch"
}

write_scratch_patch() {
  local tmp="$patch_dir/.qatsh-atsa-scratch.patch.tmp"
  write_diff "$tmp" "$m_dir/base12" "$m_dir/pre_ifft_srand" atsa.h
  : > "$patch_dir/qatsh-atsa-scratch.patch"
  for f in other-utils.c peak-detection.c tracker.c; do
    diff -u "$m_dir/base12/$f" "$m_dir/pre_ifft_srand/$f" \
      | sed "1s|--- .*|--- a/src/atsa/$f|;2s|+++ .*|+++ b/src/atsa/$f|" \
      >> "$patch_dir/qatsh-atsa-scratch.patch" || true
  done
  filter_atsa_h scratch "$tmp" "$patch_dir/.qatsh-atsa-scratch.atsa.h.tmp"
  if [[ -s "$patch_dir/.qatsh-atsa-scratch.atsa.h.tmp" ]]; then
    cat "$patch_dir/.qatsh-atsa-scratch.atsa.h.tmp" >> "$patch_dir/qatsh-atsa-scratch.patch"
  fi
  rm -f "$tmp" "$patch_dir/.qatsh-atsa-scratch.atsa.h.tmp"
  echo "wrote $patch_dir/qatsh-atsa-scratch.patch"
}

for need in final pre_ifft_srand pre_ddm pre_trackstore pre_smr pre_tracking pre_pow2 pre_scratch post_multires base12; do
  if [[ ! -d "$m_dir/$need" ]]; then
    echo "missing milestone $m_dir/$need — run scripts/bootstrap_qatsh_milestones.sh" >&2
    exit 1
  fi
done

write_ifft_patch
write_scratch_patch

write_diff "$patch_dir/qatsh-atsa-ifft-srand.patch" \
  "$m_dir/pre_ifft_srand" "$m_dir/pre_ddm" synthesis.c

write_diff "$patch_dir/qatsh-atsa-ddm-slopes-maint.patch" \
  "$m_dir/pre_ddm" "$m_dir/pre_trackstore" utilities.c

write_diff "$patch_dir/qatsh-atsa-trackstore.patch" \
  "$m_dir/pre_trackstore" "$m_dir/pre_smr" tracker.c

write_diff "$patch_dir/qatsh-atsa-smr-precompute.patch" \
  "$m_dir/post_multires" "$m_dir/final" critical-bands.c

write_diff "$patch_dir/qatsh-atsa-tracking-ds.patch" \
  "$m_dir/pre_tracking" "$m_dir/pre_pow2" peak-tracking.c

write_diff "$patch_dir/qatsh-atsa-residual-pow2.patch" \
  "$m_dir/pre_pow2" "$m_dir/pre_scratch" residual-analysis.c

write_diff "$patch_dir/qatsh-atsa-synth-state.patch" \
  "$m_dir/pre_scratch" "$m_dir/final" atsa.h synthesis.c residual-analysis.c

echo "Perf patches regenerated in $patch_dir"
