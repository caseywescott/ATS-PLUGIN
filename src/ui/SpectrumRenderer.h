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
    zoomBar, timeScrollbar, frequencyScrollbar, toolbarButton,
    // Appended so existing numeric kinds stay stable.
    floorShadow, envelopeHalo, smearnovCurve, gutterTick, infoText, badge
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
    // `filled`: `vertices` is a convex polygon to fill rather than a stroke.
    // An envelopeFill with vertices is a strip: each top vertex is filled
    // straight down to the floor line through (front.x, floorY) and
    // (back.x, floorY2).  Either fill fades from `alpha` at gradStart to
    // transparent at gradEnd when those differ.
    bool filled = false;
    float floorY2 = 0.0f;
    // Colour ramp for per-stop colours: 0 = amplitude LUT, 1 = source B ramp.
    int colorRamp = 0;
    // Text kinds: point size (0 = painter default) and 0/1/2 = left/centre/right.
    float fontSize = 0.0f;
    int justify = 0;
    [[nodiscard]] bool isStrip() const noexcept
    { return kind == SpectrumDrawKind::envelopeFill && vertices.size() >= 2; }
    [[nodiscard]] bool isPath() const noexcept
    { return vertices.size() >= 2 && !filled && !isStrip(); }
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
    int hoveredBand = -1;
    // Envelope edit range in absolute normalised time/frequency.  It is drawn
    // through the viewport (a slab in the isometric envelope views).
    bool envelopeEditing = false;
    float envelopeTimeStart = 0.0f;
    float envelopeTimeEnd = 1.0f;
    float envelopeFrequencyStart = 0.0f;
    float envelopeFrequencyEnd = 1.0f;
    // Time... edit range, inclusive frames; -1 = none.  Planar views only.
    int timeSelectionFirstFrame = -1;
    int timeSelectionLastFrame = -1;
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
    { return overlayAmpColour(ampNorm).argb; }

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
    // Visage draws every path segment as one quad, and a batch is indexed with
    // 16-bit indices (at most 16383 quads). Large models in data-scaled modes
    // (e.g. isometric at 400 partials x 2000 frames: ~100k segments) would
    // overflow both that and bgfx's per-frame transient buffer, silently
    // dropping the rest of the editor. The painter thins vertices by this
    // stride so a frame stays near the budget; endpoints are always kept.
    static constexpr std::size_t kMaxPathSegmentsPerFrame = 12000;

    [[nodiscard]] static std::size_t pathSegmentStride(
        const std::vector<SpectrumDrawCommand>& commands,
        std::size_t budget = kMaxPathSegmentsPerFrame) noexcept
    {
        // Fills are single path shapes and do not count toward the quad budget.
        std::size_t total = 0;
        for (const auto& command : commands)
            if (command.isPath())
                total += command.vertices.size() - 1;
        if (budget == 0 || total <= budget)
            return 1;
        return (total + budget - 1) / budget;
    }

    [[nodiscard]] static bool requiresPlotClip(const SpectrumDrawCommand& command) noexcept
    {
        if (command.isPath() || command.filled || command.isStrip())
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
        case SpectrumDrawKind::floorShadow:
        case SpectrumDrawKind::envelopeHalo:
        case SpectrumDrawKind::smearnovCurve:
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
        const bool envelopeMode = snapshot.mode == SpectralTraceMode::isometricEnvelope
            || snapshot.mode == SpectralTraceMode::isometricEnvelopeRdc;
        if (iso) emitIsometricBackdrop(commands, plot, options, tokens);
        else emitPlanarBackdrop(commands, plot, tokens);
        if (snapshot.mode == SpectralTraceMode::residual || snapshot.mode == SpectralTraceMode::combined)
            emitResidual(commands, snapshot, plot, options);
        if (snapshot.mode == SpectralTraceMode::combined
            || snapshot.mode == SpectralTraceMode::partials)
            emitPartials(commands, snapshot, plot, options, false);
        if (snapshot.mode == SpectralTraceMode::isometric)
            emitPartials(commands, snapshot, plot, options, true);
        else if (snapshot.mode == SpectralTraceMode::isometricResidual)
            emitIsoResidual(commands, snapshot, plot, options);
        else if (envelopeMode)
        {
            // Source B sits behind source A, on its own ramp and at 75%.
            if (options.overlaySnapshot != nullptr)
                emitEnvelope(commands, *options.overlaySnapshot, snapshot.mode, plot, options, true);
            emitEnvelope(commands, snapshot, snapshot.mode, plot, options, false);
        }
        else if (!snapshot.envelopeNorm.empty())
            emitPlanarEnvelope(commands, snapshot, plot, options);
        // JUCE keeps the Smearnov overlay entirely behind the explicit onset
        // editing toggle.  This includes its detection/threshold curves;
        // publishing the curves must not make them visible by default.
        if (options.showOnsetOverlay && !iso && snapshot.frameCount > 1)
            emitOnsets(commands, snapshot, plot, options);
        emitSelectionOverlay(commands, snapshot, plot, options);
        emitAxesAndChrome(commands, snapshot, screen, plot, options, tokens);
        emitNavigationChrome(commands, bounds, snapshot.mode, options, tokens);
        if (options.showPlayhead)
            emitPlayhead(commands, plot, options, iso, tokens);
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

    // --- primitive helpers -------------------------------------------------

    [[nodiscard]] static SpectrumDrawCommand lineCommand(SpectrumDrawKind kind, UiPoint a, UiPoint b,
                                                         std::uint32_t color, float alpha,
                                                         float width, int index = -1)
    {
        SpectrumDrawCommand c; c.kind = kind; c.color = color; c.alpha = alpha;
        c.thickness = width; c.index = index; c.vertices = { a, b };
        c.bounds = boundsOf(c.vertices); return c;
    }

    [[nodiscard]] static SpectrumDrawCommand polygonCommand(SpectrumDrawKind kind,
                                                            std::vector<UiPoint> points,
                                                            std::uint32_t color, float alpha,
                                                            int index = -1)
    {
        SpectrumDrawCommand c; c.kind = kind; c.color = color; c.alpha = alpha; c.index = index;
        c.filled = true; c.vertices = std::move(points); c.bounds = boundsOf(c.vertices); return c;
    }

    [[nodiscard]] static SpectrumDrawCommand rectCommand(SpectrumDrawKind kind, UiRect r,
                                                         std::uint32_t color, float alpha,
                                                         int index = -1)
    {
        return polygonCommand(kind, { { r.x, r.y }, { r.x + r.width, r.y },
                                      { r.x + r.width, r.y + r.height }, { r.x, r.y + r.height } },
                              color, alpha, index);
    }

    /** A filled area from `top` straight down to the line through the floor ends. */
    [[nodiscard]] static SpectrumDrawCommand stripCommand(std::vector<UiPoint> top, float floorFirst,
                                                          float floorLast, std::uint32_t color,
                                                          float alpha, UiPoint fadeFrom, UiPoint fadeTo,
                                                          int index)
    {
        SpectrumDrawCommand c; c.kind = SpectrumDrawKind::envelopeFill; c.color = color;
        c.alpha = alpha; c.index = index; c.floorY = floorFirst; c.floorY2 = floorLast;
        c.gradStart = fadeFrom; c.gradEnd = fadeTo; c.vertices = std::move(top);
        auto bounds = boundsOf(c.vertices);
        const auto floorBottom = std::max(floorFirst, floorLast);
        bounds.height = std::max(bounds.height, floorBottom - bounds.y);
        c.bounds = bounds; return c;
    }

    [[nodiscard]] static SpectrumDrawCommand textCommand(SpectrumDrawKind kind, UiRect r,
                                                         std::string text, std::uint32_t color,
                                                         float alpha, float fontSize, int justify,
                                                         int index = -1)
    {
        SpectrumDrawCommand c; c.kind = kind; c.bounds = r; c.color = color; c.alpha = alpha;
        c.index = index; c.label = std::move(text); c.fontSize = fontSize; c.justify = justify;
        return c;
    }

    template <typename AlphaForStop>
    static void setStopAlpha(GradientTrace& trace, AlphaForStop&& alphaFor)
    {
        for (auto& stop : trace.stops)
            stop.alpha = std::clamp(alphaFor(stop), 0.0f, 1.0f);
    }

    [[nodiscard]] static std::string formatFixed(double value, int decimals)
    {
        std::ostringstream out;
        out << std::fixed << std::setprecision(decimals) << value;
        return out.str();
    }

    [[nodiscard]] static float xForTime(float timeNorm, UiRect plot, SpectrumRange time) noexcept
    { return plot.x + plot.width * (timeNorm - time.start) / std::max(1.0e-6f, time.length()); }

    [[nodiscard]] static bool bandSelected(const SpectrumRenderOptions& o, int band) noexcept
    { return std::find(o.selectedBands.begin(), o.selectedBands.end(), band) != o.selectedBands.end(); }

    // --- residual ------------------------------------------------------------

    static void emitResidual(std::vector<SpectrumDrawCommand>& commands, const SpectralTraceSnapshot& snapshot,
                             UiRect plot, const SpectrumRenderOptions& o)
    {
        // Residual is context, not signal: a soft filled ridge per band,
        // brighter only when the residual view is in the foreground.
        const bool foreground = snapshot.mode == SpectralTraceMode::residual;
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
            const auto energyAt = [&](int index) {
                return band.energyNorm[static_cast<std::size_t>(
                    std::clamp(index, 0, static_cast<int>(columns) - 1))];
            };
            std::vector<TraceVertex> ridge;
            ridge.reserve(columns);
            float peak = 0.0f;
            for (std::size_t c = 0; c < columns; ++c)
            {
                const auto ci = static_cast<int>(c);
                const auto energy = std::clamp(
                    0.25f * (energyAt(ci - 1) + 2.0f * energyAt(ci) + energyAt(ci + 1)), 0.0f, 1.0f);
                const auto x = xForTime(SpectralTraceBuilder::residualColumnTimeNorm(
                                            c, columns, snapshot.frameCount), plot, timeRange);
                if (x < plot.x - 24.0f || x > plot.x + plot.width + 24.0f)
                    continue;
                ridge.push_back({ { x, baseY - energy * height * 0.92f }, energy, 1.0f });
                peak = std::max(peak, energy);
            }
            if (ridge.size() < 2)
                continue;

            if (peak > 0.005f)
            {
                auto top = ridge;
                decimateInPlace(top, 0.5f);
                std::vector<UiPoint> topPoints;
                topPoints.reserve(top.size());
                for (const auto& vertex : top)
                    topPoints.push_back(vertex.pos);
                commands.push_back(stripCommand(std::move(topPoints), baseY, baseY,
                                                SpectrumLUT::colorForAmp(peak, o.theme),
                                                foreground ? 0.16f : 0.06f,
                                                { plot.x, baseY - height }, { plot.x, baseY },
                                                band.bandIndex));
            }
            if (foreground)
                commands.push_back(lineCommand(SpectrumDrawKind::residualBand,
                                               { plot.x, std::round(baseY) },
                                               { plot.x + plot.width, std::round(baseY) },
                                               0xff53d6c2u, 0.10f, 1.0f, band.bandIndex));
            auto gradient = buildGradientTrace(ridge);
            setStopAlpha(gradient, [foreground](const TraceStop& stop) {
                const auto energy = std::min(1.0f, stop.amp);
                return foreground ? 0.34f + energy * 0.56f : 0.10f + energy * 0.16f;
            });
            commands.push_back(pathCommand(SpectrumDrawKind::residualBand, band.bandIndex, gradient,
                                           foreground ? 1.1f : 1.0f,
                                           SpectrumLUT::colorForAmp(gradient.maxAmp, o.theme), 1.0f));
        }
    }

    // --- partials ------------------------------------------------------------

    static void emitHarmonicGuides(std::vector<SpectrumDrawCommand>& commands,
                                   const SpectralTraceSnapshot& snapshot, UiRect plot,
                                   const SpectrumRenderOptions& o)
    {
        // With one partial selected, mark integer multiples of its mean
        // frequency so (in)harmonicity is visible at a glance.
        int selectedIndex = -1;
        if (o.selectedPartials.size() == 1)
            selectedIndex = o.selectedPartials.front();
        else if (o.selectedPartials.empty() && o.selectedPartial >= 0)
            selectedIndex = o.selectedPartial;
        const auto selectedTrace = std::find_if(snapshot.partials.begin(), snapshot.partials.end(),
            [selectedIndex](const auto& trace) { return trace.partialIndex == selectedIndex; });
        if (selectedTrace == snapshot.partials.end() || !(selectedTrace->meanFrequencyHz > 0.0))
            return;
        const auto frequencyRange = SpectrumGeometry::visibleFrequencyRange(o.viewport);
        const auto minHz = std::max(1.0, snapshot.minFrequencyHz);
        const auto maxHz = std::max(minHz * 1.2, snapshot.maxFrequencyHz);
        const auto logRange = std::log(maxHz / minHz);
        const auto selection = SpectralCanvasTokens::fixed().selection.argb;
        for (int harmonic = 2; harmonic <= 32; ++harmonic)
        {
            const auto frequencyHz = selectedTrace->meanFrequencyHz * harmonic;
            if (frequencyHz > maxHz)
                break;
            const auto frequencyNorm = static_cast<float>(std::log(frequencyHz / minHz) / logRange);
            const auto yNorm = (frequencyNorm - frequencyRange.start)
                / std::max(1.0e-6f, frequencyRange.length());
            if (yNorm < 0.0f || yNorm > 1.0f)
                continue;
            const auto y = plot.y + plot.height - plot.height * yNorm;
            for (float x = plot.x; x < plot.x + plot.width; x += 10.0f)
                commands.push_back(lineCommand(SpectrumDrawKind::axis, { x, y },
                                               { std::min(x + 5.0f, plot.x + plot.width), y },
                                               selection, 0.14f, 0.8f, harmonic));
            commands.push_back(textCommand(SpectrumDrawKind::axisLabel,
                                           { plot.x + 4.0f, y - 11.0f, 26.0f, 10.0f },
                                           std::to_string(harmonic) + "f", selection, 0.38f,
                                           10.0f, 0, harmonic));
        }
    }

    static void emitPartials(std::vector<SpectrumDrawCommand>& commands, const SpectralTraceSnapshot& snapshot,
                             UiRect plot, const SpectrumRenderOptions& o, bool iso)
    {
        const auto timeRange = SpectrumGeometry::visibleTimeRange(o.viewport);
        const auto frequencyRange = SpectrumGeometry::visibleFrequencyRange(o.viewport);
        if (!iso)
            emitHarmonicGuides(commands, snapshot, plot, o);

        // The isometric projection has no screen-edge clip that maps to the
        // visible range: a point outside it projects onto the floor beside
        // the grid.  Cull in data space (with a small pad) instead.
        const auto timePad = timeRange.length() * 0.025f;
        const auto frequencyPad = frequencyRange.length() * 0.04f;
        const auto visible = [&](const SpectralTracePoint& point) {
            if (!point.active) return false;
            if (!iso) return true;
            return point.timeNorm >= timeRange.start - timePad
                && point.timeNorm <= timeRange.end + timePad
                && point.freqNorm >= frequencyRange.start - frequencyPad
                && point.freqNorm <= frequencyRange.end + frequencyPad;
        };
        // Depth: later time draws brighter (and later, below).
        const auto shadeForTime = [&](float timeNorm) {
            return 0.55f + 0.45f * std::clamp((timeNorm - timeRange.start)
                                                  / std::max(1.0e-6f, timeRange.length()),
                                              0.0f, 1.0f);
        };
        constexpr float pathMargin = 8.0f;
        const UiRect pathBounds { plot.x - pathMargin, plot.y - pathMargin,
                                  plot.width + pathMargin * 2.0f, plot.height + pathMargin * 2.0f };

        std::vector<const SpectralTrace*> ordered;
        ordered.reserve(snapshot.partials.size());
        for (const auto& trace : snapshot.partials)
            ordered.push_back(&trace);
        if (iso)
        {
            // Back-to-front by mean active time (painter's algorithm).
            std::vector<std::pair<float, const SpectralTrace*>> keyed;
            keyed.reserve(ordered.size());
            for (const auto* trace : ordered)
            {
                float sum = 0.0f; int count = 0;
                for (const auto& point : trace->points)
                    if (point.active) { sum += point.timeNorm; ++count; }
                keyed.push_back({ count > 0 ? sum / static_cast<float>(count) : 0.0f, trace });
            }
            std::stable_sort(keyed.begin(), keyed.end(),
                             [](const auto& a, const auto& b) { return a.first < b.first; });
            for (std::size_t i = 0; i < keyed.size(); ++i)
                ordered[i] = keyed[i].second;

            // Floor shadows anchor each ridge to its time/frequency cell.
            for (const auto* trace : ordered)
            {
                std::vector<TraceVertex> run;
                float peak = 0.0f, shadeSum = 0.0f;
                const auto flushShadow = [&] {
                    const auto count = run.size();
                    if (count >= 2 && !(count <= 4 && peak < 0.3f))
                    {
                        const auto amp = std::min(1.0f, peak);
                        const auto shade = shadeSum / static_cast<float>(count);
                        for (auto& visibleRun : clipTraceRuns(run, pathBounds))
                        {
                            SpectrumDrawCommand c;
                            c.kind = SpectrumDrawKind::floorShadow;
                            c.index = trace->partialIndex;
                            c.color = SpectrumLUT::colorForAmp(amp, o.theme);
                            c.alpha = (0.05f + 0.10f * amp) * shade;
                            c.thickness = 1.0f;
                            for (const auto& vertex : visibleRun)
                                c.vertices.push_back(vertex.pos);
                            c.bounds = boundsOf(c.vertices);
                            commands.push_back(std::move(c));
                        }
                    }
                    run.clear(); peak = shadeSum = 0.0f;
                };
                for (const auto& point : trace->points)
                {
                    if (!visible(point)) { flushShadow(); continue; }
                    run.push_back({ SpectrumGeometry::positionForIsoCoords(
                                        point.timeNorm, point.freqNorm, 0.0f, plot, o.viewport),
                                    point.ampNorm, 1.0f });
                    peak = std::max(peak, point.ampNorm);
                    shadeSum += shadeForTime(point.timeNorm);
                }
                flushShadow();
            }
        }

        for (int pass = 0; pass < 2; ++pass)
            for (const auto* tracePtr : ordered)
            {
                const auto& trace = *tracePtr;
                const auto isSelected = selected(o, trace.partialIndex);
                if ((pass == 0) == isSelected) continue;
                const auto isHovered = trace.partialIndex == o.hoveredPartial;
                std::vector<TraceVertex> run;
                auto flush = [&]() {
                    if (run.size() < 2) { run.clear(); return; }
                    float peak = 0.0f; for (const auto& v : run) peak = std::max(peak, v.amp);
                    // Frame-scale low-amp fragments render as confetti, not information.
                    if (run.size() <= 4 && peak < 0.3f && !isSelected && !isHovered) { run.clear(); return; }
                    smoothInPlace(run, iso ? 3 : 2); decimateInPlace(run);
                    auto visibleRuns = clipTraceRuns(run, pathBounds);
                    run.clear();
                    for (const auto& visibleRun : visibleRuns)
                    {
                        auto gradient = buildGradientTrace(visibleRun);
                        if (gradient.vertices.empty())
                            continue;
                        if (isSelected || isHovered)
                        {
                            const auto base = isSelected ? SpectralCanvasTokens::fixed().selection.argb
                                                         : SpectralCanvasTokens::fixed().hover.argb;
                            // Highlights are solid: the hover/selection colour
                            // replaces the amplitude ramp.
                            gradient.stops.clear();
                            commands.push_back(pathCommand(SpectrumDrawKind::glowHalo,
                                                           trace.partialIndex, gradient,
                                                           isSelected ? (iso ? 6.4f : 6.0f)
                                                                      : (iso ? 4.5f : 4.0f),
                                                           base, iso ? 0.22f : 0.18f, true));
                            commands.push_back(pathCommand(
                                isSelected ? SpectrumDrawKind::selectedPartial
                                           : SpectrumDrawKind::partialTrace,
                                trace.partialIndex, gradient, 1.7f, base,
                                std::min(1.0f, (iso ? 0.78f : 0.72f)
                                                   + gradient.maxAmp * (iso ? 0.16f : 0.2f))));
                            continue;
                        }
                        // Constant width: loudness lives in the colour and
                        // per-stop alpha ramp, depth in the iso shade.
                        setStopAlpha(gradient, [iso](const TraceStop& stop) {
                            const auto amp = std::min(1.0f, stop.amp);
                            return iso ? (0.28f + amp * 0.58f) * std::clamp(stop.shade, 0.0f, 1.0f)
                                       : 0.28f + amp * 0.62f;
                        });
                        commands.push_back(pathCommand(SpectrumDrawKind::partialTrace,
                                                       trace.partialIndex, gradient, 1.0f,
                                                       SpectrumLUT::colorForAmp(std::min(1.0f, peak),
                                                                                o.theme),
                                                       1.0f));
                    }
                };
                for (const auto& point : trace.points)
                {
                    if (!visible(point))
                    {
                        flush();
                        continue;
                    }
                    const auto p = iso ? SpectrumGeometry::positionForIsoCoords(point.timeNorm, point.freqNorm,
                                           point.ampNorm, plot, o.viewport)
                                      : SpectrumGeometry::positionForPoint(point, plot, o.viewport);
                    run.push_back({ p, std::clamp(point.ampNorm, 0.0f, 2.0f),
                                    iso ? shadeForTime(point.timeNorm) : 1.0f });
                }
                flush();
            }
    }

    static void emitIsoResidual(std::vector<SpectrumDrawCommand>& commands, const SpectralTraceSnapshot& snapshot,
                                UiRect plot, const SpectrumRenderOptions& o)
    {
        const auto timeRange = SpectrumGeometry::visibleTimeRange(o.viewport);
        const auto frequencyRange = SpectrumGeometry::visibleFrequencyRange(o.viewport);
        const auto minHz = std::max(1.0, snapshot.minFrequencyHz);
        const auto maxHz = std::max(minHz * 1.2, snapshot.maxFrequencyHz);
        const auto logRange = std::max(1.0e-6, std::log(maxHz / minHz));
        const auto normForHz = [&](double hz) {
            return static_cast<float>(std::clamp(
                std::log(std::clamp(std::max(1.0, hz), minHz, maxHz) / minHz) / logRange, 0.0, 1.0));
        };
        const auto shadeForTime = [&](float timeNorm) {
            return 0.55f + 0.45f * std::clamp((timeNorm - timeRange.start)
                                                  / std::max(1.0e-6f, timeRange.length()),
                                              0.0f, 1.0f);
        };
        const auto tokens = SpectralCanvasTokens::fixed();
        for (const auto& band : snapshot.residualBands)
        {
            const auto frequencyNorm = std::clamp(
                0.5f * (normForHz(band.lowHz) + normForHz(band.highHz)), 0.0f, 1.0f);
            if (frequencyNorm < frequencyRange.start - frequencyRange.length() * 0.05f
                || frequencyNorm > frequencyRange.end + frequencyRange.length() * 0.05f)
                continue;
            const auto isSelected = bandSelected(o, band.bandIndex);
            const auto isHovered = band.bandIndex == o.hoveredBand;
            const auto columns = band.energyNorm.size();
            if (columns <= 1)
                continue;
            const auto energyAt = [&](int index) {
                return band.energyNorm[static_cast<std::size_t>(
                    std::clamp(index, 0, static_cast<int>(columns) - 1))];
            };
            std::vector<TraceVertex> ridge;
            std::vector<UiPoint> base;
            float peak = 0.0f;
            for (std::size_t c = 0; c < columns; ++c)
            {
                const auto timeNorm = SpectralTraceBuilder::residualColumnTimeNorm(
                    c, columns, snapshot.frameCount);
                if (timeNorm < timeRange.start - timeRange.length() * 0.03f
                    || timeNorm > timeRange.end + timeRange.length() * 0.03f)
                    continue;
                const auto ci = static_cast<int>(c);
                const auto e = 0.25f * (energyAt(ci - 1) + 2.0f * energyAt(ci) + energyAt(ci + 1));
                const auto energy = std::clamp(e, 0.0f, 1.0f);
                const auto shade = shadeForTime(timeNorm);
                ridge.push_back({ SpectrumGeometry::positionForIsoCoords(
                                      timeNorm, frequencyNorm, std::clamp(e * 1.15f, 0.0f, 1.15f),
                                      plot, o.viewport),
                                  energy, shade });
                base.push_back(SpectrumGeometry::positionForIsoCoords(timeNorm, frequencyNorm, 0.0f,
                                                                      plot, o.viewport));
                peak = std::max(peak, energy);
            }
            if (base.size() < 2)
                continue;
            // Silent bands would render as straight lines lying on the floor.
            if (peak < 0.02f && !isSelected && !isHovered)
                continue;

            // Filled ridge between crest and baseline.  The fade runs
            // perpendicular to the diagonal baseline and reaches zero at 30%
            // of the crest height, so the floor edge never shows as a line.
            const auto baseStart = base.front();
            const auto dx = base.back().x - baseStart.x, dy = base.back().y - baseStart.y;
            const auto length = std::hypot(dx, dy);
            if (length > 1.0f)
            {
                UiPoint normal { dy / length, -dx / length };
                const auto crestHeight = [&] {
                    float result = 0.0f;
                    for (const auto& v : ridge)
                        result = std::max(result, (v.pos.x - baseStart.x) * normal.x
                                                      + (v.pos.y - baseStart.y) * normal.y);
                    return result;
                };
                auto crest = crestHeight();
                if (crest < 0.5f) { normal = { -normal.x, -normal.y }; crest = crestHeight(); }
                if (crest > 2.0f)
                {
                    std::vector<UiPoint> top;
                    top.reserve(ridge.size());
                    for (const auto& vertex : ridge)
                        top.push_back(vertex.pos);
                    commands.push_back(stripCommand(
                        std::move(top), base.front().y, base.back().y,
                        isSelected ? tokens.selection.argb : SpectrumLUT::colorForAmp(peak, o.theme),
                        isSelected ? 0.26f : (isHovered ? 0.20f : 0.12f),
                        { baseStart.x + normal.x * crest, baseStart.y + normal.y * crest },
                        { baseStart.x + normal.x * crest * 0.30f, baseStart.y + normal.y * crest * 0.30f },
                        band.bandIndex));
                }
            }

            auto gradient = buildGradientTrace(ridge);
            if (isSelected || isHovered)
            {
                // Band baselines are straight lines; only show them as an
                // affordance, or they read as a grid across the floor.
                SpectrumDrawCommand baseline;
                baseline.kind = SpectrumDrawKind::axis;
                baseline.color = tokens.axisFrame.argb;
                baseline.alpha = isHovered ? 0.58f : 0.44f;
                baseline.thickness = isHovered ? 2.2f : 1.7f;
                baseline.index = band.bandIndex;
                baseline.vertices = base;
                baseline.bounds = boundsOf(baseline.vertices);
                commands.push_back(std::move(baseline));
                const auto colour = isSelected ? tokens.selection.argb : tokens.hover.argb;
                gradient.stops.clear();
                commands.push_back(pathCommand(SpectrumDrawKind::glowHalo, band.bandIndex, gradient,
                                               isSelected ? 6.0f : 4.0f, colour, 0.24f, true));
                commands.push_back(pathCommand(SpectrumDrawKind::residualBand, band.bandIndex, gradient,
                                               1.7f, colour, std::min(1.0f, 0.72f + peak * 0.2f)));
                continue;
            }
            setStopAlpha(gradient, [](const TraceStop& stop) {
                const auto energy = std::min(1.0f, stop.amp);
                // Quiet stretches sit on the ground plane; fade them out.
                const auto gate = std::clamp((energy - 0.03f) / 0.10f, 0.0f, 1.0f);
                return (0.30f + energy * 0.58f) * gate * std::clamp(stop.shade, 0.0f, 1.0f);
            });
            commands.push_back(pathCommand(SpectrumDrawKind::residualBand, band.bandIndex, gradient,
                                           1.2f, SpectrumLUT::colorForAmp(peak, o.theme), 1.0f));
        }
    }

    static void emitPlanarEnvelope(std::vector<SpectrumDrawCommand>& commands,
                                   const SpectralTraceSnapshot& snapshot, UiRect plot,
                                   const SpectrumRenderOptions& o)
    {
        if (snapshot.envelopeNorm.size() < 2) return;
        const auto timeRange = SpectrumGeometry::visibleTimeRange(o.viewport);
        std::vector<TraceVertex> vertices;
        vertices.reserve(snapshot.envelopeNorm.size());
        for (std::size_t i = 0; i < snapshot.envelopeNorm.size(); ++i)
        {
            const auto t = static_cast<float>(i) / static_cast<float>(snapshot.envelopeNorm.size() - 1);
            const auto a = std::clamp(snapshot.envelopeNorm[i], 0.0f, 1.0f);
            vertices.push_back({ { xForTime(t, plot, timeRange), plot.y + plot.height * (1.0f - a) },
                                 a, 1.0f });
        }
        for (const auto& run : clipTraceRuns(vertices, { plot.x - 8.0f, plot.y - 8.0f,
                                                         plot.width + 16.0f, plot.height + 16.0f }))
        {
            const auto gradient = buildGradientTrace(run);
            if (!gradient.vertices.empty())
                commands.push_back(pathCommand(SpectrumDrawKind::envelopeTrace, -1, gradient, 1.0f,
                                               SpectrumLUT::colorForAmp(gradient.maxAmp, o.theme), 0.78f));
        }
    }

    // --- spectral envelope -------------------------------------------------

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

    /**
        Spectral-envelope waterfall: 30 time slices, each a smoothed envelope
        across the visible frequency range, drawn back to front as a faint
        crest tint, a soft halo and a thin core whose colour follows amplitude
        along the slice.  Depth is alpha only.  The RDC mode fits a Bark-scale
        regularised cepstrum per slice instead of the Gaussian regression.
    */
    static void emitEnvelope(std::vector<SpectrumDrawCommand>& commands, const SpectralTraceSnapshot& snapshot,
                             SpectralTraceMode mode, UiRect plot, const SpectrumRenderOptions& o,
                             bool overlay)
    {
        constexpr int slices = 30, bins = 128;
        constexpr float sigma = 7.5f;
        constexpr int taperBins = 14;
        if (snapshot.partials.empty())
            return;
        const bool rdc = mode == SpectralTraceMode::isometricEnvelopeRdc;
        const auto tr = SpectrumGeometry::visibleTimeRange(o.viewport);
        const auto fr = SpectrumGeometry::visibleFrequencyRange(o.viewport);
        std::vector<float> sliceTimes(static_cast<std::size_t>(slices));
        for (int i = 0; i < slices; ++i)
            sliceTimes[static_cast<std::size_t>(i)] = tr.start + tr.length()
                * static_cast<float>(i) / static_cast<float>(slices - 1);
        // Gaussian regression fades births/deaths; the RDC fit uses only
        // segments with both ends active.
        const auto sliceSamples = sampleTracesAtSliceTimes(snapshot, sliceTimes, !rdc);
        const auto taper = [](int bin, float value) {
            const auto edgeDistance = std::min(bin, bins - 1 - bin);
            if (edgeDistance < taperBins)
                value *= 0.5f - 0.5f * std::cos(3.14159265359f
                                                * static_cast<float>(edgeDistance) / taperBins);
            return value;
        };
        const auto minHz = std::max(1.0, snapshot.minFrequencyHz);
        const auto maxHz = std::max(minHz * 1.1, snapshot.maxFrequencyHz);
        const auto logRange = std::log(maxHz / minHz);
        const auto alphaScale = overlay ? 0.75f : 1.0f;
        std::vector<float> weights, weightedAmplitudes, blurScratch;
        // One slice's envelope estimate across the visible frequency bins.
        const auto estimate = [&](const std::vector<EnvelopeSample>& sliceSample,
                                  std::vector<float>& values) -> bool {
            values.assign(static_cast<std::size_t>(bins), 0.0f);
            if (rdc)
            {
                std::vector<double> frequencies, amplitudes;
                for (const auto& sample : sliceSample)
                {
                    const auto fVisible = (sample.frequencyNorm - fr.start)
                        / std::max(1.0e-6f, fr.length());
                    if (sample.amplitudeNorm > 0.0f && fVisible >= -0.1f && fVisible <= 1.1f)
                    {
                        frequencies.push_back(minHz * std::exp(sample.frequencyNorm * logRange));
                        amplitudes.push_back(sample.amplitudeNorm);
                    }
                }
                if (frequencies.size() < 3)
                    return false;
                const auto fit = rdcDisplay::fitRDC(frequencies, amplitudes, minHz, maxHz);
                if (!fit.valid)
                    return false;
                for (int b = 0; b < bins; ++b)
                {
                    const auto normalized = fr.start + fr.length() * static_cast<double>(b) / (bins - 1);
                    const auto db = fit.evaluate(rdcDisplay::barkNorm(
                        minHz * std::exp(normalized * logRange), minHz, maxHz));
                    values[static_cast<std::size_t>(b)] = taper(
                        b, std::clamp(static_cast<float>((db + 120.0) / 120.0), 0.0f, 1.0f));
                }
            }
            else
            {
                std::vector<std::pair<float, float>> samples;
                for (const auto& sample : sliceSample)
                {
                    const auto fVisible = (sample.frequencyNorm - fr.start)
                        / std::max(1.0e-6f, fr.length());
                    if (sample.amplitudeNorm > 0.0f && fVisible >= -0.1f && fVisible <= 1.1f)
                        samples.emplace_back(fVisible, sample.amplitudeNorm);
                }
                if (samples.empty())
                    return false;
                // Sparse slices widen the kernel so isolated survivors merge
                // into one ridge instead of plateaus.
                // The legacy floor (0.08) widened the kernel up to ~3.5x and
                // flattened sparse slices into a plateau; 0.25 caps it at 2x.
                const auto density = std::clamp(static_cast<float>(samples.size()) / 24.0f, 0.25f, 1.0f);
                const auto effectiveSigma = sigma / std::sqrt(density);
                weights.assign(static_cast<std::size_t>(bins), 0.0f);
                weightedAmplitudes.assign(static_cast<std::size_t>(bins), 0.0f);
                const auto mass = effectiveSigma * 2.50662827463f;
                for (const auto& [fVisible, amp] : samples)
                {
                    // Clamp the bin position itself so the split fraction
                    // stays in [0, 1]; an unclamped top edge would deposit a
                    // negative weight in the second-to-last bin.
                    const auto bin = std::clamp(fVisible * bins, 0.0f, static_cast<float>(bins - 1));
                    const auto first = std::min(bins - 2, static_cast<int>(bin));
                    const auto fraction = bin - static_cast<float>(first);
                    weights[static_cast<std::size_t>(first)] += mass * (1.0f - fraction);
                    weightedAmplitudes[static_cast<std::size_t>(first)] += mass * (1.0f - fraction) * amp;
                    weights[static_cast<std::size_t>(first + 1)] += mass * fraction;
                    weightedAmplitudes[static_cast<std::size_t>(first + 1)] += mass * fraction * amp;
                }
                for (const auto radius : boxRadiiForGauss(effectiveSigma))
                {
                    boxBlurInPlace(weights, radius, blurScratch);
                    boxBlurInPlace(weightedAmplitudes, radius, blurScratch);
                }
                // The 0.02 prior decays the estimate to the floor as kernel
                // weight thins out instead of holding a plateau.
                for (int b = 0; b < bins; ++b)
                    values[static_cast<std::size_t>(b)] = std::max(0.0f, taper(b,
                        weightedAmplitudes[static_cast<std::size_t>(b)]
                            / (weights[static_cast<std::size_t>(b)] + 0.02f)));
            }

            return true;
        };
        // The ridge for one slice, sub-pixel decimated to keep the waterfall
        // well inside the per-frame quad budget.
        const auto ridgeFor = [&](float time, const std::vector<float>& values) {
            std::vector<TraceVertex> ridge;
            ridge.reserve(static_cast<std::size_t>(bins));
            for (int b = 0; b < bins; ++b)
            {
                const auto f = fr.start + fr.length() * static_cast<float>(b) / (bins - 1);
                const auto amp = std::clamp(values[static_cast<std::size_t>(b)], 0.0f, 1.25f);
                ridge.push_back({ SpectrumGeometry::positionForIsoCoords(time, f, amp, plot, o.viewport),
                                  amp, 1.0f });
            }
            decimateInPlace(ridge, 0.5f);
            return buildGradientTrace(ridge);
        };
        std::vector<float> values;
        for (int slice = 0; slice < slices; ++slice)
        {
            const auto time = sliceTimes[static_cast<std::size_t>(slice)];
            if (!estimate(sliceSamples[static_cast<std::size_t>(slice)], values))
                continue;
            auto gradient = ridgeFor(time, values);
            if (gradient.vertices.empty())
                continue;
            const auto depth = static_cast<float>(slice) / static_cast<float>(slices - 1);
            const auto depthAlpha = (0.40f + 0.60f * depth) * alphaScale;
            const auto peakColour = overlay ? overlayAmpColour(std::min(1.0f, gradient.maxAmp)).argb
                                            : SpectrumLUT::colorForAmp(gradient.maxAmp, o.theme);
            const auto floorStart = SpectrumGeometry::positionForIsoCoords(time, fr.start, 0.0f,
                                                                           plot, o.viewport);
            const auto floorEnd = SpectrumGeometry::positionForIsoCoords(time, fr.end, 0.0f,
                                                                         plot, o.viewport);
            // Faint body tint: crest to transparent well above the floor, so
            // overlapping slices never pile up into a dark mass.
            float crestY = gradient.vertices.front().y;
            for (const auto& vertex : gradient.vertices)
                crestY = std::min(crestY, vertex.y);
            const auto floorY = std::max(floorStart.y, floorEnd.y);
            if (floorY - crestY > 2.0f)
                commands.push_back(stripCommand(gradient.vertices, floorStart.y, floorEnd.y,
                                                peakColour, 0.10f * depthAlpha,
                                                { floorStart.x, crestY },
                                                { floorStart.x, crestY + (floorY - crestY) * 0.65f },
                                                slice));
            for (auto& stop : gradient.stops)
                stop.fade = 1.0f;
            auto halo = gradient;
            setStopAlpha(halo, [depthAlpha](const TraceStop&) { return depthAlpha * 0.20f; });
            auto haloCommand = pathCommand(SpectrumDrawKind::envelopeHalo, slice, halo, 2.2f,
                                           peakColour, 1.0f);
            haloCommand.colorRamp = overlay ? 1 : 0;
            commands.push_back(std::move(haloCommand));
            setStopAlpha(gradient, [depthAlpha](const TraceStop&) { return depthAlpha; });
            auto core = pathCommand(SpectrumDrawKind::envelopeTrace, slice, gradient,
                                    0.9f + 0.4f * depth, peakColour, 1.0f);
            core.colorRamp = overlay ? 1 : 0;
            commands.push_back(std::move(core));
        }

        // The envelope at the playhead: the slice you are hearing, drawn on
        // top in full amplitude colour with a playhead-tinted halo.
        const auto playhead = o.playheadNorm;
        if (!overlay && o.showPlayhead && std::isfinite(playhead)
            && playhead >= tr.start && playhead <= tr.end)
        {
            const auto now = sampleTracesAtSliceTimes(snapshot, { playhead }, !rdc);
            if (estimate(now.front(), values))
            {
                auto gradient = ridgeFor(playhead, values);
                if (!gradient.vertices.empty())
                {
                    for (auto& stop : gradient.stops)
                        stop.fade = 1.0f;
                    auto halo = gradient;
                    halo.stops.clear();
                    commands.push_back(pathCommand(SpectrumDrawKind::envelopeHalo, -1, halo, 5.0f,
                                                   SpectralCanvasTokens::fixed().playhead.argb, 0.22f));
                    commands.push_back(pathCommand(SpectrumDrawKind::envelopeTrace, -1, gradient, 1.8f,
                                                   SpectrumLUT::colorForAmp(gradient.maxAmp, o.theme),
                                                   1.0f));
                }
            }
        }
    }

    static void emitEnvelopeRange(std::vector<SpectrumDrawCommand>& commands,
                                  const SpectralTraceSnapshot& snapshot, UiRect plot,
                                  const SpectrumRenderOptions& o)
    {
        const auto tr = SpectrumGeometry::visibleTimeRange(o.viewport);
        const auto fr = SpectrumGeometry::visibleFrequencyRange(o.viewport);
        // Clip the slab to the visible window so it never paints outside the plot.
        const auto t0 = std::clamp(std::min(o.envelopeTimeStart, o.envelopeTimeEnd), tr.start, tr.end);
        const auto t1 = std::clamp(std::max(o.envelopeTimeStart, o.envelopeTimeEnd), tr.start, tr.end);
        if (t1 <= t0)
            return;
        const auto front = std::clamp(std::min(o.envelopeFrequencyStart, o.envelopeFrequencyEnd),
                                      fr.start, fr.end);
        const auto back = std::clamp(std::max(o.envelopeFrequencyStart, o.envelopeFrequencyEnd),
                                     fr.start, fr.end);
        constexpr std::uint32_t accent = 0xff5ed0ffu;
        constexpr float topAmp = 1.1f;
        const auto at = [&](float t, float f, float a) {
            return SpectrumGeometry::positionForIsoCoords(t, f, a, plot, o.viewport);
        };
        const auto fb0 = at(t0, front, 0.0f), fb1 = at(t1, front, 0.0f);
        const auto bb0 = at(t0, back, 0.0f), bb1 = at(t1, back, 0.0f);
        const auto ft0 = at(t0, front, topAmp), ft1 = at(t1, front, topAmp);
        const auto bt0 = at(t0, back, topAmp), bt1 = at(t1, back, topAmp);
        const auto marker = SpectrumDrawKind::selectionMarker;
        // Floor quad: the primary "this time region" cue.
        commands.push_back(polygonCommand(marker, { fb0, fb1, bb1, bb0 }, accent, 0.15f));
        for (const auto& [a, b] : { std::pair { fb0, fb1 }, std::pair { fb1, bb1 },
                                    std::pair { bb1, bb0 }, std::pair { bb0, fb0 } })
            commands.push_back(lineCommand(marker, a, b, accent, 0.5f, 1.2f));
        // End walls: front edges bold, back edges faint for depth.
        commands.push_back(lineCommand(marker, fb0, ft0, accent, 0.8f, 1.8f));
        commands.push_back(lineCommand(marker, fb1, ft1, accent, 0.8f, 1.8f));
        commands.push_back(lineCommand(marker, bb0, bt0, accent, 0.32f, 1.0f));
        commands.push_back(lineCommand(marker, bb1, bt1, accent, 0.32f, 1.0f));
        for (const auto& [a, b] : { std::pair { ft0, ft1 }, std::pair { bt0, bt1 },
                                    std::pair { ft0, bt0 }, std::pair { ft1, bt1 } })
            commands.push_back(lineCommand(marker, a, b, accent, 0.42f, 1.0f));
        const auto duration = snapshot.durationSeconds;
        const auto label = duration > 0.0
            ? formatFixed(t0 * duration, 3) + " - " + formatFixed(t1 * duration, 3) + " s"
            : std::to_string(static_cast<int>(std::lround(t0 * 100.0f))) + " - "
                + std::to_string(static_cast<int>(std::lround(t1 * 100.0f))) + " %";
        const UiRect labelBounds { ft0.x - 2.0f, ft0.y - 18.0f,
                                   10.0f + 6.2f * static_cast<float>(label.size()), 15.0f };
        auto badge = textCommand(SpectrumDrawKind::badge, labelBounds, label, 0xff8fe0ffu, 1.0f,
                                 11.0f, 1);
        badge.x2 = 0.55f; // background alpha (black)
        commands.push_back(std::move(badge));
    }

    // --- onsets / Smearnov ---------------------------------------------------

    static void emitSmearnovCurves(std::vector<SpectrumDrawCommand>& commands,
                                   const SpectralTraceSnapshot& snapshot, UiRect plot,
                                   const SpectrumRenderOptions& o)
    {
        const auto& combined = snapshot.smearnovDetectionCurve;
        const auto& threshold = snapshot.smearnovThresholdCurve;
        if (combined.size() < 2 && threshold.size() < 2)
            return;
        const auto curveHeight = std::clamp(plot.height * 0.18f, 22.0f, 54.0f);
        const UiRect area { plot.x + 2.0f, plot.y + plot.height - curveHeight - 3.0f,
                            std::max(1.0f, plot.width - 4.0f), curveHeight };
        if (area.width < 2.0f || area.height < 2.0f)
            return;
        float maxValue = 0.0f;
        for (const auto* values : { &combined, &threshold })
            for (const auto value : *values)
                if (std::isfinite(value) && value > maxValue) maxValue = value;
        if (maxValue <= 0.0f)
            return;
        commands.push_back({ SpectrumDrawKind::smearnovCurve,
                             { area.x - 2.0f, area.y - 1.0f, area.width + 4.0f, area.height + 2.0f },
                             0xff000000u, 0.15f, -1 });
        const auto timeRange = SpectrumGeometry::visibleTimeRange(o.viewport);
        const auto pointsFor = [&](const std::vector<float>& values) {
            std::vector<UiPoint> points;
            const auto count = static_cast<int>(values.size());
            if (count < 2) return points;
            const auto step = std::max(1, count / 900);
            const auto append = [&](int i) {
                const auto value = values[static_cast<std::size_t>(i)];
                if (!std::isfinite(value)) return;
                const auto t = static_cast<float>(i) / static_cast<float>(count - 1);
                const auto xNorm = (t - timeRange.start) / std::max(1.0e-6f, timeRange.length());
                if (xNorm < -0.01f || xNorm > 1.01f) return;
                points.push_back({ plot.x + plot.width * xNorm,
                                   area.y + area.height
                                       - area.height * std::clamp(value / maxValue, 0.0f, 1.0f) });
            };
            for (int i = 0; i < count; i += step) append(i);
            if ((count - 1) % step != 0) append(count - 1);
            return points;
        };
        // Threshold: dashed in groups of three segments.
        const auto thresholdPoints = pointsFor(threshold);
        for (std::size_t i = 1; i < thresholdPoints.size(); ++i)
            if ((i / 3) % 2 == 0)
                commands.push_back(lineCommand(SpectrumDrawKind::smearnovCurve, thresholdPoints[i - 1],
                                               thresholdPoints[i], 0xfffff3a0u, 0.42f, 1.05f, -11));
        const auto combinedPoints = pointsFor(combined);
        if (combinedPoints.size() >= 2)
        {
            SpectrumDrawCommand c;
            c.kind = SpectrumDrawKind::smearnovCurve; c.index = -10;
            c.color = 0xff78fff1u; c.alpha = 0.58f; c.thickness = 1.25f;
            c.vertices = combinedPoints; c.bounds = boundsOf(c.vertices);
            commands.push_back(std::move(c));
        }
    }

    static void emitOnsets(std::vector<SpectrumDrawCommand>& commands, const SpectralTraceSnapshot& snapshot,
                           UiRect plot, const SpectrumRenderOptions& o)
    {
        const auto onsets = snapshot.onsets.empty() ? legacyOnsets(snapshot) : snapshot.onsets;
        const auto residual = SpectrumGeometry::isResidualDisplayMode(snapshot.mode);
        // Desaturated amber so markers recede behind the teal partial data.
        const auto spanColour = residual ? 0xff74fff0u : 0xffcfb87au;
        const auto markerColour = residual ? 0xffbaffeau : 0xffd8c489u;
        const auto timeRange = SpectrumGeometry::visibleTimeRange(o.viewport);
        float maxStrength = 0.0f;
        for (const auto& onset : onsets)
            if (std::isfinite(onset.strength) && onset.strength > 0.0f)
                maxStrength = std::max(maxStrength, onset.strength);
        const auto strengthOf = [maxStrength](const SpectralOnset& onset) {
            if (maxStrength <= 0.0f || !std::isfinite(onset.strength)) return 0.65f;
            return std::clamp(onset.strength / maxStrength, 0.15f, 1.0f);
        };
        const auto bottom = plot.y + plot.height;
        for (std::size_t i = 0; i < onsets.size(); ++i)
        {
            const auto& onset = onsets[i];
            const auto x0 = xForTime(std::clamp(std::min(onset.preNorm, onset.peakNorm), 0.0f, 1.0f),
                                     plot, timeRange);
            const auto x1 = xForTime(std::clamp(std::max(onset.preNorm, onset.peakNorm), 0.0f, 1.0f),
                                     plot, timeRange) + 1.0f;
            if (x1 < plot.x || x0 > plot.x + plot.width)
                continue;
            const auto strength = strengthOf(onset);
            const auto left = std::max(plot.x, x0), right = std::min(plot.x + plot.width, x1);
            commands.push_back(rectCommand(SpectrumDrawKind::onsetHandle,
                                           { left, plot.y, right - left, plot.height },
                                           spanColour, 0.035f + 0.070f * strength, static_cast<int>(i)));
            for (const auto edge : { std::round(x0), std::round(x1) })
                commands.push_back(lineCommand(SpectrumDrawKind::onsetHandle, { edge, plot.y },
                                               { edge, bottom }, spanColour, 0.13f + 0.10f * strength,
                                               1.0f, static_cast<int>(i)));
        }
        emitSmearnovCurves(commands, snapshot, plot, o);
        for (std::size_t i = 0; i < onsets.size(); ++i)
        {
            const auto x = xForTime(std::clamp(onsets[i].norm, 0.0f, 1.0f), plot, timeRange);
            if (x < plot.x - 2.0f || x > plot.x + plot.width + 2.0f)
                continue;
            const auto strength = strengthOf(onsets[i]);
            const auto index = static_cast<int>(i);
            const auto marker = SpectrumDrawKind::onsetMarker;
            commands.push_back(lineCommand(marker, { x, plot.y }, { x, bottom }, markerColour,
                                           0.05f + 0.06f * strength, 3.0f, index));
            commands.push_back(lineCommand(marker, { x, plot.y }, { x, bottom }, markerColour,
                                           0.14f + 0.12f * strength, 1.2f, index));
            commands.push_back(lineCommand(marker, { x, plot.y }, { x, bottom }, markerColour,
                                           std::min(1.0f, 0.58f + 0.22f * strength), 0.75f, index));
            constexpr float headWidth = 5.0f, headHeight = 5.5f;
            const auto headAlpha = 0.42f + 0.22f * strength;
            commands.push_back(polygonCommand(marker, { { x - headWidth * 0.5f, plot.y + 1.0f },
                                                        { x + headWidth * 0.5f, plot.y + 1.0f },
                                                        { x, plot.y + headHeight } },
                                              markerColour, headAlpha, index));
            commands.push_back(polygonCommand(marker, { { x - headWidth * 0.5f, bottom - 1.0f },
                                                        { x, bottom - headHeight },
                                                        { x + headWidth * 0.5f, bottom - 1.0f } },
                                              markerColour, headAlpha, index));
        }
    }

    // --- selection, playhead ----------------------------------------------

    static void emitSelectionOverlay(std::vector<SpectrumDrawCommand>& commands,
                                     const SpectralTraceSnapshot& snapshot, UiRect plot,
                                     const SpectrumRenderOptions& o)
    {
        if (snapshot.mode == SpectralTraceMode::residual && !o.selectedBands.empty()
            && !snapshot.residualBands.empty() && snapshot.frameCount > 0)
        {
            const auto first = std::clamp(o.residualFirstFrame, 0,
                                          static_cast<int>(snapshot.frameCount) - 1);
            const auto last = std::clamp(std::max(first, o.residualLastFrame), first,
                                         static_cast<int>(snapshot.frameCount) - 1);
            const auto timeRange = SpectrumGeometry::visibleTimeRange(o.viewport);
            const auto timeAtFrame = [&](int frame) {
                return xForTime(snapshot.frameCount <= 1 ? 0.0f
                                    : static_cast<float>(frame) / static_cast<float>(snapshot.frameCount - 1),
                                plot, timeRange);
            };
            const auto x = timeAtFrame(first);
            const auto right = timeAtFrame(last);
            for (const auto bandId : o.selectedBands)
            {
                const auto it = std::find_if(snapshot.residualBands.begin(), snapshot.residualBands.end(),
                    [bandId](const auto& band) { return band.bandIndex == bandId; });
                if (it == snapshot.residualBands.end())
                    continue;
                const auto yRange = residualYRange(*it, snapshot, plot, o.viewport);
                const UiRect r { std::min(x, right), yRange.start, std::max(1.0f, std::abs(right - x)),
                                 std::max(1.0f, yRange.length()) };
                commands.push_back(rectCommand(SpectrumDrawKind::selectionMarker, r, 0xff3ffff0u,
                                               0.157f, bandId));
                auto border = SpectrumDrawCommand { SpectrumDrawKind::selectionMarker, r, 0xfffff3a0u,
                                                    1.0f, bandId };
                border.thickness = 1.4f;
                commands.push_back(std::move(border));
            }
        }
        if (!SpectrumGeometry::isIsometricDisplayMode(snapshot.mode) && snapshot.frameCount > 1
            && o.timeSelectionFirstFrame >= 0 && o.timeSelectionLastFrame >= o.timeSelectionFirstFrame)
        {
            const auto timeRange = SpectrumGeometry::visibleTimeRange(o.viewport);
            const auto last = static_cast<float>(snapshot.frameCount - 1);
            const auto x0 = std::clamp(xForTime(static_cast<float>(o.timeSelectionFirstFrame) / last, plot, timeRange),
                                       plot.x, plot.x + plot.width);
            const auto x1 = std::clamp(xForTime(static_cast<float>(o.timeSelectionLastFrame) / last, plot, timeRange),
                                       plot.x, plot.x + plot.width);
            if (x1 > x0)
            {
                const auto colour = SpectralCanvasTokens::fixed().selection.argb;
                commands.push_back(rectCommand(SpectrumDrawKind::selectionMarker,
                                               { x0, plot.y, x1 - x0, plot.height }, colour, 0.07f, -30));
                for (const auto x : { x0, x1 })
                    commands.push_back(lineCommand(SpectrumDrawKind::selectionMarker, { x, plot.y },
                                                   { x, plot.y + plot.height }, colour, 0.55f, 1.0f, -30));
            }
        }
        if (o.lassoActive && o.lassoBounds.width > 2.0f && o.lassoBounds.height > 2.0f)
        {
            commands.push_back(rectCommand(SpectrumDrawKind::selectionMarker, o.lassoBounds,
                                           0xff3ffff0u, 0.133f));
            commands.push_back({ SpectrumDrawKind::selectionMarker, o.lassoBounds, 0xffbaffeau, 1.0f, -1 });
        }
        const auto envelopeMode = snapshot.mode == SpectralTraceMode::isometricEnvelope
            || snapshot.mode == SpectralTraceMode::isometricEnvelopeRdc;
        if (o.envelopeEditing && envelopeMode)
            emitEnvelopeRange(commands, snapshot, plot, o);
        else if (o.envelopeRange.width > 0.0f && o.envelopeRange.height > 0.0f)
            commands.push_back({ SpectrumDrawKind::selectionMarker, o.envelopeRange,
                                 SpectralCanvasTokens::fixed().selection.argb, 0.72f, -1 });
    }

    static void emitPlayhead(std::vector<SpectrumDrawCommand>& commands, UiRect plot,
                             const SpectrumRenderOptions& o, bool iso,
                             const SpectralCanvasTokens& tokens)
    {
        const auto playhead = options_playhead(o);
        if (playhead < 0.0f)
            return;
        const auto timeRange = SpectrumGeometry::visibleTimeRange(o.viewport);
        const auto colour = tokens.playhead.argb;
        if (iso)
        {
            if (playhead < timeRange.start - timeRange.length() * 0.025f
                || playhead > timeRange.end + timeRange.length() * 0.025f)
                return;
            // A faint line across the ground plane with a short wake behind it.
            const auto fr = SpectrumGeometry::visibleFrequencyRange(o.viewport);
            const auto at = [&](float t, float f) {
                return SpectrumGeometry::positionForIsoCoords(t, f, 0.0f, plot, o.viewport);
            };
            const auto frontBase = at(playhead, fr.start), backBase = at(playhead, fr.end);
            const auto wakeTime = std::clamp(playhead - timeRange.length() * 0.03f, 0.0f, 1.0f);
            if (wakeTime < playhead)
            {
                const auto frontWake = at(wakeTime, fr.start), backWake = at(wakeTime, fr.end);
                auto wake = polygonCommand(SpectrumDrawKind::playheadWake,
                                           { frontWake, backWake, backBase, frontBase }, colour, 0.07f);
                wake.gradStart = { (frontBase.x + backBase.x) * 0.5f, (frontBase.y + backBase.y) * 0.5f };
                wake.gradEnd = { (frontWake.x + backWake.x) * 0.5f, (frontWake.y + backWake.y) * 0.5f };
                commands.push_back(std::move(wake));
            }
            commands.push_back(lineCommand(SpectrumDrawKind::playhead, frontBase, backBase,
                                           colour, 0.10f, 3.4f));
            commands.push_back(lineCommand(SpectrumDrawKind::playhead, frontBase, backBase,
                                           colour, 0.42f, 1.1f));
            commands.push_back({ SpectrumDrawKind::playhead,
                                 { frontBase.x - 2.0f, frontBase.y - 2.0f, 4.0f, 4.0f },
                                 colour, 0.70f, 1 });
            return;
        }
        const auto xNorm = (playhead - timeRange.start) / std::max(1.0e-6f, timeRange.length());
        if (xNorm < -0.01f || xNorm > 1.01f)
            return;
        const auto x = plot.x + plot.width * xNorm;
        const auto top = plot.y, bottom = plot.y + plot.height;
        constexpr float wakeWidth = 24.0f;
        auto wake = rectCommand(SpectrumDrawKind::playheadWake,
                                { x - wakeWidth, top, wakeWidth, plot.height }, colour, 0.14f);
        wake.gradStart = { x, top };
        wake.gradEnd = { x - wakeWidth, top };
        commands.push_back(std::move(wake));
        commands.push_back(lineCommand(SpectrumDrawKind::playhead, { x, top }, { x, bottom },
                                       colour, 0.45f, 2.6f));
        commands.push_back(lineCommand(SpectrumDrawKind::playhead, { x, top }, { x, bottom },
                                       colour, 1.0f, 1.1f));
        constexpr float headWidth = 7.0f;
        commands.push_back(polygonCommand(SpectrumDrawKind::playhead,
                                          { { x - headWidth * 0.5f, top + 1.0f },
                                            { x + headWidth * 0.5f, top + 1.0f },
                                            { x, top + 8.5f } },
                                          colour, 0.88f));
    }

    [[nodiscard]] static float options_playhead(const SpectrumRenderOptions& o) noexcept
    { return std::isfinite(o.playheadNorm) ? o.playheadNorm : -1.0f; }

    // --- axes, gutters, readouts ------------------------------------------

    static void emitAxesAndChrome(std::vector<SpectrumDrawCommand>& commands, const SpectralTraceSnapshot& snapshot,
                                  UiRect screen, UiRect plot, const SpectrumRenderOptions& options,
                                  const SpectralCanvasTokens& tokens)
    {
        const auto timeRange = SpectrumGeometry::visibleTimeRange(options.viewport);
        const auto iso = SpectrumGeometry::isIsometricDisplayMode(snapshot.mode);
        const auto residualMode = SpectrumGeometry::isResidualDisplayMode(snapshot.mode);
        // The bottom-edge time reference only means something on the planar
        // time axis; the isometric time axis is diagonal.
        if (!iso)
        {
            const auto duration = std::max(0.0, snapshot.durationSeconds);
            const auto visibleSeconds = duration * timeRange.length();
            const auto decimals = visibleSeconds < 2.0 ? 2 : visibleSeconds < 20.0 ? 1 : 0;
            for (int i = 0; i <= 16; i += 4)
            {
                const auto normalized = static_cast<float>(i) / 16.0f;
                const auto x = plot.x + plot.width * normalized;
                commands.push_back({ SpectrumDrawKind::axisTick, { x, plot.y + plot.height - 5.0f, 1.0f, 5.0f },
                                     tokens.axisFrame.argb, 0.35f, i });
                const auto label = formatFixed(duration > 0.0
                    ? (timeRange.start + normalized * timeRange.length()) * duration : normalized,
                    decimals) + "s";
                const auto labelX = i == 0 ? x + 3.0f : i == 16 ? x - 47.0f : x - 22.0f;
                commands.push_back(textCommand(SpectrumDrawKind::axisLabel,
                                               { labelX, plot.y + plot.height - 17.0f, 44.0f, 10.0f },
                                               label, 0xff6fa3b0u, 0.75f, 8.5f,
                                               i == 0 ? 0 : i == 16 ? 2 : 1, i));
            }
        }
        else
        {
            // Axis captions at the projected axis ends: FREQ under the front
            // edge, TIME beside the left edge, AMP above the origin post.
            const auto fr = SpectrumGeometry::visibleFrequencyRange(options.viewport);
            const auto at = [&](float t, float f, float a) {
                return SpectrumGeometry::positionForIsoCoords(t, f, a, plot, options.viewport);
            };
            const auto caption = (tokens.axisFrame.argb & 0x00ffffffu) | 0xff000000u;
            const auto freq = at(timeRange.start, fr.start + fr.length() * 0.5f, 0.0f);
            commands.push_back(textCommand(SpectrumDrawKind::infoText,
                                           { freq.x - 26.0f, freq.y + 4.0f, 52.0f, 12.0f },
                                           "FREQ", caption, 0.8f, 8.5f, 1));
            // TIME sits left of the origin, at the foot of the time axis.
            // Nothing is drawn left of the origin post; every spot beside the
            // floor is crossed by ridges once the well is wide.
            const auto origin = at(timeRange.start, fr.start, 0.0f);
            commands.push_back(textCommand(SpectrumDrawKind::infoText,
                                           { origin.x - 56.0f, origin.y - 14.0f, 48.0f, 12.0f },
                                           "TIME", caption, 0.8f, 8.5f, 2));
            const auto amp = at(timeRange.start, fr.start, 1.25f);
            commands.push_back(textCommand(SpectrumDrawKind::infoText,
                                           { amp.x - 24.0f, amp.y - 14.0f, 48.0f, 12.0f },
                                           "AMP", caption, 0.8f, 8.5f, 1));
        }
        const UiRect legend { plot.x + plot.width - 140.0f, plot.y + 8.0f, 132.0f, 38.0f };
        if (legend.x >= plot.x + 4.0f)
            commands.push_back({ SpectrumDrawKind::ampLegend, legend, tokens.background.argb, 0.78f, -1 });

        const UiRect gutter { plot.x - SpectrumGeometry::kBandLabelGutterWidth, plot.y,
                              SpectrumGeometry::kBandLabelGutterWidth, plot.height };
        const auto gutterDivider = [&] {
            commands.push_back({ SpectrumDrawKind::gutterTick,
                                 { std::round(gutter.x + gutter.width), gutter.y, 1.0f, gutter.height },
                                 tokens.axisFrame.argb, 0.35f, -1 });
        };
        if (SpectrumGeometry::showsBandLabelGutter(snapshot.mode, snapshot.residualBands.size(),
                                                   snapshot.frameCount))
        {
            gutterDivider();
            // Declutter by spacing from the last drawn label (bottom-up), not
            // by band height: 25 bands in a design-size plot are ~12 px each
            // and a height cutoff hid every label.
            float lastLabelY = 1.0e9f;
            for (const auto& band : snapshot.residualBands)
            {
                // Labels sit on the band they name and follow the zoom; the
                // gutter hit test uses the same band centres.
                const auto yRange = residualYRange(band, snapshot, plot, options.viewport);
                if (yRange.end < plot.y - 2.0f || yRange.start > plot.y + plot.height + 2.0f)
                    continue;
                const auto hot = band.bandIndex == options.hoveredBand
                    || bandSelected(options, band.bandIndex);
                const auto centre = (yRange.start + yRange.end) * 0.5f;
                if (lastLabelY - centre < 11.0f && !hot)
                    continue;
                lastLabelY = centre;
                const auto colour = hot ? tokens.hover.argb : 0xff5f7f8au;
                const auto lowHz = band.lowHz >= 1000.0
                    ? formatFixed(band.lowHz / 1000.0, band.lowHz >= 10000.0 ? 0 : 1) + "k"
                    : std::to_string(static_cast<int>(std::lround(band.lowHz)));
                if (yRange.length() >= 22.0f)
                {
                    commands.push_back(textCommand(SpectrumDrawKind::bandLabel,
                                                   { gutter.x + 2.0f, centre - 9.0f, 18.0f, 10.0f },
                                                   "B" + std::to_string(band.bandIndex), colour, 1.0f,
                                                   9.0f, 0, band.bandIndex));
                    commands.push_back(textCommand(SpectrumDrawKind::bandLabel,
                                                   { gutter.x + 2.0f, centre, gutter.width - 6.0f, 10.0f },
                                                   lowHz, colour, 1.0f, 9.0f, 0, band.bandIndex));
                }
                else
                    commands.push_back(textCommand(SpectrumDrawKind::bandLabel,
                                                   { gutter.x + 2.0f, centre - 5.0f, gutter.width - 6.0f, 10.0f },
                                                   lowHz, colour, 1.0f, 9.0f, 0, band.bandIndex));
            }
        }
        else if (SpectrumGeometry::showsFreqLabelGutter(snapshot.mode, snapshot.frameCount))
        {
            gutterDivider();
            // 1-2-5 ticks along the log frequency axis, following the zoom.
            const auto minHz = std::max(1.0, snapshot.minFrequencyHz);
            const auto maxHz = std::max(minHz * 1.2, snapshot.maxFrequencyHz);
            const auto logRange = std::log(maxHz / minHz);
            const auto frequencyRange = SpectrumGeometry::visibleFrequencyRange(options.viewport);
            const auto span = std::max(1.0e-6f, frequencyRange.length());
            float lastLabelY = 1.0e9f;
            int tick = 0;
            for (double decade = 10.0; decade <= 100000.0; decade *= 10.0)
                for (const auto mantissa : { 1.0, 2.0, 5.0 })
                {
                    const auto hz = decade * mantissa;
                    if (hz < minHz || hz > maxHz)
                        continue;
                    const auto yNorm = (static_cast<float>(std::log(hz / minHz) / logRange)
                                        - frequencyRange.start) / span;
                    if (yNorm < -0.001f || yNorm > 1.001f)
                        continue;
                    const auto y = plot.y + plot.height - plot.height * yNorm;
                    commands.push_back({ SpectrumDrawKind::gutterTick,
                                         { gutter.x + gutter.width - 4.0f, y - 0.4f, 4.0f, 0.8f },
                                         tokens.axisFrame.argb, 0.4f, tick });
                    if (lastLabelY - y < 13.0f)
                        continue;
                    lastLabelY = y;
                    const auto text = hz >= 1000.0
                        ? std::to_string(static_cast<int>(std::lround(hz / 1000.0))) + "k"
                        : std::to_string(static_cast<int>(std::lround(hz)));
                    commands.push_back(textCommand(SpectrumDrawKind::freqLabel,
                                                   { gutter.x + 2.0f, y - 5.0f, gutter.width - 10.0f, 10.0f },
                                                   text, 0xff5f7f8au, 1.0f, 9.0f, 0, tick++));
                }
        }

        // Footer: model summary and display frequency range.
        const auto footerY = screen.y + screen.height - 18.0f;
        const auto summary = residualMode
            ? std::to_string(snapshot.residualBands.size()) + " residual bands"
            : std::to_string(snapshot.visiblePartialCount) + "/" + std::to_string(snapshot.partialCount)
                + " partials";
        commands.push_back(textCommand(SpectrumDrawKind::infoText,
                                       { screen.x + 14.0f, footerY, 160.0f, 14.0f }, summary,
                                       0xff70ffd4u, 1.0f, 11.0f, 0));
        commands.push_back(textCommand(SpectrumDrawKind::infoText,
                                       { screen.x + screen.width - 170.0f, footerY, 156.0f, 14.0f },
                                       formatFixed(snapshot.minFrequencyHz, 0) + " - "
                                           + formatFixed(snapshot.maxFrequencyHz, 0) + " Hz",
                                       0xff70ffd4u, 1.0f, 11.0f, 2));

        // Frequency-shape pivot: where the warp is anchored, and a badge.
        const auto shapeOverlay = !residualMode && snapshot.frequencyShapeMode != FrequencyShapeMode::Linear;
        if (shapeOverlay && snapshot.resolvedFrequencyShapePivotHz > 0.0
            && snapshot.maxFrequencyHz > snapshot.minFrequencyHz)
        {
            const auto minHz = std::max(1.0, snapshot.minFrequencyHz);
            const auto maxHz = std::max(minHz * 1.2, snapshot.maxFrequencyHz);
            const auto pivotNorm = static_cast<float>(std::clamp(
                std::log(std::clamp(snapshot.resolvedFrequencyShapePivotHz, minHz, maxHz) / minHz)
                    / std::log(maxHz / minHz), 0.0, 1.0));
            constexpr std::uint32_t accent = 0xffbaffeau;
            const auto dashed = [&](UiPoint a, UiPoint b) {
                constexpr int dashes = 36;
                for (int i = 0; i < dashes; i += 2)
                {
                    const auto t0 = static_cast<float>(i) / dashes, t1 = static_cast<float>(i + 1) / dashes;
                    commands.push_back(lineCommand(SpectrumDrawKind::axis,
                                                   { a.x + (b.x - a.x) * t0, a.y + (b.y - a.y) * t0 },
                                                   { a.x + (b.x - a.x) * t1, a.y + (b.y - a.y) * t1 },
                                                   accent, 0.72f, 1.1f, -20));
                }
            };
            if (iso)
                dashed(SpectrumGeometry::positionForIsoCoords(timeRange.start, pivotNorm, 0.0f, plot,
                                                              options.viewport),
                       SpectrumGeometry::positionForIsoCoords(timeRange.end, pivotNorm, 0.0f, plot,
                                                              options.viewport));
            else
            {
                const auto f = SpectrumGeometry::visibleFrequencyRange(options.viewport);
                const auto yNorm = (pivotNorm - f.start) / std::max(1.0e-6f, f.length());
                if (yNorm >= -0.05f && yNorm <= 1.05f)
                {
                    const auto y = plot.y + plot.height - plot.height * yNorm;
                    dashed({ plot.x, y }, { plot.x + plot.width, y });
                }
            }
            const auto pivotText = snapshot.frequencyShapePivotHz <= 0.0
                ? "Auto -> " + formatFixed(snapshot.resolvedFrequencyShapePivotHz, 0) + " Hz"
                : formatFixed(snapshot.resolvedFrequencyShapePivotHz, 0) + " Hz";
            auto badge = textCommand(SpectrumDrawKind::badge,
                                     { plot.x + 7.0f, plot.y + 6.0f,
                                       std::min(280.0f, plot.width - 14.0f), 18.0f },
                                     std::string(frequencyShapeModeLabel(snapshot.frequencyShapeMode))
                                         + " " + formatFixed(snapshot.frequencyShapeAmount, 2)
                                         + " @ " + pivotText,
                                     accent, 0.9f, 11.0f, 0);
            badge.x2 = 0.80f; // background alpha (0xcc07110f)
            badge.y2 = 1.0f;  // bordered
            commands.push_back(std::move(badge));
        }

        // Hover readout.
        const auto readoutY = plot.y + (shapeOverlay ? 29.0f : 7.0f);
        std::string readout;
        if (residualMode && options.hoveredBand >= 0)
        {
            const auto band = std::find_if(snapshot.residualBands.begin(), snapshot.residualBands.end(),
                [&](const auto& b) { return b.bandIndex == options.hoveredBand; });
            if (band != snapshot.residualBands.end())
                readout = "B" + std::to_string(band->bandIndex) + "  " + formatFixed(band->lowHz, 0)
                    + "-" + formatFixed(band->highHz, 0) + " Hz";
        }
        else if (!residualMode && options.hoveredPartial >= 0)
        {
            const auto trace = std::find_if(snapshot.partials.begin(), snapshot.partials.end(),
                [&](const auto& t) { return t.partialIndex == options.hoveredPartial; });
            if (trace != snapshot.partials.end())
            {
                readout = "P" + std::to_string(trace->partialIndex) + "  ";
                if (shapeOverlay && std::abs(trace->meanFrequencyHz - trace->sourceMeanFrequencyHz) > 0.05)
                    readout += formatFixed(trace->sourceMeanFrequencyHz, 1) + " -> "
                        + formatFixed(trace->meanFrequencyHz, 1) + " Hz ("
                        + frequencyShapeModeLabel(snapshot.frequencyShapeMode) + " "
                        + formatFixed(snapshot.frequencyShapeAmount, 2) + ")";
                else
                    readout += formatFixed(trace->meanFrequencyHz, 1) + " Hz";
            }
        }
        if (!readout.empty())
            commands.push_back(textCommand(SpectrumDrawKind::infoText,
                                           { plot.x + 8.0f, readoutY, 330.0f, 14.0f }, readout,
                                           0xffbaffeau, 1.0f, 11.0f, 0));
    }

    [[nodiscard]] static const char* frequencyShapeModeLabel(FrequencyShapeMode mode) noexcept
    {
        switch (mode)
        {
        case FrequencyShapeMode::Stretch: return "Stretch";
        case FrequencyShapeMode::Compress: return "Compress";
        case FrequencyShapeMode::Harmonic: return "Harmonic";
        default: return "Linear";
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
        // Thumbs are quiet teal, not the near-white hover colour: at full
        // zoom they span the whole track and read as solid slabs.
        const auto track = tokens.axisFrame.argb;
        const auto thumb = 0xff3fa79cu;
        const auto zoomTrack = zoomBarTrackWidth(chrome.zoomBar);
        commands.push_back({ SpectrumDrawKind::zoomBar, chrome.zoomBar, track, 0.34f, 0 });
        commands.push_back({ SpectrumDrawKind::zoomBar,
                             { chrome.zoomBar.x + 8.0f + zoomTrack * timeRange.start,
                               chrome.zoomBar.y + 10.0f,
                               std::max(8.0f, zoomTrack * timeRange.length()), 6.0f },
                             thumb, 0.70f, 1 });
        commands.push_back({ SpectrumDrawKind::timeScrollbar, chrome.timeScrollbar, track, 0.14f, 0 });
        commands.push_back({ SpectrumDrawKind::timeScrollbar,
                             { chrome.timeScrollbar.x + chrome.timeScrollbar.width * timeRange.start,
                               chrome.timeScrollbar.y + 4.0f,
                               std::max(8.0f, chrome.timeScrollbar.width * timeRange.length()),
                               std::max(2.0f, chrome.timeScrollbar.height - 8.0f) },
                             thumb, timeRange.length() > 0.999f ? 0.18f : 0.55f, 1 });
        commands.push_back({ SpectrumDrawKind::frequencyScrollbar, chrome.frequencyScrollbar,
                             track, 0.14f, 0 });
        commands.push_back({ SpectrumDrawKind::frequencyScrollbar,
                             { chrome.frequencyScrollbar.x + 4.0f,
                               chrome.frequencyScrollbar.y + chrome.frequencyScrollbar.height * (1.0f - frequencyRange.end),
                               std::max(2.0f, chrome.frequencyScrollbar.width - 8.0f),
                               std::max(8.0f, chrome.frequencyScrollbar.height * frequencyRange.length()) },
                             thumb, frequencyRange.length() > 0.999f ? 0.18f : 0.55f, 1 });
    }

public:
    /** Usable width of the zoom bar's thumb track (8-px end caps). */
    [[nodiscard]] static float zoomBarTrackWidth(UiRect zoomBar) noexcept
    { return std::max(1.0f, zoomBar.width - 16.0f); }

private:
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
