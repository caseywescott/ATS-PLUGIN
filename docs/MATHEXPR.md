# MathExpr-style transforms (Phase 3)

This plugin includes an **offline** expression engine inspired by Qatsh’s partial math (see upstream `README.txt` in the Qatsh repository). Scripts run on a **worker thread** when you press **Apply script** on the **Math** tab—never on the real-time audio callback.

## Supported assignment syntax

- One assignment per line (Unix newlines).
- Lines may be empty or start with `#` for comments.
- Each non-comment line must be one of:

  - `a = <expr>` — partial amplitude (must stay ≥ 0 after evaluation)
  - `f = <expr>` — partial frequency in Hz (must stay > 0)
  - `h = <expr>` — phase in radians (only if the analyzed model includes phase data)

Use lowercase `a`, `f`, `h` on the left-hand side to match Qatsh’s README conventions.

## Variables (per inner loop over partial `p` and frame `t`)

| Symbol | Meaning |
|--------|---------|
| `P` | Number of partials |
| `T` | Number of frames |
| `p` | Current partial index `0 … P-1` |
| `t` | Current frame index `0 … T-1` |
| `a` | Current amplitude at `(p,t)` before the assignment is applied |
| `f` | Current frequency (Hz) at `(p,t)` |
| `h` | Current phase at `(p,t)` (0 if the model has no phase) |

## Functions

| Call | Meaning |
|------|---------|
| `A(pi, ti)` | Amplitude at partial `pi`, frame `ti` |
| `F(pi, ti)` | Frequency at `(pi, ti)` |
| `H(pi, ti)` | Phase at `(pi, ti)` (0 if no phase) |
| `sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `atan2` | Standard trig |
| `log`, `log10`, `exp`, `sqrt` | Transcendentals |
| `abs` / `fabs` | Absolute value |
| `min(x,y)`, `max(x,y)` | Two-argument extrema |
| `pow(x,y)` | Power |
| `pi`, `e` | Constants |

`^` is exponentiation (left-associative chain).

## Evaluation order

1. The script is split into passes (one pass per non-empty line).
2. For **each** pass, the engine iterates `p = 0…P-1`, then `t = 0…T-1`, evaluates the right-hand side with the variables above bound to the **current** cell, then writes the target (`a`, `f`, or `h`) for that cell.

Later passes see values written by earlier passes.

## Qatsh README excerpt (reference)

> Predefined variables: P, T, p, t, a, f, h, …  
> Predefined functions: A(p,t), F(p,t), H(p,t), …

Example from Qatsh (frequency shaping relative to a reference partial `pr` at frame `tr`):

```text
f = f*F(pr,tr)/F(pr,t)
```

In this plugin you can use the same expression on a line starting with `f =`.

## Limitations (MVP)

- No statistical helpers (`amean`, `fmax`, …) from the full Qatsh README—only the symbols and functions listed above.
- Residual / critical-band noise (`band_energy`) is **not** addressed by this engine.
- Very large models may take noticeable time; use **Cancel** on the Session tab to bump the job generation and stop the transform at the next inner-loop check.

## See also

- `BUILDING.md` — model size soft limits and CPU expectations.
- `src/core/AtsMathExpr.cpp` — parser and evaluator implementation.
