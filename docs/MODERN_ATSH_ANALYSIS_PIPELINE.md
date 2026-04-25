# Modern ATSH Phase 2 Analysis Pipeline

Phase 2 adds a deterministic analysis layer around the classic ATSA/Qatsh path
and introduces prototype modern components. It does not change the compatibility
default.

## Mode Registry

`src/core/AtsAnalysisPipeline.h` exposes three mode descriptors:

| ID | Default tracker | Status |
| --- | --- | --- |
| `classic_ats` | `classic_recursive_frequency_smr` | Enabled and compatibility default |
| `modern_ats` | `modern_assignment` | Enabled for explicit prototype tests |
| `experimental` | `experimental` | Registered but disabled |

The plugin analysis call now routes through `ats::runClassicAtsAnalysis`, which
wraps the existing ATSA `tracker` call. `tools/ats_offline` accepts
`--analysis-mode classic_ats` and rejects modern/experimental modes until later
phases wire those engines to real audio analysis.

Phase 5 plugin integration snapshots the selected mode/tracker/peak-estimator
parameters at Analyze time. Classic ATS remains the compatibility audio model.
When Modern ATS or the experimental estimator is selected, the plugin also
derives a versioned `ModernAtsModel` snapshot from the analyzed ATS data; Modern
assignment tracking is applied to derived peak frames, and diagnostics are read
from that modern snapshot rather than inferred from classic display fields.

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

Classic plugin analysis still uses the original ATSA tracker path unchanged.

## Tracking

Classic lifecycle normalization converts per-partial activity into shared
events:

- `birth`,
- `gap_fill`,
- `death`,
- `pruned`.

Short gaps are marked as `gap_fill`, and short active segments are marked as
`pruned`.

The modern assignment prototype tracks synthetic peak frames with a bounded
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
onsets.

## Tests

The `ats_analysis_pipeline` CTest covers:

- mode registry/default selection,
- deterministic peak estimation diagnostics,
- classic lifecycle short-gap fill and short-segment pruning,
- modern slope-aware assignment on a crossing-partial fixture,
- orphan residual energy preservation and deterministic noise render,
- transient marker detection and diagnostics,
- attack protection that blocks gap fill and resets assignment at marked
  transient frames.
