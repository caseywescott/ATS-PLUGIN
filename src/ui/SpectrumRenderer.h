#pragma once

#include "SpectrumGeometry.h"
#include "SpectrumChrome.h"
#include "SpectrumPathBuilder.h"
#include "SpectrumRdc.h"
#include "SpectrumViewModel.h"
#include "ThemeTokens.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>
#include <string>
#include <iomanip>
#include <sstream>

namespace ats::ui
{

enum class SpectrumDrawKind : std::uint8_t
{
    background, grid, residualBand, residualBandTint, partialTrace, selectedPartial,
    selectionMarker, ridgeline, glowHalo, traceGlow, isometricTile, dustPoint, playhead,
    playheadWake, axis, axisHighlight, axisTick, onsetMarker, onsetHandle, envelopeTrace,
    envelopeFill, title, emptyState, axisLabel, bandLabel, freqLabel, ampLegend, legend,
    zoomBar, timeScrollbar, frequencyScrollbar, toolbarButton
};

struct SpectrumPathCommand
{
    SpectrumDrawKind kind = SpectrumDrawKind::partialTrace;
    std::vector<UiPoint> vertices;
    std::vector<TraceStop> stops;
    UiPoint gradStart{}, gradEnd{};
    float width = 1.0f;
    bool flat = false;
    bool glow = false;
    int index = -1;
};

struct SpectrumDrawCommand
{
    SpectrumDrawKind kind = SpectrumDrawKind::background;
    UiRect bounds{};
    std::uint32_t color = 0xff000000u;
    float alpha = 1.0f;
    int index = -1;
    float x2 = 0.0f;
    float y2 = 0.0f;
    float floorY = 0.0f;
    float thickness = 1.0f;
    std::vector<UiPoint> vertices;
    std::vector<TraceStop> stops;
    UiPoint gradStart{}, gradEnd{};
    bool flat = false;
    bool glow = false;
    std::string label;
    [[nodiscard]] bool isPath() const noexcept { return vertices.size() >= 2; }
};

struct SpectrumRenderOptions
{
    ThemeId theme = ThemeId::ember;
    int selectedPartial = -1;
    float playheadNorm = 0.0f;
    bool showPlayhead = false;
    const SpectralTraceSnapshot* overlaySnapshot = nullptr;
    int hoveredPartial = -1;
    std::vector<int> selectedPartials;
    SpectrumViewport viewport{};
    bool lassoActive = false;
    UiRect lassoBounds{};
    UiRect envelopeRange{};
    bool showOnsetOverlay = false;
    std::vector<int> selectedBands;
    int residualFirstFrame = 0;
    int residualLastFrame = 0;
};

struct SpectrumLUT
{
    [[nodiscard]] static std::uint32_t colorForAmp(float ampNorm, ThemeId theme) noexcept
    {
        (void) theme;
        const float t = std::clamp(ampNorm, 0.0f, 1.0f);
        constexpr std::uint32_t stops[] = { 0xff1b4fa0u, 0xff22a7c4u,
                                             0xff3fe8b0u, 0xffb8ff7au };
        const auto scaled = t * 3.0f;
        const auto i = std::min(2, static_cast<int>(scaled));
        return lerpColor(stops[i], stops[i + 1], scaled - static_cast<float>(i));
    }

    [[nodiscard]] static std::uint32_t overlayColorForAmp(float ampNorm) noexcept
    {
        const auto t = std::clamp(ampNorm, 0.0f, 1.0f);
        return lerpColor(t < 0.5f ? 0xff8e2f9fu : 0xffd34b8fu,
                         t < 0.5f ? 0xffd34b8fu : 0xffffb45fu,
                         t < 0.5f ? t * 2.0f : (t - 0.5f) * 2.0f);
    }

private:
    [[nodiscard]] static std::uint32_t lerpColor(std::uint32_t a, std::uint32_t b,
                                                   float t) noexcept
    {
        const auto channel = [t](std::uint32_t x, std::uint32_t y) {
            return static_cast<std::uint32_t>(std::clamp(
                static_cast<float>(x) + (static_cast<float>(y) - static_cast<float>(x)) * t,
                0.0f, 255.0f));
        };
        return 0xff000000u | (channel((a >> 16) & 0xffu, (b >> 16) & 0xffu) << 16)
            | (channel((a >> 8) & 0xffu, (b >> 8) & 0xffu) << 8)
            | channel(a & 0xffu, b & 0xffu);
    }
};

class SpectrumRenderer
{
public:
    [[nodiscard]] static UiRect getScreenBounds(UiRect bounds, SpectralTraceMode mode) noexcept
    { return SpectrumGeometry::getScreenBounds(bounds, mode); }
    [[nodiscard]] static UiRect getTraceContentBounds(UiRect screen) noexcept
    { return SpectrumGeometry::getTraceContentBounds(screen); }
    [[nodiscard]] static UiRect getPlotBounds(UiRect bounds,
                                               const SpectralTraceSnapshot& snapshot) noexcept
    { return SpectrumGeometry::getPlotBounds(bounds, snapshot.mode, snapshot.residualBands.size(), snapshot.frameCount); }

    // Data primitives must never escape the spectrum plot.  Visage shares a
    // canvas across the frame tree, so an offscreen zoom coordinate can
    // otherwise tint or cover unrelated editor panels.
    [[nodiscard]] static bool requiresPlotClip(const SpectrumDrawCommand& command) noexcept
    {
        if (command.isPath())
            return true;
        switch (command.kind)
        {
        case SpectrumDrawKind::grid:
        case SpectrumDrawKind::residualBand:
        case SpectrumDrawKind::residualBandTint:
        case SpectrumDrawKind::partialTrace:
        case SpectrumDrawKind::selectedPartial:
        case SpectrumDrawKind::selectionMarker:
        case SpectrumDrawKind::ridgeline:
        case SpectrumDrawKind::glowHalo:
        case SpectrumDrawKind::traceGlow:
        case SpectrumDrawKind::isometricTile:
        case SpectrumDrawKind::dustPoint:
        case SpectrumDrawKind::playhead:
        case SpectrumDrawKind::playheadWake:
        case SpectrumDrawKind::axisTick:
        case SpectrumDrawKind::onsetMarker:
        case SpectrumDrawKind::onsetHandle:
        case SpectrumDrawKind::envelopeTrace:
        case SpectrumDrawKind::envelopeFill:
            return true;
        default:
            return false;
        }
    }

    [[nodiscard]] static std::vector<SpectrumDrawCommand> render(
        const SpectralTraceSnapshot& snapshot, UiRect bounds,
        SpectrumRenderOptions options = {})
    {
        const auto tokens = SpectralCanvasTokens::fixed();
        const auto screen = getScreenBounds(bounds, snapshot.mode);
        const auto plot = getPlotBounds(bounds, snapshot);
        std::vector<SpectrumDrawCommand> commands;
        commands.reserve(32 + snapshot.partials.size() * 3);
        commands.push_back({ SpectrumDrawKind::background, screen, tokens.background.argb, 1.0f, -1 });
        commands.push_back({ SpectrumDrawKind::axis, screen, tokens.axisFrame.argb, 0.55f, 0 });
        commands.push_back({ SpectrumDrawKind::axisHighlight,
                             { screen.x + 1.5f, screen.y + 1.5f, screen.width - 3.0f, 3.0f },
                             tokens.hover.argb, 0.08f, 0 });
        commands.push_back({ SpectrumDrawKind::title,
                             { screen.x + 14.0f, screen.y + 6.0f, 180.0f, 15.0f },
                             0xff6fa3b0u, 0.75f, -1 });
        const bool hasModel = snapshot.frameCount > 0;
        if (!hasModel)
        {
            commands.push_back({ SpectrumDrawKind::emptyState, plot, 0xff5bffbbu, 1.0f, -1 });
            return commands;
        }

        const bool iso = SpectrumGeometry::isIsometricDisplayMode(snapshot.mode);
        if (iso) emitIsometricBackdrop(commands, plot, options, tokens);
        else emitPlanarBackdrop(commands, plot, tokens);
        // JUCE keeps the Smearnov overlay entirely behind the explicit onset
        // editing toggle.  This includes its detection/threshold curves;
        // publishing the curves must not make them visible by default.
        if (options.showOnsetOverlay && !iso)
            emitSmearnovCurves(commands, snapshot, plot, options);
        if (snapshot.mode == SpectralTraceMode::residual || snapshot.mode == SpectralTraceMode::combined)
            emitResidual(commands, snapshot, plot, options);
        if (snapshot.mode == SpectralTraceMode::combined
            || snapshot.mode == SpectralTraceMode::partials)
            emitPartials(commands, snapshot, plot, options, false);
        if (snapshot.mode == SpectralTraceMode::isometric)
            emitPartials(commands, snapshot, plot, options, true);
        else if (snapshot.mode == SpectralTraceMode::isometricResidual)
            emitIsoResidual(commands, snapshot, plot, options);
        else if (snapshot.mode == SpectralTraceMode::isometricEnvelope
                 || snapshot.mode == SpectralTraceMode::isometricEnvelopeRdc)
            emitEnvelope(commands, snapshot, plot, options);
        else if (!snapshot.envelopeNorm.empty())
            emitPlanarEnvelope(commands, snapshot, plot, options);
        if (options.overlaySnapshot != nullptr)
        {
            if (snapshot.mode == SpectralTraceMode::isometricEnvelope
                || snapshot.mode == SpectralTraceMode::isometricEnvelopeRdc)
                emitEnvelope(commands, *options.overlaySnapshot, plot, options, true);
        }
        if (options.showOnsetOverlay && !iso)
            emitOnsets(commands, snapshot, plot);
        if (options.lassoActive && options.lassoBounds.width > 0.0f && options.lassoBounds.height > 0.0f)
            commands.push_back({ SpectrumDrawKind::selectionMarker, options.lassoBounds,
                                 SpectralCanvasTokens::fixed().selection.argb, 0.72f, -1 });
        if (options.envelopeRange.width > 0.0f && options.envelopeRange.height > 0.0f)
            commands.push_back({ SpectrumDrawKind::selectionMarker, options.envelopeRange,
                                 SpectralCanvasTokens::fixed().selection.argb, 0.72f, -1 });
        emitResidualSelection(commands, snapshot, plot, options);
        emitAxesAndChrome(commands, snapshot, screen, plot, options, tokens);
        emitNavigationChrome(commands, bounds, snapshot.mode, options, tokens);
        if (options.showPlayhead)
        {
            const auto x = plot.x + plot.width * std::clamp(options.playheadNorm, 0.0f, 1.0f);
            for (int i = 6; i >= 1; --i)
                commands.push_back({ SpectrumDrawKind::playheadWake,
                                     { x - static_cast<float>(i), plot.y, 1.0f, plot.height },
                                     tokens.playhead.argb, 0.015f * static_cast<float>(7 - i), -1 });
            commands.push_back({ SpectrumDrawKind::playhead, { x, plot.y, 1.0f, plot.height },
                                 tokens.playhead.argb, 0.70f, -1 });
        }
        const auto selected = options.selectedPartial >= 0 || !options.selectedPartials.empty();
        for (const auto& item : SpectrumChromeLayout::toolbarItems(bounds, snapshot.mode, selected))
        {
            SpectrumDrawCommand command;
            command.kind = SpectrumDrawKind::toolbarButton;
            command.bounds = item.bounds;
            command.color = tokens.background.argb;
            command.alpha = item.enabled ? 0.82f : 0.35f;
            command.index = static_cast<int>(item.command);
            command.label = std::string(item.label);
            commands.push_back(std::move(command));
        }
        commands.push_back({ SpectrumDrawKind::legend,
                             { plot.x + plot.width - 140.0f, plot.y + 8.0f, 132.0f, 38.0f },
                             tokens.axisFrame.argb, 0.45f, static_cast<int>(snapshot.mode) });
        return commands;
    }

    [[nodiscard]] static std::vector<SpectrumDrawCommand> render(
        const SpectralTraceSnapshot& snapshot, UiRect bounds, SpectrumViewport viewport,
        SpectrumRenderOptions options = {})
    { options.viewport = viewport; return render(snapshot, bounds, std::move(options)); }

private:
    [[nodiscard]] static bool selected(const SpectrumRenderOptions& o, int index) noexcept
    { return index == o.selectedPartial || std::find(o.selectedPartials.begin(), o.selectedPartials.end(), index) != o.selectedPartials.end(); }

    static SpectrumDrawCommand pathCommand(SpectrumDrawKind kind, int index, const GradientTrace& trace,
                                           float width, std::uint32_t color, float alpha, bool glow = false)
    {
        SpectrumDrawCommand c; c.kind = kind; c.index = index; c.color = color; c.alpha = alpha;
        c.thickness = width; c.vertices = trace.vertices; c.stops = trace.stops;
        c.gradStart = trace.gradStart; c.gradEnd = trace.gradEnd; c.flat = trace.flat; c.glow = glow;
        c.bounds = boundsOf(trace.vertices); return c;
    }

    static void emitPlanarBackdrop(std::vector<SpectrumDrawCommand>& commands, UiRect plot,
                                   const SpectralCanvasTokens& tokens)
    {
        // This is the reference 48-bit stream, including its
        // signed-int-to-uint32 conversion in nextFloat().  The seed and call
        // order are image data: changing either moves every dust point.
        std::uint64_t state = 0x5eedu;
        const auto next = [&]() {
            state = ((state * 0x5deece66dULL) + 11ULL) & 0xffffffffffffULL;
            const auto nextInt = static_cast<std::uint32_t>(
                static_cast<std::int32_t>(state >> 16));
            return static_cast<float>(nextInt) / 4294967296.0f;
        };
        const auto dots = static_cast<int>(std::max(0.0f, plot.width * plot.height / 2600.0f));
        for (int i = 0; i < dots; ++i)
        {
            const auto x = plot.x + plot.width * next();
            const auto y = plot.y + plot.height * next();
            const auto radius = 0.4f + next() * 0.9f;
            const auto alpha = 0.02f + next() * 0.07f;
            const auto color = next() < 0.3f ? 0xff58c9a8u : 0xff4f7fd9u;
            commands.push_back({ SpectrumDrawKind::dustPoint, { x - radius, y - radius, radius * 2.0f, radius * 2.0f }, color, alpha, i });
        }
        for (int i = 0; i <= 16; ++i)
        {
            const auto x = std::round(plot.x + plot.width * static_cast<float>(i) / 16.0f);
            commands.push_back({ SpectrumDrawKind::grid, { x, plot.y, 1.0f, plot.height },
                                 i % 4 == 0 ? tokens.gridMajor.argb : tokens.gridMinor.argb,
                                 i % 4 == 0 ? 0.35f : 0.22f, i });
        }
        for (int i = 0; i <= 12; ++i)
        {
            const auto y = std::round(plot.y + plot.height * static_cast<float>(i) / 12.0f);
            commands.push_back({ SpectrumDrawKind::grid, { plot.x, y, plot.width, 1.0f },
                                 i % 4 == 0 ? tokens.gridMajor.argb : tokens.gridMinor.argb,
                                 i % 4 == 0 ? 0.35f : 0.22f, i });
        }
    }

    static void emitIsometricBackdrop(std::vector<SpectrumDrawCommand>& commands, UiRect plot,
                                      const SpectrumRenderOptions& o, const SpectralCanvasTokens& tokens)
    {
        const auto tr = SpectrumGeometry::visibleTimeRange(o.viewport);
        const auto fr = SpectrumGeometry::visibleFrequencyRange(o.viewport);
        for (int i = 0; i <= 16; ++i)
        {
            std::vector<UiPoint> line;
            for (int j = 0; j <= 10; ++j)
                line.push_back(SpectrumGeometry::positionForIsoCoords(
                    tr.start + tr.length() * i / 16.0f, fr.start + fr.length() * j / 10.0f,
                    0.0f, plot, o.viewport));
            SpectrumDrawCommand c; c.kind = SpectrumDrawKind::grid;
            c.color = i % 4 == 0 ? tokens.gridMajor.argb : tokens.gridMinor.argb;
            c.alpha = i % 4 == 0 ? 0.35f : 0.22f;
            c.vertices = std::move(line); c.bounds = boundsOf(c.vertices);
            commands.push_back(std::move(c));
        }
        for (int j = 0; j <= 10; ++j)
        {
            std::vector<UiPoint> line;
            for (int i = 0; i <= 16; ++i)
                line.push_back(SpectrumGeometry::positionForIsoCoords(
                    tr.start + tr.length() * i / 16.0f, fr.start + fr.length() * j / 10.0f,
                    0.0f, plot, o.viewport));
            SpectrumDrawCommand c; c.kind = SpectrumDrawKind::grid;
            c.color = j % 4 == 0 ? tokens.gridMajor.argb : tokens.gridMinor.argb;
            c.alpha = j % 4 == 0 ? 0.35f : 0.22f;
            c.vertices = std::move(line); c.bounds = boundsOf(c.vertices);
            commands.push_back(std::move(c));
        }
        const auto addPost = [&](float time, float frequency) {
            SpectrumDrawCommand c; c.kind = SpectrumDrawKind::axis;
            c.color = tokens.axisFrame.argb; c.alpha = 0.55f;
            c.vertices = { SpectrumGeometry::positionForIsoCoords(time, frequency, 0.0f,
                                                                    plot, o.viewport),
                           SpectrumGeometry::positionForIsoCoords(time, frequency, 1.25f,
                                                                    plot, o.viewport) };
            c.bounds = boundsOf(c.vertices);
            commands.push_back(std::move(c));
        };
        addPost(tr.start, fr.start);
        addPost(tr.end, fr.start);
        addPost(tr.start, fr.end);
    }

    [[nodiscard]] static SpectrumRange residualYRange(const SpectralResidualBandTrace& band,
                                                      const SpectralTraceSnapshot& snapshot,
                                                      UiRect plot, const SpectrumViewport& viewport) noexcept
    {
        const auto minHz = std::max(1.0, snapshot.minFrequencyHz);
        const auto maxHz = std::max(minHz * 1.2, snapshot.maxFrequencyHz);
        const auto logRange = std::max(1.0e-6, std::log(maxHz / minHz));
        const auto normalise = [minHz, maxHz, logRange](double hz) {
            return static_cast<float>(std::clamp(
                std::log(std::clamp(hz, minHz, maxHz) / minHz) / logRange, 0.0, 1.0));
        };
        const auto visible = SpectrumGeometry::visibleFrequencyRange(viewport);
        const auto screenY = [&](float value) {
            return plot.y + plot.height - plot.height
                * (value - visible.start) / std::max(1.0e-6f, visible.length());
        };
        const auto high = screenY(normalise(band.highHz));
        const auto low = screenY(normalise(band.lowHz));
        return { std::min(high, low), std::max(high, low) };
    }

    static void emitResidual(std::vector<SpectrumDrawCommand>& commands, const SpectralTraceSnapshot& snapshot,
                             UiRect plot, const SpectrumRenderOptions& o)
    {
        const auto timeRange = SpectrumGeometry::visibleTimeRange(o.viewport);
        for (const auto& band : snapshot.residualBands)
        {
            const auto yRange = residualYRange(band, snapshot, plot, o.viewport);
            const auto baseY = yRange.end;
            const auto height = std::max(1.0f, yRange.length());
            const auto tintTop = std::max(plot.y, yRange.start);
            const auto tintBottom = std::min(plot.y + plot.height, yRange.end);
            if (tintBottom > tintTop)
                commands.push_back({ SpectrumDrawKind::residualBandTint,
                                     { plot.x, tintTop, plot.width, tintBottom - tintTop },
                                     SpectralCanvasTokens::fixed().residualHigh.argb,
                                     0.025f, band.bandIndex });
            const auto columns = band.energyNorm.size();
            if (columns < 2 || timeRange.length() <= 0.0f)
                continue;
            std::vector<TraceVertex> ridge;
            ridge.reserve(columns);
            for (std::size_t c = 0; c < columns; ++c)
            {
                const auto sample = [&](int index) {
                    return std::clamp(band.energyNorm[static_cast<std::size_t>(
                        std::clamp(index, 0, static_cast<int>(columns) - 1))], 0.0f, 1.0f);
                };
                const auto energy = 0.25f * (sample(static_cast<int>(c) - 1)
                                           + 2.0f * sample(static_cast<int>(c))
                                           + sample(static_cast<int>(c) + 1));
                const auto absoluteTime = (static_cast<float>(c) + 0.5f)
                    / static_cast<float>(columns);
                const auto x = plot.x + plot.width
                    * (absoluteTime - timeRange.start) / timeRange.length();
                if (x < plot.x - 24.0f || x > plot.x + plot.width + 24.0f)
                    continue;
                ridge.push_back({ { x, baseY - energy * height * 0.92f }, energy, 1.0f });
            }
            if (ridge.size() >= 2)
            {
                const auto gradient = buildGradientTrace(ridge);
                commands.push_back(pathCommand(SpectrumDrawKind::residualBand, band.bandIndex,
                                                gradient, 1.0f,
                                                SpectrumLUT::colorForAmp(gradient.maxAmp, o.theme),
                                                0.16f + 0.42f * gradient.maxAmp,
                                                gradient.maxAmp > 0.6f));
            }
        }
    }

    static void emitPartials(std::vector<SpectrumDrawCommand>& commands, const SpectralTraceSnapshot& snapshot,
                             UiRect plot, const SpectrumRenderOptions& o, bool iso)
    {
        const auto timeRange = SpectrumGeometry::visibleTimeRange(o.viewport);
        const auto frequencyRange = SpectrumGeometry::visibleFrequencyRange(o.viewport);
        if (!iso)
        {
            int selectedIndex = -1;
            if (o.selectedPartials.size() == 1)
                selectedIndex = o.selectedPartials.front();
            else if (o.selectedPartials.empty() && o.selectedPartial >= 0)
                selectedIndex = o.selectedPartial;
            const auto selectedTrace = std::find_if(snapshot.partials.begin(), snapshot.partials.end(),
                [selectedIndex](const auto& trace) { return trace.partialIndex == selectedIndex; });
            if (selectedTrace != snapshot.partials.end() && selectedTrace->meanFrequencyHz > 0.0)
            {
                const auto minHz = std::max(1.0, snapshot.minFrequencyHz);
                const auto maxHz = std::max(minHz * 1.0001, snapshot.maxFrequencyHz);
                const auto logRange = std::log(maxHz / minHz);
                for (int harmonic = 2; harmonic <= 32; ++harmonic)
                {
                    const auto frequencyHz = selectedTrace->meanFrequencyHz * harmonic;
                    if (!(frequencyHz >= minHz && frequencyHz <= maxHz))
                        continue;
                    const auto frequencyNorm = static_cast<float>(
                        std::log(frequencyHz / minHz) / logRange);
                    if (frequencyNorm < frequencyRange.start || frequencyNorm > frequencyRange.end)
                        continue;
                    const auto y = plot.y + plot.height - plot.height
                        * (frequencyNorm - frequencyRange.start)
                            / std::max(1.0e-6f, frequencyRange.length());
                    for (float x = plot.x; x < plot.x + plot.width; x += 10.0f)
                    {
                        const auto end = std::min(x + 5.0f, plot.x + plot.width);
                        commands.push_back({ SpectrumDrawKind::axis,
                                             { x, y, end - x, 0.0f },
                                             SpectralCanvasTokens::fixed().selection.argb,
                                             0.14f, harmonic, end, y, 0.0f, 0.8f,
                                             { { x, y }, { end, y } }, {}, {}, {}, true });
                    }
                    std::ostringstream label;
                    label << harmonic << 'f';
                    commands.push_back({ SpectrumDrawKind::axisLabel,
                                         { plot.x + 4.0f, y - 6.0f, 28.0f, 12.0f },
                                         SpectralCanvasTokens::fixed().selection.argb,
                                         0.38f, harmonic, 0.0f, 0.0f, 0.0f, 1.0f,
                                         {}, {}, {}, {}, false, false, label.str() });
                }
            }
        }
        for (int pass = 0; pass < 2; ++pass)
            for (const auto& trace : snapshot.partials)
            {
                const auto isSelected = selected(o, trace.partialIndex);
                if ((pass == 0) == isSelected) continue;
                const auto isHovered = trace.partialIndex == o.hoveredPartial;
                std::vector<TraceVertex> run;
                auto flush = [&]() {
                    if (run.size() < 2) { run.clear(); return; }
                    float peak = 0.0f; for (const auto& v : run) peak = std::max(peak, v.amp);
                    if (run.size() <= 4 && peak < 0.3f && !isSelected && !isHovered) { run.clear(); return; }
                    smoothInPlace(run, 2); decimateInPlace(run);
                    constexpr float pathMargin = 8.0f;
                    const UiRect pathBounds { plot.x - pathMargin, plot.y - pathMargin,
                                              plot.width + pathMargin * 2.0f,
                                              plot.height + pathMargin * 2.0f };
                    auto visibleRuns = clipTraceRuns(run, pathBounds);
                    run.clear();
                    for (const auto& visibleRun : visibleRuns)
                    {
                        const auto gradient = buildGradientTrace(visibleRun);
                        if (gradient.vertices.empty())
                            continue;
                        const auto kind = isSelected ? SpectrumDrawKind::selectedPartial
                                                     : SpectrumDrawKind::partialTrace;
                        const auto color = isSelected ? SpectralCanvasTokens::fixed().selection.argb
                            : (isHovered ? SpectralCanvasTokens::fixed().hover.argb
                                         : SpectrumLUT::colorForAmp(std::min(1.0f, peak), o.theme));
                        if (isSelected || isHovered)
                            commands.push_back(pathCommand(SpectrumDrawKind::glowHalo,
                                                            trace.partialIndex, gradient, 5.0f,
                                                            color, 0.18f, true));
                        // The JUCE painter has no extra glow pass for ordinary
                        // loud traces.  Besides being a raster divergence, that
                        // duplicate path could overflow Visage's per-frame path
                        // atlas on a 900-partial analysis.
                        commands.push_back(pathCommand(kind, trace.partialIndex, gradient,
                                                        isSelected || isHovered ? 1.7f : 1.0f, color,
                                                        isSelected || isHovered ? 0.92f
                                                            : 0.28f + std::min(1.0f, peak) * 0.62f,
                                                        false));
                    }
                };
                for (const auto& point : trace.points)
                {
                    if (!point.active)
                    {
                        flush();
                        continue;
                    }
                    const auto p = iso ? SpectrumGeometry::positionForIsoCoords(point.timeNorm, point.freqNorm,
                                           point.ampNorm, plot, o.viewport)
                                      : SpectrumGeometry::positionForPoint(point, plot, o.viewport);
                    run.push_back({ p, iso ? point.ampNorm : std::min(2.0f, point.ampNorm), 1.0f });
                }
                flush();
                // The complete gradient path is the primitive.  Do not add
                // synthetic endpoint markers: the legacy painter never emits
                // a second ridgeline or tile for a trace endpoint.
            }
    }

    static void emitIsoResidual(std::vector<SpectrumDrawCommand>& commands, const SpectralTraceSnapshot& snapshot,
                                UiRect plot, const SpectrumRenderOptions& o)
    {
        for (const auto& band : snapshot.residualBands)
        {
            const auto minHz = std::max(1.0, snapshot.minFrequencyHz);
            const auto maxHz = std::max(minHz * 1.2, snapshot.maxFrequencyHz);
            const auto logRange = std::max(1.0e-6, std::log(maxHz / minHz));
            const auto centreHz = std::max(minHz, (band.lowHz + band.highHz) * 0.5);
            const auto f = static_cast<float>(std::clamp(
                std::log(std::clamp(centreHz, minHz, maxHz) / minHz) / logRange, 0.0, 1.0));
            for (std::size_t c = 1; c < band.energyNorm.size(); ++c)
            {
                const auto t0 = static_cast<float>(c - 1) / std::max<std::size_t>(1, band.energyNorm.size() - 1);
                const auto t1 = static_cast<float>(c) / std::max<std::size_t>(1, band.energyNorm.size() - 1);
                const auto p0 = SpectrumGeometry::positionForIsoCoords(t0, f, band.energyNorm[c - 1], plot, o.viewport);
                const auto p1 = SpectrumGeometry::positionForIsoCoords(t1, f, band.energyNorm[c], plot, o.viewport);
                commands.push_back({ SpectrumDrawKind::residualBand, { p0.x, p0.y, p1.x - p0.x, p1.y - p0.y },
                                     SpectrumLUT::colorForAmp(std::max(band.energyNorm[c - 1], band.energyNorm[c]), o.theme),
                                     0.16f, band.bandIndex, 0, 0, 0, 1.0f });
            }
        }
    }

    static void emitPlanarEnvelope(std::vector<SpectrumDrawCommand>& commands,
                                   const SpectralTraceSnapshot& snapshot, UiRect plot,
                                   const SpectrumRenderOptions& o, bool overlay = false)
    {
        if (snapshot.envelopeNorm.size() < 2) return;
        for (std::size_t i = 1; i < snapshot.envelopeNorm.size(); ++i)
        {
            const auto t0 = static_cast<float>(i - 1) / (snapshot.envelopeNorm.size() - 1);
            const auto t1 = static_cast<float>(i) / (snapshot.envelopeNorm.size() - 1);
            const auto a = std::clamp(snapshot.envelopeNorm[i - 1], 0.0f, 1.0f);
            const auto b = std::clamp(snapshot.envelopeNorm[i], 0.0f, 1.0f);
            commands.push_back({ SpectrumDrawKind::envelopeTrace,
                                 { plot.x + plot.width * t0, plot.y + plot.height * (1.0f - a),
                                   plot.width * (t1 - t0), plot.height * (a - b) },
                                 overlay ? SpectrumLUT::overlayColorForAmp(b)
                                         : SpectrumLUT::colorForAmp(b, o.theme),
                                 overlay ? 0.64f : 0.78f, static_cast<int>(i - 1) });
        }
    }

    static void emitSmearnovCurves(std::vector<SpectrumDrawCommand>& commands,
                                   const SpectralTraceSnapshot& snapshot, UiRect plot,
                                   const SpectrumRenderOptions& o)
    {
        const auto emit = [&](const std::vector<float>& values, std::uint32_t color, float alpha, int index) {
            if (values.size() < 2) return;
            std::vector<TraceVertex> vertices;
            vertices.reserve(values.size());
            for (std::size_t i = 0; i < values.size(); ++i)
            {
                const auto t = static_cast<float>(i) / static_cast<float>(values.size() - 1);
                vertices.push_back({ { plot.x + plot.width * t,
                                        plot.y + plot.height * (1.0f - std::clamp(values[i], 0.0f, 1.0f)) },
                                      std::clamp(values[i], 0.0f, 1.0f), 1.0f });
            }
            const auto trace = buildGradientTrace(vertices);
            if (!trace.vertices.empty())
                commands.push_back(pathCommand(SpectrumDrawKind::envelopeTrace, index, trace,
                                                0.9f, color, alpha));
        };
        emit(snapshot.smearnovDetectionCurve, 0xfff0b35fu, 0.66f, -10);
        emit(snapshot.smearnovThresholdCurve, 0xffdf668fu, 0.52f, -11);
        (void) o;
    }

    [[nodiscard]] static std::array<int, 3> boxRadiiForGauss(float sigma) noexcept
    {
        const auto idealWidth = std::sqrt(4.0f * sigma * sigma + 1.0f);
        auto lowerWidth = std::max(1, static_cast<int>(idealWidth));
        if (lowerWidth % 2 == 0) --lowerWidth;
        const auto upperWidth = lowerWidth + 2;
        const auto idealLowerCount = (12.0f * sigma * sigma - 3.0f * lowerWidth * lowerWidth
                                      - 12.0f * lowerWidth - 9.0f)
            / (-4.0f * lowerWidth - 4.0f);
        const auto lowerCount = std::clamp(static_cast<int>(std::lround(idealLowerCount)), 0, 3);
        return { { ((0 < lowerCount ? lowerWidth : upperWidth) - 1) / 2,
                   ((1 < lowerCount ? lowerWidth : upperWidth) - 1) / 2,
                   ((2 < lowerCount ? lowerWidth : upperWidth) - 1) / 2 } };
    }

    static void boxBlurInPlace(std::vector<float>& values, int radius,
                               std::vector<float>& scratch)
    {
        if (radius <= 0 || values.empty()) return;
        scratch = values;
        const auto inverseWidth = 1.0f / static_cast<float>(2 * radius + 1);
        float accumulator = 0.0f;
        for (int i = 0; i <= std::min(radius, static_cast<int>(values.size()) - 1); ++i)
            accumulator += scratch[static_cast<std::size_t>(i)];
        for (int i = 0; i < static_cast<int>(values.size()); ++i)
        {
            values[static_cast<std::size_t>(i)] = accumulator * inverseWidth;
            const auto added = i + radius + 1;
            const auto removed = i - radius;
            if (added < static_cast<int>(values.size()))
                accumulator += scratch[static_cast<std::size_t>(added)];
            if (removed >= 0)
                accumulator -= scratch[static_cast<std::size_t>(removed)];
        }
    }

    // Keep the legacy per-trace monotonic sweep here rather than rebuilding a
    // segment search for every envelope slice.  The one-sided branch is
    // important: a partial that starts or ends between slices remains visible
    // while its amplitude fades, with its frequency held at the active end.
    struct EnvelopeSample
    {
        float frequencyNorm = 0.0f;
        float amplitudeNorm = 0.0f;
        double frequencyHz = 0.0;
    };

    [[nodiscard]] static std::vector<std::vector<EnvelopeSample>>
    sampleTracesAtSliceTimes(const SpectralTraceSnapshot& snapshot,
                             const std::vector<float>& sliceTimes,
                             bool fadeInactive)
    {
        std::vector<std::vector<EnvelopeSample>> result(sliceTimes.size());
        for (const auto& trace : snapshot.partials)
        {
            if (trace.points.size() < 2) continue;
            const auto firstTime = trace.points.front().timeNorm;
            const auto lastTime = trace.points.back().timeNorm;
            std::size_t pointIndex = 1;
            for (std::size_t slice = 0; slice < sliceTimes.size(); ++slice)
            {
                const auto time = sliceTimes[slice];
                if (time < firstTime) continue;
                if (time > lastTime) break;
                while (pointIndex < trace.points.size()
                       && trace.points[pointIndex].timeNorm < time)
                    ++pointIndex;
                if (pointIndex >= trace.points.size()) break;
                const auto& previous = trace.points[pointIndex - 1];
                const auto& current = trace.points[pointIndex];
                if (previous.timeNorm > time) continue;
                const auto span = current.timeNorm - previous.timeNorm;
                const auto amount = span > 1.0e-6f
                    ? (time - previous.timeNorm) / span : 0.0f;
                float amplitude = 0.0f;
                float frequency = -1.0f;
                double sourceFrequency = 0.0;
                if (previous.active && current.active)
                {
                    amplitude = previous.ampNorm
                        + amount * (current.ampNorm - previous.ampNorm);
                    frequency = previous.freqNorm
                        + amount * (current.freqNorm - previous.freqNorm);
                    sourceFrequency = previous.frequencyHz
                        + amount * (current.frequencyHz - previous.frequencyHz);
                }
                else if (fadeInactive && (previous.active || current.active))
                {
                    const auto previousAmplitude = previous.active ? previous.ampNorm : 0.0f;
                    const auto currentAmplitude = current.active ? current.ampNorm : 0.0f;
                    amplitude = previousAmplitude
                        + amount * (currentAmplitude - previousAmplitude);
                    frequency = previous.active ? previous.freqNorm : current.freqNorm;
                    sourceFrequency = previous.active ? previous.frequencyHz : current.frequencyHz;
                }
                if (amplitude > 0.0f && frequency >= 0.0f)
                    result[slice].push_back({ frequency, amplitude, sourceFrequency });
            }
        }
        return result;
    }

    static void emitEnvelope(std::vector<SpectrumDrawCommand>& commands, const SpectralTraceSnapshot& snapshot,
                             UiRect plot, const SpectrumRenderOptions& o, bool overlay = false)
    {
        constexpr int slices = 30, bins = 128;
        constexpr float sigma = 7.5f;
        constexpr int taperBins = 14;
        if (snapshot.partials.empty())
            return;
        const auto tr = SpectrumGeometry::visibleTimeRange(o.viewport);
        const auto fr = SpectrumGeometry::visibleFrequencyRange(o.viewport);
        const auto sliceTimes = [&] {
            std::vector<float> result(static_cast<std::size_t>(slices));
            for (int i = 0; i < slices; ++i)
                result[static_cast<std::size_t>(i)] = tr.start + tr.length()
                    * static_cast<float>(i) / static_cast<float>(slices - 1);
            return result;
        }();
        const auto sliceSamples = sampleTracesAtSliceTimes(snapshot, sliceTimes, true);
        const auto rdcSliceSamples = snapshot.mode == SpectralTraceMode::isometricEnvelopeRdc
            ? sampleTracesAtSliceTimes(snapshot, sliceTimes, false)
            : std::vector<std::vector<EnvelopeSample>> {};
        for (int slice = slices - 1; slice >= 0; --slice)
        {
            const auto time = tr.start + tr.length() * static_cast<float>(slice) / (slices - 1);
            std::vector<std::pair<float, float>> samples;
            std::vector<double> rdcFrequencies;
            std::vector<double> rdcAmplitudes;
            const auto sampleIndex = static_cast<std::size_t>(slice);
            for (const auto& sample : sliceSamples[sampleIndex])
            {
                const auto f = sample.frequencyNorm;
                const auto amp = sample.amplitudeNorm;
                const auto fVisible = (f - fr.start) / std::max(1.0e-6f, fr.length());
                if (amp > 0.0f && fVisible >= -0.1f && fVisible <= 1.1f)
                {
                    samples.emplace_back(fVisible, amp);
                }
            }
            if (snapshot.mode == SpectralTraceMode::isometricEnvelopeRdc)
                for (const auto& sample : rdcSliceSamples[sampleIndex])
                {
                    const auto fVisible = (sample.frequencyNorm - fr.start)
                        / std::max(1.0e-6f, fr.length());
                    if (sample.amplitudeNorm > 0.0f
                        && fVisible >= -0.1f && fVisible <= 1.1f)
                    {
                        rdcFrequencies.push_back(sample.frequencyHz);
                        rdcAmplitudes.push_back(sample.amplitudeNorm);
                    }
                }
            if (samples.empty()) continue;

            const auto density = std::clamp(static_cast<float>(samples.size()) / 24.0f, 0.08f, 1.0f);
            const auto effectiveSigma = sigma / std::sqrt(density);
            std::vector<float> weights(static_cast<std::size_t>(bins), 0.0f);
            std::vector<float> weightedAmplitudes(static_cast<std::size_t>(bins), 0.0f);
            for (const auto& [fVisible, amp] : samples)
            {
                const auto bin = std::clamp(fVisible, 0.0f, 1.0f) * bins;
                const auto first = std::min(bins - 2, static_cast<int>(bin));
                const auto fraction = bin - static_cast<float>(first);
                const auto mass = effectiveSigma * 2.50662827463f;
                weights[static_cast<std::size_t>(first)] += mass * (1.0f - fraction);
                weightedAmplitudes[static_cast<std::size_t>(first)] += mass * (1.0f - fraction) * amp;
                weights[static_cast<std::size_t>(first + 1)] += mass * fraction;
                weightedAmplitudes[static_cast<std::size_t>(first + 1)] += mass * fraction * amp;
            }
            std::vector<float> blurScratch;
            for (const auto radius : boxRadiiForGauss(effectiveSigma))
            {
                boxBlurInPlace(weights, radius, blurScratch);
                boxBlurInPlace(weightedAmplitudes, radius, blurScratch);
            }
            const auto valueAt = [&](int bin) {
                auto value = weightedAmplitudes[static_cast<std::size_t>(bin)]
                    / (weights[static_cast<std::size_t>(bin)] + 0.02f);
                const auto edgeDistance = std::min(bin, bins - 1 - bin);
                if (edgeDistance < taperBins)
                {
                    const auto t = static_cast<float>(edgeDistance) / taperBins;
                    value *= 0.5f - 0.5f * std::cos(3.14159265359f * t);
                }
                return std::max(0.0f, value);
            };
            std::vector<float> values(static_cast<std::size_t>(bins), 0.0f);
            for (int b = 0; b < bins; ++b)
                values[static_cast<std::size_t>(b)] = valueAt(b);
            if (snapshot.mode == SpectralTraceMode::isometricEnvelopeRdc)
            {
                const auto fit = rdcDisplay::fitRDC(rdcFrequencies, rdcAmplitudes,
                                                    snapshot.minFrequencyHz, snapshot.maxFrequencyHz);
                if (fit.valid)
                {
                    const auto minHz = std::max(1.0, snapshot.minFrequencyHz);
                    const auto maxHz = std::max(minHz * 1.1, snapshot.maxFrequencyHz);
                    const auto logRange = std::log(maxHz / minHz);
                    for (int b = 0; b < bins; ++b)
                    {
                        const auto normalized = fr.start + fr.length() * static_cast<double>(b) / (bins - 1);
                        const auto hz = minHz * std::exp(normalized * logRange);
                        const auto db = fit.evaluate(rdcDisplay::barkNorm(hz, minHz, maxHz));
                        values[static_cast<std::size_t>(b)] = std::clamp(
                            static_cast<float>((db + 120.0) / 120.0), 0.0f, 1.25f);
                    }
                }
            }
            std::vector<TraceVertex> ridge;
            for (int b = 0; b < bins; ++b)
            {
                const auto f = fr.start + fr.length() * static_cast<float>(b) / (bins - 1);
                const auto amp = std::clamp(values[static_cast<std::size_t>(b)], 0.0f, 1.25f);
                ridge.push_back({ SpectrumGeometry::positionForIsoCoords(time, f,
                                                                          amp, plot, o.viewport), amp, 1.0f });
            }
            const auto gradient = buildGradientTrace(ridge);
            if (!gradient.vertices.empty())
            {
                const auto floorStart = SpectrumGeometry::positionForIsoCoords(
                    time, fr.start, 0.0f, plot, o.viewport);
                const auto floorEnd = SpectrumGeometry::positionForIsoCoords(
                    time, fr.end, 0.0f, plot, o.viewport);
                for (std::size_t i = 1; i < gradient.vertices.size(); ++i)
                {
                    const auto proportion0 = static_cast<float>(i - 1)
                        / static_cast<float>(gradient.vertices.size() - 1);
                    const auto proportion1 = static_cast<float>(i)
                        / static_cast<float>(gradient.vertices.size() - 1);
                    const auto floorY = floorStart.y + (floorEnd.y - floorStart.y) *
                        ((proportion0 + proportion1) * 0.5f);
                    commands.push_back({ SpectrumDrawKind::envelopeFill,
                                         { gradient.vertices[i - 1].x, gradient.vertices[i - 1].y,
                                           0.0f, 0.0f },
                                         overlay ? SpectrumLUT::overlayColorForAmp(gradient.maxAmp)
                                                 : SpectrumLUT::colorForAmp(gradient.maxAmp, o.theme),
                                         overlay ? 0.07f : 0.14f, slice,
                                         gradient.vertices[i].x, gradient.vertices[i].y, floorY, 1.0f });
                }
                commands.push_back(pathCommand(SpectrumDrawKind::envelopeTrace, slice, gradient, 1.0f,
                                                overlay ? SpectrumLUT::overlayColorForAmp(gradient.maxAmp)
                                                        : SpectrumLUT::colorForAmp(gradient.maxAmp, o.theme),
                                                (overlay ? 0.12f : 0.16f) + 0.40f * gradient.maxAmp));
            }
        }
    }

    static void emitOnsets(std::vector<SpectrumDrawCommand>& commands, const SpectralTraceSnapshot& snapshot, UiRect plot)
    {
        const auto onsets = snapshot.onsets.empty() ? legacyOnsets(snapshot) : snapshot.onsets;
        for (std::size_t i = 0; i < onsets.size(); ++i)
        {
            const auto x = plot.x + plot.width * std::clamp(onsets[i].norm, 0.0f, 1.0f);
            commands.push_back({ SpectrumDrawKind::onsetMarker, { x, plot.y, 2.0f, plot.height },
                                 SpectralCanvasTokens::fixed().selection.argb, 0.52f, static_cast<int>(i) });
            for (const auto edge : { onsets[i].preNorm, onsets[i].peakNorm })
                commands.push_back({ SpectrumDrawKind::onsetHandle,
                                     { plot.x + plot.width * std::clamp(edge, 0.0f, 1.0f) - 2.0f, plot.y, 4.0f, plot.height },
                                     SpectralCanvasTokens::fixed().selection.argb, 0.30f, static_cast<int>(i) });
        }
    }

    static void emitResidualSelection(std::vector<SpectrumDrawCommand>& commands,
                                      const SpectralTraceSnapshot& snapshot, UiRect plot,
                                      const SpectrumRenderOptions& options)
    {
        if (options.selectedBands.empty() || snapshot.residualBands.empty()
            || snapshot.frameCount == 0
            || (snapshot.mode != SpectralTraceMode::residual
                && snapshot.mode != SpectralTraceMode::isometricResidual))
            return;
        const auto first = std::clamp(options.residualFirstFrame, 0,
                                      static_cast<int>(snapshot.frameCount) - 1);
        const auto last = std::clamp(std::max(first, options.residualLastFrame), first,
                                     static_cast<int>(snapshot.frameCount) - 1);
        const auto timeRange = SpectrumGeometry::visibleTimeRange(options.viewport);
        const auto timeAtFrame = [&](int frame) {
            const auto norm = snapshot.frameCount <= 1 ? 0.0f
                : static_cast<float>(frame) / static_cast<float>(snapshot.frameCount - 1);
            return plot.x + plot.width * (norm - timeRange.start)
                / std::max(1.0e-6f, timeRange.length());
        };
        const auto x = timeAtFrame(first);
        const auto right = timeAtFrame(last);
        for (const auto bandId : options.selectedBands)
        {
            const auto it = std::find_if(snapshot.residualBands.begin(), snapshot.residualBands.end(),
                [bandId](const auto& band) { return band.bandIndex == bandId; });
            if (it == snapshot.residualBands.end())
                continue;
            const auto yRange = residualYRange(*it, snapshot, plot, options.viewport);
            commands.push_back({ SpectrumDrawKind::selectionMarker,
                                 { std::min(x, right), yRange.start,
                                   std::max(1.0f, std::abs(right - x)),
                                   std::max(1.0f, yRange.length()) },
                                 SpectralCanvasTokens::fixed().selection.argb, 0.72f, bandId });
        }
    }

    static void emitAxesAndChrome(std::vector<SpectrumDrawCommand>& commands, const SpectralTraceSnapshot& snapshot,
                                  UiRect screen, UiRect plot, const SpectrumRenderOptions& options,
                                  const SpectralCanvasTokens& tokens)
    {
        const auto timeRange = SpectrumGeometry::visibleTimeRange(options.viewport);
        const auto duration = std::max(0.0, snapshot.durationSeconds);
        const auto visibleSeconds = duration * timeRange.length();
        const auto decimals = visibleSeconds < 2.0 ? 2 : visibleSeconds < 20.0 ? 1 : 0;
        for (int i = 0; i <= 16; i += 4)
        {
            const auto normalized = static_cast<float>(i) / 16.0f;
            const auto x = plot.x + plot.width * normalized;
            commands.push_back({ SpectrumDrawKind::axisTick, { x, plot.y + plot.height - 5.0f, 1.0f, 5.0f },
                                 tokens.axisFrame.argb, 0.35f, i });
            std::ostringstream label;
            label << std::fixed << std::setprecision(decimals)
                  << (duration > 0.0 ? (timeRange.start + normalized * timeRange.length()) * duration
                                     : normalized) << 's';
            const auto labelX = i == 0 ? x + 3.0f : i == 16 ? x - 47.0f : x - 22.0f;
            commands.push_back({ SpectrumDrawKind::axisLabel, { labelX, plot.y + plot.height - 17.0f,
                                                                  44.0f, 10.0f },
                                 0xff6fa3b0u, 0.75f, i, 0.0f, 0.0f, 0.0f, 1.0f, {}, {}, {}, {},
                                 false, false, label.str() });
        }
        const UiRect legend { plot.x + plot.width - 140.0f, plot.y + 8.0f, 132.0f, 38.0f };
        if (legend.x >= plot.x + 4.0f)
            commands.push_back({ SpectrumDrawKind::ampLegend, legend, tokens.background.argb, 0.78f, -1 });
        if (SpectrumGeometry::showsBandLabelGutter(snapshot.mode, snapshot.residualBands.size(), snapshot.frameCount))
            for (std::size_t i = 0; i < snapshot.residualBands.size(); ++i)
            {
                const auto& band = snapshot.residualBands[i];
                const auto y = plot.y + plot.height *
                    (1.0f - (static_cast<float>(i) + 0.5f) /
                             static_cast<float>(std::max<std::size_t>(1, snapshot.residualBands.size())));
                std::ostringstream label;
                label << 'B' << std::setw(2) << std::setfill('0') << std::max(0, band.bandIndex);
                commands.push_back({ SpectrumDrawKind::bandLabel, { screen.x, y - 6.0f, 34.0f, 12.0f },
                                     0xff6fa3b0u, 0.75f, band.bandIndex, 0.0f, 0.0f, 0.0f, 1.0f,
                                     {}, {}, {}, {}, false, false, label.str() });
            }
        else if (SpectrumGeometry::showsFreqLabelGutter(snapshot.mode, snapshot.frameCount))
            for (int i = 0; i <= 4; ++i)
            {
                const auto normalized = static_cast<double>(i) / 4.0;
                const auto minHz = std::max(1.0, snapshot.minFrequencyHz);
                const auto maxHz = std::max(minHz, snapshot.maxFrequencyHz);
                const auto hz = minHz * std::pow(maxHz / minHz, normalized);
                std::ostringstream label;
                label << std::fixed << std::setprecision(hz >= 1000.0 ? 0 : 0) << hz;
                const auto y = plot.y + plot.height * (1.0f - static_cast<float>(normalized));
                commands.push_back({ SpectrumDrawKind::freqLabel, { screen.x, y - 6.0f, 34.0f, 12.0f },
                                     0xff6fa3b0u, 0.75f, i, 0.0f, 0.0f, 0.0f, 1.0f,
                                     {}, {}, {}, {}, false, false, label.str() });
            }
    }

    static void emitNavigationChrome(std::vector<SpectrumDrawCommand>& commands, UiRect bounds,
                                     SpectralTraceMode mode, const SpectrumRenderOptions& options,
                                     const SpectralCanvasTokens& tokens)
    {
        const auto chrome = SpectrumChromeLayout::layout(bounds, mode);
        const auto time = SpectrumGeometry::visibleTimeRange(options.viewport);
        const auto frequency = SpectrumGeometry::visibleFrequencyRange(options.viewport);
        const auto clampRange = [](SpectrumRange range) {
            range.start = std::clamp(range.start, 0.0f, 1.0f);
            range.end = std::clamp(range.end, range.start, 1.0f);
            return range;
        };
        const auto timeRange = clampRange(time);
        const auto frequencyRange = clampRange(frequency);
        const auto track = tokens.axisFrame.argb;
        const auto thumb = tokens.hover.argb;
        commands.push_back({ SpectrumDrawKind::zoomBar, chrome.zoomBar, track, 0.34f, 0 });
        commands.push_back({ SpectrumDrawKind::zoomBar,
                             { chrome.zoomBar.x + 8.0f + 188.0f * timeRange.start,
                               chrome.zoomBar.y + 9.0f,
                               std::max(8.0f, 188.0f * timeRange.length()), 8.0f },
                             thumb, 0.78f, 1 });
        commands.push_back({ SpectrumDrawKind::timeScrollbar, chrome.timeScrollbar, track, 0.34f, 0 });
        commands.push_back({ SpectrumDrawKind::timeScrollbar,
                             { chrome.timeScrollbar.x + chrome.timeScrollbar.width * timeRange.start,
                               chrome.timeScrollbar.y,
                               std::max(8.0f, chrome.timeScrollbar.width * timeRange.length()),
                               chrome.timeScrollbar.height },
                             thumb, 0.72f, 1 });
        commands.push_back({ SpectrumDrawKind::frequencyScrollbar, chrome.frequencyScrollbar,
                             track, 0.34f, 0 });
        commands.push_back({ SpectrumDrawKind::frequencyScrollbar,
                             { chrome.frequencyScrollbar.x,
                               chrome.frequencyScrollbar.y + chrome.frequencyScrollbar.height * (1.0f - frequencyRange.end),
                               chrome.frequencyScrollbar.width,
                               std::max(8.0f, chrome.frequencyScrollbar.height * frequencyRange.length()) },
                             thumb, 0.72f, 1 });
    }

    [[nodiscard]] static UiRect boundsOf(const std::vector<UiPoint>& points) noexcept
    {
        if (points.empty()) return {};
        float left = points.front().x, right = left, top = points.front().y, bottom = top;
        for (const auto& p : points) { left = std::min(left, p.x); right = std::max(right, p.x); top = std::min(top, p.y); bottom = std::max(bottom, p.y); }
        return { left, top, right - left, bottom - top };
    }

    [[nodiscard]] static bool rectsIntersect(UiRect a, UiRect b) noexcept
    {
        return a.x < b.x + b.width && a.x + a.width > b.x
            && a.y < b.y + b.height && a.y + a.height > b.y;
    }

    [[nodiscard]] static std::vector<SpectralOnset> legacyOnsets(const SpectralTraceSnapshot& snapshot)
    {
        std::vector<SpectralOnset> result; result.reserve(snapshot.onsetNorms.size());
        for (const auto value : snapshot.onsetNorms) result.push_back({ value, value, value, 0.0f });
        return result;
    }
};

} // namespace ats::ui
