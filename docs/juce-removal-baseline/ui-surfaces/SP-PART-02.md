# SP-PART-02 — Spectrum partial edit action

Status: implemented

Replaces `SpectrogramPanel::runPartialMute()` / `runPartialDelete()` JUCE
processor write calls.

## Ownership

`SpectrumInteractionModel::applyPartialEdit(int partial, int frame, bool apply)`
returns a `PartialEditAction{partial, frame, apply}`.

- `apply == true` means "restore this partial".
- `apply == false` means "mute/delete this partial".
- The returned action is dispatched through `EditorHost::dispatch()` to the
  processor's `PartialEditService`.

`handleKey()` with `kKeyDelete` or `kKeyBackspace` when `selectedPartial >= 0`
produces `PartialEditAction{selectedPartial, 0, false}` automatically.

Evidence:

```text
arch -x86_64 cmake --build build-review --target spectrum_dispatch_test -j4
ctest --test-dir build-review --output-on-failure -R '^spectrum_dispatch_test$'
```

`test_partial_edit` verifies: correct partial/frame/apply passthrough.
`test_handle_key` (Delete path) verifies keyboard→PartialEditAction routing.

Deviations: audio-path evidence (silence after mute, restored sound after undo)
is gated on `DSP-EDIT-001` and requires a live render pass.
