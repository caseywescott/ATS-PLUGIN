#include "../src/ui/SpectrumInteractionModel.h"
#include "../src/ui/SpectrumViewModel.h"

#include <cassert>
#include <cmath>

using namespace ats::ui;

// SP-DRAW-02: cacheRevision changes with new snapshot revisions
static void test_cache_revision()
{
    SpectrumViewModel vm;
    assert(vm.cacheRevision() == 0);

    SpectralTraceSnapshot snap1;
    snap1.sourceRevision       = 1;
    snap1.displayShapeRevision = 0;
    assert(vm.accept(snap1));
    const auto rev1 = vm.cacheRevision();
    assert(rev1 != 0);

    SpectralTraceSnapshot snap2;
    snap2.sourceRevision       = 1;
    snap2.displayShapeRevision = 1;
    assert(vm.accept(snap2));
    assert(vm.cacheRevision() != rev1);

    SpectralTraceSnapshot snap3;
    snap3.sourceRevision       = 2;
    snap3.displayShapeRevision = 0;
    assert(vm.accept(snap3));
    assert(vm.cacheRevision() != rev1);
}

// SP-DRAW-03: stale snapshot is rejected when sourceRevision regresses
static void test_stale_rejection()
{
    SpectrumViewModel vm;

    SpectralTraceSnapshot fresh;
    fresh.sourceRevision       = 5;
    fresh.displayShapeRevision = 2;
    assert(vm.accept(fresh));

    SpectralTraceSnapshot staleSource;
    staleSource.sourceRevision       = 4;
    staleSource.displayShapeRevision = 99;
    assert(!vm.accept(staleSource));
    assert(vm.snapshot().sourceRevision == 5);

    SpectralTraceSnapshot staleShape;
    staleShape.sourceRevision       = 5;
    staleShape.displayShapeRevision = 1;
    assert(!vm.accept(staleShape));
    assert(vm.snapshot().displayShapeRevision == 2);

    SpectralTraceSnapshot equalShape;
    equalShape.sourceRevision       = 5;
    equalShape.displayShapeRevision = 2;
    assert(vm.accept(equalShape));
}

// SP-MENU-01: menu() produces exactly kModeCount items with correct labels
static void test_menu_items()
{
    SpectrumInteractionModel model;
    const auto items = model.menu(true, true);
    assert(items.size() == SpectrumInteractionModel::kModeCount);
    assert(items[0].label == "Trace + Residual");
    assert(items[1].label == "Partials");
    assert(items[2].label == "Residual");
    assert(items[3].label == "Iso 3D");
    assert(items[4].label == "Iso 3D Residual");
    assert(items[5].label == "Iso 3D Envelope");
    assert(items[6].label == "Iso 3D Env (RDC)");
}

// SP-MENU-02: menu() checked item tracks current mode; residual disabled when !hasResidual
static void test_menu_checked_and_enabled()
{
    SpectrumInteractionModel model;
    model.setMode(SpectralTraceMode::partials);
    const auto items = model.menu(true, false);
    assert(!items[0].checked);
    assert(items[1].checked);
    assert(!items[2].enabled);  // residual disabled when no residual
    assert(items[3].enabled);

    const auto noSource = model.menu(false, false);
    for (const auto& item : noSource)
        assert(!item.enabled);
}

// SP-MENU-03: selectMenuItem() dispatches SetSpectralViewAction with correct source+mode
static void test_menu_dispatch()
{
    SpectrumInteractionModel model;
    model.setSource(SpectralTraceSource::sourceA);

    // select partials (id=1)
    const auto action = model.selectMenuItem(1, true, false);
    assert(action.source == SpectrumInteractionModel::sourceIndex(SpectralTraceSource::sourceA));
    assert(action.mode == static_cast<int>(SpectralTraceMode::partials));

    // disabled item returns mode=-1
    const auto disabled = model.selectMenuItem(2, true, false); // residual, no residual
    assert(disabled.mode == -1);

    // out-of-range id
    const auto oob = model.selectMenuItem(99, true, false);
    assert(oob.mode == -1);

    // sourceB
    model.setSource(SpectralTraceSource::sourceB);
    const auto fromB = model.selectMenuItem(0, true, true);
    assert(fromB.source == 2);
    assert(fromB.mode == static_cast<int>(SpectralTraceMode::combined));
}

// SP-NAV-01: zoom and pan update navigation state with clamping
static void test_zoom_pan()
{
    SpectrumInteractionModel model;
    assert(model.zoom(2.0f, 0.5f, 0.5f));
    assert(model.navigation().timeEnd - model.navigation().timeStart < 1.0f);
    assert(model.navigation().frequencyEnd - model.navigation().frequencyStart < 1.0f);

    // pan to edge and confirm clamping
    assert(model.pan(10.0f, -10.0f));
    assert(model.navigation().timeEnd <= 1.0f);
    assert(model.navigation().frequencyStart >= 0.0f);

    // invalid inputs rejected
    assert(!model.zoom(0.0f, 0.5f, 0.5f));
    assert(!model.zoom(-1.0f, 0.5f, 0.5f));
    assert(!model.pan(std::numeric_limits<float>::infinity(), 0.0f));
}

// SP-NAV-02: fit() resets navigation to full view
static void test_fit()
{
    SpectrumInteractionModel model;
    model.zoom(4.0f, 0.5f, 0.5f);
    model.fit();
    const auto& nav = model.navigation();
    assert(nav.timeStart == 0.0f && nav.timeEnd == 1.0f);
    assert(nav.frequencyStart == 0.0f && nav.frequencyEnd == 1.0f);
}

// SP-PART-01: selectPartial() updates selectedPartial; rejects < -1
static void test_select_partial()
{
    SpectrumInteractionModel model;
    assert(model.selectPartial(0));
    assert(model.navigation().selectedPartial == 0);
    assert(model.selectPartial(42));
    assert(model.navigation().selectedPartial == 42);
    assert(model.selectPartial(-1));
    assert(model.navigation().selectedPartial == -1);
    assert(!model.selectPartial(-2));
}

// SP-PART-02: applyPartialEdit() returns correct PartialEditAction
static void test_partial_edit()
{
    SpectrumInteractionModel model;
    const auto action = model.applyPartialEdit(5, 10, true);
    assert(action.partial == 5 && action.frame == 10 && action.apply);
    const auto remove = model.applyPartialEdit(3, 7, false);
    assert(!remove.apply);
}

// SP-RES-01: selectResidualRange() validates frame order and updates navigation
static void test_residual_range()
{
    SpectrumInteractionModel model;
    assert(model.selectResidualRange({0, 1, 2}, 10, 20));
    assert(model.navigation().residualFirstFrame == 10);
    assert(model.navigation().residualLastFrame  == 20);
    assert(model.navigation().selectedBands.size() == 3);

    // lastFrame < firstFrame rejected
    assert(!model.selectResidualRange({0}, 20, 10));

    // firstFrame < 0 rejected
    assert(!model.selectResidualRange({0}, -1, 5));

    // equal first/last is valid (single frame)
    assert(model.selectResidualRange({0}, 5, 5));
}

// SP-RES-02: applyResidualEdit() returns correct ResidualEditAction
static void test_residual_edit()
{
    SpectrumInteractionModel model;
    const auto action = model.applyResidualEdit(3, 8, -6.5f);
    assert(action.band == 3 && action.frame == 8);
    assert(std::abs(action.value - (-6.5f)) < 1e-6f);
}

// SP-RES-03: selectBand() updates selectedBand; rejects < -1
static void test_select_band()
{
    SpectrumInteractionModel model;
    assert(model.selectBand(7));
    assert(model.navigation().selectedBand == 7);
    assert(model.selectBand(-1));
    assert(!model.selectBand(-2));
}

// SP-SMEAR-01: setOnsetOverlay() wraps onsets into EditSmearnovOnsetsAction
static void test_onset_overlay()
{
    SpectrumInteractionModel model;
    const auto action = model.setOnsetOverlay({0.1f, 0.5f, 0.9f});
    assert(action.normalizedOnsets.size() == 3);
    assert(std::abs(action.normalizedOnsets[0] - 0.1f) < 1e-6f);
}

// SP-SMEAR-02: toggleOnset() adds absent onset (sorted) and removes present onset
static void test_toggle_onset()
{
    SpectrumInteractionModel model;
    std::vector<float> onsets = {0.2f, 0.6f};

    // add at 0.4 (not within snapRadius of existing)
    auto result = model.toggleOnset(onsets, 0.4f);
    assert(result.normalizedOnsets.size() == 3);
    // should be sorted
    assert(result.normalizedOnsets[0] <= result.normalizedOnsets[1]);
    assert(result.normalizedOnsets[1] <= result.normalizedOnsets[2]);

    // remove at 0.2 (within default snapRadius 0.02)
    auto result2 = model.toggleOnset(onsets, 0.21f);
    assert(result2.normalizedOnsets.size() == 1);
    assert(std::abs(result2.normalizedOnsets[0] - 0.6f) < 1e-5f);

    // empty list: add only
    auto result3 = model.toggleOnset({}, 0.5f);
    assert(result3.normalizedOnsets.size() == 1);
    assert(std::abs(result3.normalizedOnsets[0] - 0.5f) < 1e-6f);
}

// SP-ENV-01: applyEnvelopeGain() returns ResidualEditAction with gainLinear as value
static void test_envelope_gain()
{
    SpectrumInteractionModel model;
    const auto action = model.applyEnvelopeGain(2, 15, 1.5f);
    assert(action.band == 2 && action.frame == 15);
    assert(std::abs(action.value - 1.5f) < 1e-6f);
    // unity gain
    const auto unity = model.applyEnvelopeGain(0, 0, 1.0f);
    assert(std::abs(unity.value - 1.0f) < 1e-6f);
}

// SP-KEY-01: handleKey() dispatches correct actions per key code
static void test_handle_key()
{
    SpectrumInteractionModel model;

    // fit key ('0') resets zoom
    model.zoom(4.0f, 0.5f, 0.5f);
    auto fitResult = model.handleKey(SpectrumInteractionModel::kKeyFit, false, false);
    assert(!fitResult.has_value());
    assert(model.navigation().timeStart == 0.0f && model.navigation().timeEnd == 1.0f);

    // home key resets zoom too
    model.zoom(4.0f, 0.5f, 0.5f);
    auto homeResult = model.handleKey(SpectrumInteractionModel::kKeyHome, false, false);
    assert(!homeResult.has_value());
    assert(model.navigation().timeStart == 0.0f);

    // escape clears selection
    model.selectPartial(5);
    model.selectBand(3);
    model.selectResidualRange({0, 1}, 2, 8);
    auto escResult = model.handleKey(SpectrumInteractionModel::kKeyEscape, false, false);
    assert(!escResult.has_value());
    assert(model.navigation().selectedPartial == -1);
    assert(model.navigation().selectedBand    == -1);
    assert(model.navigation().selectedBands.empty());

    // delete with partial selected dispatches PartialEditAction
    model.selectPartial(7);
    auto delPartial = model.handleKey(SpectrumInteractionModel::kKeyDelete, false, false);
    assert(delPartial.has_value());
    assert(std::holds_alternative<PartialEditAction>(*delPartial));
    const auto& pe = std::get<PartialEditAction>(*delPartial);
    assert(pe.partial == 7 && !pe.apply);

    // delete with no partial but band selected dispatches ResidualEditAction
    model.selectPartial(-1);
    model.selectBand(4);
    auto delBand = model.handleKey(SpectrumInteractionModel::kKeyDelete, false, false);
    assert(delBand.has_value());
    assert(std::holds_alternative<ResidualEditAction>(*delBand));
    const auto& re = std::get<ResidualEditAction>(*delBand);
    assert(re.band == 4 && re.value == 0.0f);

    // delete with nothing selected returns nullopt
    model.selectPartial(-1);
    model.selectBand(-1);
    auto delNone = model.handleKey(SpectrumInteractionModel::kKeyDelete, false, false);
    assert(!delNone.has_value());

    // backspace behaves same as delete for partial
    model.selectPartial(3);
    auto backspace = model.handleKey(SpectrumInteractionModel::kKeyBackspace, false, false);
    assert(backspace.has_value());
    assert(std::holds_alternative<PartialEditAction>(*backspace));

    // unknown key returns nullopt
    auto unknown = model.handleKey(65 /* 'A' */, false, false);
    assert(!unknown.has_value());
}

// SP-DRAW-02: cacheRevision is 0 before any snapshot, non-zero after
static void test_cache_revision_zero()
{
    SpectrumViewModel vm;
    assert(vm.cacheRevision() == 0);
    assert(!vm.hasSnapshot());
    vm.clear();
    assert(vm.cacheRevision() == 0);
}

static void test_extended_keyboard_dispatch()
{
    SpectrumInteractionModel model;
    model.selectBand(3);
    assert(model.selectResidualRange({ 3, 4 }, 10, 20));
    const auto copy = model.handleKey('c', false, true);
    assert(copy.has_value() && std::holds_alternative<SpectrumEditAction>(*copy));
    assert(std::get<SpectrumEditAction>(*copy).command == SpectrumEditCommand::copyResidual);
    const auto paste = model.handleKey('v', false, true);
    assert(paste.has_value() && std::get<SpectrumEditAction>(*paste).command == SpectrumEditCommand::pasteResidual);
    const auto swap = model.handleKey('x', false, true);
    assert(swap.has_value() && std::get<SpectrumEditAction>(*swap).command == SpectrumEditCommand::swapResidual);

    model.setHoveredPartial(8);
    model.setAvailablePartials({ 8, 20, 21 });
    model.setAvailableBands({ 3, 4, 5 });
    assert(!model.handleKey('a', false, true).has_value());
    assert(model.navigation().selectedPartials.size() == 3);
    assert(!model.handleKey('h', false, false).has_value());
    assert(model.navigation().selectedPartials.size() == 2);
    model.setEnvelopeRange(0.8f, 0.2f, 1.2f, -0.2f);
    assert(model.navigation().envelopeEditing);
    assert(model.navigation().envelopeTimeStart == 0.2f);
    model.clearEnvelope();
    assert(!model.navigation().envelopeEditing);
    const auto clean = model.setOnsetOverlay({ 2.0f, -1.0f, 0.5f, NAN });
    assert(clean.normalizedOnsets.size() == 3);
    assert(clean.normalizedOnsets.front() == 0.0f && clean.normalizedOnsets.back() == 1.0f);
}

int main()
{
    test_cache_revision();
    test_cache_revision_zero();
    test_stale_rejection();
    test_menu_items();
    test_menu_checked_and_enabled();
    test_menu_dispatch();
    test_zoom_pan();
    test_fit();
    test_select_partial();
    test_partial_edit();
    test_residual_range();
    test_residual_edit();
    test_select_band();
    test_onset_overlay();
    test_toggle_onset();
    test_envelope_gain();
    test_handle_key();
    test_extended_keyboard_dispatch();
    return 0;
}
