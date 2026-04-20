# Determinism expectations (PRD §5.1)

ATS analysis and resynthesis use floating-point math across many frames. **Bit-identical output across machines or compiler versions is not guaranteed**, even with identical input WAVs, `ANARGS`, and `SPARAMS`.

What we **do** expect:

- **Same toolchain, same CPU architecture, same optimization level:** outputs should match within strict floating-point tolerance for short fixtures (see `tests/golden_compare.py`).
- **Cross-platform golden hashes** are intentionally **not** asserted; tests use RMS / peak / sample-count heuristics instead.

If a future phase needs stronger reproducibility, options include pinned compiler versions, `-ffp-contract=off`-style flags where supported, and reference renders generated per platform in CI.
