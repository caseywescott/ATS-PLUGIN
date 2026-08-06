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
    ridgeline,      // short marker at each partial's peak-amplitude frame
    glowHalo,       // wide translucent rect around the selected-partial trace
    isometricTile,  // 3×3 dot in isometric (time×freq×amp) projection
    dustPoint,      // 1×1 sub-pixel dot for very-low-amplitude partial regions
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

// Maps normalized amplitude [0,1] to an ARGB color using a per-theme gradient.
// Ember:     dark brown → orange → bright white-orange
// Porcelain: dark navy  → cyan  → bright white-blue
struct SpectrumLUT
{
    [[nodiscard]] static std::uint32_t colorForAmp(float ampNorm, ThemeId theme) noexcept
    {
        const float t = std::clamp(ampNorm, 0.0f, 1.0f);
        uint32_t lo, hi;
        float s;
        if (theme == ThemeId::porcelain)
        {
            if (t < 0.5f) { lo = 0xff0a1a2eu; hi = 0xff20b0dfu; s = t * 2.0f; }
            else           { lo = 0xff20b0dfu; hi = 0xffdfefff; s = (t - 0.5f) * 2.0f; }
        }
        else
        {
            if (t < 0.5f) { lo = 0xff402010u; hi = 0xffe06010u; s = t * 2.0f; }
            else           { lo = 0xffe06010u; hi = 0xffffefcfu; s = (t - 0.5f) * 2.0f; }
        }
        return lerpColor(lo, hi, s);
    }

private:
    [[nodiscard]] static std::uint32_t lerpColor(std::uint32_t a, std::uint32_t b, float t) noexcept
    {
        const auto ar = (a >> 16) & 0xff; const auto br = (b >> 16) & 0xff;
        const auto ag = (a >>  8) & 0xff; const auto bg = (b >>  8) & 0xff;
        const auto ab =  a        & 0xff; const auto bb =  b        & 0xff;
        const auto r = ar + static_cast<uint32_t>(static_cast<float>(static_cast<int>(br) - static_cast<int>(ar)) * t);
        const auto g = ag + static_cast<uint32_t>(static_cast<float>(static_cast<int>(bg) - static_cast<int>(ag)) * t);
        const auto bv = ab + static_cast<uint32_t>(static_cast<float>(static_cast<int>(bb) - static_cast<int>(ab)) * t);
        return 0xff000000u | (r << 16) | (g << 8) | bv;
    }
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

        const bool isIsometric = snapshot.mode == SpectralTraceMode::isometric
                               || snapshot.mode == SpectralTraceMode::isometricResidual
                               || snapshot.mode == SpectralTraceMode::isometricEnvelope
                               || snapshot.mode == SpectralTraceMode::isometricEnvelopeRdc;
        const bool lutColored = snapshot.mode == SpectralTraceMode::partials
                             || snapshot.mode == SpectralTraceMode::isometric;

        if (isIsometric)
        {
            emitIsometric(commands, snapshot, bounds, options, tokens);
        }
        else
        {
            // Residual energy is a backdrop; partial traces are emitted afterward
            // so the renderer cannot accidentally bury the model structure.
            for (const auto& band : snapshot.residualBands)
                for (std::size_t column = 0; column < band.energyNorm.size(); ++column)
                {
                    const auto x = bounds.x + bounds.width * static_cast<float>(column)
                                               / static_cast<float>(std::max<std::size_t>(1, band.energyNorm.size()));
                    const auto w = bounds.width / static_cast<float>(std::max<std::size_t>(1, band.energyNorm.size()));
                    const auto h = bounds.height * std::clamp(band.energyNorm[column], 0.0f, 1.0f) * 0.4f;
                    const auto ampNorm = std::clamp(band.energyNorm[column], 0.0f, 1.0f);
                    const auto color = lutColored
                        ? SpectrumLUT::colorForAmp(ampNorm, options.theme)
                        : tokens.accent.argb;
                    commands.push_back({ SpectrumDrawKind::residualBand,
                                         { x, bounds.y + bounds.height - h, w, h },
                                         color, 0.22f, band.bandIndex });
                }

            for (const auto& trace : snapshot.partials)
            {
                const auto selected = trace.partialIndex == options.selectedPartial;
                const auto kind = selected ? SpectrumDrawKind::selectedPartial
                                           : SpectrumDrawKind::partialTrace;

                float peakAmp = 0.0f;
                float peakTimeX = bounds.x, peakFreqY = bounds.y;

                for (std::size_t i = 1; i < trace.points.size(); ++i)
                {
                    const auto& a = trace.points[i - 1];
                    const auto& b = trace.points[i];
                    if (!a.active || !b.active)
                        continue;
                    const auto x1 = bounds.x + bounds.width  * std::clamp(a.timeNorm, 0.0f, 1.0f);
                    const auto y1 = bounds.y + bounds.height * (1.0f - std::clamp(a.freqNorm, 0.0f, 1.0f));
                    const auto x2 = bounds.x + bounds.width  * std::clamp(b.timeNorm, 0.0f, 1.0f);
                    const auto y2 = bounds.y + bounds.height * (1.0f - std::clamp(b.freqNorm, 0.0f, 1.0f));
                    const auto color = lutColored
                        ? SpectrumLUT::colorForAmp(b.ampNorm, options.theme)
                        : tokens.accent.argb;
                    const auto alpha = selected ? 0.92f
                                     : (b.ampNorm < 0.12f ? 0.0f : 0.15f + 0.77f * b.ampNorm);
                    if (b.ampNorm < 0.12f && !selected)
                    {
                        commands.push_back({ SpectrumDrawKind::dustPoint,
                                             { x2, y2, 1.0f, 1.0f },
                                             color, 0.25f, trace.partialIndex });
                    }
                    else
                    {
                        commands.push_back({ kind, { x1, y1, x2 - x1, y2 - y1 },
                                             color, alpha, trace.partialIndex });
                    }
                    if (b.ampNorm > peakAmp) { peakAmp = b.ampNorm; peakTimeX = x2; peakFreqY = y2; }
                }

                if (selected && trace.points.size() > 1)
                {
                    for (std::size_t i = 1; i < trace.points.size(); ++i)
                    {
                        const auto& b = trace.points[i];
                        if (!b.active) continue;
                        const auto x2 = bounds.x + bounds.width  * std::clamp(b.timeNorm, 0.0f, 1.0f);
                        const auto y2 = bounds.y + bounds.height * (1.0f - std::clamp(b.freqNorm, 0.0f, 1.0f));
                        commands.push_back({ SpectrumDrawKind::glowHalo,
                                             { x2 - 3.0f, y2 - 3.0f, 6.0f, 6.0f },
                                             tokens.accent.argb, 0.18f, trace.partialIndex });
                    }
                }

                if (peakAmp > 0.0f)
                    commands.push_back({ SpectrumDrawKind::ridgeline,
                                         { peakTimeX - 3.0f, peakFreqY, 6.0f, 1.0f },
                                         lutColored ? SpectrumLUT::colorForAmp(peakAmp, options.theme)
                                                    : tokens.accent.argb,
                                         0.55f, trace.partialIndex });
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

private:
    // Cabinet-oblique projection: (timeNorm, freqNorm, ampNorm) → (screenX, screenY).
    // Keeps all rendered tiles within bounds for the typical ATS partial range.
    [[nodiscard]] static std::pair<float, float> isoProject(
        float timeNorm, float freqNorm, float ampNorm, const UiRect& bounds) noexcept
    {
        const float sx = bounds.x + bounds.width  * (0.35f + timeNorm * 0.45f - freqNorm * 0.20f);
        const float sy = bounds.y + bounds.height * (0.80f - ampNorm  * 0.50f + freqNorm * 0.15f);
        return { sx, sy };
    }

    static void emitIsometric(std::vector<SpectrumDrawCommand>& commands,
                               const SpectralTraceSnapshot& snapshot, const UiRect& bounds,
                               const SpectrumRenderOptions& options, const ThemeTokens& tokens)
    {
        const bool isEnvMode = snapshot.mode == SpectralTraceMode::isometricEnvelope
                            || snapshot.mode == SpectralTraceMode::isometricEnvelopeRdc;
        const bool isResidual = snapshot.mode == SpectralTraceMode::isometricResidual;

        if (isResidual)
        {
            for (const auto& band : snapshot.residualBands)
            {
                const auto freqNorm = snapshot.maxFrequencyHz > snapshot.minFrequencyHz
                    ? static_cast<float>((band.lowHz + band.highHz) * 0.5 / snapshot.maxFrequencyHz)
                    : 0.0f;
                for (std::size_t col = 0; col < band.energyNorm.size(); ++col)
                {
                    const auto timeNorm = static_cast<float>(col)
                        / static_cast<float>(std::max<std::size_t>(1, band.energyNorm.size() - 1));
                    const auto ampNorm = band.energyNorm[col];
                    const auto [sx, sy] = isoProject(timeNorm, freqNorm, ampNorm, bounds);
                    commands.push_back({ SpectrumDrawKind::isometricTile, { sx, sy, 3.0f, 3.0f },
                                         SpectrumLUT::colorForAmp(ampNorm, options.theme),
                                         0.15f + 0.80f * ampNorm, band.bandIndex });
                }
            }
        }
        else if (isEnvMode)
        {
            for (std::size_t i = 0; i < snapshot.envelopeNorm.size(); ++i)
            {
                const auto timeNorm = static_cast<float>(i)
                    / static_cast<float>(std::max<std::size_t>(1, snapshot.envelopeNorm.size() - 1));
                const auto ampNorm = snapshot.envelopeNorm[i];
                const auto [sx, sy] = isoProject(timeNorm, 0.0f, ampNorm, bounds);
                commands.push_back({ SpectrumDrawKind::isometricTile, { sx, sy, 3.0f, 3.0f },
                                     SpectrumLUT::colorForAmp(ampNorm, options.theme),
                                     0.20f + 0.75f * ampNorm, static_cast<int>(i) });
            }
            if (snapshot.mode == SpectralTraceMode::isometricEnvelopeRdc)
            {
                // RDC floor: a horizontal plane at ampNorm=0 to show the noise floor
                for (int t = 0; t <= 20; ++t)
                {
                    for (int f = 0; f <= 10; ++f)
                    {
                        const auto [sx, sy] = isoProject(
                            static_cast<float>(t) / 20.0f, static_cast<float>(f) / 10.0f,
                            0.0f, bounds);
                        commands.push_back({ SpectrumDrawKind::isometricTile, { sx, sy, 2.0f, 2.0f },
                                             tokens.secondaryText.argb, 0.12f, -1 });
                    }
                }
            }
        }
        else
        {
            // isometric partials view
            for (const auto& trace : snapshot.partials)
            {
                const auto selected = trace.partialIndex == options.selectedPartial;
                for (const auto& pt : trace.points)
                {
                    if (!pt.active) continue;
                    const auto [sx, sy] = isoProject(pt.timeNorm, pt.freqNorm, pt.ampNorm, bounds);
                    const auto color = SpectrumLUT::colorForAmp(pt.ampNorm, options.theme);
                    const auto alpha = selected ? 0.90f : (0.15f + 0.80f * pt.ampNorm);
                    const auto kind = selected ? SpectrumDrawKind::selectedPartial
                                               : SpectrumDrawKind::isometricTile;
                    commands.push_back({ kind, { sx, sy, 3.0f, 3.0f }, color, alpha,
                                         trace.partialIndex });
                }
            }
        }
    }
};

} // namespace ats::ui
