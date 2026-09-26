#include "../src/ui/SpectrumRenderer.h"

#include <cassert>
#include <cmath>
#include <limits>

using namespace ats::ui;

static SpectralTraceSnapshot makeSnapshot(SpectralTraceMode mode)
{
    SpectralTraceSnapshot snap;
    snap.mode = mode;
    snap.minFrequencyHz = 20.0;
    snap.maxFrequencyHz = 20000.0;
    snap.frameCount = 2;
    snap.residualBands.push_back({ 4, 200.0, 400.0, { 0.2f, 0.8f } });
    SpectralTrace trace;
    trace.partialIndex = 7;
    trace.peakAmpNorm = 0.9f;
    trace.points = { { 0, 0.0f, 0.2f, 0.4f, 220.0, 220.0, 0.2, true },
                     { 1, 1.0f, 0.8f, 0.9f, 440.0, 440.0, 0.8, true } };
    snap.partials.push_back(trace);
    snap.onsetNorms = { 0.25f, 0.75f };
    snap.envelopeNorm = { 0.1f, 0.8f, 0.4f };
    return snap;
}

static bool hasKind(const std::vector<SpectrumDrawCommand>& cmds, SpectrumDrawKind kind)
{
    for (const auto& c : cmds) if (c.kind == kind) return true;
    return false;
}

int main()
{
    const UiRect bounds { 10.0f, 20.0f, 400.0f, 200.0f };

    // Native path atlases allocate from path bounds before applying a canvas
    // scissor.  Crossing segments must be retained but bounded, and malformed
    // numeric points must split (not poison) the visible path.
    {
        std::vector<TraceVertex> source {
            { { -1000.0f, 50.0f }, 0.2f, 1.0f },
            { { 1000.0f, 50.0f }, 0.8f, 1.0f },
            { { std::numeric_limits<float>::quiet_NaN(), 50.0f }, 0.5f, 1.0f },
            { { 50.0f, -1000.0f }, 0.3f, 1.0f },
            { { 50.0f, 1000.0f }, 0.7f, 1.0f }
        };
        const UiRect clip { 0.0f, 0.0f, 100.0f, 100.0f };
        const auto runs = clipTraceRuns(source, clip);
        assert(runs.size() == 2);
        for (const auto& run : runs)
        {
            assert(run.size() >= 2);
            for (const auto& vertex : run)
            {
                assert(std::isfinite(vertex.pos.x) && std::isfinite(vertex.pos.y));
                assert(vertex.pos.x >= clip.x && vertex.pos.x <= clip.x + clip.width);
                assert(vertex.pos.y >= clip.y && vertex.pos.y <= clip.y + clip.height);
            }
        }
    }

    // --- combined mode (baseline) ---
    {
        auto snap = makeSnapshot(SpectralTraceMode::combined);
        snap.durationSeconds = 4.0;
        SpectrumRenderOptions options { ThemeId::ember, 7, 0.25f, true };
        options.showOnsetOverlay = true;
        auto ember = SpectrumRenderer::render(snap, bounds, options);
        auto porcelain = SpectrumRenderer::render(snap, bounds,
            { ThemeId::porcelain, 7, 0.25f, true });
        assert(!ember.empty() && ember.front().kind == SpectrumDrawKind::background);
        // The spectral canvas is fixed so screenshots remain comparable across
        // the surrounding Ember and Porcelain editor themes.
        assert(ember.front().color == porcelain.front().color);
        assert(ember.front().color == 0xff0a1220u);
        // Published Smearnov data is passive until the explicit onset-edit
        // toggle is enabled.
        assert(!hasKind(porcelain, SpectrumDrawKind::onsetMarker));

        std::size_t residualIndex = ember.size();
        std::size_t traceIndex = ember.size();
        bool hasPlayhead = false;
        bool hasSelected = false;
        bool hasAxes = false;
        bool hasOnsets = false;
        bool hasEnvelope = false;
        for (std::size_t i = 0; i < ember.size(); ++i)
        {
            if (ember[i].kind == SpectrumDrawKind::residualBand  && residualIndex == ember.size()) residualIndex = i;
            if (ember[i].kind == SpectrumDrawKind::selectedPartial && traceIndex == ember.size()) traceIndex = i;
            if (ember[i].kind == SpectrumDrawKind::playhead)       hasPlayhead = true;
            if (ember[i].kind == SpectrumDrawKind::selectedPartial) hasSelected = true;
            if (ember[i].kind == SpectrumDrawKind::axisTick)       hasAxes = true;
            if (ember[i].kind == SpectrumDrawKind::onsetMarker)    hasOnsets = true;
            if (ember[i].kind == SpectrumDrawKind::envelopeTrace)  hasEnvelope = true;
        }
        assert(residualIndex < ember.size());
        assert(traceIndex < ember.size());
        assert(residualIndex < traceIndex);
        assert(hasPlayhead);
        assert(hasSelected);
        assert(hasAxes);
        assert(hasOnsets);
        assert(hasEnvelope);
        assert(ember.back().kind == SpectrumDrawKind::legend);
        // The complete gradient path is the only partial primitive; endpoint
        // ridgelines are not fabricated by the recovered painter.
        assert(!hasKind(ember, SpectrumDrawKind::ridgeline));
        // selected partial should have glow halo
        assert(hasKind(ember, SpectrumDrawKind::glowHalo));
        for (const auto& command : ember)
        {
            if (command.kind == SpectrumDrawKind::dustPoint && command.index == 0)
            {
                // First five values of the reference generator at seed 0x5eed;
                // this guards the recurrence and per-dot consumption order.
                assert(std::abs(command.bounds.x - 115.709f) < 0.01f);
                assert(std::abs(command.bounds.y - 105.420f) < 0.01f);
                assert(std::abs(command.bounds.width - 2.498f) < 0.01f);
                assert(std::abs(command.bounds.height - 2.498f) < 0.01f);
            }
        }
        int zoomBars = 0, timeScrollbars = 0, frequencyScrollbars = 0, timeLabels = 0;
        for (const auto& command : ember)
        {
            if (command.kind == SpectrumDrawKind::zoomBar) ++zoomBars;
            if (command.kind == SpectrumDrawKind::timeScrollbar) ++timeScrollbars;
            if (command.kind == SpectrumDrawKind::frequencyScrollbar) ++frequencyScrollbars;
            if (command.kind == SpectrumDrawKind::axisLabel && !command.label.empty()) ++timeLabels;
        }
        assert(zoomBars == 2 && timeScrollbars == 2 && frequencyScrollbars == 2);
        assert(timeLabels == 5);

        // D1 positive case: a point exactly at the zoom centre remains at the
        // plot centre instead of being discarded by the offscreen cull.
        auto anchored = snap;
        anchored.partials.clear();
        SpectralTrace anchor;
        anchor.partialIndex = 123;
        anchor.peakAmpNorm = 0.9f;
        anchor.points = { { 0, 0.25f, 0.50f, 0.9f, 440.0, 440.0, 0.9, true },
                          { 1, 0.30f, 0.52f, 0.9f, 450.0, 450.0, 0.9, true } };
        anchored.partials.push_back(anchor);
        SpectrumRenderOptions anchorOptions;
        anchorOptions.selectedPartial = 123;
        anchorOptions.viewport.timeCenter = 0.25f;
        anchorOptions.viewport.timeZoom = 4.0f;
        const auto anchoredCommands = SpectrumRenderer::render(anchored, bounds, anchorOptions);
        const auto anchoredPlot = SpectrumRenderer::getPlotBounds(bounds, anchored);
        bool foundAnchor = false;
        for (const auto& command : anchoredCommands)
        {
            if (command.kind == SpectrumDrawKind::selectedPartial && command.index == 123)
            {
                foundAnchor = true;
                assert(!command.vertices.empty());
                assert(std::abs(command.vertices.front().x
                                - (anchoredPlot.x + anchoredPlot.width * 0.5f)) < 0.01f);
            }
        }
        assert(foundAnchor);

        options.viewport.timeZoom = 4.0f;
        options.viewport.frequencyZoom = 2.0f;
        SpectralTrace offscreen;
        offscreen.partialIndex = 99;
        offscreen.peakAmpNorm = 0.95f;
        offscreen.points = { { 0, 0.02f, 0.02f, 0.95f, 30.0, 30.0, 0.9, true },
                             { 1, 0.08f, 0.08f, 0.95f, 35.0, 35.0, 0.9, true } };
        snap.partials.push_back(offscreen);
        const auto zoomed = SpectrumRenderer::render(snap, bounds, options);
        for (const auto& command : zoomed)
            assert(command.index != 99);

        // Editing a selected trace while deeply zoomed used to emit a huge
        // rectangular glow and unclipped residual tint.  On Visage's shared
        // canvas those primitives washed the entire editor cyan.
        SpectrumRenderOptions editZoom;
        editZoom.selectedPartial = 7;
        editZoom.viewport.timeCenter = 1.0f / 3.0f;
        editZoom.viewport.timeZoom = SpectrumGeometry::kMaxZoom;
        editZoom.viewport.frequencyCenter = 0.4f;
        editZoom.viewport.frequencyZoom = SpectrumGeometry::kMaxZoom;
        const auto editCommands = SpectrumRenderer::render(snap, bounds, editZoom);
        const auto editPlot = SpectrumRenderer::getPlotBounds(bounds, snap);
        bool foundPathHalo = false;
        bool foundClippedTint = false;
        for (const auto& command : editCommands)
        {
            if (command.kind == SpectrumDrawKind::glowHalo)
            {
                foundPathHalo = true;
                assert(command.isPath());
                assert(SpectrumRenderer::requiresPlotClip(command));
            }
            if (command.kind == SpectrumDrawKind::traceGlow)
                assert(command.isPath());
            if (command.kind == SpectrumDrawKind::residualBandTint)
            {
                foundClippedTint = true;
                assert(command.bounds.y >= editPlot.y - 0.001f);
                assert(command.bounds.y + command.bounds.height
                       <= editPlot.y + editPlot.height + 0.001f);
                assert(SpectrumRenderer::requiresPlotClip(command));
            }
        }
        assert(foundPathHalo);
        assert(foundClippedTint);
        assert(!hasKind(editCommands, SpectrumDrawKind::traceGlow));
    }

    // --- partials mode: LUT-colored, no residualBand ---
    {
        auto snap = makeSnapshot(SpectralTraceMode::partials);
        snap.residualBands.clear(); // builder removes residual in partials mode
        auto cmds = SpectrumRenderer::render(snap, bounds, { ThemeId::ember, -1, 0.0f, false });
        assert(!cmds.empty() && cmds.front().kind == SpectrumDrawKind::background);
        assert(cmds.back().kind == SpectrumDrawKind::legend);
        assert(!hasKind(cmds, SpectrumDrawKind::residualBand));
        assert(hasKind(cmds, SpectrumDrawKind::partialTrace));
        assert(!hasKind(cmds, SpectrumDrawKind::ridgeline));
        // LUT color should differ from flat accent (not 0xff5f9db4)
        for (const auto& c : cmds)
            if (c.kind == SpectrumDrawKind::partialTrace)
                assert(c.color != 0xff5f9db4u); // not flat accent
    }

    // --- residual mode: only residualBand, no partialTrace ---
    {
        auto snap = makeSnapshot(SpectralTraceMode::residual);
        snap.partials.clear(); // builder removes partials in residual mode
        auto cmds = SpectrumRenderer::render(snap, bounds, { ThemeId::ember, -1, 0.0f, false });
        assert(cmds.back().kind == SpectrumDrawKind::legend);
        assert(hasKind(cmds, SpectrumDrawKind::residualBand));
        assert(!hasKind(cmds, SpectrumDrawKind::partialTrace));

        SpectrumRenderOptions selected;
        selected.selectedBands = { 4 };
        selected.residualFirstFrame = 0;
        selected.residualLastFrame = 1;
        const auto selectedCommands = SpectrumRenderer::render(snap, bounds, selected);
        bool hasResidualSelection = false;
        for (const auto& command : selectedCommands)
            hasResidualSelection = hasResidualSelection
                || (command.kind == SpectrumDrawKind::selectionMarker && command.index == 4);
        assert(hasResidualSelection);
    }

    // --- isometric mode: produces a projected partial path ---
    {
        auto snap = makeSnapshot(SpectralTraceMode::isometric);
        snap.residualBands.clear();
        auto cmds = SpectrumRenderer::render(snap, bounds, { ThemeId::ember, -1, 0.0f, false });
        assert(cmds.back().kind == SpectrumDrawKind::legend);
        assert(!hasKind(cmds, SpectrumDrawKind::isometricTile));
        assert(hasKind(cmds, SpectrumDrawKind::partialTrace));
        assert(!hasKind(cmds, SpectrumDrawKind::residualBand));
        assert(!hasKind(cmds, SpectrumDrawKind::onsetMarker));
        int traceSegments = 0;
        int gridPaths = 0;
        int axisPosts = 0;
        for (const auto& c : cmds)
        {
            if (c.kind == SpectrumDrawKind::grid && c.isPath()) ++gridPaths;
            if (c.kind == SpectrumDrawKind::axis && c.isPath()) ++axisPosts;
            if (c.kind == SpectrumDrawKind::partialTrace && c.isPath())
            {
                ++traceSegments;
                assert(c.vertices.size() >= 2);
                assert(c.stops.size() >= 2);
                assert(c.gradStart.x != c.gradEnd.x || c.gradStart.y != c.gradEnd.y);
            }
        }
        assert(traceSegments == 1);
        assert(gridPaths == 28 && axisPosts == 3);
    }

    // --- isometricResidual: produces residual paths from bands ---
    {
        auto snap = makeSnapshot(SpectralTraceMode::isometricResidual);
        snap.partials.clear();
        auto cmds = SpectrumRenderer::render(snap, bounds, { ThemeId::ember, -1, 0.0f, false });
        assert(cmds.back().kind == SpectrumDrawKind::legend);
        assert(hasKind(cmds, SpectrumDrawKind::residualBand));
        assert(!hasKind(cmds, SpectrumDrawKind::isometricTile));
    }

    // --- isometricEnvelope: empty partial input has no fabricated tiles ---
    {
        auto snap = makeSnapshot(SpectralTraceMode::isometricEnvelope);
        snap.partials.clear();
        snap.residualBands.clear();
        auto cmds = SpectrumRenderer::render(snap, bounds, { ThemeId::ember, -1, 0.0f, false });
        assert(cmds.back().kind == SpectrumDrawKind::legend);
        assert(!hasKind(cmds, SpectrumDrawKind::isometricTile));
    }

    // Envelope slices must be genuine projected ridges with fills, not a
    // repeated copy of the flat envelope vector.
    {
        auto snap = makeSnapshot(SpectralTraceMode::isometricEnvelope);
        auto cmds = SpectrumRenderer::render(snap, bounds, { ThemeId::ember, -1, 0.0f, false });
        int slices = 0, fills = 0;
        for (const auto& c : cmds)
        {
            if (c.kind == SpectrumDrawKind::envelopeTrace && c.isPath()) ++slices;
            if (c.kind == SpectrumDrawKind::envelopeFill) ++fills;
        }
        assert(slices == 30 && fills > 30);
    }

    // --- isometricEnvelopeRdc: empty input has no fabricated floor tiles ---
    {
        auto snap = makeSnapshot(SpectralTraceMode::isometricEnvelopeRdc);
        snap.partials.clear();
        snap.residualBands.clear();
        auto cmds = SpectrumRenderer::render(snap, bounds, { ThemeId::ember, -1, 0.0f, false });
        assert(cmds.back().kind == SpectrumDrawKind::legend);
        assert(!hasKind(cmds, SpectrumDrawKind::isometricTile));
        // An empty envelope has no RDC surface commands.
        int isoCount = 0;
        for (const auto& c : cmds) if (c.kind == SpectrumDrawKind::isometricTile) ++isoCount;
        assert(isoCount == 0);
    }

    // --- SpectrumLUT: the fixed blue/cyan/mint/lime ramp is theme independent ---
    {
        const auto c1 = SpectrumLUT::colorForAmp(0.5f, ThemeId::ember);
        const auto c2 = SpectrumLUT::colorForAmp(0.5f, ThemeId::porcelain);
        assert(c1 == c2);
        assert((c1 >> 24) == 0xff && (c2 >> 24) == 0xff); // fully opaque
    }

    return 0;
}
