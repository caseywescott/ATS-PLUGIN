# SP-ENV-01 — Spectrum isometric envelope gain

Status: implemented

Replaces `SpectrogramPanel::scaleEnvBoxGain()` which computed a gain multiplier
from a drag delta and wrote it to the residual matrix.

## Ownership

`SpectrumInteractionModel::applyEnvelopeGain(int band, int frame, float gainLinear)`
returns a `ResidualEditAction{band, frame, gainLinear}`.

`gainLinear` is the linear scale factor (1.0 = unity). The residual processor
applies this factor to the band's envelope column at `frame`. The caller computes
`gainLinear` from the normalised drag delta against the isometric display
coordinate system.

Evidence:

```text
arch -x86_64 cmake --build build-review --target spectrum_dispatch_test -j4
ctest --test-dir build-review --output-on-failure -R '^spectrum_dispatch_test$'
```

`test_envelope_gain` verifies: band/frame passthrough; gainLinear stored as
`value`; unity gain produces 1.0 value.

Deviations: drag-delta → gainLinear transform and isometric coordinate mapping
require the Visage native event harness. The `isEnvelopeDisplayMode()` predicate
check (gates which modes enable gain dragging) is a follow-on pure test.
