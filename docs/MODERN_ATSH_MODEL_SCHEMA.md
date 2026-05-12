# Modern ATSH Model Schema

**Schema version:** `1`

This document covers the Phase 1 data model added before the modern analyzer
pipeline exists. The current plugin state reader remains unchanged; the modern
model is a versioned, optional snapshot format that later phases can embed or
render without changing the classic ATSA/Qatsh-compatible path.

## Header

`ats::ModernAtsHeader` records:

- `schemaVersion`: currently `1`.
- `analysisMode`: `classic_ats`, `modern_ats`, or `experimental`.
- `trackerMode`: `classic_recursive_frequency_smr`, `modern_assignment`, or
  `experimental`.
- `sampleRate`: source/model sample rate.
- `hopSizeSamples`: analysis hop in samples.
- `windowSizeSamples`: analysis window size in samples.
- `inputChannelCount`: number of channels in the imported source.
- `channelPolicy`: `mono_input` for mono sources, or
  `downmix_to_mono_average` for current stereo/multichannel MVP analysis.
- `parameterHash`: deterministic hash of the analysis parameters.
- `sourceHash`: deterministic hash of the analyzed source.

## Partial Frames

Each `ats::ModernAtsPartialFrame` preserves the classic ATS frame payload:

- amplitude,
- frequency in Hz,
- phase in radians, stored as sine-oscillator phase at the analysis frame start
  for direct Modern source analysis,
- SMR in dB.
- per-partial noise energy.

It also carries Phase 1 diagnostics:

- `confidence`: normalized confidence in `[0, 1]`,
- `reason`: `matched`, `born`, `slept`, `interpolated`, `pruned`, or
  `rejected`.

## Optional Modern Fields

Optional fields are represented as empty vectors when absent, so a classic-only
model can serialize without synthetic transient or residual data.

- Per-frame orphan residual band energy: empty or 25 Bark-band values.
- Track lifecycle events: partial id, frame index, event kind, and detail.
- Assignment cost breakdowns: frame, partial, peak, frequency, amplitude, SMR,
  slope, birth/death, spurious-peak, and total cost.
- Transient markers: frame index, onset time, window bounds, and gain.
- Transient buffers: marker index, sample rate, gain, and short grain samples.
- Warnings: deterministic strings for analysis warnings.

The deterministic JSON serializer in `src/core/AtsModernModel.cpp` is intended
for tests, debugging, and initial state-size budgeting. A compact binary chunk
can be added later behind the same `ats::ModernAtsModel` structure if plugin
state size requires it.

## Representative State Sizes

The `ats_modern_model` CTest generates representative in-memory models with
partials, frames, diagnostics, orphan residual bands, lifecycle events, and
transient markers, then serializes them through the Phase 1 JSON path. These are
budgets, not golden production sizes:

| Fixture shape | Partials | Frames | Observed JSON size | Budget |
| --- | ---: | ---: | ---: | ---: |
| Sustained | 32 | 180 | 984,215 bytes | `< 1.1 MB` |
| Vibrato | 40 | 360 | 2,437,672 bytes | `< 2.6 MB` |
| Close/crossing partials | 48 | 300 | 2,432,392 bytes | `< 2.6 MB` |
| Plucked/percussive | 64 | 240 | 2,591,579 bytes | `< 2.8 MB` |
| Noisy voice/flute | 48 | 300 | 2,433,157 bytes | `< 2.6 MB` |
| Dense polyphony | 128 | 480 | 10,297,343 bytes | `< 11.0 MB` |
| Percussive hit | 96 | 180 | 2,908,033 bytes | `< 3.2 MB` |

These thresholds keep Phase 1 honest while leaving room for a later binary
serializer to reduce host session size before the model is embedded in plugin
state.
