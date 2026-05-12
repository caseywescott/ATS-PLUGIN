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
- **Output safety:** non-finite samples and denormal-sized values are clamped to
  zero, and the final mixed snapshot passes through a deterministic soft-knee
  limiter. By default, samples at or below 0.9 pass through unchanged and dense
  partial/residual/transient sums are bounded to full scale.

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
The plugin's final preview mixer uses the same `AtsOutputLimiter` helper after
partials, residuals, and master trim are summed, so aggressive audition gains do
not emit unchecked full-scale overs.

## Representative Budgets

Measured by `ats_modern_synth` on this development machine at 48 kHz, 256-sample
hop. The guardrail is intentionally generous for CI variance; the observed runs
are well below realtime.

| Shape | Samples | State bytes | Output bytes | Render seconds | CPU ratio | Peak abs |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Sustained | 46,080 | 940,010 | 184,320 | 0.046318 | 0.048248 | 0.844233 |
| Vibrato | 92,160 | 2,287,126 | 368,640 | 0.102078 | 0.053166 | 0.979048 |
| Close/crossing partials | 76,800 | 2,247,762 | 307,200 | 0.094183 | 0.058865 | 0.997505 |
| Plucked/percussive | 61,440 | 2,368,089 | 245,760 | 0.088862 | 0.069424 | 0.999964 |
| Noisy voice/flute | 76,800 | 2,247,762 | 307,200 | 0.093534 | 0.058459 | 0.997505 |
| Dense polyphony | 122,880 | 9,052,530 | 491,520 | 0.281418 | 0.109929 | 1.000000 |
| Percussive hit | 46,080 | 2,598,406 | 184,320 | 0.085701 | 0.089271 | 1.000000 |

Regression coverage:

- deterministic orphan residual output for a fixed seed,
- Bark-shaped residual output where high-band residual energy renders brighter
  than low-band energy,
- transient crossfade begins and ends at zero,
- transient gain can mute transient playback,
- output soft limiting preserves below-knee samples while bounding dense
  partial sums to full scale,
- denormal and non-finite sample detection,
- empty/silent modern snapshots preserve timeline length without non-finite
  output.
