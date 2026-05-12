# Modern ATSH Phase 3 Synthesis

Phase 3 adds an offline-prepared modern snapshot renderer while keeping the
classic ATSA/Qatsh render path intact. Classic-mode plugin preview still uses
the classic path for analyzed `ATS_SOUND` data; Modern/Experimental preview
renders modern partial, transient-buffer, and source-derived residual snapshots
through `src/core/AtsModernSynth.h` / `.cpp`.

## Render Plan

`prepareModernAtsRenderPlan` validates a versioned `ats::ModernAtsModel`,
captures immutable render settings, and computes the output length. Allocation
and validation belong to this prepare step.

`renderModernAtsPlanIntoBuffer` writes into caller-owned memory. Its real-time
safety contract is:

- no allocation inside the render function,
- no locks,
- no file I/O,
- no analysis or model mutation,
- caller owns the output buffer and immutable model lifetime.

`checkModernAtsRealtimeSafety` exposes this contract for tests and future UI or
host diagnostics.

## Rendered Components

- **Partials:** linear per-frame amplitude/frequency interpolation from modern
  snapshot partial frames.
- **Orphan residual:** independent deterministic seeded noise streams shaped by
  the stored 25-band Bark residual energy, controlled by `orphanResidualGain`.
- **Transients:** short transient buffers referenced by transient markers,
  mixed with raised-cosine edge fades and controlled by `transientGain`.
- **Silence safety:** non-finite samples and denormal-sized values are clamped to
  zero.

The classic renderer remains `do_synthesis` through the existing ATSA path and
is still covered by `golden_offline`, `ats_roundtrip`, and plugin build
verification.

The plugin Modern preview renders partials and captured transient buffers into
the partial layer (`transientGain = 1.0`) and renders source-derived Modern
residual bands into the residual layer (`orphanResidualGain = 1.0`). This lets
preserved attacks participate in the normal partials/residual mix while keeping
the residual-only control focused on the Modern model's untracked source energy.
The residual renderer is shared through `AtsResidualBandNoise` so the older
analysis helper and Modern preview both preserve the spectral placement of
orphan residual bands instead of collapsing them to broadband noise.

## Representative Budgets

Measured by `ats_modern_synth` on this development machine at 48 kHz, 256-sample
hop. The guardrail is intentionally generous for CI variance; the observed runs
are well below realtime.

| Shape | Samples | State bytes | Output bytes | Render seconds | CPU ratio |
| --- | ---: | ---: | ---: | ---: | ---: |
| Sustained | 46,080 | 940,010 | 184,320 | 0.046668 | 0.048613 |
| Vibrato | 92,160 | 2,287,126 | 368,640 | 0.099113 | 0.051621 |
| Close/crossing partials | 76,800 | 2,247,762 | 307,200 | 0.089963 | 0.056227 |
| Plucked/percussive | 61,440 | 2,368,089 | 245,760 | 0.086714 | 0.067745 |
| Noisy voice/flute | 76,800 | 2,247,762 | 307,200 | 0.091672 | 0.057295 |
| Dense polyphony | 122,880 | 9,052,530 | 491,520 | 0.277118 | 0.108249 |
| Percussive hit | 46,080 | 2,598,406 | 184,320 | 0.084328 | 0.087842 |

Regression coverage:

- deterministic orphan residual output for a fixed seed,
- Bark-shaped residual output where high-band residual energy renders brighter
  than low-band energy,
- transient crossfade begins and ends at zero,
- transient gain can mute transient playback,
- denormal and non-finite sample detection,
- empty/silent modern snapshots preserve timeline length without non-finite
  output.
