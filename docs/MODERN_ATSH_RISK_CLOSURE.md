# Modern ATSH Risk Closure

Phase 8 starts closing the PRD section 8 open questions that affect release
behavior. The first closed decision is stereo/multichannel analysis policy.

## Channel Policy

The MVP analysis engine remains a mono ATS/ATSH model internally:

- mono WAV input is analyzed directly and recorded as `mono_input`,
- stereo or wider WAV input is averaged into a mono analysis buffer and recorded
  as `downmix_to_mono_average`,
- per-channel and mid/side analysis stay reserved schema values until they have
  dedicated analysis, morphing, UI, and acceptance coverage.

The policy is explicit in `ats::ModernAtsHeader`:

- `inputChannelCount` records the imported source channel count,
- `channelPolicy` records the analysis channel decision.

This keeps old ATS compatibility intact while making stereo imports
reproducible from modern snapshots. It also prevents a future per-channel or
mid/side implementation from silently changing existing sessions.

## Validation Rules

`validateModernAtsModel` enforces:

- positive input channel count,
- mono sources use `mono_input`,
- multichannel sources do not use `mono_input`,
- partial channel indices are inside the recorded source channel count.

The `ats_modern_model` CTest covers serialization, default policy selection,
and invalid policy rejection.
