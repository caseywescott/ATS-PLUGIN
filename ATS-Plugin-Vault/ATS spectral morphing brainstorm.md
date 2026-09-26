# ATS spectral morphing: brainstorm

## Goal

Interpolate frequency and amplitude trajectories between two ATS analyses so that a **morph parameter** (e.g. 0 = sound A, 1 = sound B) produces a continuous family of sounds that feels **musically and perceptually coherent**, with **fewer metallic smears, phasiness, pumping, and “wrong” partial births/deaths**.

**Historical precedent:** the 1994 film *Farinelli* fused countertenor Derek Lee Ragin and soprano Eva Mala-Godlevska into a single castrato voice (~3,000 manual edits plus IRCAM vocal-tract morphing). See [[Farinelli voice synthesis]] and [Making of Castrato](https://www.youtube.com/watch?v=9BlZ4HagwRI).

## Status snapshot

This note started as a pre-implementation brainstorm. As of `2026-05-23`, most of the baseline morph-to-ATS plan described here has been implemented in the plugin codebase and captured in:

- `docs/MORPH_TO_ATS_ANALYSIS_PRD.md`
- `docs/MORPH_TO_ATS_ANALYSIS_TASKS.md`

What is now in place:

- Bark is the default perceptual grid, with Mel available as an alternate option.
- Partials are assigned by instantaneous frequency, with track-centroid diagnostics.
- Correspondence tiers now include frequency-rank, greedy nearest, and assignment-matrix matching.
- Frequency interpolation uses log-frequency by default, with Bark-coordinate interpolation available.
- Amplitude interpolation uses power by default, with dB interpolation available.
- Band-energy bookkeeping, unmatched-partial policies, residual/noise morphing, phase strategy metadata, transient handling, immutable morph snapshots, prepared snapshot playback, and UI diagnostics are all implemented.
- The Session UI now includes source A/B slots, morph lanes, policy controls, band inspection, live snapshot states, and morph-focused presets.

Still open in practice:

- Listening validation and refinement of defaults.
- Deciding whether some defaults should shift after broader subjective testing, especially unmatched-partial handling and Mel promotion.
- Deciding how far to push phase-heavy or onset-heavy strategies beyond the current baseline.

## Current idea (baseline)

1. **Perceptual frequency grid**: Partition the spectrum using **Bark** (or Mel) bands so grouping respects rough human pitch/timbre scaling.
2. **Within-band partials**: Assign sinusoidal partials to bands (by instantaneous frequency or track centroid).
3. **Morph**: For each band (or pooled partials in that band), **interpolate frequency and amplitude** (and possibly phase) as a function of morph amount.

This is still the right description of the implemented baseline: it avoids matching thousands of partials globally and keeps computation local. The main risks remain **ambiguous correspondence** inside a band, **energy bookkeeping**, and **phase/time** behavior when partial counts differ.

---

## Why naive band-wise interpolation hurts

| Issue | What goes wrong |
|--------|------------------|
| **Partial identity** | Two partials in the same Bark bin are not interchangeable; cross-bleeding swaps “which is which” frame-to-frame → zipper noise, beating, wrong timbre. |
| **Count mismatch** | A has 3 strong partials in a band, B has 1; linear mixing does not say *which* three become *which* one without a **matching** rule. |
| **Frequency domain** | Linear Hz interpolation is not perceptually uniform; very low and very high partials behave differently. **Log-frequency** (or ERB/Bark warping) usually behaves better for small shifts. |
| **Amplitude domain** | Linear amp mix can **under-represent** quiet components and cause **level jumps**; **power** or **dB** interpolation (with a floor) often sounds smoother for timbre. |
| **Phase** | Ignoring phase can still work for noise-like content, but for **harmonic/coherent** material, random phase per partial causes **phasiness** or **comb filtering** when partials are dense. |
| **Temporal incoherence** | Morphing **per frame** without **trajectory smoothing** across time produces **jitter** in both freq and amp. |
| **Onsets / transients** | ATS partials smooth attacks; morphing two attacks often yields a **blurred** attack unless you **special-case** transient regions. |

Use this table as a checklist when listening for artifacts.

---

## What landed from this brainstorm

### Implemented baseline and beyond

Several items that were originally framed here as directions are now real parts of the system:

- **Within-band matching**:
  frequency-rank pairing shipped first, then greedy nearest matching, then an advanced assignment-matrix strategy with bounded candidate sets.
- **Perceptual interpolation domains**:
  log-frequency and Bark-coordinate interpolation are both implemented; power and dB amplitude interpolation are both implemented.
- **Trajectory stabilization**:
  temporal smoothing and correspondence churn diagnostics are implemented.
- **Birth/death and count mismatch handling**:
  fade, hold, merge-to-residual, and discard-with-opt-in policies are implemented, along with per-band energy diagnostics.
- **Residual/noise separation**:
  deterministic partials and residual/noise content are treated as separate lanes with independent gain paths.
- **Phase and transient policies**:
  named phase strategies and transient policies are implemented as recipe metadata and UI controls.
- **Prepared playback path**:
  morph recipes render immutable snapshots off the audio thread, then the plugin plays prepared partial/residual layers safely on the audio thread.

### Implemented user-facing controls

The plugin now exposes most of the controls imagined here:

- host-automatable `morphAmount`
- linked and unlinked morph lanes
- performance macro and smoothing
- source A/B slots with diagnostics
- policy controls for grid, correspondence, unmatched handling, interpolation domains, phase, and transient behavior
- Bark/Mel band strip with selected-band inspector
- quick snapshots, variation slots, and live stale/rebuilding/ready/warning states
- factory morph presets for stable default, tonal, vocal/formant, percussive, and noisy texture use cases

## Directions to make morphing more realistic

### 1. Better correspondence than “everything in the bin”

Status:
the system now has three correspondence tiers, so this section is no longer hypothetical. The remaining question is which strategy should become the default for which musical material.

**Band pooling** is a cheap prior, but **within-band matching** helps a lot:

- **Greedy nearest-neighbor** in log-frequency, optionally constrained by amplitude rank (match strongest to strongest first).
- **Stable marriage / assignment** on a cost matrix (frequency distance + optional harmonic index penalty) for frames where counts are similar.
- **Soft assignment** (e.g. small transport weights) so a partial can contribute to multiple targets with weights that sum to 1 — reduces hard swaps.

**Harmonic structure**: If you can infer a local **F0** or harmonic index \(k\) for each partial, add a term to the cost: penalize matching partials with very different \(k\). That keeps morphs from sliding noise partials onto harmonic “slots” incorrectly.

### 2. Interpolate in perceptual domains

Status:
implemented. The remaining work is mostly listening-driven default selection and preset tuning.

- **Frequency**: **log Hz**, **ERB**, or **Bark** coordinates for interpolation; optionally clamp to band edges to avoid partials leaking implausibly across formants.
- **Amplitude**: **power** (\(a^2\)) or **dB** with a noise floor; consider **loudness-weighted** crossfade so weak partials do not dominate the blend.
- **Optional**: interpolate **band energy** and **spectral shape** (envelope) separately from **fine peak placement** — coarse timbre follows the morph, fine detail is regularized.

### 3. Trajectory smoothing across time

Status:
implemented in the baseline through continuity-aware smoothing and churn metrics. More advanced global tracking could still improve difficult material.

Even with good per-frame matching, **match tracks across frames** (Viterbi-style or Hungarian with temporal continuity cost) so each partial’s path is **smooth** in log-f and dB. Costs:

- Frequency jump penalty
- Amplitude jump penalty
- **Birth/death** penalty for appearing/disappearing tracks (or fade in/out over a few ms)

This reduces **twittering** and random reassignment glitches.

### 4. Handle birth, death, and count mismatch explicitly

Status:
implemented. This is now mostly a default-policy and perceptual-tuning problem rather than a missing-feature problem.

When \(n_A \neq n_B\) in a band:

- **Resynthesis strategy**: Map excess partials to **distributed energy** in the band (noise floor or shaped noise) instead of forcing spurious peaks.
- **Morph-dependent splitting/merging**: At morph 0.5, allow **two** tracks to gradually merge into **one** by collapsing frequency separation and summing power — closer to how listeners fuse components.

Avoid silently dropping energy; **conserve band power** (within a tolerance) to prevent **hollow** or **over-bright** bands.

### 5. Phase strategies (pick one philosophy)

Status:
metadata and policy plumbing are implemented, but perceptual validation is still open. The current default remains oscillator continuation.

- **Coarse morph, fine phase random**: Accept random phase for highly inharmonic material; use **relative phase** preservation within a **cluster** when partials are clearly harmonic.
- **Minimum-phase / STFT-hybrid**: For critical passages, **morph amplitudes + frequencies** in a frame model but **re-estimate phase** via overlap-add with a phase vocoder or Griffin–Lim style projection (heavier, but can reduce phasiness).
- **Locked phase to a reference**: Choose one analysis as **phase donor** and morph only magnitudes toward the other — classic “spectral crossfade” flavor, very stable but less “true interpolation.”

### 6. Separate **deterministic partials** from **stochastic residual**

Status:
implemented as separate morph lanes with residual/noise diagnostics and independent gain paths.

Real ATS data often benefits from treating **sinusoidal tracks** and **noise / residual** differently:

- Morph **sinusoids** with the rules above.
- Morph **noise band energies** (or residual envelopes) with **smoother, possibly independent** curves (sometimes **only amplitude** of noise bands).

This reduces **airy hiss** fighting **tonal peaks** in the same bin.

### 7. Onset- and formant-aware morphing

Status:
partially implemented. Transient markers, transient policy metadata, and onset-alignment warnings are in place. Explicit formant-aware morphing is still future work.

- **Time-warp or crossfade around attacks**: Detect **onset strength**; near onsets, bias toward **one** source’s partial set or use a **shorter analysis window** so the attack is not over-smoothed.
- **Formant preservation**: Identify **formant peaks** (broad bumps) and interpolate **formant frequencies/bandwidths** explicitly, then **re-attach** finer partial structure — good for vowel-like morphs.

### 8. Morph controls beyond a single knob

Status:
implemented in the current UI through linked/unlinked lanes, performance macro, smoothing, band inspection, snapshots, and morph presets.

Single-parameter morphs are ambiguous. Consider exposing:

- **Timbre vs pitch** split (e.g. envelope from A, fine pitch micro-variation from B).
- **Spectral tilt** vs **peak structure** as separate sub-sliders.
- **Inharmonicity** blend (useful for bell ↔ string morphs).

### 9. Validation and iteration loop

Status:
substantially implemented. The repo now has fixture manifests, a metric harness, recipe tests, energy/churn diagnostics, and performance profiling. The main remaining loop is subjective listening and preset/default refinement.

- **Objective**: band-wise RMS error, spectral centroid / spread trajectory smoothness, harmonic-to-noise ratio vs morph amount.
- **Subjective**: sweep morph at **fixed MIDI pitch** and **fixed duration**; log “bad” morph values and inspect **which Bark bands** misbehave — often one band drives the whole artifact.

---

## Possible implementation tiers

1. **Tier 0**: Bark bins → per-bin amp/freq interpolation, naive partial pairing by sorted frequency.
2. **Tier 1**: Log-f + dB interpolation, greedy matching, temporal median / one-pole smoothing on tracks.
3. **Tier 2**: Assignment with continuity, band power conservation, separate noise-band morph.
4. **Tier 3**: Harmonic index / F0-informed matching, onset logic, optional phase vocoder projection pass.

Current progress:
the codebase has effectively completed Tier 0 through Tier 2 of this note’s framing, plus much of the surrounding UI, persistence, and playback integration. Tier 3 remains the clearest “next ideas” bucket.

---

## Open questions after the first implementation pass

- **Default policy tuning**:
  should unmatched-partial defaults stay fade-out/fade-in, or move toward merge-into-residual once broader listening confirms it behaves better?
- **Mel promotion**:
  should Mel remain an alternate preset path, or earn a more central role for specific material classes?
- **Time alignment depth**:
  normalized time is implemented as the baseline, but richer anchor- or onset-aware alignment is still open.
- **User intent balance**:
  for presets and future defaults, where should the system sit between crossfade-like stability and stronger “true intermediate timbre” behavior?
- **Tier 3 expansion**:
  is the next push harmonic-index/F0-informed matching, stronger transient intelligence, or optional phase-vocoder-style projection for difficult material?

---

## Related vault notes

- [[Modern ATSH Engine - PRD Summary]]
- [[Plugin (VST) - PRD Summary]]

---

*This note is now best read as a record of the original morphing direction plus a map of what remains interesting after the first implementation pass.*
