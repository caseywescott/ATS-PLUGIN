// Commit 27 evidence: UI-SYNTH-01, UI-SYNTH-02, UI-PARTIAL-01, UI-MATH-01
// Covers: synthesis parameter setters/dimming/labels/Both-playback, partial
// cell validation/focus-loss/undo, and MathExpr worker state machine.

#include "../src/ui/EditorActionProtocol.h"
#include "../src/ui/EditorPageModels.h"
#include "../src/engine/PartialEditService.h"

#include <cassert>
#include <chrono>
#include <cmath>
#include <limits>

// ---------------------------------------------------------------------------
// UI-SYNTH-01: parameter controls and Linear dimming
// ---------------------------------------------------------------------------
static void test_synthesis_dimming_and_modes()
{
    using namespace ats::ui;
    SynthesisPageModel model;

    // Default mode is linear — shape controls are dimmed.
    assert(model.shapeControlsDimmed());
    assert(model.state().shapeMode == FrequencyShapeMode::linear);

    // Any non-linear mode lifts the dim.
    for (auto mode : { FrequencyShapeMode::stretch,
                       FrequencyShapeMode::compress,
                       FrequencyShapeMode::harmonic })
    {
        model.setShapeMode(mode);
        assert(!model.shapeControlsDimmed());
    }
    model.setShapeMode(FrequencyShapeMode::linear);
    assert(model.shapeControlsDimmed());
}

static void test_synthesis_parameter_setters()
{
    using namespace ats::ui;
    SynthesisPageModel model;

    // frequencyFactor: must be finite and positive.
    assert(model.setFrequencyFactor(2.0f));
    assert(model.state().frequencyFactor == 2.0f);
    assert(!model.setFrequencyFactor(0.0f));
    assert(!model.setFrequencyFactor(-1.0f));
    assert(!model.setFrequencyFactor(std::numeric_limits<float>::quiet_NaN()));

    // timeStretch: must be finite and positive.
    assert(model.setTimeStretch(1.5f));
    assert(model.state().timeStretch == 1.5f);
    assert(!model.setTimeStretch(0.0f));
    assert(!model.setTimeStretch(std::numeric_limits<float>::infinity()));

    // startSeconds: must be finite and non-negative.
    assert(model.setStartSeconds(0.5f));
    assert(model.state().startSeconds == 0.5f);
    assert(!model.setStartSeconds(-0.1f));
    assert(!model.setStartSeconds(std::numeric_limits<float>::quiet_NaN()));

    // shapeAmount: must be finite (any real value allowed).
    assert(model.setShapeAmount(-2.0f));
    assert(model.state().shapeAmount == -2.0f);
    assert(!model.setShapeAmount(std::numeric_limits<float>::quiet_NaN()));

    // shapePivotHz: must be finite and non-negative.
    assert(model.setShapePivotHz(1000.0f));
    assert(model.state().shapePivotHz == 1000.0f);
    assert(!model.setShapePivotHz(-1.0f));
    assert(!model.setShapePivotHz(std::numeric_limits<float>::quiet_NaN()));

    // Boolean flags require no validation — just toggle.
    model.setUsePhase(false);
    assert(!model.state().usePhase);
    model.setUsePhase(true);
    assert(model.state().usePhase);

    model.setUseAllPartials(false);
    assert(!model.state().useAllPartials);

    model.setUseIfftSynth(true);
    assert(model.state().useIfftSynth);

    model.setResidualOnly(true);
    assert(model.state().residualOnly);
}

// ---------------------------------------------------------------------------
// UI-SYNTH-01: duration label ("FULL") and pivot label ("Auto")
// ---------------------------------------------------------------------------
static void test_synthesis_labels()
{
    using namespace ats::ui;
    SynthesisPageModel model;

    // Duration 0 → "FULL".
    assert(model.setDuration(0.0f));
    assert(model.durationLabel() == "FULL");

    // Non-zero duration shows numeric value.
    assert(model.setDuration(2.5f));
    assert(model.durationLabel().find("2.5") != std::string::npos);

    // Negative duration is rejected; label is unchanged.
    assert(!model.setDuration(-1.0f));
    assert(model.durationLabel().find("2.5") != std::string::npos);

    // Pivot ≤ 0.005 → "Auto".
    assert(model.setShapePivotHz(0.0f));
    assert(model.pivotLabel() == "Auto");

    // Positive pivot → "{N} Hz".
    assert(model.setShapePivotHz(1000.0f));
    const auto piv = model.pivotLabel();
    assert(piv.find("Hz") != std::string::npos);
    assert(piv.find("1000") != std::string::npos);
}

// ---------------------------------------------------------------------------
// UI-SYNTH-02: Both-playback mode label and setHasSource
// ---------------------------------------------------------------------------
static void test_synthesis_both_playback()
{
    using namespace ats::ui;
    SynthesisPageModel model;

    // Default is Classic combined.
    assert(!model.state().layerMixPlayback);
    assert(model.bothPlaybackLabel() == "Classic combined");

    // Switching to Layer mix.
    model.setLayerMixPlayback(true);
    assert(model.state().layerMixPlayback);
    assert(model.bothPlaybackLabel() == "Layer mix");

    // setHasSource gates controls.
    assert(!model.state().hasSource);
    model.setHasSource(true);
    assert(model.state().hasSource);
}

// ---------------------------------------------------------------------------
// UI-SYNTH: action validation for synthesis-related actions
// ---------------------------------------------------------------------------
static void test_synthesis_action_validation()
{
    using namespace ats::ui;

    // SetParameterAction for each synthesis parameter.
    const std::string params[] = {
        "freq", "timeStretch", "synStart", "synDur",
        "freqShapeMode", "freqShapeAmount", "freqShapePivotHz",
        "usePhase", "useAllPartials", "useIfftSynth", "residualOnly", "classicPreviewMode"
    };
    for (const auto& id : params)
    {
        const auto v = validateAction(EditorAction { SetParameterAction { id, 0.5f } });
        assert(v.valid);
    }

    // Empty ID is invalid.
    assert(!validateAction(EditorAction { SetParameterAction { {}, 0.5f } }).valid);

    // Non-finite value is invalid.
    assert(!validateAction(EditorAction {
        SetParameterAction { "freq", std::numeric_limits<float>::quiet_NaN() } }).valid);

    // SetParameterAction contract: UI thread, gesture undo.
    const auto contract = actionContractFor<SetParameterAction>();
    assert(contract.owner == ActionThreadDomain::ui);
    assert(contract.undo == ActionUndoPolicy::gesture);
}

// ---------------------------------------------------------------------------
// UI-PARTIAL-01: cell validation, out-of-range rejection, focus-loss cancel
// ---------------------------------------------------------------------------
static void test_partial_cell_validation()
{
    using namespace ats::ui;
    PartialPageModel model;
    model.setDimensions(4, 8);

    // Valid cell: in-range indices, finite amplitude and positive frequency.
    model.beginCell(2, 3, 0.5, 440.0);
    assert(model.cellIsValid(2, 3, 0.5, 440.0));

    // Out-of-range partial rejected.
    assert(!model.cellIsValid(4, 3, 0.5, 440.0));
    assert(!model.cellIsValid(-1, 3, 0.5, 440.0));

    // Out-of-range frame rejected.
    assert(!model.cellIsValid(2, 8, 0.5, 440.0));

    // Non-finite amplitude rejected.
    assert(!model.cellIsValid(2, 3, std::numeric_limits<double>::quiet_NaN(), 440.0));

    // Non-positive frequency rejected.
    assert(!model.cellIsValid(2, 3, 0.5, 0.0));
    assert(!model.cellIsValid(2, 3, 0.5, -1.0));

    // Commit succeeds for a valid in-progress edit.
    model.setDraft(0.8, 880.0);
    const auto cmd = model.commitCell();
    assert(cmd.has_value());
    assert(cmd->kind == ats::PartialEditKind::writeCell);
    assert(cmd->partial == 2 && cmd->frame == 3);
    assert(cmd->frequencyHz == 880.0);

    // After commit the view is no longer editing.
    assert(!model.view().editing());
}

static void test_partial_focus_loss_cancels_edit()
{
    using namespace ats::ui;
    PartialPageModel model;
    model.setDimensions(4, 8);

    // Begin editing a valid cell.
    model.beginCell(1, 2, 0.3, 220.0);
    assert(model.view().editing());

    // Focus loss cancels the draft without producing a command.
    model.focusLoss();
    assert(!model.view().editing());
    assert(!model.commitCell().has_value());
}

static void test_partial_out_of_bounds_beginCell_then_commit()
{
    using namespace ats::ui;
    PartialPageModel model;
    model.setDimensions(4, 8);

    // Begin a cell outside the matrix dimensions.
    model.beginCell(10, 20, 0.5, 440.0);
    // commitCell checks cellIsValid and returns nullopt for invalid position.
    assert(!model.commitCell().has_value());
}

// ---------------------------------------------------------------------------
// UI-PARTIAL-01: undo/redo through PartialEditService
// ---------------------------------------------------------------------------
static void test_partial_undo_redo()
{
    ats::PartialMatrix matrix(3, 4);
    for (int p = 0; p < 3; ++p)
        for (int f = 0; f < 4; ++f)
            matrix.at(p, f) = { 1.0 + p, 100.0 + 10.0 * p, 0.0 };

    ats::PartialEditService service;

    // Select box [0..1, 0..1].
    auto box = ats::PartialEditCommand{};
    box.kind = ats::PartialEditKind::box;
    box.firstPartial = 0; box.lastPartial = 1; box.firstFrame = 0; box.lastFrame = 1;
    assert(service.apply(matrix, box).accepted);

    // Scale the selection to 0.5x.
    auto scale = ats::PartialEditCommand{};
    scale.kind = ats::PartialEditKind::scaleSelection;
    scale.value = 0.5;
    assert(service.apply(matrix, scale).changed);
    const double scaledAmp = matrix.at(0, 0).amplitude;
    assert(scaledAmp < 1.0);

    // Undo restores original amplitude.
    assert(service.canUndo());
    assert(service.undo(matrix).changed);
    assert(matrix.at(0, 0).amplitude > scaledAmp);

    // Redo re-applies scale.
    assert(service.canRedo());
    assert(service.redo(matrix).changed);
    assert(matrix.at(0, 0).amplitude == scaledAmp);

    // Invalid write does not create an undo entry.
    const auto before = matrix;
    auto bad = ats::PartialEditCommand{};
    bad.kind = ats::PartialEditKind::writeCell;
    bad.partial = 0; bad.frame = 0; bad.amplitude = -1.0; bad.frequencyHz = 100.0;
    const auto badResult = service.apply(matrix, bad);
    assert(!badResult.accepted);
    assert(matrix.cells == before.cells);
}

// ---------------------------------------------------------------------------
// UI-PARTIAL-01: PartialEditAction validation in EditorActionProtocol
// ---------------------------------------------------------------------------
static void test_partial_action_protocol()
{
    using namespace ats::ui;

    // Valid partial action.
    const auto valid = validateAction(EditorAction { PartialEditAction { 0, 0, true } });
    assert(valid.valid);

    // Negative partial is invalid.
    assert(!validateAction(EditorAction { PartialEditAction { -1, 0, true } }).valid);

    // Negative frame is invalid.
    assert(!validateAction(EditorAction { PartialEditAction { 0, -1, true } }).valid);

    // Contract: UI thread, teardown cancellation, one transaction undo.
    const auto contract = actionContractFor<PartialEditAction>();
    assert(contract.owner == ActionThreadDomain::ui);
    assert(contract.cancellation == ActionCancellation::teardown);
    assert(contract.undo == ActionUndoPolicy::oneTransaction);
}

// ---------------------------------------------------------------------------
// UI-MATH-01: worker state machine
// ---------------------------------------------------------------------------
static void test_math_worker_states()
{
    using namespace ats::ui;
    MathPageModel model;

    // Idle with empty draft: cannot apply.
    assert(!model.canApply());
    assert(!model.applyAction().has_value());
    assert(model.status() == MathStatus::idle);

    // Set a single-line draft.
    model.setDraft("amp = amp * 0.5");
    assert(model.canApply());
    const auto action = model.applyAction();
    assert(action.has_value());
    assert(action->script == "amp = amp * 0.5");

    // beginApply transitions to running; canApply returns false while running.
    assert(model.beginApply());
    assert(model.status() == MathStatus::running);
    assert(!model.canApply());

    // Success: flash lasts 600 ms.
    const auto t0 = std::chrono::steady_clock::now();
    model.complete(true, {}, t0);
    assert(model.status() == MathStatus::success);
    assert(model.error().empty());
    assert(model.successFlashActive(t0 + std::chrono::milliseconds(599)));
    assert(!model.successFlashActive(t0 + std::chrono::milliseconds(601)));
}

static void test_math_error_state()
{
    using namespace ats::ui;
    MathPageModel model;
    model.setDraft("amp = bad$");
    assert(model.beginApply());

    const auto t0 = std::chrono::steady_clock::now();
    model.complete(false, "syntax error at '$'", t0);
    assert(model.status() == MathStatus::error);
    assert(model.error() == "syntax error at '$'");
    // Error state: no flash.
    assert(!model.successFlashActive(t0));
    // After error, canApply is true again so user can fix the script.
    assert(model.canApply());
}

static void test_math_cancel_state()
{
    using namespace ats::ui;
    MathPageModel model;
    model.setDraft("amp *= 0.9");
    assert(model.beginApply());
    model.cancel();
    assert(model.status() == MathStatus::cancelled);
    // Draft is preserved after cancel.
    assert(model.draft() == "amp *= 0.9");
    // canApply true again after cancel.
    assert(model.canApply());
}

// ---------------------------------------------------------------------------
// UI-MATH-01: focus-loss preserves draft and does not submit
// ---------------------------------------------------------------------------
static void test_math_focus_loss_preserves_draft()
{
    using namespace ats::ui;
    MathPageModel model;
    const std::string script = "freq = freq * 1.5\namp = amp * 0.8";
    model.setDraft(script);

    // Focus loss while idle: draft is preserved, status unchanged.
    model.focusLoss();
    assert(model.draft() == script);
    assert(model.status() == MathStatus::idle);
    assert(model.canApply());

    // Focus loss while running: draft and running status are preserved.
    assert(model.beginApply());
    model.focusLoss();
    assert(model.status() == MathStatus::running);
    assert(model.draft() == script);
}

// ---------------------------------------------------------------------------
// UI-MATH-01: multiline script and action validation
// ---------------------------------------------------------------------------
static void test_math_multiline_and_action_protocol()
{
    using namespace ats::ui;
    MathPageModel model;

    // Multiline draft (newline-separated expressions).
    const std::string multiline = "amp = amp * 0.9\nfrequency = frequency * 1.1\n";
    model.setDraft(multiline);
    assert(model.canApply());
    assert(model.applyAction()->script == multiline);

    // Empty script is invalid per EditorActionProtocol.
    assert(!validateAction(EditorAction { ApplyMathAction { {} } }).valid);

    // Non-empty script is valid.
    assert(validateAction(EditorAction { ApplyMathAction { "amp *= 2" } }).valid);

    // Contract: worker thread, explicit cancellation, one transaction undo.
    const auto contract = actionContractFor<ApplyMathAction>();
    assert(contract.owner == ActionThreadDomain::worker);
    assert(contract.cancellation == ActionCancellation::explicitCancel);
    assert(contract.undo == ActionUndoPolicy::oneTransaction);

    // Help and starter text are non-empty.
    assert(!MathPageModel::helpText().empty());
    assert(!MathPageModel::starterText().empty());
}

// ---------------------------------------------------------------------------
// UI-MATH-01: beginApply idempotency (cannot double-start)
// ---------------------------------------------------------------------------
static void test_math_cannot_double_start()
{
    using namespace ats::ui;
    MathPageModel model;
    model.setDraft("amp = 1.0");
    assert(model.beginApply());
    assert(!model.beginApply());   // already running
}

int main()
{
    test_synthesis_dimming_and_modes();
    test_synthesis_parameter_setters();
    test_synthesis_labels();
    test_synthesis_both_playback();
    test_synthesis_action_validation();

    test_partial_cell_validation();
    test_partial_focus_loss_cancels_edit();
    test_partial_out_of_bounds_beginCell_then_commit();
    test_partial_undo_redo();
    test_partial_action_protocol();

    test_math_worker_states();
    test_math_error_state();
    test_math_cancel_state();
    test_math_focus_loss_preserves_draft();
    test_math_multiline_and_action_protocol();
    test_math_cannot_double_start();
    return 0;
}
