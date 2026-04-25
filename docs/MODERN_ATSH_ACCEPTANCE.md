# Modern ATSH Acceptance Gates

Phase 7 turns the PRD section 7 quality goals into a deterministic acceptance
harness. The harness lives in `src/core/AtsModernAcceptance.h` / `.cpp` and is
covered by the `ats_modern_acceptance` CTest.

## Gates

The report evaluates:

- classic render parity using normalized RMS error and a compact spectral
  distance metric,
- modern tracker behavior against classic tracker metrics for crossing/close
  partial and sustained fixtures,
- transient marker/buffer preservation, attack sharpness, and click-step limits,
- partial plus orphan residual energy preservation,
- deterministic morph recipe recall and deterministic fixed-seed morph renders,
- modern synthesis real-time-safety flags, finite sample output, and CPU budget.

Each gate emits a stable id, observed value, threshold, pass/fail state, and a
short detail string. Callers can keep the default thresholds or supply a stricter
fixture-specific `ModernAtsAcceptanceThresholds` instance.

## Current representative fixture status

`ats_modern_acceptance` uses synthetic fixtures so it can run in CI without
external audio assets:

| Fixture | Coverage |
| --- | --- |
| Classic parity sine | normalized RMS and spectral distance stay at zero for identical renders |
| Crossing partials | modern slope-aware assignment has fewer identity errors than classic nearest matching |
| Sustained tone | modern continuity and identity match classic behavior |
| Protected attack | transient marker and buffer increase attack sharpness while staying below the click-step guardrail |
| Morph recall | recipe JSON and fixed-seed rendered output are deterministic |
| Modern synth | render plan reports caller-owned buffers, no allocation/locks/I/O/analysis, finite samples, and bounded CPU |

These fixtures are not a replacement for listening tests or external reference
renders. They are the release gate that prevents regressions in the acceptance
criteria before larger audio fixture sweeps are added.
