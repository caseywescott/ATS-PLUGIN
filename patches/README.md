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

## `qatsh-atsa-vdsp-fft.patch`

Analysis speedup, exact (no behavioral change). On Apple platforms `fft()` is
routed through Accelerate's NEON-vectorized double-precision FFT
(`vDSP_fft_zipD`) instead of the scalar radix-2 transform, which dominates
analysis time. The scalar code is preserved as `fft_scalar()` and used as a
fallback for non-power-of-two sizes and on non-Apple builds (guarded by
`#ifdef __APPLE__`), so the patch is portable. `fft()` already stores real and
imaginary parts in separate arrays, which maps directly onto vDSP's
`DSPDoubleSplitComplex` with no interleaving. The direction mapping was verified
to machine epsilon: qatsh `fft(is=1)` equals vDSP's Inverse direction (both use
the `+j` convention, consistent with `to_polar`'s `atan2(-y,x)`), `fft(is=-1)`
equals vDSP's Forward direction; both vDSP directions are unnormalized like the
scalar transform (the complex `zipD` routine applies no factor-of-2). vDSP
setups are memoized per `log2(n)` behind a mutex so lazy creation stays safe
once the frame loop is parallelized. `estimator_accuracy_test` is unchanged and
all 16 CTest stay green. Measured ~1.65x faster `tracker()` on native arm64 on
top of the real-FFT packing patch (~849 ms -> ~513 ms; ~2.34x vs the original
two-FFT scalar baseline). Touches `src/atsa/other-utils.c` only; the build links
`-framework Accelerate` on Apple.

```sh
./scripts/apply_qatsh_vdsp_fft.sh apply
./scripts/apply_qatsh_vdsp_fft.sh status
./scripts/apply_qatsh_vdsp_fft.sh reverse-check
```

CMake requires it by default (marker `ATSA_VDSP_FFT` in `other-utils.c`). Use
`-DATS_REQUIRE_QATSH_VDSP_FFT=OFF` to build against unpatched Qatsh.

## `qatsh-atsa-parallel.patch`

Analysis speedup, behavior-preserving. The per-frame analysis work (windowing,
packed FFT, peak detection, DDM slopes, the multi-resolution long-window pass and
SMR evaluation) is independent across frames, so it is split into two phases.
Phase A runs every frame's peak extraction in parallel across the available cores
(one POSIX thread per core, each with its own FFT scratch), writing each frame's
peaks into its own slot. Phase B then walks the frames in order and does the
serial, order-dependent peak tracking exactly as before. Because each Phase A
frame writes only its own slot and reads only read-only context, the per-frame
peaks are identical to the serial pass, so Phase B — and thus the whole analysis
— is deterministic and bit-for-bit unchanged. The window-midpoint sample numbers
(`win_samps`), which the old loop accumulated from a running file pointer, are
precomputed from their closed form so the stage needs no shared running state.
The only shared mutable state in the frame path was the installed main-lobe gain
table (file-static in `peak-detection.c`); it is made `_Thread_local` so each
worker installs the default or long-window table around its own `peak_detection`
calls. POSIX threads are used where available; Windows builds (`_WIN32`) fall
back to the serial path. `estimator_accuracy_test` is bit-identical and all 16
CTest stay green (including the `golden_offline` full-analysis determinism
oracle). Measured ~1.9x faster `tracker()` on a 10-core native arm64 build on top
of the vDSP patch (~513 ms -> ~270 ms warm; the speedup is Amdahl-limited by the
still-serial residual/synthesis stages). Touches `src/atsa/tracker.c` and
`src/atsa/peak-detection.c`.

```sh
./scripts/apply_qatsh_parallel.sh apply
./scripts/apply_qatsh_parallel.sh status
./scripts/apply_qatsh_parallel.sh reverse-check
```

CMake requires it by default (marker `ATSA_PARALLEL` in `tracker.c` and
`peak-detection.c`). Use `-DATS_REQUIRE_QATSH_PARALLEL=OFF` to build against
unpatched Qatsh.

## `qatsh-atsa-residual-parallel.patch`

Analysis speedup, behavior-preserving. After the frame-tracking stage the two
serial residual stages dominate `tracker()` wall time: `compute_residual`
(partial resynthesis subtracted from the original — millions of `cos()` calls)
and `residual_analysis` (one windowed FFT per frame plus critical-band energy).
The residual FFT size is `2*window_size` (e.g. 4410), which is **not** a power of
two, so it does not benefit from the vDSP FFT patch and runs the scalar
mixed-radix transform — making `residual_analysis` the single largest serial
cost. Rather than change the FFT size (which would alter the stored band
energies), both stages are parallelized across frames. Each frame is fully
independent: `compute_residual` writes a disjoint output slice
`[(frm-1)*frame_size, frm*frame_size)` and reads only read-only partial/input
data; `residual_analysis` windows a disjoint span (closed-form position
`frame_n*hop - M_2`), runs its own per-worker FFT scratch and writes only its own
column of `band_arr`. Output is therefore bit-for-bit identical to the serial
pass. POSIX threads where available; Windows builds (`_WIN32`) fall back to the
serial path. `golden_offline` (the bit-exact full-analysis oracle) and all 16
CTest stay green. Measured ~2.5x faster `tracker()` on a 10-core native arm64
build on top of the frame-loop parallel patch (~270 ms -> ~110 ms; ~11x vs the
original two-FFT scalar baseline). Touches `src/atsa/residual.c` and
`src/atsa/residual-analysis.c`.

```sh
./scripts/apply_qatsh_residual_parallel.sh apply
./scripts/apply_qatsh_residual_parallel.sh status
./scripts/apply_qatsh_residual_parallel.sh reverse-check
```

CMake requires it by default (marker `ATSA_PARALLEL` in `residual.c` and
`residual-analysis.c`). Use `-DATS_REQUIRE_QATSH_RESIDUAL_PARALLEL=OFF` to build
against unpatched Qatsh.

## `qatsh-atsa-mainlobe-parallel.patch`

Analysis speedup, behavior-preserving. With the frame and residual stages
parallelized, `build_mainlobe_gain` becomes the single largest *serial* stage of
`tracker()` (~33% of warm runtime). It tabulates the window main-lobe gain
`G(delta)` at `res` (1025) evenly spaced offsets, each a full DTFT sum over the
analysis window — `res * win_size` `cos`/`sin` evaluations (~4.5M for a 2205-tap
window, and ~4x that for the long multi-resolution window). Each table entry is
independent, so the `res` loop is split across cores. The per-index arithmetic is
factored out unchanged (`mainlobe_gain_range`) and called from one thread per
contiguous index range, so the table is bit-for-bit identical to the serial
build. POSIX threads where available; Windows builds (`_WIN32`) fall back to the
serial path. `golden_offline` (the bit-exact full-analysis oracle), the
window-gain amplitude check in `estimator_accuracy_test`, and all 16 CTest stay
green. Measured ~38 ms -> ~5 ms for the default-window table on a 10-core native
arm64 build (a larger absolute win with multi-resolution analysis, where the long
window's table dominates). Touches `src/atsa/peak-detection.c` only.

```sh
./scripts/apply_qatsh_mainlobe_parallel.sh apply
./scripts/apply_qatsh_mainlobe_parallel.sh status
./scripts/apply_qatsh_mainlobe_parallel.sh reverse-check
```

CMake requires it by default (marker `mainlobe_worker` in `peak-detection.c`).
Use `-DATS_REQUIRE_QATSH_MAINLOBE_PARALLEL=OFF` to build against unpatched Qatsh.

## `qatsh-atsa-ifft-srand.patch`

Fixes **C-002**: seeds the synthesis PRNG before the IFFT dispatch so the opt-in
`use_ifft` path gets deterministic residual noise. Extends the IFFT patch — apply
*after* `qatsh-atsa-ifft.patch`. Touches `src/atsa/synthesis.c` only.

```sh
./scripts/apply_qatsh_ifft_srand.sh apply
./scripts/apply_qatsh_ifft_srand.sh status
./scripts/apply_qatsh_ifft_srand.sh reverse-check
```

CMake requires it by default (`srand` precedes `sparams->use_ifft` in `synthesis.c`).
Use `-DATS_REQUIRE_QATSH_IFFT_SRAND=OFF` to bypass.

## `qatsh-atsa-ddm-slopes-maint.patch`

Fixes **C-003**: includes `frq_slope`/`amp_slope` in `remove_partials` swaps and
`add_partial` copy/zero-fill when `has_slopes` is set. Apply *after*
`qatsh-atsa-ddm.patch`. Touches `src/atsa/utilities.c` only.

```sh
./scripts/apply_qatsh_ddm_slopes_maint.sh apply
./scripts/apply_qatsh_ddm_slopes_maint.sh status
```

CMake requires it by default (marker: slope swaps in `remove_partials`).
Use `-DATS_REQUIRE_QATSH_DDM_SLOPES_MAINT=OFF` to bypass.

## `qatsh-atsa-trackstore.patch`

**A-001**: inverts the track-store loop from O(P²·T) to O(P·T) by walking frames
then peaks. Bit-identical output. Apply after the parallel patch stack.
T touches `src/atsa/tracker.c` only.

```sh
./scripts/apply_qatsh_trackstore.sh apply
./scripts/apply_qatsh_trackstore.sh status
```

CMake requires it by default (marker `ATSA_TRACKSTORE` in `tracker.c`).
Use `-DATS_REQUIRE_QATSH_TRACKSTORE=OFF` to bypass.

## `qatsh-atsa-smr-precompute.patch`

**A-003**: hoists `frq2bark`/`amp2db_spl` out of the O(n²) SMR pair loop in
`evaluate_smr`. Bit-identical. Touches `src/atsa/critical-bands.c` only.

```sh
./scripts/apply_qatsh_smr_precompute.sh apply
./scripts/apply_qatsh_smr_precompute.sh status
```

CMake requires it by default (marker `ATSA_SMR_PRECOMPUTE`).
Use `-DATS_REQUIRE_QATSH_SMR_PRECOMPUTE=OFF` to bypass.

## `qatsh-atsa-tracking-ds.patch`

**A-002**: binary-search candidate peaks on the sorted frequency axis and use
peak-index identity for assignment (partial **C-005** fix). Bit-identical tie
order preserved. Touches `src/atsa/peak-tracking.c` only.

```sh
./scripts/apply_qatsh_tracking_ds.sh apply
./scripts/apply_qatsh_tracking_ds.sh status
```

CMake requires it by default (marker `ATSA_TRACKING_DS`).
Use `-DATS_REQUIRE_QATSH_TRACKING_DS=OFF` to bypass.
