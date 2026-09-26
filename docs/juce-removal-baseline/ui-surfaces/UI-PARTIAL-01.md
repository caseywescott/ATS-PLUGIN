# UI-PARTIAL-01 — Partial cell editor: validation, focus-loss, and undo

Status: implemented

Replaces `PartialEditPanel` JUCE `TextEditor` / slider widgets and
`AtsAudioProcessor::writePartialCell`.

## Ownership

`PartialPageModel` (in `src/ui/EditorPageModels.h`) owns the draft cell state
and routing to `PartialEditView` and `PartialEditService`.

- `setDimensions(partials, frames)` records the valid index range.
- `beginCell(partial, frame, amp, freq)` opens a draft and begins an edit.
- `setDraft(amp, freq)` updates the in-progress draft without committing.
- `commitCell()` validates the draft via `cellIsValid()` and returns a
  `PartialEditCommand{writeCell, …}` only when the draft indices are in-range,
  amplitude is finite and non-negative, and frequency is finite and positive.
  Returns `std::nullopt` for invalid inputs — no model mutation occurs.
- `focusLoss()` calls `PartialEditView::cancelCellEdit()`, discarding the draft
  without producing a command. This matches the source behavior where the text
  editor losing focus without an explicit Return did not commit the value.

`PartialEditService::apply()` applies the commit command to a `PartialMatrix`
copy, validates the cell, pushes an undo entry only on success, and commits the
copy to the matrix. Undo/redo restores complete matrix snapshots.

## Action protocol

`PartialEditAction` maps to: `ActionThreadDomain::ui`,
`ActionCancellation::teardown`, `ActionUndoPolicy::oneTransaction`.
Negative partial or frame index is rejected by `validateAction`.

Evidence:

```text
arch -x86_64 cmake --build build-review \
  --target synth_partial_math_page_test partial_edit_transaction_test -j4
ctest --test-dir build-review --output-on-failure \
  -R '^(synth_partial_math_page_test|partial_edit_transaction_test)$'
```

`synth_partial_math_page_test` covers: valid cell commit, out-of-range
rejection (partial/frame out of bounds, non-finite amplitude, non-positive
frequency), focus-loss cancel (edit discarded, `commitCell` returns nullopt),
out-of-bounds `beginCell` then commit rejected, undo/redo through
`PartialEditService`, and `PartialEditAction` protocol validation.

`partial_edit_transaction_test` covers: box select, scale, undo/redo, invalid
write rejection, dB draw, clipboard copy/paste, lasso, gain box, seek, zoom,
pan, keyboard events, and `PartialEditView` commit.

Deviations: live text-editor focus dispatch remains a follow-on native harness
probe. Residual editing is covered under UI-SPECTRUM-05 / SP-RES-*.
