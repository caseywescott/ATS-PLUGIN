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
    assert(layout.viewMenuButton.width == 182.0f);
    assert(layout.deleteButton.width == 72.0f);
    assert(layout.viewMenuButton.x < layout.deleteButton.x);
    const auto residualScreen = ats::ui::SpectrumGeometry::getScreenBounds(
        { 0.0f, 0.0f, 1200.0f, 800.0f }, ats::ui::SpectralTraceMode::residual);
    assert(layout.header.y + layout.header.height <= residualScreen.y);
    const auto combinedLayout = ats::ui::SpectrumChromeLayout::layout(
        { 0.0f, 0.0f, 1200.0f, 800.0f }, ats::ui::SpectralTraceMode::combined);
    const auto combinedScreen = ats::ui::SpectrumGeometry::getScreenBounds(
        { 0.0f, 0.0f, 1200.0f, 800.0f }, ats::ui::SpectralTraceMode::combined);
    assert(combinedLayout.header.y + combinedLayout.header.height > combinedScreen.y);
    assert(combinedLayout.header.y + combinedLayout.header.height
           < ats::ui::SpectrumGeometry::getPlotBounds(
               { 0.0f, 0.0f, 1200.0f, 800.0f }, ats::ui::SpectralTraceMode::combined,
               0, 2).y);
    const auto residualTools = ats::ui::SpectrumChromeLayout::toolbarItems(
        { 0.0f, 0.0f, 1200.0f, 800.0f }, ats::ui::SpectralTraceMode::residual, true);
    assert(residualTools.size() == 7 && residualTools.front().enabled);
    assert(residualTools[0].bounds.width == 52.0f);
    assert(residualTools[1].bounds.width == 62.0f);
    assert(residualTools[2].bounds.width == 52.0f);
    assert(residualTools[3].bounds.width == 56.0f);
    assert(residualTools[4].bounds.width == 56.0f);
    assert(residualTools[5].bounds.width == 56.0f);
    assert(residualTools[6].bounds.width == 44.0f);
    const auto partialTools = ats::ui::SpectrumChromeLayout::toolbarItems(
        { 0.0f, 0.0f, 1200.0f, 800.0f }, ats::ui::SpectralTraceMode::partials, false);
    assert(partialTools.size() == 5 && !partialTools.front().enabled);
    assert(partialTools[3].command == ats::ui::SpectrumEditCommand::prunePartials);
    assert(partialTools[0].bounds.width == 52.0f);
    assert(partialTools[1].bounds.width == 48.0f);
    assert(partialTools[2].bounds.width == 56.0f);
    assert(partialTools[3].bounds.width == 52.0f);
    assert(partialTools[4].bounds.width == 44.0f);
    const auto residualControls = ats::ui::SpectrumChromeLayout::toolbarControls(
        { 0.0f, 0.0f, 1200.0f, 800.0f }, ats::ui::SpectralTraceMode::residual);
    assert(residualControls.size() == 3);
    assert(residualControls[0].kind == ats::ui::SpectrumChromeLayout::ToolbarControlKind::slider);
    assert(residualControls[1].kind == ats::ui::SpectrumChromeLayout::ToolbarControlKind::label);
    assert(residualControls[1].bounds.width == 44.0f);
    assert(residualControls[2].kind == ats::ui::SpectrumChromeLayout::ToolbarControlKind::editor);
    const auto envelope = ats::ui::SpectrumChromeLayout::layout(
        { 0.0f, 0.0f, 1200.0f, 800.0f }, ats::ui::SpectralTraceMode::isometricEnvelope,
        true, true);
    assert(envelope.onsetEditButton.width == 90.0f);
    assert(envelope.envSourceAButton.width == 76.0f);
    assert(envelope.envSourceBButton.width == 76.0f);
    assert(envelope.selectionLabel.width > 0.0f);
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
