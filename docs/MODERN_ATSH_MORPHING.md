# Modern ATSH Phase 4 Morphing

Phase 4 adds a model-domain morph engine. Morph recipes are separate from
source models, serialize deterministically, and produce a new
`ats::ModernAtsModel` snapshot that can be rendered by the Phase 3 synth core.

## Recipe

`ats::MorphRecipe` records:

- recipe id,
- source model id,
- target model id,
- correspondence strategy,
- unmatched source/target policies,
- transient mode,
- transient-window protection,
- optional F0/harmonicity hints,
- independent envelopes for frequency, amplitude, partial noise, orphan
  residual, time, transient timing, transient gain, and transient source mix.

Supported unmatched policies:

- `fade_out`,
- `fade_in`,
- `nearest`,
- `hold`,
- `discard`.

## Correspondence

The morph engine supports explicit maps and automatic maps:

- **Harmonic-number matching:** used when F0 and harmonicity are reliable.
- **Nearest-frequency matching:** greedy matching using frequency distance with
  slope and SMR terms.
- **Automatic fallback:** harmonic when reliable, nearest otherwise, with
  unmatched source/target partials retained according to policy.

Correspondence entries store source id, target id, label, source/target labels,
and unmatched policies. Serialization is stable so session recall can snapshot
recipes without depending on the source model payload.

## Interpolation

`morphModernAtsModels` emits a fresh modern snapshot:

- Frequency, amplitude, per-partial noise, orphan residual, and time use
  independent envelopes.
- Phase uses shortest-arc interpolation when both sides are active and keeps the
  active side phase at births/deaths.
- Unmatched source partials can fade out, hold, or discard.
- Unmatched target partials can fade in, hold, or discard.
- Transient timing, source blend, and gain are separately controllable.

The output snapshot remains deterministic and renderable through
`ats::renderModernAtsModel`.

## Transient-Aware Controls

Transient markers and short buffers are paired by marker order for Phase 4.
Recipes can choose source-only, target-only, blended, or morph-position-following
transient source selection. Protected transient windows are kept open so
percussive attacks do not collapse into sustained material during morphing.

## Tests

The `ats_modern_morph` CTest covers:

- deterministic recipe serialization and state recall fields,
- harmonic-number correspondence,
- nearest-frequency fallback for non-harmonic material,
- unmatched source/target fade policies,
- independent interpolation of frequency, amplitude, partial noise, orphan
  residual, and time,
- transient timing/source/gain morph controls,
- deterministic, finite Phase 3 renders of morphed snapshots,
- simple click-size regression checks while sweeping morph position.

Fixture metrics currently recorded by the test include output partial count,
frame count, transient marker count, total protected attack-window duration, and
orphan residual energy.

Phase 5 plugin integration consumes the morph workspace parameters against the
current and previous Modern ATS analysis snapshots. The UI diagnostics report
matched/unmatched correspondence counts and output partial counts from
`morphModernAtsModels`, while unavailable source slots are reported as waiting
instead of silently producing an inert control state.
