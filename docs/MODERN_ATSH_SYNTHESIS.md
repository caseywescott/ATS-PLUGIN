# Modern ATSH Phase 3 Synthesis

Phase 3 adds an offline-prepared modern snapshot renderer while keeping the
classic ATSA/Qatsh render path intact. Classic-mode plugin preview still uses
the classic path for analyzed `ATS_SOUND` data; Modern/Experimental preview
renders modern partial and transient-buffer snapshots through
`src/core/AtsModernSynth.h` / `.cpp` while retaining the classic residual layer
for compatibility.

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
- **Orphan residual:** independent deterministic seeded noise from per-frame
  orphan residual band energy, controlled by `orphanResidualGain`.
- **Transients:** short transient buffers referenced by transient markers,
  mixed with raised-cosine edge fades and controlled by `transientGain`.
- **Silence safety:** non-finite samples and denormal-sized values are clamped to
  zero.

The classic renderer remains `do_synthesis` through the existing ATSA path and
is still covered by `golden_offline`, `ats_roundtrip`, and plugin build
verification.

The plugin Modern preview renders partials and captured transient buffers into
the partial layer (`transientGain = 1.0`) and keeps the ATSA residual render in
the residual layer. This lets preserved attacks participate in the normal
partials/residual mix without routing raw ATSA Bark-band residual energy through
the modern orphan-noise renderer.

## Representative Budgets

Measured by `ats_modern_synth` on this development machine at 48 kHz, 256-sample
hop. The guardrail is intentionally generous for CI variance; the observed runs
are well below realtime.

| Shape | Samples | State bytes | Output bytes | Render seconds | CPU ratio |
| --- | ---: | ---: | ---: | ---: | ---: |
| Sustained | 46,080 | 810,310 | 184,320 | 0.127167 | 0.132466 |
| Vibrato | 92,160 | 2,010,939 | 368,640 | 0.309021 | 0.160948 |
| Close/crossing partials | 76,800 | 1,998,723 | 307,200 | 0.289762 | 0.181101 |
| Plucked/percussive | 61,440 | 2,152,177 | 245,760 | 0.301128 | 0.235256 |
| Noisy voice/flute | 76,800 | 1,998,723 | 307,200 | 0.291483 | 0.182177 |
| Dense polyphony | 122,880 | 8,461,680 | 491,520 | 1.12852 | 0.440828 |
| Percussive hit | 46,080 | 2,406,837 | 184,320 | 0.327039 | 0.340665 |

Regression coverage:

- deterministic orphan residual output for a fixed seed,
- transient crossfade begins and ends at zero,
- transient gain can mute transient playback,
- denormal and non-finite sample detection,
- empty/silent modern snapshots preserve timeline length without non-finite
  output.
