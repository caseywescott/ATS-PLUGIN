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
                const auto x1 = bounds.x + bounds.width * a.timeNorm;
                const auto y1 = bounds.y + bounds.height * (1.0f - a.freqNorm);
                const auto x2 = bounds.x + bounds.width * b.timeNorm;
                const auto y2 = bounds.y + bounds.height * (1.0f - b.freqNorm);
                commands.push_back({ kind, { x1, y1, x2 - x1, y2 - y1 },
                                     tokens.accent.argb, selected ? 0.92f : 0.15f + 0.77f * b.ampNorm,
                                     trace.partialIndex });
            }
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
