#!/usr/bin/env python3
"""Compare spectrum parity artifacts without hiding the first divergence.

JSON artifacts are compared recursively.  Files with a binary extension are
compared byte-for-byte, which keeps the canonical binary artifact authoritative
when a producer emits both JSON diagnostics and a packed representation.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import sys
from pathlib import Path
from typing import Any


DEFAULT_ABS = 1.0e-6
DEFAULT_REL = 1.0e-6


def load_json(path: Path) -> Any:
    with path.open(encoding="utf-8") as stream:
        return json.load(stream)


def path_text(path: tuple[object, ...]) -> str:
    return "$" + "".join(f"[{part!r}]" if isinstance(part, int) else f".{part}" for part in path)


def compare_values(oracle: Any, candidate: Any, path: tuple[object, ...],
                   abs_tol: float, rel_tol: float, errors: list[str],
                   maximum: list[float]) -> None:
    if isinstance(oracle, bool) or isinstance(candidate, bool):
        if oracle != candidate:
            errors.append(f"{path_text(path)}: {oracle!r} != {candidate!r}")
        return
    if isinstance(oracle, (int, float)) and isinstance(candidate, (int, float)):
        if not (math.isfinite(float(oracle)) and math.isfinite(float(candidate))):
            if oracle != candidate:
                errors.append(f"{path_text(path)}: {oracle!r} != {candidate!r}")
            return
        difference = abs(float(oracle) - float(candidate))
        maximum[0] = max(maximum[0], difference)
        allowed = abs_tol + rel_tol * max(abs(float(oracle)), abs(float(candidate)))
        if difference > allowed:
            errors.append(f"{path_text(path)}: oracle={oracle!r}, candidate={candidate!r}, "
                          f"error={difference:.9g}, allowed={allowed:.9g}")
        return
    if type(oracle) is not type(candidate):
        errors.append(f"{path_text(path)}: type {type(oracle).__name__} != {type(candidate).__name__}")
        return
    if isinstance(oracle, dict):
        if list(oracle) != list(candidate):
            errors.append(f"{path_text(path)}: object keys/order differ: "
                          f"{list(oracle)!r} != {list(candidate)!r}")
            return
        for key in oracle:
            compare_values(oracle[key], candidate[key], path + (key,),
                           abs_tol, rel_tol, errors, maximum)
            if errors:
                return
        return
    if isinstance(oracle, list):
        if len(oracle) != len(candidate):
            errors.append(f"{path_text(path)}: length {len(oracle)} != {len(candidate)}")
            return
        for index, (left, right) in enumerate(zip(oracle, candidate)):
            compare_values(left, right, path + (index,), abs_tol, rel_tol, errors, maximum)
            if errors:
                return
        return
    if oracle != candidate:
        errors.append(f"{path_text(path)}: {oracle!r} != {candidate!r}")


def compare_file(oracle: Path, candidate: Path, abs_tol: float, rel_tol: float) -> tuple[bool, str, float]:
    if not oracle.exists() or not candidate.exists():
        return False, f"missing artifact: {oracle if not oracle.exists() else candidate}", 0.0
    if oracle.suffix not in {".json", ".jsonl", ".spx"}:
        left, right = oracle.read_bytes(), candidate.read_bytes()
        return (left == right, "binary bytes differ", 0.0)
    try:
        left, right = load_json(oracle), load_json(candidate)
    except (OSError, json.JSONDecodeError) as exc:
        return False, f"invalid JSON artifact: {exc}", 0.0
    errors: list[str] = []
    maximum = [0.0]
    compare_values(left, right, (), abs_tol, rel_tol, errors, maximum)
    return not errors, errors[0] if errors else "", maximum[0]


def check_manifest(path: Path) -> int:
    manifest = load_json(path)
    required = {"schemaVersion", "oracleCommit", "fixtures", "states", "artifacts"}
    missing = required - set(manifest)
    if missing:
        print(f"manifest missing keys: {sorted(missing)}", file=sys.stderr)
        return 2
    if manifest["oracleCommit"] != "65341d64c5317f465dd5cfc8f5a95981512516ad":
        print("manifest oracleCommit is not the pinned legacy commit", file=sys.stderr)
        return 2
    for fixture in manifest["fixtures"]:
        fixture_path = path.parent / fixture["path"]
        if not fixture_path.exists():
            print(f"manifest fixture missing: {fixture_path}", file=sys.stderr)
            return 2
        digest = hashlib.sha256(fixture_path.read_bytes()).hexdigest()
        if digest != fixture["sha256"]:
            print(f"fixture hash mismatch: {fixture_path}", file=sys.stderr)
            return 2
    print(f"OK: spectrum parity manifest is valid ({len(manifest['fixtures'])} fixture(s))")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("oracle", nargs="?", type=Path)
    parser.add_argument("candidate", nargs="?", type=Path)
    parser.add_argument("--manifest", type=Path)
    parser.add_argument("--abs-tol", type=float, default=DEFAULT_ABS)
    parser.add_argument("--rel-tol", type=float, default=DEFAULT_REL)
    args = parser.parse_args()
    if args.manifest is not None:
        result = check_manifest(args.manifest)
        if args.oracle is None:
            return result
    if args.oracle is None or args.candidate is None:
        parser.error("oracle and candidate artifact directories are required")
    names = {path.name for path in args.oracle.iterdir()} | {path.name for path in args.candidate.iterdir()}
    failures = 0
    for name in sorted(names):
        ok, message, maximum = compare_file(args.oracle / name, args.candidate / name,
                                             args.abs_tol, args.rel_tol)
        if ok:
            print(f"OK {name} (max error {maximum:.9g})")
        else:
            print(f"FAIL {name}: {message}")
            failures += 1
            break
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
