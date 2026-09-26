# Spectrum View Exact-Parity Recovery Specification

**Status:** required recovery work; the spectrum surface must not be described as parity-complete.

**Supersedes for completion and acceptance:** `docs/spectrum-view-parity-spec.md` and
`docs/spectrum-view-parity-verification.md`. The earlier spec remains useful as an inventory of
the legacy surface, but its green tests and performance result are not evidence of parity.

**Legacy oracle:** commit
`65341d64c5317f465dd5cfc8f5a95981512516ad`, which is `1a64f030^` and is the last commit
containing the shipped JUCE `SpectrogramPanel`, `SpectrumRender`, and spectral snapshot builder.

**Primary outcome:** for the same source model, parameter state, view state, window size, scale
factor, and input-event sequence, the JUCE-free build must produce the same spectral data,
derived envelope/residual data, viewport state, draw geometry, editing actions, and visible
result as the legacy JUCE build.

---

## 1. Why the first parity effort did not converge

The first spec was largely a source-code inventory. Its implementation was verified mainly by
tests written against the new implementation, command-presence assertions, hand-computed
geometry, and a renderer performance test. Those checks can all pass while the two products
consume different data or implement different algorithms.

This recovery changes the method:

1. The legacy executable and legacy pure functions are the oracle.
2. Every stage emits a canonical artifact that can be compared independently.
3. Raw model parity is proved before snapshot parity; snapshot parity before envelope parity;
   envelope and viewport parity before raster parity.
4. A phase cannot be accepted from screenshots alone.
5. A golden may only be produced by the pinned legacy oracle, never by the new implementation.

No visual tuning is allowed while an upstream artifact differs. A wrong ridge, trace, or
scrolling result must be fixed at the first divergent layer.

---

## 2. Meaning of “exact parity”

Exact parity has five independently enforced parts:

| Layer | Required parity |
|---|---|
| Model | Same analysis parameters, metadata, frame/partial counts, active masks, partial identities, `frq`, `amp`, `pha`, `band_energy`, duration, and sample rate. |
| Derived data | Same spectral snapshot fields, ordering, decimation, display references, frequency bounds, onset curves, envelope samples, RDC coefficients, and residual columns. |
| Behavior | Same source/mode lifecycle, viewport state after every event, hit result, selection state, edit command, rebuild policy, and playhead mapping. |
| Geometry | Same command order, path topology, vertices/control points, gradient stops, fills, clipping, stroke widths, text/legend bounds, and scrollbar/thumb bounds. |
| Raster | No semantic or geometric visual difference. Pixel differences are allowed only for subpixel anti-aliasing or glyph rasterization caused by JUCE versus Visage. |

“Looks close,” the presence of a draw-command kind, and tests whose expected values were
derived from the new code do not meet this definition.

### 2.1 Numeric comparison policy

- Integers, enum values, vector lengths, active masks, partial/band order, frame indices,
  command kinds, command order, colors, and event outcomes: **exact**.
- Imported `.ats` double arrays: **bit-identical** after both loaders have produced an
  `ATS_SOUND`.
- WAV analysis: exact structure and active masks; each double must be bit-identical when the
  same Qatsh/ATSA code path is used. A nonzero tolerance requires a written root-cause note
  proving an unavoidable floating-point backend difference and may not hide identity/count or
  active/inactive differences.
- Snapshot floats and envelope intermediates: absolute error at most `1e-6`.
- Screen geometry: absolute error at most `0.05` logical pixel per coordinate.
- Navigation state after an input event: absolute error at most `1e-6`.
- Raster plot crop at fixed 1x and 2x scale: edge displacement at most `0.75` physical pixel,
  mean absolute channel error at most `1/255`, and at most `0.5%` of plot pixels outside a
  `3/255` per-channel tolerance. Any coherent shifted/missing ridge fails regardless of the
  aggregate score.

Tolerance increases are not a completion strategy. They require an attached difference image
and proof that data, command topology, and geometry already pass.

---

## 3. Confirmed divergences in the current recovery branch

These are observed source differences, not guesses. They must each begin with a failing oracle
comparison.

| ID | Current behavior | Legacy behavior / impact |
|---|---|---|
| XPD-01 | `SpectralTraceBuilder` chooses `min(220, frameCount)` residual columns and partitions frames proportionally (`SpectralTraceBuilder.h:304-329`). | Legacy always requests/clamps to 220 columns, uses `framesPerColumn = ceil(frames / columns)`, and leaves tail columns silent when necessary. Both column count and time grouping currently differ. |
| XPD-02 | Residual `energyNorm` is clamped to `[0,1]` at `SpectralTraceBuilder.h:328`. | Legacy clamps only below zero. A preserved display reference may legitimately yield residual energy above 1.0. |
| XPD-03 | Residual band edges are included in `minHz`/`maxHz` at `SpectralTraceBuilder.h:243-253`. | Legacy derives bounds only from active partial frequencies and returns the legacy defaults on its early-exit paths. This changes every partial, band, envelope, hit-test, and gutter Y coordinate. |
| XPD-04 | Frequency-shape settings are published, but `HostAdapter` never builds shape ranks and the builder leaves `meanFrequencyHz` and every `frequencyHz` unshaped (`HostAdapter.h:303-316`, `SpectralTraceBuilder.h:283-298`). | Legacy calls `buildFrequencyShapeRanks`, applies `applyFrequencyShape` to bounds, mean frequency, and every active display point, while preserving `sourceFrequencyHz`. |
| XPD-05 | Snapshot contents depend on view mode; residual modes omit partials and non-residual modes omit residual bands (`SpectralTraceBuilder.h:205-209`). | The legacy snapshot builder is mode-independent. Mode selects painters, not source data. |
| XPD-06 | Non-finite onset-curve entries are removed and onset records are reordered (`SpectralTraceBuilder.h:179-197`). | Legacy copies detector vectors and onset order without shrinking their frame-aligned arrays. Filtering can shift curve time positions. |
| XPD-07 | Current-source `sourceRevision` is based on `editorSnapshotRevision`, which increments on every publication (`HostAdapter.h:353-357`). | Legacy rebuilds on stable model/source/shape/onset revisions. The current value invalidates spectrum work even when the model is unchanged. |
| XPD-08 | Gaussian envelope sampling ignores a segment unless both endpoints are active (`SpectrumRenderer.h:668-678`). | Legacy Gaussian envelope sampling uses `fadeInactive=true`: a segment with one inactive endpoint fades to zero while holding the active endpoint’s frequency. Birth/death regions therefore differ. |
| XPD-09 | Current RDC basis, fit domain, weights, order, and regularizer differ (`SpectrumRdc.h:43-129`). | Legacy uses `[1, 2cos(kπx)]`, amplitude-normalized dB points, amplitude weights, adaptive order up to 10, and `0.008 * k^4` regularization. Current code uses unweighted log-linear amplitude, a different basis, default order 12, and `0.015 * k^2`. |
| XPD-10 | Planar and isometric partials share a simplified emitter; it adds endpoint ridgelines/tiles and lacks the legacy isometric depth ordering, floor shadows, culling, three-pass smoothing, and per-depth alpha policy (`SpectrumRenderer.h:393-521`). | These are different render algorithms, not framework translations. |
| XPD-11 | Planar and isometric residuals are simplified line/tile approximations (`SpectrumRenderer.h:346-391`, `523-547`). | Legacy uses filled ridges, selection/hover halos, energy gating, band-aware baselines, depth shading, and different Combined-versus-Residual treatment. |
| XPD-12 | Plain wheel input zooms; Shift/Command wheel pans, and only one scalar delta reaches the handler (`VisageEditor.cpp:1510-1535`). | Legacy plain wheel pans; Command/Ctrl wheel zooms, preserving independent `deltaX` and `deltaY`, with Shift/Alt choosing the zoom axis. |
| XPD-13 | Drag pan divides by the whole well, does not multiply by visible span, and has the opposite frequency sign (`VisageEditor.cpp:1340-1358`). | Legacy uses plot bounds, the drag-start centers, and `visibleRange.length`; its shipped mapping must be reproduced, including its isometric quirks. |
| XPD-14 | `SpectrumInteractionModel::setMode` calls `fit()` (`SpectrumInteractionModel.h:154-168`). | Legacy preserves the viewport across mode changes and resets it only when the source changes. |
| XPD-15 | `setSnapshot` is called during drawing and clears hover state each time (`SpectrumInteractionModel.h:103-134`, `VisageEditor.cpp:487-488`). | Legacy clears hover on a real snapshot rebuild, not on every paint. |

The audit must continue. New differences go into this table and receive an `XPD-*` regression
before implementation proceeds.

---

## 4. Oracle and artifact pipeline

### 4.1 Required tools

Create these test-only facilities:

| Path | Purpose |
|---|---|
| `tools/reference/spectrum_reference_export.cpp` | Compiles inside a temporary worktree at the pinned legacy commit and exports model, snapshot, envelope, render, and navigation artifacts. It calls legacy production functions; it must not reimplement their math. |
| `tools/spectrum_candidate_export.cpp` | Exports the same schema from the JUCE-free implementation. |
| `scripts/capture_spectrum_reference.sh` | Creates a temporary legacy worktree, verifies the exact SHA, overlays/builds the reference exporter, and writes goldens plus provenance. |
| `tests/spectrum_parity_compare.py` | Compares artifacts layer by layer and reports the first mismatching JSON path/array index and maximum numeric error. |
| `tests/fixtures/spectrum-parity/manifest.json` | Lists fixture hashes, parameter states, source/mode/view states, expected artifact files, and oracle provenance. |

The capture script must refuse to run when the resolved legacy SHA differs. It must never update
goldens in the ordinary test command.

### 4.2 Canonical artifact schema

Each case emits:

1. `analysis-params.json`: all `AnalysisParams`/`ANARGS` fields after UI mapping and before
   `tracker`, encoded as decimal plus hexadecimal floating-point.
2. `model.spx`: canonical little-endian dump of every relevant `ATS_SOUND` scalar and array,
   including null-row markers and active masks.
3. `model-summary.json`: counts, ranges, peaks, hashes per lane, and the first divergent values
   for human diagnosis.
4. `snapshot.spx`: all snapshot scalars and vectors in stored order. No sorted or normalized
   diagnostic projection may replace the real order.
5. `envelope.spx`: slice times; sampled `(partial, segment, freqNorm, ampNorm)` membership;
   Gaussian `sumW`, `sumWA`, radii, final bins; RDC fit points, weights, effective order,
   normal matrix, RHS, coefficients, evaluated bins; and final ridge stops.
6. `navigation.jsonl`: one pre-event and post-event state per scripted input event.
7. `commands.spx`: clip stack, command kind/order, path verb stream, vertices/control points,
   fill topology, gradient stops, colors, alpha, widths, bounds, IDs, and text.
8. `plot.png` and `window.png`: deterministic 1x and 2x captures with fixed playhead and dust.
9. `provenance.json`: legacy SHA, exporter SHA, compiler, architecture, OS, scale factor, window
   size, fixture SHA-256, parameter-state hash, and font hashes.

JSON numeric output alone is insufficient for model/snapshot comparisons because ordinary
decimal formatting can hide ULP differences. The binary artifact is authoritative; JSON is the
diagnostic companion.

### 4.3 Fixture matrix

Commit deterministic, redistributable fixtures. A local root-level `crotales.ats` is useful for
manual QA but is not a CI fixture until it is committed with provenance and licensing.

Minimum cases:

- `synthetic-short-17`: fewer frames than 220 residual columns; active/inactive partial edges;
  residual impulses; exact-peak ties; an amplitude and residual value above a preserved ruler.
- `synthetic-nondivisible-997`: exposes legacy `ceil(frames/220)` grouping and tail columns.
- `synthetic-221`: boundary case immediately above the requested residual column count.
- `synthetic-sparse-decay`: one-sided active envelope segments and fewer than 11 RDC points in
  selected frames.
- `synthetic-dense-900x720`: culling, order, performance, and path-topology stress.
- `synthetic-residual-only` and `synthetic-partials-only`: preserve legacy early-exit and default
  bounds behavior even if it appears undesirable.
- `source-a-b`: two deliberately different peaks, durations, frequency ranges, and active masks
  to prove complementary overlay selection and independent normalization.
- `analysis-tone`, `analysis-chirp`, `analysis-impulses-noise`: generated WAV inputs at 44.1 and
  48 kHz using default and non-default analysis parameters.
- `crotales`: the real dense acceptance model once it is legally committed or generated from a
  documented source.

Every snapshot case runs all four frequency-shape modes and both fresh and preserved display
references. Every render case runs all seven view modes at fit, 4x, 8x, and a clamped 32x edge
viewport.

---

## 5. Recovery tasks and gates

Tasks are ordered by causality. Work from a later phase may not be used to compensate for a
failure in an earlier phase.

### Phase P0 — Freeze and prove the legacy oracle

#### SPX-000 — Pin provenance

- Record the full legacy SHA, current SHA, build options, Qatsh patch manifest hash, font hashes,
  fixture hashes, app size, and scale factors.
- Verify that `git rev-parse 1a64f030^` equals the pinned SHA.
- Build and launch the legacy reference from a clean temporary worktree.

**Gate:** a second clean capture produces byte-identical non-raster artifacts and identical
raster hashes when the playhead is fixed.

#### SPX-010 — Build the reference exporter

- Add canonical binary writers that do not transform, sort, clamp, or omit data.
- Expose test-only wrappers around legacy static envelope helpers rather than copying their math
  into the exporter.
- Export pre-render intermediates and post-projection command/path data.

**Gate:** exporter summaries reconcile with what the legacy UI displays: counts, duration,
bounds, peaks, selected IDs, and viewport labels.

#### SPX-020 — Capture the golden corpus

- Capture the full fixture/state matrix.
- Make the capture command separate from the compare command.
- Add a manifest check that rejects missing artifacts, wrong provenance, changed fixture hashes,
  or candidate-produced goldens.

**Gate:** CI can consume the frozen corpus without JUCE, while an explicit local command can
rebuild it from the pinned oracle.

### Phase P1 — Raw model and analysis parity

#### SPX-100 — Imported ATS model equivalence

- Feed identical `.ats` bytes through the legacy loader and `SourceImportService`.
- Compare every `ATS_SOUND` scalar, pointer-row presence, and array value before any display
  transformation.
- Do not compare screenshots or snapshots until this passes.

**Gate:** `model.spx` is byte-identical for every imported ATS fixture.

#### SPX-110 — Analysis parameter equivalence

- Compare every UI-parameter-to-`AnalysisParams` field and the final `ANARGS` passed to
  `tracker`.
- Audit start/duration and Nyquist safety coercions in `analysisParamsForSource`; the legacy
  branch did not perform those coercions at the same layer.
- Audit defaults, choice rounding, analysis type offset, window indices, multi-resolution fields,
  sample count, source sample rate, mono conversion, and residual output ownership.

**Gate:** parameters are identical for the ordinary valid domain. Any deliberate safety-only
deviation is isolated to an invalid-input test and cannot affect the parity fixtures.

#### SPX-120 — Tracker output equivalence

- Run both builds on the same generated WAV bytes and parameters.
- Compare partial count/identity, frame count/times, `frq`, `amp`, `pha`, `band_energy`, duration,
  and sample rate.
- Produce lane-level error heatmaps for any failure.

**Gate:** structure and active masks are exact, and numeric values meet §2.1. If this fails,
stop; no spectral renderer task may be accepted.

### Phase P2 — Spectral snapshot parity

#### SPX-200 — One mode-independent snapshot owner

- Remove the duplicated manual `SpectralTraceInput` construction in `HostAdapter::editorSnapshot`.
- Build the full legacy-equivalent snapshot once per `(source revision, shape revision,
  onset-overlay revision, display reference)`.
- Keep view mode outside the data builder. Painters decide which lanes are visible.
- Use stable model/source revisions, not the UI publication counter.

**Gate:** switching mode changes no snapshot artifact field except UI-owned mode metadata; an
unchanged model produces no rebuild/invalidation.

#### SPX-210 — Partial snapshot algorithm

Port `buildSpectralTraceSnapshotForSound` as a reviewable pure function with the legacy loop and
branch structure intact:

- source `partialCount`, `frameCount`, and duration from the model, not inferred vector lengths;
- identical active predicate and early returns;
- bounds from displayed active partial frequencies only, with the exact log padding/fallback;
- `buildFrequencyShapeRanks` and `applyFrequencyShape` for bounds, mean, and point frequency;
- preserve `sourceFrequencyHz` separately;
- legacy `std::sort` ordering, truncation condition, point budget/step, and final-frame append;
- lower-only normalization for `peakAmpNorm` and point `ampNorm`.

**Gate:** every scalar, partial order, point count, frame index, active flag, frequency, and
amplitude field matches `snapshot.spx`.

#### SPX-220 — Residual snapshot algorithm

Reproduce exactly:

```text
columns          = clamp(16, 512, requestedColumns)       // requested = 220
framesPerColumn  = max(1, ceil(model.frames / columns))
firstFrame       = column * framesPerColumn
lastFrame        = min(model.frames, firstFrame + framesPerColumn)
columnEnergy     = max(valid positive energy in [firstFrame,lastFrame))
energyNorm       = max(0, (10*log10(max(1e-20,columnEnergy)) - floorDb) / 72)
```

- Keep all requested columns, including silent tail columns.
- Preserve model band order and exact ATSA band edges.
- Do not use residual edges to change partial-derived display frequency bounds.
- Keep values above 1.0 under a preserved display reference.

**Gate:** exact band order, 220-column count, frame membership, and normalized values for the
17/221/997-frame fixtures.

#### SPX-230 — Onsets, curves, sources, and ruler lifecycle

- Preserve frame alignment and ordering of detection/threshold curves.
- Map onset `frame`, `preFrame`, and `peakFrame` through the model frame count without silently
  sorting or changing vector length.
- Match fresh versus preserved display-reference behavior for mode change, source change,
  delete/mute, gain/scale, overlay rebuild, and copy-only operations.
- Build Source A/B/current and the complementary envelope snapshot with the legacy source-choice
  rules. The complementary surface receives an empty display reference and normalizes
  independently.

**Gate:** scripted rebuild sequences produce the same references, revisions, snapshot hashes,
and source choices after every operation.

### Phase P3 — Envelope and RDC parity before drawing

#### SPX-300 — Slice sampling

- Port `makeSliceTimes` and the monotonic per-trace sampling sweep verbatim.
- Gaussian mode uses `fadeInactive=true`; RDC mode uses `false`.
- Preserve the active endpoint’s frequency when fading a one-sided segment.
- Record source partial/segment membership for each sample.

**Gate:** slice times, sample membership/order, `freqNorm`, and `ampNorm` match the oracle for all
30 slices.

#### SPX-310 — Gaussian envelope

- Port the exact three Kovesi box radii, zero-padded running-sum blur, scatter mass,
  `kPriorW=0.02`, sparse-density sigma, 128-bin mapping, and 14-bin cosine taper.
- Do not substitute edge-renormalized blur, a general DSP Gaussian, or a visually similar
  smoother.

**Gate:** radii, `sumW`, `sumWA`, final bins, stop count/order, and ridge geometry match.

#### SPX-320 — RDC envelope

Port the shipped algorithm, not a generic cepstral envelope:

- `kSlices=30`, `kGridSize=128`, `kOrder=10`, `kReg=8e-3`, `kMinDb=-120`,
  `kDbRange=120`, `kTaperBins=14`;
- basis row `[1, 2cos(πx), ..., 2cos(kπx)]` using the same recurrence;
- fit point `dB = -120 + ampNorm*120` and weight `max(1e-4, ampNorm)`;
- effective order `min(10, max(1, pointCount-1))`;
- second-derivative penalty `reg*k^4`;
- the same pivot threshold, float precision, solve order, dense-grid evaluation, normalization,
  taper, and stop-dedupe rule.

Delete or quarantine the incompatible generic algorithm in `SpectrumRdc.h`; it must not remain
reachable from the parity surface.

**Gate:** fit inputs, matrix/RHS, effective order, coefficients, evaluated bins, and final ridges
match the oracle.

### Phase P4 — Viewport, scrolling, and input-event parity

#### SPX-400 — Canonical viewport state

- Store the legacy state directly: `timeCenter`, `timeZoom`, `freqCenter`, `freqZoom` with
  `[1,32]` zoom clamps.
- Derive ranges from center/zoom. Do not alternate between start/end and center/zoom in separate
  components.
- Source change resets to fit; mode change preserves the viewport.
- A snapshot refresh does not mutate viewport state.

**Gate:** all scripted lifecycle sequences match `navigation.jsonl` after every step.

#### SPX-410 — Lossless native input contract

The backend event contract must carry `deltaX`, `deltaY`, precise/non-precise flag if available,
Shift, Command/Ctrl, Alt, pointer position, button identity, and magnify scale. A scalar wheel
delta cannot express the legacy behavior.

- Plain wheel pans. Preserve dominant/single-axis fallback and the exact
  `14*16` pixels-per-delta constant.
- Command/Ctrl wheel zooms by `pow(1.12, dominantDelta)`.
- Command/Ctrl+Shift zooms time only; Command/Ctrl+Alt zooms frequency only; otherwise zoom both.
- Magnify zooms both axes at the pointer.
- Preserve legacy planar anchoring even in isometric modes.
- Preserve isometric wheel mapping: horizontal screen movement controls frequency and vertical
  screen movement controls time with the shipped signs.

**Gate:** a table of at least 50 wheel/magnify events covering signs, both deltas, all modifiers,
fit/4x/32x, edges, and planar/isometric modes matches exactly.

#### SPX-420 — Drag pan, scrollbars, and zoom bar

- Middle-button or Alt-drag starts panning; ordinary selection drag must not pan.
- Use drag-start centers, plot dimensions, and visible-range lengths exactly.
- Preserve the legacy shipped drag mapping in isometric views even where it differs from wheel
  mapping.
- Horizontal/vertical scrollbar range, thumb, enabled state, alpha, inversion, and isometric axis
  swap must match.
- Zoom-bar callbacks, suppression guard, center preservation, and reset behavior must match.

**Gate:** viewport state and all chrome/thumb rectangles match after drag and scrollbar scripts;
there is no cumulative drift after 1,000 alternating pan events.

### Phase P5 — Draw-command fidelity

#### SPX-500 — Express the legacy primitives without approximation

The command model must support everything the legacy renderer uses:

- a path verb stream containing move, line, cubic, close, and multiple subpaths;
- one curved/rounded stroke over a complete path;
- multi-stop linear/radial gradients;
- arbitrary closed-path fills, including ridge-to-baseline polygons;
- independent fill and stroke paths;
- clip push/pop;
- dashed strokes;
- deterministic dust ellipses;
- text plus alignment/font/letter-spacing metadata.

If Visage lacks one primitive, write a small backend adapter that preserves the legacy geometry.
Do not replace a fill with tiles, a path with endpoint markers, or a gradient stroke with one
segment command per input point.

**Gate:** the candidate can round-trip every oracle command/path field in `commands.spx`.

#### SPX-510 — Port `SpectrumRender` primitives literally

- Port `smoothInPlace`, `decimateInPlace`, Catmull-Rom controls, stop dedupe, taper, flat-path
  handling, LUT interpolation, gradient stroke, and `drawRidgeSlices` with matching float math and
  operation order.
- Unit tests use oracle exports, not hand-derived expectations alone.

**Gate:** path verb streams, controls, stops, max amp, and draw order match across sparse, tied,
and dense traces.

### Phase P6 — Mode painters

#### SPX-600 — Backdrop and planar modes

- Port backdrop, clip order, grid snapping, time labels, title, empty state, vignette, dust RNG,
  amp legend, gutters, planar residual backdrop, planar partials, harmonic guides, hover, and
  selection exactly.
- Remove fabricated endpoint ridgelines/tiles not emitted by JUCE.
- Preserve Combined-versus-Residual flags, alphas, passes, and selected-last ordering.

**Gate:** command parity passes for empty, sparse, dense, selected, hovered, and preserved-ruler
cases in Combined, Partials, and Residual.

#### SPX-610 — Isometric partial and residual modes

- Port floor grid/axis posts, visibility padding, painter depth order, floor shadows, depth shade,
  smoothing pass counts, filled residual ridges, gating, baseline rules, halos, and strokes.
- Preserve the planar/isometric half-column time offset in residual rendering.

**Gate:** command and plot-raster parity pass in Iso 3D and Iso 3D Residual at all viewport
states.

#### SPX-620 — Envelope modes

- Render the P3 ridge artifacts via the exact legacy `drawRidgeSlices` order and alpha policy.
- Match Source A/B toggle order, warm overlay ramp, independent normalization, selection slab,
  floor placement, and RDC/non-RDC painter choice.

**Gate:** every ridge can be matched by `(source, slice index, stop index)` with no missing,
extra, or shifted surface; plot-raster parity passes for both envelope modes.

#### SPX-630 — Chrome and playhead

- Match header overlap behavior first, then decide any correction in a separate non-parity change.
- Match toolbars, labels, scrollbars, zoom bar, display cache scale, invalidation boundaries,
  playhead target selection, follow filter, reset conditions, wake, cull bounds, and 60 Hz overlay.

**Gate:** fixed-playhead command parity and live scripted playhead trajectories pass; playhead-only
updates do not rebuild snapshot/envelope data or invalidate the static display cache.

### Phase P7 — Interaction and editing parity

#### SPX-700 — Hit-testing and selection

- Hit-test the same projected, active geometry with the same radii and tie behavior.
- Match lasso, residual range, band gutter, envelope slab, onset precedence, selection ordering,
  additive modifiers, hover lifecycle, mouse-exit behavior, and cursor state.

**Gate:** identical event scripts produce identical selected/hovered IDs and readouts after every
event in all modes.

#### SPX-710 — Editing and rebuild discipline

- Compare the full command payload reaching the model for delete, scale, draw, gain, smooth,
  tilt, copy/paste, swap, redistribute, solo, freeze, prune, and onset edits.
- Compare the resulting raw model dump, undo transaction, display-reference choice, revision
  latch, snapshot, and selection state after each edit.
- Preserve no-rebuild copy behavior and non-preserving mute/delete behavior.

**Gate:** action traces and post-edit `model.spx`/`snapshot.spx` match for every operation and
undo/redo round trip.

### Phase P8 — End-to-end acceptance

#### SPX-800 — Deterministic visual matrix

Capture both apps at fixed window sizes `1200x900` and `1600x900`, scale 1x and 2x:

- seven modes;
- empty, short, sparse, dense, Source A/B, and crotales fixtures;
- fit, 4x, 8x, and edge-clamped 32x viewports;
- no selection, hover, single select, lasso multi-select, active edit affordance;
- fixed playhead positions `0.0`, `0.25`, `0.5`, `0.99`.

Generate side-by-side, absolute-difference, and threshold-mask images. The report must link each
failed region back to its first divergent artifact layer.

**Gate:** all data/behavior/geometry comparisons are green and raster thresholds in §2.1 pass.

#### SPX-810 — Performance after parity

- Measure snapshot build, Gaussian envelope, RDC envelope, static repaint, and playhead-only frame
  separately.
- Optimize only after the unoptimized literal port passes the oracle.
- Each optimization must preserve the full artifact hashes or accepted numeric tolerances.

**Gate:** dense static repaint median remains at or below 25 ms at 1600x900 on the documented
reference machine, with no parity regression.

#### SPX-820 — Correct the project record

- Replace `docs/spectrum-view-parity-verification.md` with generated oracle results and artifact
  hashes.
- Update the eight `UI-SPECTRUM-*` records and traceability manifest only after SPX-800 passes.
- Record any intentionally fixed legacy bug as a separate post-parity decision and test. It may
  not be folded into the parity port.

**Gate:** no document says “parity complete” unless it links to a green SPX-800 report from the
current commit.

---

## 6. Dependency order

```text
P0 oracle
  -> P1 model/analysis
    -> P2 snapshot
      -> P3 envelope math
      -> P4 viewport/events
        -> P5 command primitives
          -> P6 painters/chrome
            -> P7 interaction/edit results
              -> P8 visual, performance, documentation
```

P3 and P4 may proceed in parallel only after P2 is green. P5 may begin as infrastructure, but
no painter can be accepted while its source data or viewport artifacts differ.

---

## 7. Pull-request and test discipline

- One phase per reviewable change series; use the `SPX-*` IDs in commits and test names.
- Every `XPD-*` starts with an oracle comparison that fails on the current branch.
- The failure must identify the first divergent layer and field/index.
- Never update a golden in the same commit that changes candidate production code.
- Golden-update changes contain only pinned-oracle artifacts, provenance, and a human-reviewed
  diff report.
- Do not bless the candidate output because it appears preferable. First reproduce JUCE; improve
  it afterward in a separately approved change.
- Do not optimize or refactor a legacy loop until its literal port passes and the optimization is
  proven artifact-equivalent.
- Do not stage unrelated mode-bit and AppleDouble changes in this repository.

Required fast checks:

```text
python3 tests/spectrum_parity_compare.py --layers model,snapshot,envelope,navigation,commands
ctest --test-dir build-review-ui --output-on-failure -R '^spectrum_(model|snapshot|envelope|rdc|navigation|command|raster)_parity'
python3 tests/architecture/traceability_manifest_check.py
python3 tests/ui/ui_parity_manifest_check.py
git diff --check
```

The final report also runs the clean legacy recapture verification and complete visual matrix.

---

## 8. Final acceptance checklist

- [ ] Pinned legacy oracle is reproducible and provenance-checked.
- [ ] Imported ATS models are byte-identical before display processing.
- [ ] WAV analysis parameters and tracker output match.
- [ ] Snapshot scalars, lane order, frame indices, partial identities, active masks, normalized
      amplitudes, frequencies, residual columns, bounds, onsets, and curves match.
- [ ] Gaussian and RDC intermediates and ridges match, not merely their presence or count.
- [ ] Mode changes preserve viewport; source changes reset it; refreshes do neither.
- [ ] Wheel, magnify, drag, scrollbars, and zoom bar match after every scripted event.
- [ ] Draw-command order, path topology, fill geometry, gradients, strokes, clips, and chrome
      geometry match.
- [ ] All seven mode raster comparisons pass at 1x and 2x for the full matrix.
- [ ] Clicking, lassoing, drawing, scaling, onset editing, and all toolbar actions produce the
      same selection/action/model/rebuild result.
- [ ] No fabricated marker, tile, clamping, sorting, filtering, fallback, or “close enough”
      algorithm remains in the parity path.
- [ ] Performance passes only after parity passes.
- [ ] Project docs no longer make an evidence-free parity claim.

Parity is complete only when every box is checked by generated evidence from the same candidate
commit. A green renderer unit test or a visually improved screenshot is not a substitute.
