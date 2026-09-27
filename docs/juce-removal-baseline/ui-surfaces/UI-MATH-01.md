# UI-MATH-01 — MathExpr script editor and worker-result state machine

Status: verified (2026-09-27)

Replaces `MathExprPanel` JUCE `TextEditor`, progress/error labels, and the
timer-driven success flash.

## Ownership

`MathPageModel` (in `src/ui/EditorPageModels.h`) owns all MathExpr UI state.
No timer or widget owns secondary truth; the model is the single state source.

### State machine

| Trigger | State before | State after |
|---|---|---|
| `setDraft(script)` (every text edit, and on Return) | any | draft updated; status unchanged |
| `beginApply()` | idle / success / error / cancelled | `running`; returns `false` if already running |
| `complete(true, {}, now)` | running | `success`; flash expires at `now + 600 ms` |
| `complete(false, msg, now)` | running | `error`; `error()` returns `msg` |
| `cancel()` | running | `cancelled`; draft preserved |
| `focusLoss()` | any | draft preserved; in-flight worker unaffected |

`canApply()` returns `true` only when draft is non-empty and status is not
`running`. `applyAction()` returns `std::nullopt` when `!canApply()`, otherwise
`ApplyMathAction{draft_}`.

`successFlashActive(now)` returns `true` exactly when `status == success` and
`now < flashUntil_`. The flash window is 600 ms — no polling timer or
widget-side countdown is used; the caller supplies the current time.

### Help and starter text

`helpText()` and `starterText()` are `static constexpr` and available without
a model instance. This avoids the JUCE approach of storing them in label
widgets.

### Action protocol

`ApplyMathAction` requires a non-empty `script`; empty script is rejected by
`validateAction` with code `"empty_math_script"`. The contract assigns
`ActionThreadDomain::worker`, `ActionCancellation::explicitCancel`,
`ActionUndoPolicy::oneTransaction`.

Evidence:

```text
arch -x86_64 cmake --build build-review \
  --target synth_partial_math_page_test editor_page_models_test -j4
ctest --test-dir build-review --output-on-failure \
  -R '^(synth_partial_math_page_test|editor_page_models_test)$'
```

`synth_partial_math_page_test` covers: idle→running→success flash at
599/601 ms, idle→running→error (message preserved, no flash), cancel state
(draft preserved, canApply re-enabled), focus-loss draft preservation (idle and
running), multiline draft, empty-script protocol rejection, action thread-domain
and undo-policy contract, and double-start idempotency.

`editor_page_models_test` covers the baseline beginApply / complete / flash
boundary.

Deviations: live script-editor text entry and worker result dispatch require
the native Visage harness. The `ats_math_expr_test` provides deterministic
script evaluation evidence separately.

## Verification (2026-09-27, `f2057a51`)

RMJ-032 step 18: typed script + APPLY SCRIPT emits ApplyMath with the text (draft now updates per edit); host_current_model_workflow_test applies and undoes MATH on the current model.
