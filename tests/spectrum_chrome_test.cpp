#include "../src/ui/SpectrumChrome.h"

#include <cassert>
#include <cmath>

int main()
{
    const auto layout = ats::ui::SpectrumChromeLayout::layout(
        { 0.0f, 0.0f, 1200.0f, 800.0f }, ats::ui::SpectralTraceMode::residual, true, true);
    assert(layout.zoomBar.width == ats::ui::SpectrumGeometry::kZoomBarWidth);
    assert(layout.timeScrollbar.height == ats::ui::SpectrumGeometry::kTraceScrollBarThickness);
    assert(layout.frequencyScrollbar.width == ats::ui::SpectrumGeometry::kTraceScrollBarThickness);
    assert(layout.header.height == 50.0f);
    assert(layout.toolbar.height == 24.0f);
    assert(layout.header.y == 5.0f);
    assert(layout.header.y + layout.header.height <= layout.toolbar.y + layout.toolbar.height);
    assert(layout.zoomBar.x + layout.zoomBar.width + 6.0f == 1195.0f);
    // A wide well shows every view as a button, after the source chip and
    // before DELETE, with no menu chip.
    assert(layout.modeStrip);
    assert(layout.viewMenuButton.width == 0.0f);
    assert(layout.sourceMenuButton.width == ats::ui::SpectrumChromeLayout::kSourceMenuWidth);
    assert(layout.deleteButton.width == 72.0f);
    for (std::size_t i = 0; i < layout.modeButtons.size(); ++i)
    {
        const auto& button = layout.modeButtons[i];
        assert(button.width > 0.0f && button.height == 22.0f);
        assert(button.x >= layout.sourceMenuButton.x + layout.sourceMenuButton.width);
        if (i > 0)
            assert(button.x >= layout.modeButtons[i - 1].x + layout.modeButtons[i - 1].width);
    }
    assert(layout.modeButtons.back().x + layout.modeButtons.back().width < layout.deleteButton.x);
    assert(layout.onsetEditButton.x + layout.onsetEditButton.width <= layout.selectionLabel.x);
    assert(layout.selectionLabel.x + layout.selectionLabel.width <= layout.zoomBar.x);
    assert(layout.selectionLabel.width
           >= ats::ui::SpectrumChromeLayout::kMinSelectionLabelWidth - 0.01f);
    // A narrow well keeps the single view chip (it opens one flat menu).
    const auto narrow = ats::ui::SpectrumChromeLayout::layout(
        { 0.0f, 0.0f, 557.0f, 470.0f }, ats::ui::SpectralTraceMode::residual, true, true);
    assert(!narrow.modeStrip);
    assert(narrow.viewMenuButton.width == 182.0f);
    assert(narrow.viewMenuButton.x < narrow.deleteButton.x);
    assert(narrow.modeButtons.front().width == 0.0f && narrow.sourceMenuButton.width == 0.0f);
    // The strip appears exactly at its minimum width, and every part clears
    // the zoom bar there.
    const auto minWidth = ats::ui::SpectrumChromeLayout::modeStripMinWidth() + 10.0f;
    const auto atMin = ats::ui::SpectrumChromeLayout::layout(
        { 0.0f, 0.0f, minWidth, 470.0f }, ats::ui::SpectralTraceMode::isometricEnvelope, true, true);
    assert(atMin.modeStrip);
    assert(atMin.zoomBar.width >= ats::ui::SpectrumChromeLayout::kMinZoomBarWidth - 0.01f);
    assert(atMin.selectionLabel.x + atMin.selectionLabel.width <= atMin.zoomBar.x + 0.01f);
    assert(atMin.envSourceAButton.width == 0.0f);
    assert(!ats::ui::SpectrumChromeLayout::layout(
        { 0.0f, 0.0f, minWidth - 1.0f, 470.0f }, ats::ui::SpectralTraceMode::combined).modeStrip);
    const auto residualScreen = ats::ui::SpectrumGeometry::getScreenBounds(
        { 0.0f, 0.0f, 1200.0f, 800.0f }, ats::ui::SpectralTraceMode::residual);
    assert(layout.header.y + layout.header.height <= residualScreen.y);
    const auto combinedLayout = ats::ui::SpectrumChromeLayout::layout(
        { 0.0f, 0.0f, 1200.0f, 800.0f }, ats::ui::SpectralTraceMode::combined);
    const auto combinedScreen = ats::ui::SpectrumGeometry::getScreenBounds(
        { 0.0f, 0.0f, 1200.0f, 800.0f }, ats::ui::SpectralTraceMode::combined);
    // Every mode with a tool row reserves it: the row no longer covers the
    // top of the screen (JUCE reserved 30 px and let it overlap the title).
    assert(combinedLayout.header.y + combinedLayout.header.height <= combinedScreen.y);
    for (const auto mode : { ats::ui::SpectralTraceMode::combined, ats::ui::SpectralTraceMode::partials,
                             ats::ui::SpectralTraceMode::residual,
                             ats::ui::SpectralTraceMode::isometricResidual,
                             ats::ui::SpectralTraceMode::isometricEnvelope,
                             ats::ui::SpectralTraceMode::isometricEnvelopeRdc })
    {
        const auto modeLayout = ats::ui::SpectrumChromeLayout::layout(
            { 0.0f, 0.0f, 557.0f, 470.0f }, mode, true, true);
        const auto modeScreen = ats::ui::SpectrumGeometry::getScreenBounds(
            { 0.0f, 0.0f, 557.0f, 470.0f }, mode);
        assert(modeLayout.toolbar.y + modeLayout.toolbar.height <= modeScreen.y);
        // At the design-size well, every tool and control ends inside the panel
        // and the zoom bar clears the left-hand buttons.
        const auto right = modeLayout.toolbar.x + modeLayout.toolbar.width;
        for (const auto& item : ats::ui::SpectrumChromeLayout::toolbarItems(
                 { 0.0f, 0.0f, 557.0f, 470.0f }, mode, true))
            assert(item.bounds.x + item.bounds.width <= right + 0.01f);
        for (const auto& control : ats::ui::SpectrumChromeLayout::toolbarControls(
                 { 0.0f, 0.0f, 557.0f, 470.0f }, mode))
            assert(control.bounds.x + control.bounds.width <= right + 0.01f);
        for (const auto& button : { modeLayout.onsetEditButton, modeLayout.envSourceAButton,
                                    modeLayout.envSourceBButton })
            assert(button.width == 0.0f || button.x + button.width <= modeLayout.zoomBar.x);
    }
    const auto residualTools = ats::ui::SpectrumChromeLayout::toolbarItems(
        { 0.0f, 0.0f, 1200.0f, 800.0f }, ats::ui::SpectralTraceMode::residual, true);
    assert(residualTools.size() == 7 && residualTools.front().enabled);
    assert(residualTools[0].bounds.width == 44.0f);
    assert(residualTools[1].bounds.width == 54.0f);
    assert(residualTools[6].bounds.width == 44.0f);
    const auto partialTools = ats::ui::SpectrumChromeLayout::toolbarItems(
        { 0.0f, 0.0f, 1200.0f, 800.0f }, ats::ui::SpectralTraceMode::partials, false);
    assert(partialTools.size() == 9 && !partialTools.front().enabled);
    assert(partialTools[3].command == ats::ui::SpectrumEditCommand::prunePartials);
    // Selection tools stay enabled with nothing selected.
    assert(partialTools[4].command == ats::ui::SpectrumEditCommand::selectPartialsByCriteria
           && partialTools[4].enabled);
    assert(partialTools[5].command == ats::ui::SpectrumEditCommand::invertPartialSelection
           && partialTools[5].enabled);
    assert(partialTools[6].command == ats::ui::SpectrumEditCommand::timeEditMenu
           && partialTools[6].enabled);
    assert(partialTools[7].command == ats::ui::SpectrumEditCommand::toolsMenu
           && partialTools[7].enabled && partialTools[7].bounds.width == 56.0f);
    assert(partialTools[8].command == ats::ui::SpectrumEditCommand::tiltPartialEnvelope
           && partialTools[8].bounds.width == 44.0f);
    const auto residualControls = ats::ui::SpectrumChromeLayout::toolbarControls(
        { 0.0f, 0.0f, 1200.0f, 800.0f }, ats::ui::SpectralTraceMode::residual);
    assert(residualControls.size() == 2);
    assert(residualControls[0].kind == ats::ui::SpectrumChromeLayout::ToolbarControlKind::slider);
    assert(residualControls[1].kind == ats::ui::SpectrumChromeLayout::ToolbarControlKind::editor);
    // The A/B shortcuts belong to the narrow header; the wide one has the
    // source chip instead.
    const auto envelope = ats::ui::SpectrumChromeLayout::layout(
        { 0.0f, 0.0f, 800.0f, 800.0f }, ats::ui::SpectralTraceMode::isometricEnvelope,
        true, true);
    assert(!envelope.modeStrip);
    assert(envelope.onsetEditButton.width == 72.0f);
    assert(envelope.envSourceAButton.width == 36.0f);
    assert(envelope.envSourceBButton.width == 36.0f);
    assert(envelope.selectionLabel.width > 0.0f);
    const auto wideEnvelope = ats::ui::SpectrumChromeLayout::layout(
        { 0.0f, 0.0f, 1200.0f, 800.0f }, ats::ui::SpectralTraceMode::isometricEnvelope,
        true, true);
    assert(wideEnvelope.modeStrip && wideEnvelope.envSourceAButton.width == 0.0f);
    const auto envelopeTools = ats::ui::SpectrumChromeLayout::toolbarItems(
        { 0.0f, 0.0f, 1200.0f, 800.0f }, ats::ui::SpectralTraceMode::isometricEnvelope, true);
    assert(envelopeTools.size() == 1 && envelopeTools.front().bounds.width == 88.0f);
    const auto iso = ats::ui::SpectrumChromeLayout::layout(
        { 0.0f, 0.0f, 1200.0f, 800.0f }, ats::ui::SpectralTraceMode::isometric);
    assert(iso.header.height == 26.0f && iso.toolbar.height == 0.0f);
    assert(ats::ui::SpectrumChromeLayout::toolbarItems(
        { 0.0f, 0.0f, 1200.0f, 800.0f }, ats::ui::SpectralTraceMode::isometric, true).empty());
    return 0;
}
