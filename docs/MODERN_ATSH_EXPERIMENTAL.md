# Modern ATSH Experimental Notes

This note captures the current Phase 6 experimental prototypes and the rules used to decide whether they should stay experimental or graduate.

## Commit 28: Reassigned/DDM-style peak estimator

- Added `ExperimentalReassignedPeakEstimator` behind the experimental analysis surface.
- Added `comparePeakEstimators(...)` to compare candidate peak acceptance, matched-peak ratio, mean frequency deltas, and CPU cost against a baseline estimator.
- Current recommendation rule:
  - keep experimental unless matched-peak ratio is strong and CPU stays within an acceptable multiplier relative to baseline.

Use this when:

- close/crossing peaks are unstable with the classic estimator,
- frequency localization quality matters more than strict ATS parity,
- you are explicitly running non-default experimental analysis passes.

## Commit 29: S+T+N transient lane prototype

- Added compact transient-lane prototype types:
  - `ExperimentalTransientAtom`
  - `ExperimentalTransientLane`
- Added prototype builders/synthesis:
  - `buildExperimentalTransientLane(...)`
  - `synthesizeExperimentalTransientLane(...)`
- Added lane-vs-preservation comparison:
  - `compareTransientLanes(...)`

Graduation decision rule:

- keep experimental until compact lane energy behavior and transient counts stay acceptably close to preservation-buffer behavior for representative fixtures.

## Commit 30: DDSP-assisted helper policy

- Added policy evaluator:
  - `evaluateDdspHelperPolicy(...)`
- The policy enforces:
  - classical ATS model remains the editable source of truth,
  - DDSP helpers are only acceptable for assistive offline tasks (parameter estimation, denoising, preset suggestion),
  - no runtime model dependency in synthesis/analysis core until licensing, model size, and determinism constraints are fully resolved.
