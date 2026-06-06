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

## `qatsh-atsa-multires.patch`

Adds an optional multi-resolution analysis front-end (PRD 6.4). A single
analysis window forces one time/frequency compromise: long enough to resolve
closely-spaced bass partials smears high-frequency attacks, and vice versa.
When `ANARGS.multi_res` is set, `tracker.c` runs a second, longer window
(`mr_win_mult x win_size`, default 2x) centered on the same frame midpoint and
detects peaks only in the low band `[lowest_bin, crossover_bin]` (crossover
default 500 Hz); the default window keeps the high band `(crossover_bin,
highest_bin]`. The two bands are disjoint by FFT bin, so the merge is a plain
`push_peak` concatenation with no extra de-duplication. The long pass carries
its own window, derivative window, norm, main-lobe gain table and DDM slope fit,
so QIFFT amplitudes and within-frame slopes stay correct across the crossover.

`multi_res == 0` is the default and leaves every downstream value untouched
(bit-identical to single-resolution; covered by `ats_roundtrip`). Validated in
`tests/multires_resolution_test.c`: a long window resolves two tones 45 Hz apart
that a short window merges, and end-to-end the front-end recovers both low tones
cleanly (200 / 245 Hz at full coverage) where single-resolution settles on a
blended-centroid track, while the high partial — riding the unchanged default
window — is preserved. Apply it *after* `qatsh-atsa-ddm.patch`. Touches
`src/atsa/atsa.h`, `tracker.c`.

```sh
./scripts/apply_qatsh_multires.sh apply
./scripts/apply_qatsh_multires.sh status
./scripts/apply_qatsh_multires.sh reverse-check
```

CMake requires it by default (marker `ATSA_MULTIRES` in `tracker.c` and
`atsa.h`). Use `-DATS_REQUIRE_QATSH_MULTIRES=OFF` to build against unpatched
Qatsh.

## `qatsh-atsa-ifft.patch`

Adds an opt-in overlap-add inverse-FFT additive synthesis path (Rodet-Depalle,
PRD 7.x) alongside the existing oscillator bank. `do_synthesis_ifft` deposits
each partial as a Hann main lobe — `0.5 Dir(x) - 0.25 Dir(x-1) - 0.25 Dir(x+1)`,
where `Dir` is the normalized Dirichlet (periodic-sinc) kernel — at its
fractional bin into a single spectrum, inverse-FFTs to one windowed grain, and
overlap-adds with COLA normalization. Cost is flat in partial count beyond the
deposit, so dense spectra synthesize in roughly constant time per frame instead
of scaling with the number of oscillators. The path handles the straight-through
(no time-warp) deterministic case on a uniform hop grid; the residual reuses the
existing band-noise generator.

It is gated by the new `SPARAMS.use_ifft` flag, which defaults to `0`. With
`use_ifft == 0` the oscillator bank runs unchanged and bit-identical (covered by
`ats_roundtrip`); only `use_ifft == 1` dispatches to the IFFT path. Validated in
`tests/ifft_synth_test.c`: against a direct time-domain oscillator reference on
stationary sinusoids, the IFFT path reconstructs amplitude *and* phase in the
overlap-add-steady interior to `nrmse = 0.0007`. Apply it *after*
`qatsh-atsa-multires.patch`. Touches `src/atsa/atsa.h`, `synthesis.c`.

```sh
./scripts/apply_qatsh_ifft.sh apply
./scripts/apply_qatsh_ifft.sh status
./scripts/apply_qatsh_ifft.sh reverse-check
```

CMake requires it by default (marker `ATSA_IFFT_SYNTH` in `synthesis.c` and
`atsa.h`). Use `-DATS_REQUIRE_QATSH_IFFT=OFF` to build against unpatched Qatsh.

## `qatsh-atsa-rfft-pack.patch`

Analysis speedup, exact (no behavioral change). Each frame ran two real-input
FFTs — one on the windowed signal, one on the derivative-windowed signal (the
latter feeds frequency reassignment). Since both transform the *same* real
samples, they are packed into one complex FFT `Z = FFT(main + j*deriv)` and
recovered by Hermitian symmetry (`FFT(main)[k] = (Z[k]+conj(Z[N-k]))/2`,
`FFT(deriv)[k] = (Z[k]-conj(Z[N-k]))/(2j)`), halving the per-frame FFT count.
The same packing is applied to the multi-resolution long-window pass. The
recombination is mathematically exact and independent of the FFT sign
convention; it differs from two separate FFTs only in floating-point rounding,
so `tests/estimator_accuracy_test.c` is bit-for-bit unchanged and all 16 CTest
targets stay green. Measured ~1.42x faster `tracker()` on a native arm64 build
(7.8 s, 20-partial tone: ~1198 ms -> ~843 ms). Touches `src/atsa/tracker.c`
only (adds the `rfft_unpack` helper plus packed-buffer scratch). Apply it
*after* `qatsh-atsa-multires.patch`.

```sh
./scripts/apply_qatsh_rfft_pack.sh apply
./scripts/apply_qatsh_rfft_pack.sh status
./scripts/apply_qatsh_rfft_pack.sh reverse-check
```

CMake requires it by default (marker `ATSA_RFFT_PACK` in `tracker.c`). Use
`-DATS_REQUIRE_QATSH_RFFT_PACK=OFF` to build against unpatched Qatsh.
