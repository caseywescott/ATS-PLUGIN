#pragma once

#include "SpectrumViewModel.h"
#include "ThemeTokens.h"
#include "UiBackend.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ats::ui
{

enum class SpectrumDrawKind : std::uint8_t
{
    background,
    grid,
    residualBand,
    partialTrace,
    selectedPartial,
    playhead,
    axis,
    axisTick,
    onsetMarker,
    envelopeTrace,
    axisLabel,
    legend
};

struct SpectrumDrawCommand
{
    SpectrumDrawKind kind = SpectrumDrawKind::background;
    UiRect bounds{};
    std::uint32_t color = 0xff000000u;
    float alpha = 1.0f;
    int index = -1;
};

struct SpectrumRenderOptions
{
    ThemeId theme = ThemeId::ember;
    int selectedPartial = -1;
    float playheadNorm = 0.0f;
    bool showPlayhead = false;
};

class SpectrumRenderer
{
public:
    [[nodiscard]] static std::vector<SpectrumDrawCommand> render(
        const SpectralTraceSnapshot& snapshot, UiRect bounds,
        SpectrumRenderOptions options = {})
    {
        const auto tokens = ThemeTokens::forTheme(options.theme);
        std::vector<SpectrumDrawCommand> commands;
        commands.reserve(12 + snapshot.partials.size() * 8);
        commands.push_back({ SpectrumDrawKind::background, bounds,
                             tokens.spectrumCanvas.argb, 1.0f, -1 });

        const auto gridColor = tokens.secondaryText.argb;
        commands.push_back({ SpectrumDrawKind::axis,
                             { bounds.x, bounds.y, bounds.width, 1.0f },
                             gridColor, 0.62f, 0 });
        commands.push_back({ SpectrumDrawKind::axis,
                             { bounds.x, bounds.y + bounds.height - 1.0f, bounds.width, 1.0f },
                             gridColor, 0.62f, 1 });
        commands.push_back({ SpectrumDrawKind::axis,
                             { bounds.x, bounds.y, 1.0f, bounds.height },
                             gridColor, 0.62f, 2 });
        commands.push_back({ SpectrumDrawKind::axis,
                             { bounds.x + bounds.width - 1.0f, bounds.y, 1.0f, bounds.height },
                             gridColor, 0.62f, 3 });
        for (int i = 1; i < 4; ++i)
        {
            const auto x = bounds.x + bounds.width * static_cast<float>(i) / 4.0f;
            commands.push_back({ SpectrumDrawKind::grid, { x, bounds.y, 1.0f, bounds.height },
                                 gridColor, 0.14f, i });
        }
        for (int i = 1; i < 3; ++i)
        {
            const auto y = bounds.y + bounds.height * static_cast<float>(i) / 3.0f;
            commands.push_back({ SpectrumDrawKind::grid, { bounds.x, y, bounds.width, 1.0f },
                                 gridColor, 0.14f, i });
        }

        // Stable time ticks make the viewport readable even when the trace is
        // empty. Frequency ticks use the model's logarithmic bounds and stay
        // deterministic for screenshot and host-probe comparisons.
        for (int i = 0; i <= 4; ++i)
        {
            const auto x = bounds.x + bounds.width * static_cast<float>(i) / 4.0f;
            commands.push_back({ SpectrumDrawKind::axisTick,
                                 { x, bounds.y + bounds.height - 5.0f, 1.0f, 5.0f },
                                 gridColor, 0.72f, i });
        }
        for (int i = 0; i <= 4; ++i)
        {
            const auto normalized = static_cast<float>(i) / 4.0f;
            const auto y = bounds.y + bounds.height * (1.0f - normalized);
            commands.push_back({ SpectrumDrawKind::axisTick,
                                 { bounds.x, y, 5.0f, 1.0f }, gridColor, 0.72f, 10 + i });
        }

        // Residual energy is a backdrop; partial traces are emitted afterward
        // so the renderer cannot accidentally bury the model structure.
        for (const auto& band : snapshot.residualBands)
            for (std::size_t column = 0; column < band.energyNorm.size(); ++column)
            {
                const auto x = bounds.x + bounds.width * static_cast<float>(column)
                                           / static_cast<float>(std::max<std::size_t>(1, band.energyNorm.size()));
                const auto w = bounds.width / static_cast<float>(std::max<std::size_t>(1, band.energyNorm.size()));
                const auto h = bounds.height * std::clamp(band.energyNorm[column], 0.0f, 1.0f) * 0.4f;
                commands.push_back({ SpectrumDrawKind::residualBand,
                                     { x, bounds.y + bounds.height - h, w, h },
                                     tokens.accent.argb, 0.22f, band.bandIndex });
            }

        for (const auto& trace : snapshot.partials)
        {
            const auto selected = trace.partialIndex == options.selectedPartial;
            const auto kind = selected ? SpectrumDrawKind::selectedPartial : SpectrumDrawKind::partialTrace;
            for (std::size_t i = 1; i < trace.points.size(); ++i)
            {
                const auto& a = trace.points[i - 1];
                const auto& b = trace.points[i];
                if (!a.active || !b.active)
                    continue;
                const auto x1 = bounds.x + bounds.width * std::clamp(a.timeNorm, 0.0f, 1.0f);
                const auto y1 = bounds.y + bounds.height * (1.0f - std::clamp(a.freqNorm, 0.0f, 1.0f));
                const auto x2 = bounds.x + bounds.width * std::clamp(b.timeNorm, 0.0f, 1.0f);
                const auto y2 = bounds.y + bounds.height * (1.0f - std::clamp(b.freqNorm, 0.0f, 1.0f));
                commands.push_back({ kind, { x1, y1, x2 - x1, y2 - y1 },
                                     tokens.accent.argb, selected ? 0.92f : 0.15f + 0.77f * b.ampNorm,
                                     trace.partialIndex });
            }
        }

        for (std::size_t i = 1; i < snapshot.envelopeNorm.size(); ++i)
        {
            const auto x1 = bounds.x + bounds.width * static_cast<float>(i - 1)
                / static_cast<float>(std::max<std::size_t>(1, snapshot.envelopeNorm.size() - 1));
            const auto x2 = bounds.x + bounds.width * static_cast<float>(i)
                / static_cast<float>(std::max<std::size_t>(1, snapshot.envelopeNorm.size() - 1));
            const auto y1 = bounds.y + bounds.height * (1.0f
                - std::clamp(snapshot.envelopeNorm[i - 1], 0.0f, 1.0f));
            const auto y2 = bounds.y + bounds.height * (1.0f
                - std::clamp(snapshot.envelopeNorm[i], 0.0f, 1.0f));
            commands.push_back({ SpectrumDrawKind::envelopeTrace, { x1, y1, x2 - x1, y2 - y1 },
                                 tokens.text.argb, 0.78f, static_cast<int>(i - 1) });
        }

        for (std::size_t i = 0; i < snapshot.onsetNorms.size(); ++i)
        {
            const auto x = bounds.x + bounds.width * std::clamp(snapshot.onsetNorms[i], 0.0f, 1.0f);
            commands.push_back({ SpectrumDrawKind::onsetMarker, { x, bounds.y, 2.0f, bounds.height },
                                 tokens.accent.argb, 0.52f, static_cast<int>(i) });
        }

        if (options.showPlayhead)
        {
            const auto x = bounds.x + bounds.width * std::clamp(options.playheadNorm, 0.0f, 1.0f);
            commands.push_back({ SpectrumDrawKind::playhead, { x, bounds.y, 1.0f, bounds.height },
                                 tokens.text.argb, 0.70f, -1 });
        }
        commands.push_back({ SpectrumDrawKind::axisLabel, { bounds.x, bounds.y + bounds.height - 16.0f,
                                                              bounds.width, 16.0f },
                             tokens.secondaryText.argb, 0.80f, -1 });
        commands.push_back({ SpectrumDrawKind::legend, { bounds.x + bounds.width - 140.0f, bounds.y,
                                                           140.0f, 18.0f },
                             tokens.text.argb, 0.90f, static_cast<int>(snapshot.mode) });
        return commands;
    }
};

} // namespace ats::ui
