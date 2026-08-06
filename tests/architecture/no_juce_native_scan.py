#!/usr/bin/env python3
"""Reject JUCE include/API leakage from the full native tree (src + tests + tools)."""

from __future__ import annotations

import pathlib
import re
import sys


# Directories that must never contain JUCE code
SCAN_DIRECTORIES = (
    "src/core", "src/engine", "src/state", "src/platform", "src/ui", "src/host",
    "src/audio",
    "tests", "tools",
)

SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".h", ".hpp", ".mm", ".py"}
FORBIDDEN = re.compile(
    r"(?:#\s*include\s*[<\"]\s*juce|\bjuce::|\bJUCE_[A-Z0-9_]+)",
    re.IGNORECASE,
)

# Files whose job is to document the JUCE removal — they legitimately reference
# JUCE names as historical labels, not as live API calls.
ALLOWLIST_NAMES = {
    "ats_juce_replacement_traceability.json",
    "no_juce_native_scan.py",  # this script itself
    "traceability_manifest_check.py",
}


def main() -> int:
    root = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else ".").resolve()
    violations: list[tuple[pathlib.Path, int, str]] = []
    scanned = 0
    for directory in SCAN_DIRECTORIES:
        scan_root = root / directory
        if not scan_root.exists():
            continue
        for path in scan_root.rglob("*"):
            if not path.is_file() or path.suffix not in SOURCE_SUFFIXES:
                continue
            if path.name.startswith("._") or path.name in ALLOWLIST_NAMES:
                continue
            scanned += 1
            try:
                text = path.read_text(encoding="utf-8", errors="replace")
            except OSError:
                continue
            for line_number, line in enumerate(text.splitlines(), 1):
                if FORBIDDEN.search(line):
                    violations.append((path.relative_to(root), line_number, line.strip()))

    if violations:
        print("JUCE leakage detected in native source tree:")
        for path, line_number, line in violations:
            print(f"  {path}:{line_number}: {line}")
        return 1

    print(f"OK: no JUCE include/API leakage in {scanned} source files across full tree")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
