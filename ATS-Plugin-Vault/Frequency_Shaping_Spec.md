# Frequency Shaping — Product & Implementation Spec

**Status:** Implemented; playback wiring follow-up patched (May 29, 2026)  
**Covers matrix features:** Frequency Stretching, Frequency Compression, Harmonic Scaling  
**Primary surface:** Synthesis tab + spectrogram live display  
**Related code:** `SynthTabPanel`, `PluginProcessor`, `AtsParams::SynthParams`, `SpectrogramPanel`, `buildSpectralTraceSnapshotForSound`

---

## 1. Goal

Add **non-uniform frequency warping** at synthesis time—beyond the existing uniform `freq` transpose—while keeping UI compact and reusing existing plugin patterns.

Users must **see and hear** shaping immediately:

- **Display:** partial/residual trace view updates on every parameter movement without waiting for offline resynthesis.
- **Audio:** preview resynth follows existing `synthParamsDirty` path with coalesced background worker; playback restarts or continues from current playhead when the new buffer is ready.

---

## 2. Scope

### In scope (v1)

| Feature | User-facing name | Mode |
| ------- | ---------------- | ---- |
| Uniform transpose (existing) | **Linear** | `freq` knob only |
| Frequency stretching | **Stretch** | Log spacing expands around pivot |
| Frequency compression | **Compress** | Log spacing tightens around pivot |
| Harmonic scaling | **Harmonic** | Warp by harmonic index / partial rank |

### Out of scope (v1)

- Morph snapshot baking of warp when raw source frame data is unavailable
- Per-partial warp curves or drawn warp maps
- Analysis-tab (ANARGS) parameters
- Residual Bark-band frequency warping
- MathExpr scripting (phase 2)

---

## 3. UX — single “Frequency shaping” block

### 3.1 Location

**Synthesis tab only** (`SynthTabPanel`). Do not add controls to Session, Analysis, or Morph lanes.

### 3.2 Layout (reuse existing widgets)

```
Row 0  Hint (updated copy)
Row 1  [freq] [timeStretch] [synStart] [synDur]     ← unchanged rotaries
Row 2  Frequency shaping
       Mode [ComboBox]   Amount [rotary]   Pivot [rotary/slider]
Row 3  Phase / partials / residual checkboxes        ← unchanged
Row 4  Both playback combo + hint                      ← unchanged
```

**Widget reuse:**

| Control | Pattern source |
| ------- | -------------- |
| Mode | `classicPreviewModeBox` (APVTS `AudioParameterChoice`) |
| Amount | `freqSlider` rotary + `SliderAttachment` |
| Pivot | `synStartSlider` range style; Hz with `0 = Auto` |
| Section hint | `hint` label on Synth tab |

### 3.3 Mode combo (`freqShapeMode`)

| Index | Label | Row 2 visibility |
| ----- | ----- | ---------------- |
| 0 | Linear | Amount + Pivot **disabled**; only `freq` active |
| 1 | Stretch | Amount + Pivot enabled |
| 2 | Compress | Amount + Pivot enabled |
| 3 | Harmonic | Amount + Pivot enabled; pivot label **F0 (Hz)** |

### 3.4 Dynamic labels & tooltips

| Mode | Amount label | Amount help |
| ---- | ------------ | ----------- |
| Linear | — | — |
| Stretch | Spacing expand | `1` = no warp; `>1` spreads partials in log space around pivot |
| Compress | Spacing tighten | `<1` pulls partials together in log space around pivot |
| Harmonic | Harmonic ratio | `1` = no warp; `>1` exaggerates harmonic spacing; `<1` collapses |

Pivot help (all non-linear modes): *Frequency in Hz about which warping is applied. `0` = auto (see §5.3).*

### 3.5 Hint text (Row 0)

> These parameters mirror SPARAMS-style global synthesis (transpose, time window, phase). **Frequency shaping** warps partial spacing at playback time; the trace display updates immediately. Use the Analysis tab to tune the next analyze pass.

### 3.6 Presets (`PresetLibrary`)

Add factory entries (full-state or parameter-only):

- **Inharmonic spread** — Stretch, amount ~1.35, pivot auto  
- **Clustered partials** — Compress, amount ~0.72, pivot auto  
- **Harmonic bloom** — Harmonic, amount ~1.15, pivot auto  

---

## 4. Parameters (APVTS)

All host-automatable; persisted in plugin state and `.atspreset`.

| ID | Type | Range | Default | Notes |
| -- | ---- | ----- | ------- | ----- |
| `freqShapeMode` | Choice | Linear, Stretch, Compress, Harmonic | Linear | |
| `freqShapeAmount` | Float | 0.25 … 4.0, skew 0.35 | 1.0 | Identity at 1.0 |
| `freqShapePivotHz` | Float | 0 … 4000 Hz | 0 | `0` = auto pivot |

**Application order at synthesis:**

1. Frequency shape warp (per partial, per frame)  
2. Uniform `freq` factor  
3. MIDI semitone offset (`midiSemitoneOffset`)

Linear mode: skip step 1 (amount ignored).

Register in `createParameterLayout()`. Changes call `parameterChanged()` → mark display dirty (§6) + `synthParamsDirty` (§7).

---

## 5. DSP — shared warp functions

Implement once in `src/core/` (e.g. `AtsFrequencyShape.h/.cpp`) used by **both** display and resynth.

### 5.1 Stretch / compress (log-pivot warp)

For source frequency `f > 0`, pivot `p > 0`, amount `a > 0`:

```
fWarped = p * pow(f / p, a)
```

- `a > 1` → stretch (wider spacing)  
- `a < 1` → compress (tighter spacing)  
- `a = 1` → identity  

Mode enum selects whether UI copy says “stretch” or “compress”; same formula for both.

After warp, clamp to `[1 Hz, nyquist * 0.99]`.

### 5.2 Harmonic scaling

For partial with harmonic index `n` (1-based rank by ascending mean frequency or track order), fundamental `F0 = pivot`:

```
fHarmonic = n * F0
fWarped   = fHarmonic * pow(f / fHarmonic, a)    when fHarmonic > 0
```

If `fHarmonic` undefined (non-harmonic partial), fall back to log-pivot warp with same `a`.

Harmonic index `n` for v1: **sort partials by mean active frequency**, assign ranks 1…P (document in UI tooltip as approximate).

### 5.3 Auto pivot (`freqShapePivotHz == 0`)

Compute once per model + shape revision:

1. **Default:** amplitude-weighted mean frequency of active partials at mid-frame.  
2. **Harmonic mode fallback:** same value treated as F0 estimate.  
3. Cache in processor; invalidate when `modelRevision` changes or pivot param changes from auto to manual.

Expose resolved pivot in UI readout: `Auto → 187 Hz`.

---

## 6. Real-time display updates (required)

The spectrogram **must** reflect shaping parameters immediately while the user drags controls—without mutating the stored ATS model and without waiting for `startResynthWorker()`.

### 6.1 Requirements

| Requirement | Target |
| ----------- | ------ |
| Trace Y position (frequency) | Updates on param change within **one UI frame** (~10 Hz timer + immediate `parameterChanged` repaint) |
| Pivot / F0 guide line | Moves in sync with pivot control or auto estimate |
| Hover readout | Shows **warped** Hz at cursor, e.g. `P12 880→946 Hz (Stretch 1.2)` |
| Iso 3D modes | Same warped frequencies for partial geometry |
| Residual bands | **Unchanged** in v1 (noise path not warped); no misleading residual Y shift |
| Model data | `ATS_SOUND` never modified by shaping params |

### 6.2 Architecture

```
APVTS change (freqShape*)
        │
        ├─► displayShapeRevision++     (message thread, cheap)
        │         │
        │         └─► SpectrogramPanel::timerCallback / parameter listener
        │               rebuildTraceSnapshot() with shape applied at read time
        │
        └─► synthParamsDirty++         (existing)
                  └─► startResynthWorker() when coalesced
```

**Do not** bump `modelRevision` for shape-only changes (shape is not an edit to the analysis).

### 6.3 Implementation — display path

Extend `buildSpectralTraceSnapshotForSound()` (or post-process trace points in `SpectrogramPanel::rebuildTraceSnapshot`):

1. Read current shape params from APVTS (`readFrequencyShapeFromApvts()`).  
2. When building each `AtsPartialTracePoint`, set `frequencyHz` from model, then replace with `applyFrequencyShape(f, partialIndex, rank, settings)`.  
3. Recompute `freqNorm` from warped Hz and existing `minFrequencyHz` / `maxFrequencyHz` (recompute Y bounds if warp expands range).  
4. Store `displayShapeRevision` on snapshot; skip rebuild if `modelRevision` and `displayShapeRevision` unchanged.

**SpectrogramPanel** already polls `processor.getModelRevision()` at 10 Hz; extend to:

```cpp
processor.getDisplayShapeRevision()  // new atomic, incremented in parameterChanged for shape IDs
```

On mismatch → `rebuildTraceSnapshot(false)` (preserve amp display reference if desired).

Optional: call `repaintDisplay()` directly from `parameterChanged` for shape IDs when editor is open (sub-10 Hz latency while dragging).

### 6.4 Visual overlays (`paintSelectionOverlay`)

When `freqShapeMode != Linear`:

- Dashed horizontal line at resolved pivot Hz (planar + iso).  
- Top-left badge: `Stretch 1.35 @ 220 Hz` (mode, amount, pivot).  
- Colour: reuse residual hover accent (`0xffbaffea`).

When `Linear`, hide overlay.

### 6.5 Selection & editing interaction

- Partial selection indices unchanged (model indices).  
- **Partials tab** cell editor shows **stored** model Hz, not warped (tooltip: “Shaping applied at synthesis”).  
- Vertical scale drag on traces still edits **model** amplitudes, not display warp.

---

## 7. Real-time audio updates

Reuse existing preview pipeline.

### 7.1 Resynth trigger

Shape params listed in `parameterChanged` alongside `freq`, `timeStretch`, etc.:

- Set `synthParamsDirty`  
- `triggerAsyncUpdate()` → `startResynthWorker()`  
- Coalesce rapid drags via existing `resynthJobSerial` cancellation

### 7.2 Apply warp at synthesis

In `renderClassicLayers()` / path to `do_synthesis`:

- **Option A (preferred v1):** Pre-pass copies `ATS_SOUND` to stack buffer, warps `frq[p][f]` in copy, then synthesizes (model lock held briefly).  
- **Option B:** Wrap synthesis entry to apply warp when reading frequencies (if ATSA API allows hook).

Include shape fields in `previewLayerCacheCore` equality check (`samePreviewLayerCore`) so cache invalidates when shape changes.

### 7.3 Playback UX while dragging

| State | Behavior |
| ----- | -------- |
| Not playing | Replace preview buffer when worker completes; no extra UI |
| Playing | When new buffer ready: **restart from same normalized playhead** if possible, else continue from sample 0; avoid glitches via existing buffer swap |
| Resynthesizing | Session/Analysis status may show worker busy; **trace display still live** (§6) |

### 7.4 Morph / source slots

- **Current model preview:** full shape support.  
- **Source A/B listen:** shape applies to slot resynth (same APVTS globals); listen buttons rebuild the slot buffer on play so cached audio cannot mask shape changes.  
- **Morph preview / timeline / bounce:** shape applies to raw source frame data by rendering from shaped copies of the ATS sources. Frozen snapshot fallback remains unwarped when raw source sounds are unavailable.

---

## 8. State & persistence

- Shape params live in APVTS (preset-safe).  
- Not serialized into `.ats` files (playback-only, like `freq`).  
- Plugin state version bump if new fields appended to existing blob.  
- `SynthParams` struct extended:

```cpp
enum class FrequencyShapeMode { Linear, Stretch, Compress, Harmonic };

struct FrequencyShapeSettings {
    FrequencyShapeMode mode = FrequencyShapeMode::Linear;
    double amount = 1.0;
    double pivotHz = 0.0;       // 0 = auto
    double resolvedPivotHz = 0.0; // runtime cache, not persisted
};
```

---

## 9. Acceptance criteria

### Display (real-time)

- [x] Dragging **Amount** while in Stretch mode moves partial traces vertically within 100 ms perceptually (immediate paint on drag).  
- [x] Changing **Mode** combo instantly switches trace geometry between linear / stretched / compressed / harmonic preview.  
- [x] **Pivot** drag moves dashed guide line and trace warp together.  
- [x] **Auto pivot** updates readout when model changes; badge shows resolved Hz.  
- [x] Residual / Iso 3D Residual band rows do not shift when shape changes.  
- [x] No change to stored partial frequencies when adjusting shape (verify via Partials tab readout).

### Audio

- [x] Preview resynth completes after shape change and reflects warp audibly.  
- [x] `freq` + shape stack correctly (transpose after warp).  
- [x] Automation of `freqShapeAmount` during playback produces updated output without crash.  
- [x] Rapid slider movement coalesces to one audible resynth (no worker pile-up).

### UX

- [x] Linear mode hides/disables shape amount and pivot.  
- [x] Tooltips match mode-specific copy (§3.4).  
- [x] At least one factory preset demonstrates each non-linear mode.

### Regression

- [x] Default preset (Linear, amount 1) bit-matches current behavior.  
- [x] Morph snapshot stale/ready UX unchanged when only shape params move.

---

## 10. Implementation phases

### Phase 1 — Core + display (real-time visual)

1. [x] `AtsFrequencyShape` + unit tests (stretch/compress/harmonic identities at amount=1).  
2. [x] APVTS params + `SynthTabPanel` Row 2.  
3. [x] `displayShapeRevision` + warped trace snapshot.  
4. [x] Spectrogram pivot overlay + badge.

### Phase 2 — Audible resynth

1. [x] Warp copy in `renderClassicLayers`.  
2. [x] Layer cache includes shape in core equality.  
3. [x] Playback playhead preservation on buffer swap.

### Phase 3 — Polish

1. [x] PresetLibrary entries.  
2. [x] Feature matrix row updates.  
3. [ ] Optional: bipolar “Spacing” combining stretch/compress into one mode.

### Verification

- [x] `build/tests/frequency_shape_test`
- [x] `ctest --test-dir build -R frequency_shape_test --output-on-failure`
- [x] `cmake --build build --target AtsResynth`
- [x] `cmake --build build --target AtsResynth_All`

### May 29 follow-up verification

- [x] Source A/B listen rebuilds preview buffers after shape changes instead of reusing stale cached audio.
- [x] Morph preview, morph timeline preview, and timeline bounce apply frequency shaping when raw source frame data is available.
- [x] Frequency shape mode is read from the `AudioParameterChoice` index for playback/display consistency.
- [x] Installed VST3/AU/Standalone artifacts refreshed after the playback wiring fix.

---

## 11. Feature matrix mapping (when shipped)

| Matrix row | Status after v1 |
| ---------- | --------------- |
| Frequency Stretching | **Done** |
| Frequency Compression | **Done** |
| Harmonic Scaling | **Done** (rank-based v1; F0 inference improved in v2) |

Evidence string: *Synthesis Frequency shaping + live spectrogram warp display + resynth preview.*

---

## 12. Open questions

1. **Harmonic rank:** mean-frequency sort vs tracker partial index—which matches user expectation for plucked/inharmonic sources?  
2. **Nyquist handling:** clamp vs fold partials above Nyquist? (Default: clamp + optional warning in badge.)  
3. **Formant link:** defer formant-preserving pitch to separate spec; do not overload this combo.

---

## 13. File touch list (implementation)

| File | Change |
| ---- | ------ |
| `src/core/AtsFrequencyShape.h/.cpp` | Warp math + tests |
| `src/core/AtsParams.h` | `FrequencyShapeSettings` on `SynthParams` |
| `src/plugin/PluginProcessor.cpp` | APVTS, `parameterChanged`, resynth apply, `displayShapeRevision` |
| `src/plugin/PluginProcessor.h` | `getDisplayShapeRevision()`, read helpers |
| `src/plugin/SynthTabPanel.cpp/.h` | Row 2 UI |
| `src/plugin/SpectrogramPanel.cpp` | Overlay, hover warped Hz, revision poll |
| `src/plugin/PresetLibrary.cpp` | Factory presets |
| `tests/frequency_shape_test.cpp` | Math identities + monotonicity |
