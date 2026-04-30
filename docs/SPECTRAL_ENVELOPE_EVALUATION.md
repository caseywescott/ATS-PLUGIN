# Spectral-Envelope Evaluation and Lookup Policies

**Status:** Phase 3 implementation note
**Related docs:** [SPECTRAL_ENVELOPE_PRD.md](./SPECTRAL_ENVELOPE_PRD.md), [SPECTRAL_ENVELOPE_TASKS.md](./SPECTRAL_ENVELOPE_TASKS.md)

Envelope coefficients are the source of truth. Evaluation helpers sample those coefficients at requested frequencies for partial transforms, target-envelope lookup, and UI overlays.

---

## Mapping Modes

- **Direct Hz:** use the requested frequency as the envelope lookup frequency.
- **Normalized Nyquist:** map the requested frequency through its normalized Nyquist position from a source axis to a target axis.
- **Perceptual warp:** map by normalized position on a perceptual axis such as Bark, mel, or ERB.
- **Range warp:** map by linear position between source and target frequency ranges.

These mappings do not require F0 or harmonic numbering.

---

## Extrapolation Policies

- **Clamp:** out-of-range requests are clamped to the target axis boundary and reported with `inRange = false`.
- **Reject:** out-of-range requests fail evaluation and return `ok = false`.

The default policy is clamp, because UI grids and hybrid mappings often need stable edge behavior. Destructive transforms can choose reject when silent failure would be safer than boundary coloring.

---

## Optional LPC / All-Pole Estimator

The LPC estimator is available as a lightweight, non-default comparison path. It resamples the frame's weighted log-amplitude points onto the configured envelope axis, derives an autocorrelation from that power spectrum, solves a Levinson-Durbin all-pole model, and stores the evaluated envelope as sampled log-amplitude values.

LPC is useful for broad resonant, formant-like contours when the frame has enough support across frequency. It should be treated carefully on sparse ATS partial frames: isolated high-energy partials, missing bands, high-pitched material, and inharmonic partial clusters can pull the all-pole model into narrow resonances that read as overfit rather than a general spectral envelope. Sparse LPC fits report diagnostics so presets and UI warnings can steer users back toward RDC or true-envelope comparisons when appropriate.

---

## Research Prototype: F0-Adaptive Waveform Envelope

The F0-adaptive waveform prototype is intentionally optional and non-default. It is exposed as `f0-adaptive/waveform-prototype`, but it only estimates when the frame includes waveform-derived spectrum samples plus reliable F0 metadata. Partial-only ATS frames, missing waveform data, out-of-range F0 values, low F0 confidence, and sparse spectra are rejected with diagnostics instead of silently falling back to a speech-oriented assumption.

The prototype follows the CheapTrick/WORLD design idea at a high level: smooth a dense waveform spectrum using a frequency bandwidth tied to the current F0, then store the result as sampled log-amplitude envelope values. This can help singing, spoken voice, and monophonic pitched instruments where F0 is reliable and the waveform spectrum contains enough harmonic detail. It is not a general replacement for RDC: inharmonic sources, dense polyphony, noisy textures, percussive frames, missing waveform input, or uncertain F0 should continue to use the default sparse-partial estimator or another explicit comparison estimator.

---

## Task 4.3 Research Metrics

The Phase 4 Task 4.3 sub-band prototype adds two evaluation metrics on top of the standard envelope RMS checks:

- **Band Seam Error:** evaluate each adjacent band pair over their overlap region before the final overlap-add mix, then report seam RMS and seam max in dB. The current research threshold is `<= 0.75 dB RMS` and `<= 1.8 dB max`.
- **Boundary-Sweep Zipper Error:** for a synthetic sequence that moves a bright bump across a band boundary, subtract the known truth motion from the estimated frame-to-frame motion and report RMS / max residual in dB. This is the guard against visually or audibly obvious stitching jumps when a feature crosses from one local band fit to another.
