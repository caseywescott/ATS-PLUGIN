#include "../src/ui/SpectrumInteractionModel.h"

#include <cassert>
#include <cmath>

int main()
{
    ats::ui::SpectrumInteractionModel model;
    model.setSource(ats::ui::SpectralTraceSource::sourceB);
    const auto menu = model.menu(true, false);
    assert(menu.size() == 7);
    assert(menu[0].checked && menu[0].enabled);
    assert(!menu[2].enabled);
    assert(menu[0].label == "Trace + Residual");
    assert(menu[3].label == "Iso 3D");
    assert(menu[4].label == "Iso 3D Residual");
    assert(menu[5].label == "Iso 3D Envelope");
    assert(menu[6].label == "Iso 3D Env (RDC)");
    assert(model.selectMenuItem(2, true, false).mode == -1);
    const ats::ui::SpectrumCapabilities capabilities {
        false, true, true, false, "Current", "crotales A", "Source B" };
    assert(model.menu(capabilities)[2].enabled);
    const auto sourceLabels = model.sourceMenuLabels(capabilities);
    assert(sourceLabels[1] == "crotales A" && sourceLabels[2].empty());
    const auto select = model.selectMenuItem(4, true, true);
    assert(select.source == 2 && select.mode == static_cast<int>(ats::ui::SpectralTraceMode::isometricResidual));

    assert(model.zoom(2.0f, 0.5f, 0.5f));
    assert(model.navigation().timeEnd - model.navigation().timeStart < 1.0f);
    assert(model.pan(10.0f, -10.0f));
    assert(model.navigation().timeEnd <= 1.0f);
    assert(model.navigation().frequencyStart >= 0.0f);
    assert(!model.zoom(0.0f, 0.5f, 0.5f));
    model.fit();
    assert(model.navigation().timeStart == 0.0f && model.navigation().timeEnd == 1.0f);

    model.setPointerCaptured(true);
    assert(model.navigation().pointerCaptured);
    assert(model.selectPartial(12));
    assert(model.selectBand(3));
    assert(model.applyPartialEdit(12, 8, true).apply);
    assert(model.applyResidualEdit(3, 8, -4.0f).value == -4.0f);
    assert(model.setOnsetOverlay({ 0.1f, 0.8f }).normalizedOnsets.size() == 2);
    model.setEnvelopeRange(0.2f, 0.8f, 0.1f, 0.6f);
    assert(model.navigation().envelopeEditing);
    assert(model.navigation().envelopeTimeStart == 0.2f);
    assert(model.navigation().envelopeFrequencyEnd == 0.6f);

    ats::ui::SpectralTraceSnapshot snapshot;
    snapshot.mode = ats::ui::SpectralTraceMode::partials;
    snapshot.frameCount = 2;
    snapshot.minFrequencyHz = 100.0;
    snapshot.maxFrequencyHz = 1000.0;
    snapshot.partials.push_back({ 4, 0.8f, 440.0, 440.0, 0,
                                  { { 0, 0.25f, 0.37f, 0.55f, 0.8f, 440.0, 440.0, true },
                                    { 1, 0.75f, 0.72f, 0.60f, 0.8f, 440.0, 440.0, true } } });
    snapshot.onsets.push_back({ 0.5f, 0.4f, 0.6f, 1.0f });
    model.setMode(ats::ui::SpectralTraceMode::partials);
    model.setSnapshot(&snapshot, { 0.0f, 0.0f, 800.0f, 500.0f });
    model.fit();
    const auto plot = model.interactionPlotBounds();
    const auto pinchPoint = ats::ui::UiPoint {
        plot.x + plot.width * 0.25f, plot.y + plot.height * 0.25f };
    assert(model.zoomAtScreenPoint(pinchPoint, 2.0f, 2.0f));
    const auto pinchViewport = model.viewport();
    assert(std::abs(pinchViewport.timeCenter - 0.375f) < 1.0e-5f);
    assert(std::abs(pinchViewport.frequencyCenter - 0.625f) < 1.0e-5f);
    assert(std::abs(pinchViewport.timeZoom - 2.0f) < 1.0e-5f);
    assert(std::abs(pinchViewport.frequencyZoom - 2.0f) < 1.0e-5f);
    assert(!model.zoomAtScreenPoint({ plot.x - 1.0f, pinchPoint.y }, 2.0f, 2.0f));

    model.fit();
    assert(model.zoomAtScreenPoint({ plot.x + plot.width * 0.5f,
                                     plot.y + plot.height * 0.5f }, 128.0f, 128.0f));
    const auto maxViewport = model.viewport();
    assert(std::abs(maxViewport.timeZoom - ats::ui::SpectrumGeometry::kMaxZoom) < 1.0e-5f);
    assert(std::abs(maxViewport.frequencyZoom - ats::ui::SpectrumGeometry::kMaxZoom) < 1.0e-5f);

    // Magnify keeps planar pointer anchoring even in an isometric display.
    model.setMode(ats::ui::SpectralTraceMode::isometric);
    model.fit();
    const auto isoPlot = model.interactionPlotBounds();
    const auto isoPoint = ats::ui::UiPoint {
        isoPlot.x + isoPlot.width * 0.25f, isoPlot.y + isoPlot.height * 0.25f };
    assert(model.zoomAtScreenPoint(isoPoint, 2.0f, 2.0f));
    const auto isoViewport = model.viewport();
    assert(std::abs(isoViewport.timeCenter - 0.375f) < 1.0e-5f);
    assert(std::abs(isoViewport.frequencyCenter - 0.625f) < 1.0e-5f);
    model.setMode(ats::ui::SpectralTraceMode::partials);
    model.fit();

    assert(model.findNearestPartial({ 400.0f, 250.0f }, 12.0f) == 4);
    assert(model.findNearestPartial({ 10.0f, 10.0f }, 4.0f) == -1);
    model.setHoveredPartial(4);
    model.setHoveredBand(3);
    model.setSnapshot(&snapshot, { 0.0f, 0.0f, 800.0f, 500.0f });
    // A paint-time snapshot refresh with the same revisions is not a rebuild;
    // transient hover state survives it.
    assert(model.navigation().hoveredPartial == 4
           && model.navigation().hoveredBand == 3
           && model.navigation().hoveredResidualFrame == -1);
    assert(model.togglePartialSelection(4));
    assert(model.navigation().selectedPartials.size() == 1);
    assert(model.selectPartialsInLasso({ 200.0f, 100.0f, 500.0f, 300.0f }) == 1);

    const auto deleteAction = model.handleKey(ats::ui::SpectrumInteractionModel::kKeyDelete, false, false);
    assert(deleteAction.has_value() && std::holds_alternative<ats::ui::SpectrumEditAction>(*deleteAction));
    assert(std::get<ats::ui::SpectrumEditAction>(*deleteAction).command == ats::ui::SpectrumEditCommand::deletePartials);
    assert(std::get<ats::ui::SpectrumEditAction>(*deleteAction).firstFrame == 0
           && std::get<ats::ui::SpectrumEditAction>(*deleteAction).lastFrame == 1);

    model.setMode(ats::ui::SpectralTraceMode::residual);
    snapshot.mode = ats::ui::SpectralTraceMode::residual;
    model.setSnapshot(&snapshot, { 0.0f, 0.0f, 800.0f, 500.0f });
    model.selectBand(3);
    const auto muteAction = model.handleKey(ats::ui::SpectrumInteractionModel::kKeyDelete, false, false);
    assert(muteAction.has_value() && std::holds_alternative<ats::ui::SpectrumEditAction>(*muteAction));
    assert(std::get<ats::ui::SpectrumEditAction>(*muteAction).command == ats::ui::SpectrumEditCommand::muteResidual);

    const auto residualPlot = ats::ui::SpectrumGeometry::getPlotBounds(
        { 0.0f, 0.0f, 800.0f, 500.0f }, snapshot.mode, 0, snapshot.frameCount);
    const auto handle = model.onsetHandleAtPoint({ residualPlot.x + residualPlot.width * 0.4f,
                                                   residualPlot.y + residualPlot.height * 0.5f }, 6.0f);
    assert(handle.kind == ats::ui::OnsetHandleKind::leftEdge && handle.index == 0);

    // A lasso in a planar view also records its time span for Time... edits;
    // the isometric views do not (their time axis is diagonal).
    {
        ats::ui::SpectralTraceSnapshot timed;
        timed.mode = ats::ui::SpectralTraceMode::partials;
        timed.frameCount = 101;
        timed.durationSeconds = 1.0;
        ats::ui::SpectrumInteractionModel picker;
        picker.setMode(ats::ui::SpectralTraceMode::partials);
        const ats::ui::UiRect well { 0.0f, 0.0f, 900.0f, 400.0f };
        picker.setSnapshot(&timed, well);
        assert(!picker.hasTimeSelection());
        const auto plot = picker.interactionPlotBounds();
        const auto y = plot.y + plot.height * 0.5f;
        picker.beginLasso({ plot.x + plot.width * 0.25f, y - 20.0f });
        picker.updateLasso({ plot.x + plot.width * 0.75f, y + 20.0f });
        picker.endLasso();
        assert(picker.hasTimeSelection());
        assert(std::abs(picker.navigation().timeSelectionFirstFrame - 25) <= 1);
        assert(std::abs(picker.navigation().timeSelectionLastFrame - 75) <= 1);
        picker.clearTimeSelection();
        assert(!picker.hasTimeSelection());

        picker.setMode(ats::ui::SpectralTraceMode::isometric);
        timed.mode = ats::ui::SpectralTraceMode::isometric;
        picker.setSnapshot(&timed, well);
        picker.beginLasso({ plot.x + plot.width * 0.25f, y - 20.0f });
        picker.updateLasso({ plot.x + plot.width * 0.75f, y + 20.0f });
        picker.endLasso();
        assert(!picker.hasTimeSelection());
    }
    return 0;
}
