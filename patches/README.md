# Vendor patches

`qatsh/` is intentionally ignored by the top-level repository, but this plugin builds against `qatsh/src/atsa`.
Keep any local ATSA fixes mirrored here so a fresh checkout can reproduce the same engine behavior.

Apply the residual fixes after cloning or updating Qatsh with:

```sh
./scripts/apply_qatsh_residual_fixes.sh apply
```

Check patch state with:

```sh
./scripts/apply_qatsh_residual_fixes.sh status
./scripts/apply_qatsh_residual_fixes.sh reverse-check
```

The helper uses `git apply --ignore-space-change` because some upstream Qatsh files may have mixed line endings.
By default, CMake also requires these residual fixes and will fail configuration when the expected markers are missing.
Use `-DATS_REQUIRE_QATSH_RESIDUAL_FIXES=OFF` only when intentionally testing unpatched Qatsh behavior.

## `qatsh-atsa-progress.patch`

Adds optional analysis-progress reporting to the engine: zero-initialized
`progress_cb` / `progress_user` fields on `ANARGS` (`atsa.h`) and throttled
progress callbacks in the frame/residual loop (`tracker.c`). The plugin's
`PluginProcessor` sets `an.progress_cb` to drive its analysis progress bar, so
this patch is required for the plugin build. Apply it *before*
`qatsh-atsa-reassignment.patch` — the reassignment hunks sit adjacent to the
progress block in `tracker.c`.

```sh
./scripts/apply_qatsh_progress.sh apply
./scripts/apply_qatsh_progress.sh status
```

CMake requires it by default (marker: the `progress_cb` field in `atsa.h`).
Use `-DATS_REQUIRE_QATSH_PROGRESS=OFF` to bypass.

## `qatsh-atsa-reassignment.patch`

Adds derivative-window frequency reassignment (Auger-Flandrin) to the analysis
front-end: `peak_detection` gains a derivative-window spectrum argument and an
`ATSA_REASSIGN_FREQ` helper, and `tracker.c` builds the derivative window and a
second per-frame FFT alongside the analysis FFT. Frequencies come from the
reassigned estimate (exact for stationary sinusoids, far less biased than
parabolic); amplitude and phase are unchanged. Validated in
`tests/estimator_accuracy_test.c` (~29x lower worst-case frequency error
unpadded). Touches `src/atsa/atsa.h`, `peak-detection.c`, `tracker.c`.

```sh
./scripts/apply_qatsh_reassignment.sh apply
./scripts/apply_qatsh_reassignment.sh status
./scripts/apply_qatsh_reassignment.sh reverse-check
```

CMake also requires this patch by default (markers `ATSA_REASSIGN_FREQ` in
`peak-detection.c` and `tracker.c`). Use `-DATS_REQUIRE_QATSH_REASSIGNMENT=OFF`
to build against unpatched Qatsh.

## `qatsh-atsa-qifft.patch`

Replaces the dB-parabolic peak height with window-gain (bias-corrected QIFFT)
amplitude estimation. The dB-parabolic height carries a window- and
offset-dependent bias; the true amplitude of a stationary sinusoid is the raw
peak-bin magnitude divided by the analytic window main-lobe gain `G(delta)` at
the sub-bin offset. `tracker.c` precomputes `G` over `delta` in `[-0.5, 0.5]`
(`build_mainlobe_gain`) and installs it (`set_mainlobe_gain`) before the frame
loop, so `peak_detection` keeps its signature. The offset uses the reassigned
frequency when available. Validated in `tests/estimator_accuracy_test.c`
(~0.0003 dB vs ~0.031 dB parabolic worst-case unpadded, >100x). Apply it
*after* `qatsh-atsa-reassignment.patch`. Touches `src/atsa/atsa.h`,
`peak-detection.c`, `tracker.c`.

```sh
./scripts/apply_qatsh_qifft.sh apply
./scripts/apply_qatsh_qifft.sh status
./scripts/apply_qatsh_qifft.sh reverse-check
```

CMake requires it by default (marker `ATSA_QIFFT_AMP` in `peak-detection.c` and
`tracker.c`). Use `-DATS_REQUIRE_QATSH_QIFFT=OFF` to build against unpatched
Qatsh.

## `qatsh-atsa-ddm.patch`

Adds per-partial within-frame AM/FM slope estimation (order-2 Distribution
Derivative Method, Betser 2009) and a curvature-aware cubic-frequency synthesis
path. For each detected peak, `tracker.c` solves a 2x2 complex system from the
windowed segment's time-domain inner products `{h, tau*h, tau^2*h, h', tau*h'}`
(textbook `-j` convention, independent of qatsh's `+j` FFT) to recover the
chirp rate `Im(b2)*sr^2/pi` (Hz/s) and amplitude log-slope `Re(b1)*sr`. These
land in new in-memory `ATS_SOUND` arrays `frq_slope`/`amp_slope` and a
`has_slopes` flag. The **on-disk `.ats` format is unchanged** — slopes are an
in-memory-only refinement, so loaded sounds get `has_slopes = 0` and reproduce
exact legacy LIN3 behavior (bit-identical roundtrip).

Synthesis uses the slope only for sub-hop curvature: `do_synthesis` builds a
cubic through LIN3's same three control points but replaces the kinked center
slope with the DDM chirp. Because inter-frame interpolation was already exact
for linear motion, the cubic adds value only at vibrato turning points and
reduces *exactly* to LIN3 when motion is linear. Raw DDM estimates are unstable
on weak/near-stationary partials (measured up to ~5x the inter-frame chord), so
the center tangent is bounded to `3*min(|secantL|,|secantR|)` (Fritsch-Carlson
1980 monotone-cubic limit): steady partials clamp toward LIN3, vibrato flanks
keep their curvature. Apply it *after* `qatsh-atsa-qifft.patch`. Touches
`src/atsa/atsa.h`, `peak-detection.c`, `tracker.c`, `utilities.c`,
`synthesis.c`.

```sh
./scripts/apply_qatsh_ddm.sh apply
./scripts/apply_qatsh_ddm.sh status
./scripts/apply_qatsh_ddm.sh reverse-check
```

CMake requires it by default (marker `ATSA_DDM_SLOPES` in `tracker.c` and
`synthesis.c`). Use `-DATS_REQUIRE_QATSH_DDM=OFF` to build against unpatched
Qatsh.
