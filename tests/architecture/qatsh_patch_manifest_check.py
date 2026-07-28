#!/usr/bin/env python3
"""Verify that every declared Qatsh patch has a source marker and patch file."""

from pathlib import Path
import json
import sys


def main() -> int:
    if len(sys.argv) not in (2, 3):
        print("usage: qatsh_patch_manifest_check.py MANIFEST [ATSA_SOURCE_DIR]", file=sys.stderr)
        return 2
    manifest_path = Path(sys.argv[1]).resolve()
    repo = manifest_path.parents[2]
    source = Path(sys.argv[2]).resolve() if len(sys.argv) == 3 else repo / "qatsh" / "src" / "atsa"
    data = json.loads(manifest_path.read_text(encoding="utf-8"))
    failures = []
    patch_dir = repo / data["patch_directory"]
    for entry in data["entries"]:
        patch = patch_dir / entry["patch"]
        if not patch.is_file():
            failures.append(f"{entry['id']}: missing patch file {patch}")
        for relative, marker in entry["checks"]:
            source_file = source / relative
            if not source_file.is_file():
                failures.append(f"{entry['id']}: missing source file {source_file}")
                continue
            if marker not in source_file.read_text(encoding="utf-8", errors="replace"):
                failures.append(f"{entry['id']}: marker {marker!r} missing from {source_file}")
    if failures:
        print("FAIL: Qatsh patch manifest", file=sys.stderr)
        print("\n".join(failures), file=sys.stderr)
        return 1
    print(f"OK: {len(data['entries'])} Qatsh patches and source marker sets verified")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
