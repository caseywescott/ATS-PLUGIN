# Modern ATSH Phase 2 Analysis Pipeline

Phase 2 adds a deterministic analysis layer around the classic ATSA/Qatsh path
and introduces direct modern analysis components. It does not change the
compatibility default.

## Mode Registry

`src/core/AtsAnalysisPipeline.h` exposes three mode descriptors:

| ID | Default tracker | Status |
| --- | --- | --- |
| `classic_ats` | `classic_recursive_frequency_smr` | Enabled and compatibility default |
| `modern_ats` | `modern_assignment` | Enabled for explicit direct-analysis runs |
| `experimental` | `experimental` | Registered for gated experimental analysis |

The plugin analysis call now routes through `ats::runClassicAtsAnalysis`, which
wraps the existing ATSA `tracker` call for compatibility, ATS export, and the
classic residual render layer. `tools/ats_offline` accepts
`--analysis-mode classic_ats` and remains classic-only for external parity
tests.

Phase 5 plugin integration snapshots the selected mode/tracker/peak-estimator
parameters at Analyze time. Classic ATS remains the compatibility audio model,
but Modern ATS and Experimental mode now call `ats::runModernAtsAnalysis` on
the downmixed source samples. That direct path estimates STFT peaks before
tracking, applies the selected tracker to those source-derived peak frames, and
stores a versioned `ModernAtsModel` whose `sourceHash` is based on the audio
input rather than an intermediate `ATS_SOUND`. The plugin then overlays classic
ATSA residual-band energy when available so residual display/rendering remains
compatible while modern partial tracking can improve independently.

## Peak Estimation

`ats::IPeakEstimator` defines the deterministic peak-estimator interface.
`ats::ClassicStftPeakEstimator` operates on precomputed spectrum bins and
provides diagnostics for:

- raw local maxima,
- accepted peaks,
- rejected peaks,
- frequency limits,
- magnitude threshold,
- rejection reasons.

`ats::runModernAtsAnalysis` computes deterministic windowed DFT frames from the
source audio, feeds those bins into the selected estimator, caps accepted peaks
per frame for predictable CPU, and records per-frame diagnostics. Classic
plugin analysis still uses the original ATSA tracker path unchanged.

## Tracking

Classic lifecycle normalization converts per-partial activity into shared
events:

- `birth`,
- `gap_fill`,
- `death`,
- `pruned`.

Short gaps are marked as `gap_fill`, and short active segments are marked as
`pruned`.

The modern assignment prototype tracks direct peak frames with a bounded
candidate set per active track. Its cost matrix includes frequency, amplitude,
SMR, optional frequency-slope continuity, birth/death penalties, and spurious
peak penalty configuration. Cost breakdowns are recorded as
`assignment_costs` in the modern model schema.

## Residual and Transients

`computeOrphanResidualBandEnergy` keeps unassigned critical-band energy as
orphan residual instead of discarding it. `renderOrphanResidualBandNoise` adds a
deterministic seeded noise render path for that orphan residual lane.

`detectTransientMarkers` combines spectral flux, positive energy rise, and
model discontinuity into a thresholded onset score. Diagnostics expose scores,
threshold, and rejected candidate frame indices for tuning.

Attack protection is represented in the analysis layer before full transient
modeling exists. Protected transient frames can block short-gap fill in classic
lifecycle normalization, and the assignment prototype can reset active tracks
at marked attack frames so interpolation does not smear plucked/percussive
onsets. Direct Modern ATS analysis also captures bounded source-audio snippets
for each transient marker; markers store the matching buffer index so modern
synthesis can play preserved attacks through the transient lane.

## Tests

The `ats_analysis_pipeline` CTest covers:

- mode registry/default selection,
- deterministic peak estimation diagnostics,
- classic lifecycle short-gap fill and short-segment pruning,
- modern slope-aware assignment on a crossing-partial fixture,
- orphan residual energy preservation and deterministic noise render,
- transient marker detection and diagnostics,
- attack protection that blocks gap fill and resets assignment at marked
  transient frames,
- direct audio-to-modern-model analysis, including source-audio hashing,
  channel-policy metadata, assignment-cost diagnostics, validation, and
  source sine peak tracking,
- direct transient-buffer capture from source audio, including marker/buffer
  reference validation and source-impulse preservation.
