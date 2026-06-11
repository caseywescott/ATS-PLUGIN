#!/usr/bin/env bash
# Capture ATSA milestone trees under /tmp/atsa_m/ for patch regeneration.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
qatsh_dir="${QATSH_DIR:-$repo_root/qatsh}"
m_dir="${ATS_MILESTONE_DIR:-/tmp/atsa_m}"
final_src="${ATS_FINAL_DIR:-}"

mkdir -p "$m_dir"/{final,pre_synth,pre_pow2,pre_tracking,pre_smr,pre_trackstore,pre_ddm,pre_ifft_srand,pre_scratch,post_multires,base12}

if [[ -n "$final_src" && -d "$final_src" ]]; then
  cp "$final_src"/* "$m_dir/final/"
elif [[ -f "$qatsh_dir/src/atsa/synthesis.c" ]]; then
  cp "$qatsh_dir/src/atsa"/*.{c,h} "$m_dir/final/" 2>/dev/null || cp "$qatsh_dir/src/atsa/"* "$m_dir/final/"
else
  echo "No final ATSA tree: set ATS_FINAL_DIR or populate qatsh/src/atsa" >&2
  exit 1
fi

R="$repo_root"
cp "$m_dir/final"/* "$qatsh_dir/src/atsa/"

git -C "$qatsh_dir" apply --reverse --ignore-space-change "$R/patches/qatsh-atsa-synth-state.patch"
cp "$qatsh_dir/src/atsa"/* "$m_dir/pre_scratch/"

git -C "$qatsh_dir" apply --reverse --ignore-space-change "$R/patches/qatsh-atsa-residual-pow2.patch"
cp "$qatsh_dir/src/atsa"/* "$m_dir/pre_pow2/"

git -C "$qatsh_dir" apply --reverse --ignore-space-change "$R/patches/qatsh-atsa-tracking-ds.patch"
cp "$qatsh_dir/src/atsa"/* "$m_dir/pre_tracking/"

git -C "$qatsh_dir" apply --reverse --ignore-space-change "$R/patches/qatsh-atsa-smr-precompute.patch"
cp "$qatsh_dir/src/atsa"/* "$m_dir/pre_smr/"

git -C "$qatsh_dir" apply --reverse --ignore-space-change "$R/patches/qatsh-atsa-trackstore.patch"
cp "$qatsh_dir/src/atsa"/* "$m_dir/pre_trackstore/"

git -C "$qatsh_dir" apply --reverse --ignore-space-change "$R/patches/qatsh-atsa-ddm-slopes-maint.patch"
cp "$qatsh_dir/src/atsa"/* "$m_dir/pre_ddm/"

git -C "$qatsh_dir" apply --reverse --ignore-space-change "$R/patches/qatsh-atsa-ifft-srand.patch"
cp "$qatsh_dir/src/atsa"/* "$m_dir/pre_ifft_srand/"

git -C "$qatsh_dir" checkout -- src/atsa
for s in residual_fixes progress reassignment qifft ddm multires; do
  "$repo_root/scripts/apply_qatsh_${s}.sh" apply >/dev/null
done
cp "$qatsh_dir/src/atsa"/* "$m_dir/post_multires/"

git -C "$qatsh_dir" checkout -- src/atsa
for s in residual_fixes progress reassignment qifft ddm multires ifft rfft_pack vdsp_fft parallel residual_parallel mainlobe_parallel; do
  if "$repo_root/scripts/apply_qatsh_${s}.sh" apply >/dev/null 2>&1; then
    :
  elif [[ "$s" == "ifft" ]]; then
    echo "note: ifft patch not yet applied during bootstrap (regenerate after first pass)" >&2
  else
    echo "failed to apply base patch $s during bootstrap" >&2
    exit 1
  fi
done
cp "$qatsh_dir/src/atsa"/* "$m_dir/base12/" 2>/dev/null || true

echo "Milestones written to $m_dir"
