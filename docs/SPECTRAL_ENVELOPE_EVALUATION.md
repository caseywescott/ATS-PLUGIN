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
