#!/usr/bin/env bash
# Apply the full Qatsh ATSA patch stack in documented order.
# To refresh perf patches from a known-good qatsh/src/atsa tree, run:
#   scripts/bootstrap_qatsh_milestones.sh
#   scripts/regenerate_qatsh_perf_patches.sh
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
qatsh_dir="${QATSH_DIR:-$repo_root/qatsh}"

if [[ ! -d "$qatsh_dir" ]]; then
  echo "Qatsh directory not found: $qatsh_dir" >&2
  exit 1
fi

# Qatsh upstream base: 5515648358ffaa8d863b3f2507e03a4943d79667. local_base
# carries the historical local commit a536955 that the rest of the stack was
# cut against; residual_only_time_axis goes last because it rewrites the
# ats_load / init_sound time-axis handling touched by earlier patches.
base_patches=(
  local_base residual_fixes progress reassignment qifft ddm multires ifft
  rfft_pack vdsp_fft parallel residual_parallel mainlobe_parallel
)
perf_patches=(
  ifft_srand ddm_slopes_maint trackstore smr_precompute tracking_ds
  residual_pow2 scratch synth_state residual_only_time_axis
  cubic_phase_overflow vdsp_setup_atomic
  partial_free_bands trim_segment_average
)

apply_one() {
  local name="$1"
  "$repo_root/scripts/apply_qatsh_${name}.sh" apply
}

echo "Applying base Qatsh ATSA patches..."
for p in "${base_patches[@]}"; do
  echo "  -> $p"
  apply_one "$p"
done

echo "Applying performance / correctness Qatsh ATSA patches..."
for p in "${perf_patches[@]}"; do
  echo "  -> $p"
  apply_one "$p"
done

echo "All Qatsh ATSA patches applied."
