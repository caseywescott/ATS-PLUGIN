# UI-SYNTH-02 — Both-playback mode and synthesis transport

Status: implemented

Replaces `SynthTabPanel::classicPreviewModeBox` APVTS combo attachment and the
`classicPreviewModeHint` label.

`SynthesisPageModel::setLayerMixPlayback(bool)` tracks the "Classic combined"
vs "Layer mix" selection. `bothPlaybackLabel()` returns the exact user-visible
string: `"Classic combined"` when `layerMixPlayback == false`, `"Layer mix"`
when `true`. This mirrors the two items in the source
`classicPreviewModeBox.addItemList({"Classic combined", "Layer mix"}, 1)`.

The Both-playback selection is dispatched as a `SetParameterAction` for the
`classicPreviewMode` parameter ID. `setHasSource(bool)` gates transport
controls; `hasSource == false` disables playback dispatch on the page.

Evidence:

```text
arch -x86_64 cmake --build build-review \
  --target synth_partial_math_page_test -j4
ctest --test-dir build-review --output-on-failure \
  -R '^synth_partial_math_page_test$'
```

The test covers: default "Classic combined" label, switch to "Layer mix",
setHasSource guard, and SetParameterAction validation for classicPreviewMode.

Deviations: audio-path evidence (Both playback producing output) remains a
follow-on DSP-PLAY gate. Live combo dispatch evidence requires native Visage
Visage dispatch harness.
