#pragma once

#include "EditorHost.h"
#include "SpectrumGeometry.h"

#include <algorithm>
#include <cstdint>
#include <string_view>
#include <vector>

namespace ats::ui
{

struct SpectrumChromeLayout
{
    UiRect header{};
    UiRect toolbar{};
    UiRect viewMenuButton{};
    UiRect deleteButton{};
    UiRect onsetEditButton{};
    UiRect envSourceAButton{};
    UiRect envSourceBButton{};
    UiRect selectionLabel{};
    UiRect zoomBar{};
    UiRect timeScrollbar{};
    UiRect frequencyScrollbar{};

    [[nodiscard]] static SpectrumChromeLayout layout(UiRect localBounds,
                                                      SpectralTraceMode mode,
                                                      bool hasOnsets = false,
                                                      bool hasSelection = false) noexcept
    {
        // Chrome is anchored to the panel's own five-pixel inset.  The screen
        // rectangle has already had its legacy 30/50-pixel reservation applied;
        // anchoring chrome to that rectangle makes the JUCE header overlap the
        // screen in the four intentionally preserved 30-pixel modes.
        auto base = localBounds;
        base.x += 5.0f;
        base.y += 5.0f;
        base.width = std::max(0.0f, base.width - 10.0f);
        base.height = std::max(0.0f, base.height - 10.0f);
        const auto screen = SpectrumGeometry::getScreenBounds(localBounds, mode);
        const auto content = SpectrumGeometry::getTraceContentBounds(screen);
        const auto headerHeight = mode == SpectralTraceMode::isometric ? 26.0f : 50.0f;
        const auto toolbarHeight = headerHeight > 30.0f ? 24.0f : 0.0f;
        const auto rowY = base.y + 2.0f;
        const auto rowHeight = 22.0f;

        SpectrumChromeLayout result;
        result.header = { base.x, base.y, base.width, headerHeight };
        result.toolbar = { base.x, base.y + 26.0f, base.width, toolbarHeight };
        result.zoomBar = { base.x + base.width - 6.0f - SpectrumGeometry::kZoomBarWidth,
                           base.y, SpectrumGeometry::kZoomBarWidth, 26.0f };

        auto x = base.x;
        result.viewMenuButton = { x, rowY, 182.0f, rowHeight };
        x += 182.0f + 6.0f;
        (void) hasSelection; // visibility is a paint/interaction concern; geometry is stable.
        result.deleteButton = { x, rowY, 72.0f, rowHeight };
        x += 72.0f + 8.0f;
        if (hasOnsets)
        {
            result.onsetEditButton = { x, rowY, 90.0f, rowHeight };
            x += 90.0f + 8.0f;
        }
        const auto envelope = mode == SpectralTraceMode::isometricEnvelope
                           || mode == SpectralTraceMode::isometricEnvelopeRdc;
        if (envelope)
        {
            result.envSourceAButton = { x, rowY, 76.0f, rowHeight };
            x += 76.0f + 4.0f;
            result.envSourceBButton = { x, rowY, 76.0f, rowHeight };
            x += 76.0f + 4.0f;
        }
        result.selectionLabel = { x, rowY,
                                  std::max(0.0f, result.zoomBar.x - 6.0f - x), rowHeight };

        result.timeScrollbar = { content.x, content.y + content.height,
                                 content.width, SpectrumGeometry::kTraceScrollBarThickness };
        result.frequencyScrollbar = { content.x + content.width, content.y,
                                      SpectrumGeometry::kTraceScrollBarThickness, content.height };
        return result;
    }

    struct ToolbarItem
    {
        SpectrumEditCommand command = SpectrumEditCommand::copyResidual;
        std::string_view label;
        UiRect bounds{};
        bool enabled = false;
    };

    enum class ToolbarControlKind : std::uint8_t { label, slider, editor };

    struct ToolbarControl
    {
        ToolbarControlKind kind = ToolbarControlKind::label;
        std::string_view label;
        UiRect bounds{};
        float value = 0.5f;
    };

    [[nodiscard]] static std::vector<ToolbarItem> toolbarItems(
        UiRect localBounds, SpectralTraceMode mode, bool hasSelection)
    {
        const auto chrome = layout(localBounds, mode);
        std::vector<ToolbarItem> result;
        if (chrome.toolbar.height <= 0.0f)
            return result;

        const auto residual = mode == SpectralTraceMode::residual
                           || mode == SpectralTraceMode::isometricResidual;
        const auto envelope = mode == SpectralTraceMode::isometricEnvelope
                           || mode == SpectralTraceMode::isometricEnvelopeRdc;
        const auto y = chrome.toolbar.y + 4.0f;
        const auto h = std::max(0.0f, chrome.toolbar.height - 8.0f);
        auto x = chrome.toolbar.x + 6.0f;
        const auto gap = 4.0f;
        const auto add = [&](SpectrumEditCommand command, std::string_view label, float width) {
            result.push_back({ command, label, { x, y, width, h }, hasSelection });
            x += width + gap;
        };

        if (residual)
        {
            add(SpectrumEditCommand::drawResidual, "Draw", 52.0f);
            add(SpectrumEditCommand::smoothResidual, "Smooth", 62.0f);
            add(SpectrumEditCommand::copyResidual, "Copy", 52.0f);
            add(SpectrumEditCommand::pasteResidual, "Paste", 56.0f);
            add(SpectrumEditCommand::swapResidual, "Swap", 56.0f);
            add(SpectrumEditCommand::redistResidual, "Redist", 56.0f);
            x += 4.0f + tiltSliderWidth(chrome) + 4.0f;
            add(SpectrumEditCommand::tiltResidual, "Tilt", 44.0f);
        }
        else if (envelope)
        {
            x += 76.0f + 4.0f + smoothSliderWidth(chrome) + 8.0f;
            add(SpectrumEditCommand::smoothPartialEnvelope, "Smooth Env", 88.0f);
        }
        else
        {
            add(SpectrumEditCommand::drawPartialFrequency, "Draw", 52.0f);
            add(SpectrumEditCommand::soloPartials, "Solo", 48.0f);
            add(SpectrumEditCommand::freezePartial, "Freeze", 56.0f);
            add(SpectrumEditCommand::prunePartials, "Prune", 52.0f);
            x += 4.0f + tiltSliderWidth(chrome) + 4.0f;
            add(SpectrumEditCommand::tiltPartialEnvelope, "Tilt", 44.0f);
        }
        return result;
    }

    [[nodiscard]] static std::vector<ToolbarControl> toolbarControls(
        UiRect localBounds, SpectralTraceMode mode)
    {
        const auto chrome = layout(localBounds, mode);
        std::vector<ToolbarControl> result;
        if (chrome.toolbar.height <= 0.0f)
            return result;
        const auto y = chrome.toolbar.y + 4.0f;
        const auto h = std::max(0.0f, chrome.toolbar.height - 8.0f);
        auto x = chrome.toolbar.x + 6.0f;
        const auto gap = 4.0f;
        const auto residual = mode == SpectralTraceMode::residual
                           || mode == SpectralTraceMode::isometricResidual;
        const auto envelope = mode == SpectralTraceMode::isometricEnvelope
                           || mode == SpectralTraceMode::isometricEnvelopeRdc;
        if (residual)
        {
            x += 52.0f + gap + 62.0f + gap + 52.0f + gap + 56.0f + gap + 56.0f + gap + 56.0f;
            x += 8.0f;
            result.push_back({ ToolbarControlKind::slider, "Tilt", { x, y, tiltSliderWidth(chrome), h }, 0.5f });
            x += tiltSliderWidth(chrome) + 4.0f + 44.0f + 8.0f;
            result.push_back({ ToolbarControlKind::label, "dB", { x, y, 44.0f, h }, 0.5f });
            x += 44.0f + 4.0f;
            result.push_back({ ToolbarControlKind::editor, "-72 dB", { x, y, 72.0f, h }, 0.0f });
        }
        else if (envelope)
        {
            result.push_back({ ToolbarControlKind::label, "Smooth", { x, y, 76.0f, h }, 0.5f });
            x += 76.0f + 4.0f;
            result.push_back({ ToolbarControlKind::slider, "", { x, y, smoothSliderWidth(chrome), h }, 0.5f });
        }
        else
        {
            x += 52.0f + gap + 48.0f + gap + 56.0f + gap + 52.0f + 8.0f;
            result.push_back({ ToolbarControlKind::slider, "Tilt", { x, y, tiltSliderWidth(chrome), h }, 0.5f });
        }
        return result;
    }

private:
    [[nodiscard]] static float tiltSliderWidth(const SpectrumChromeLayout& chrome) noexcept
    { return std::max(72.0f, chrome.toolbar.width / 4.0f); }

    [[nodiscard]] static float smoothSliderWidth(const SpectrumChromeLayout& chrome) noexcept
    { return std::max(120.0f, chrome.toolbar.width / 3.0f); }
};

} // namespace ats::ui
