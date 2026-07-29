#!/usr/bin/env python3
"""Reject JUCE include/API leakage from the native BUILD_PLUGIN=OFF graph."""

from __future__ import annotations

import pathlib
import re
import sys


NATIVE_DIRECTORIES = ("src/core", "src/engine", "src/state", "src/platform", "src/ui", "src/host")
SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".h", ".hpp", ".mm"}
FORBIDDEN = re.compile(
    r"(?:#\s*include\s*[<\"]\s*juce|\bjuce::|\bJUCE_[A-Z0-9_]+)",
    re.IGNORECASE,
)


def main() -> int:
    root = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else ".").resolve()
    violations: list[tuple[pathlib.Path, int, str]] = []
    scanned = 0
    for directory in NATIVE_DIRECTORIES:
        for path in (root / directory).rglob("*"):
            if not path.is_file() or path.suffix not in SOURCE_SUFFIXES or path.name.startswith("._"):
                continue
            scanned += 1
            for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
                if FORBIDDEN.search(line):
                    violations.append((path.relative_to(root), line_number, line.strip()))

    if violations:
        print("JUCE leakage detected in native source graph:")
        for path, line_number, line in violations:
            print(f"  {path}:{line_number}: {line}")
        return 1

    print(f"OK: no JUCE include/API leakage in {scanned} native source files")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
