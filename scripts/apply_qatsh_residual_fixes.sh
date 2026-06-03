#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
qatsh_dir="${QATSH_DIR:-$repo_root/qatsh}"
patch_file="$repo_root/patches/qatsh-atsa-residual-fixes.patch"
mode="${1:-status}"

if [[ ! -d "$qatsh_dir" ]]; then
  echo "Qatsh directory not found: $qatsh_dir" >&2
  exit 1
fi

if [[ ! -f "$patch_file" ]]; then
  echo "Patch file not found: $patch_file" >&2
  exit 1
fi

check_not_applied() {
  git -C "$qatsh_dir" apply --check --ignore-space-change "$patch_file"
}

check_applied() {
  git -C "$qatsh_dir" apply --check --reverse --ignore-space-change "$patch_file"
}

case "$mode" in
  apply)
    if check_applied >/dev/null 2>&1; then
      echo "Qatsh residual fixes are already applied."
      exit 0
    fi
    check_not_applied
    git -C "$qatsh_dir" apply --ignore-space-change "$patch_file"
    echo "Applied Qatsh residual fixes."
    ;;
  check)
    check_not_applied
    echo "Qatsh residual fixes can be applied cleanly."
    ;;
  reverse-check)
    check_applied
    echo "Qatsh residual fixes are applied."
    ;;
  status)
    if check_applied >/dev/null 2>&1; then
      echo "applied"
    elif check_not_applied >/dev/null 2>&1; then
      echo "not applied"
    else
      echo "unknown or conflicting" >&2
      exit 1
    fi
    ;;
  *)
    echo "Usage: $0 [status|apply|check|reverse-check]" >&2
    exit 2
    ;;
esac
