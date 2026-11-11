#pragma once

#include "EditorHost.h"
#include "SpectrumGeometry.h"

#include <algorithm>
#include <array>
#include <utility>
#include <cstdint>
#include <string_view>
#include <vector>

namespace ats::ui
{

struct SpectrumChromeLayout
{
    static constexpr float kOnsetButtonWidth = 72.0f;
    static constexpr float kSourceButtonWidth = 36.0f;
    static constexpr float kMinZoomBarWidth = 96.0f;

    // Wide wells show every view as a button instead of hiding them in a
    // menu.  Order and count follow SpectralTraceMode.
    static constexpr std::size_t kModeButtonCount = 7;
    static constexpr std::array<std::string_view, kModeButtonCount> kModeButtonLabels {
        "Trace+Res", "Partials", "Residual", "Iso 3D", "Iso Res", "Iso Env", "Env RDC" };
    static constexpr std::array<float, kModeButtonCount> kModeButtonWidths {
        76.0f, 62.0f, 64.0f, 56.0f, 60.0f, 60.0f, 64.0f };
    static constexpr float kSourceMenuWidth = 132.0f;
    static constexpr float kMinSelectionLabelWidth = 60.0f;

    [[nodiscard]] static constexpr float modeStripWidth() noexcept
    {
        float total = 0.0f;
        for (const auto w : kModeButtonWidths) total += w;
        return total;
    }

    // The narrowest inset header that fits the source menu, the view strip,
    // DELETE, ONSETS, a selection label and the minimum zoom bar.
    [[nodiscard]] static constexpr float modeStripMinWidth() noexcept
    {
        return kSourceMenuWidth + 6.0f + modeStripWidth() + 8.0f
            + 72.0f + 8.0f + kOnsetButtonWidth + 8.0f
            + kMinSelectionLabelWidth + 6.0f + kMinZoomBarWidth + 6.0f;
    }

    UiRect header{};
    UiRect toolbar{};
    // Narrow wells: one chip that opens the view and source menu.
    UiRect viewMenuButton{};
    // Wide wells: a button per view and a menu chip for the source.
    bool modeStrip = false;
    std::array<UiRect, kModeButtonCount> modeButtons{};
    UiRect sourceMenuButton{};
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

        auto x = base.x;
        result.modeStrip = base.width >= modeStripMinWidth();
        if (result.modeStrip)
        {
            result.sourceMenuButton = { x, rowY, kSourceMenuWidth, rowHeight };
            x += kSourceMenuWidth + 6.0f;
            for (std::size_t i = 0; i < kModeButtonCount; ++i)
            {
                result.modeButtons[i] = { x, rowY, kModeButtonWidths[i], rowHeight };
                x += kModeButtonWidths[i];
            }
            x += 8.0f;
        }
        else
        {
            result.viewMenuButton = { x, rowY, 182.0f, rowHeight };
            x += 182.0f + 6.0f;
        }
        (void) hasSelection; // visibility is a paint/interaction concern; geometry is stable.
        result.deleteButton = { x, rowY, 72.0f, rowHeight };
        x += 72.0f + 8.0f;
        // The zoom bar takes what the left-hand buttons leave.  Its width must
        // not depend on onset visibility (hit tests call layout() without it),
        // so the onset button's slot is always reserved.
        // The A/B shortcuts are only needed when the source sits behind the
        // narrow view menu; the wide header has its own source chip.
        const auto envelope = !result.modeStrip
            && (mode == SpectralTraceMode::isometricEnvelope
                || mode == SpectralTraceMode::isometricEnvelopeRdc);
        const auto leftEnd = x + kOnsetButtonWidth + 8.0f
            + (envelope ? 2.0f * (kSourceButtonWidth + 4.0f) : 0.0f);
        const auto zoomWidth = std::clamp(base.x + base.width - 6.0f - leftEnd - 6.0f,
                                          kMinZoomBarWidth, SpectrumGeometry::kZoomBarWidth);
        result.zoomBar = { base.x + base.width - 6.0f - zoomWidth, base.y, zoomWidth, 26.0f };
        if (hasOnsets)
        {
            result.onsetEditButton = { x, rowY, kOnsetButtonWidth, rowHeight };
            x += kOnsetButtonWidth + 8.0f;
        }
        if (envelope)
        {
            result.envSourceAButton = { x, rowY, kSourceButtonWidth, rowHeight };
            x += kSourceButtonWidth + 4.0f;
            result.envSourceBButton = { x, rowY, kSourceButtonWidth, rowHeight };
            x += kSourceButtonWidth + 4.0f;
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
            for (std::size_t i = 0; i < kResidualTools.size(); ++i)
                add(kResidualTools[i].first, kResidualTools[i].second, kResidualToolWidths[i]);
            x += 4.0f + residualTiltSliderWidth(chrome) + 4.0f;
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
            x += residualToolsWidth(gap) + 8.0f;
            const auto tilt = residualTiltSliderWidth(chrome);
            result.push_back({ ToolbarControlKind::slider, "Tilt", { x, y, tilt, h }, 0.5f });
            // The editor's text carries the unit; a separate "dB" label
            // pushed the row past the panel edge.
            x += tilt + 4.0f + 44.0f + 8.0f;
            result.push_back({ ToolbarControlKind::editor, "-72 dB",
                               { x, y, kResidualEditorWidth, h }, 0.0f });
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
    static constexpr std::array<std::pair<SpectrumEditCommand, std::string_view>, 6> kResidualTools {{
        { SpectrumEditCommand::drawResidual, "Draw" },
        { SpectrumEditCommand::smoothResidual, "Smooth" },
        { SpectrumEditCommand::copyResidual, "Copy" },
        { SpectrumEditCommand::pasteResidual, "Paste" },
        { SpectrumEditCommand::swapResidual, "Swap" },
        { SpectrumEditCommand::redistResidual, "Redist" } }};
    static constexpr std::array<float, 6> kResidualToolWidths { 44.0f, 54.0f, 44.0f, 48.0f, 46.0f, 50.0f };
    static constexpr float kResidualEditorWidth = 64.0f;

    [[nodiscard]] static float residualToolsWidth(float gap) noexcept
    {
        float width = 0.0f;
        for (const auto w : kResidualToolWidths) width += w;
        return width + gap * static_cast<float>(kResidualToolWidths.size() - 1);
    }

    // The residual row is the widest; its slider takes what the buttons, the
    // Tilt button and the dB editor leave, so the row ends inside the panel.
    [[nodiscard]] static float residualTiltSliderWidth(const SpectrumChromeLayout& chrome) noexcept
    {
        const auto fixed = 6.0f + residualToolsWidth(4.0f) + 8.0f + 4.0f + 44.0f + 8.0f
            + kResidualEditorWidth + 6.0f;
        return std::clamp(chrome.toolbar.width - fixed, 40.0f,
                          std::max(40.0f, chrome.toolbar.width / 4.0f));
    }

    [[nodiscard]] static float tiltSliderWidth(const SpectrumChromeLayout& chrome) noexcept
    { return std::max(72.0f, chrome.toolbar.width / 4.0f); }

    [[nodiscard]] static float smoothSliderWidth(const SpectrumChromeLayout& chrome) noexcept
    { return std::max(120.0f, chrome.toolbar.width / 3.0f); }
};

} // namespace ats::ui
