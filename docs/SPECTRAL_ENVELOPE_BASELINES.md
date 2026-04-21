# Spectral-Envelope Transfer Baselines

**Status:** Phase 0 oracle baseline
**Related docs:** [SPECTRAL_ENVELOPE_PRD.md](./SPECTRAL_ENVELOPE_PRD.md), [SPECTRAL_ENVELOPE_FIXTURES.md](./SPECTRAL_ENVELOPE_FIXTURES.md)

This document defines the first deterministic baseline for spectral-envelope transfer. It does not estimate an envelope yet. Instead, it uses known analytic source and target envelopes over an inharmonic partial set so later implementations can compare against a clear expected result.

---

## Baseline Purpose

The baseline demonstrates why target-only envelope scaling is not the default product behavior.

If a source amplitude already contains its own spectral envelope, then simply multiplying by the target envelope leaves the source color in place and adds the target color on top. For a vocoder-like transfer, the safer default is:

1. remove the source envelope from the source partial amplitudes,
2. preserve the source fine detail,
3. apply the target envelope.

This is the behavior described in [SPECTRAL_ENVELOPE_PRD.md](./SPECTRAL_ENVELOPE_PRD.md) section 5.4.

---

## Oracle Frame

The Phase 0 test constructs one synthetic ATS-like frame:

- inharmonic partial frequencies,
- a known source envelope in dB,
- a known target envelope in dB,
- fine per-partial detail that should survive transfer.

The source frame is:

```text
source_amp_db = source_env_db(f) + fine_detail_db
```

The expected decolored transfer is:

```text
expected_db = fine_detail_db + target_env_db(f)
```

The target-only baseline is:

```text
target_only_db = source_amp_db + target_env_db(f)
```

The decolor-transfer baseline is:

```text
decolor_transfer_db = source_amp_db - source_env_db(f) + target_env_db(f)
```

The test asserts that `decolor_transfer_db` reproduces `expected_db` within floating-point tolerance and that `target_only_db` has a materially larger error because it retains the source envelope.

---

## Non-Speech Guardrail

The partial frequencies are intentionally not a harmonic series. This baseline must remain valid without:

- F0 estimation,
- harmonic-number lookup,
- speech formant assumptions,
- waveform access.

Future fixture tests can add speech and singing, but this baseline protects the default general-source behavior.
