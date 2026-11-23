#include "../src/ui/SpectrumRenderer.h"

#include <algorithm>
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
                // y follows the plot, which starts below the tool row.
                assert(std::abs(command.bounds.y - 120.439f) < 0.01f);
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
        // One crest-tint strip per slice, plus a halo per slice.
        assert(slices == 30 && fills == 30);

        // With the playhead inside the view, one extra slice marks the
        // envelope being heard, on top of the waterfall.
        SpectrumRenderOptions playing;
        playing.showPlayhead = true;
        playing.playheadNorm = 0.4f;
        const auto withPlayhead = SpectrumRenderer::render(snap, bounds, playing);
        int playheadSlices = 0;
        std::size_t lastSlice = 0, playheadSlice = 0;
        for (std::size_t i = 0; i < withPlayhead.size(); ++i)
            if (withPlayhead[i].kind == SpectrumDrawKind::envelopeTrace && withPlayhead[i].isPath())
            {
                if (withPlayhead[i].index == -1) { ++playheadSlices; playheadSlice = i; }
                else lastSlice = i;
            }
        assert(playheadSlices == 1 && playheadSlice > lastSlice);
    }

    // Band labels are spaced, not dropped, when bands are ~12 px tall.
    {
        auto residual = makeSnapshot(SpectralTraceMode::residual);
        residual.residualBands.clear();
        double low = 100.0;
        for (int band = 0; band < 25; ++band)
        {
            residual.residualBands.push_back({ band, low, low * 1.3, { 0.5f, 0.5f } });
            low *= 1.3;
        }
        int labels = 0;
        float previous = -1.0e9f;
        for (const auto& c : SpectrumRenderer::render(residual, { 0.0f, 0.0f, 557.0f, 470.0f },
                                                      SpectrumRenderOptions {}))
            if (c.kind == SpectrumDrawKind::bandLabel)
            {
                ++labels;
                assert(std::abs(c.bounds.y - previous) >= 10.9f || c.bounds.y == previous);
                previous = c.bounds.y;
            }
        assert(labels >= 10);
    }

    // Envelope source B draws behind source A on its own ramp.
    {
        auto snap = makeSnapshot(SpectralTraceMode::isometricEnvelope);
        auto overlay = snap;
        SpectrumRenderOptions options;
        options.overlaySnapshot = &overlay;
        const auto cmds = SpectrumRenderer::render(snap, bounds, options);
        int lastOverlay = -1, firstPrimary = -1;
        for (int i = 0; i < static_cast<int>(cmds.size()); ++i)
            if (cmds[static_cast<std::size_t>(i)].kind == SpectrumDrawKind::envelopeTrace)
            {
                if (cmds[static_cast<std::size_t>(i)].colorRamp == 1) lastOverlay = i;
                else if (firstPrimary < 0) firstPrimary = i;
            }
        assert(lastOverlay >= 0 && firstPrimary > lastOverlay);
    }

    // The envelope edit range is a slab through the viewport, not a planar box.
    {
        auto snap = makeSnapshot(SpectralTraceMode::isometricEnvelope);
        snap.durationSeconds = 2.0;
        SpectrumRenderOptions options;
        options.envelopeEditing = true;
        options.envelopeTimeStart = 0.25f;
        options.envelopeTimeEnd = 0.5f;
        options.envelopeFrequencyStart = 0.0f;
        options.envelopeFrequencyEnd = 1.0f;
        const auto cmds = SpectrumRenderer::render(snap, bounds, options);
        int slabPolygons = 0;
        bool label = false;
        for (const auto& c : cmds)
        {
            if (c.kind == SpectrumDrawKind::selectionMarker && c.filled) ++slabPolygons;
            if (c.kind == SpectrumDrawKind::badge && c.label == "0.500 - 1.000 s") label = true;
        }
        assert(slabPolygons == 1 && label);
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

    // Overlays follow the time zoom: the playhead, onset markers and the
    // detector curves used to be drawn on the unzoomed axis.
    {
        auto snap = makeSnapshot(SpectralTraceMode::combined);
        snap.onsets = { { 0.75f, 0.70f, 0.80f, 1.0f } };
        for (int i = 0; i <= 32; ++i)
            snap.smearnovDetectionCurve.push_back(static_cast<float>(i % 4) * 0.25f);
        SpectrumRenderOptions options;
        options.showPlayhead = true;
        options.playheadNorm = 0.75f;
        options.showOnsetOverlay = true;
        options.viewport.timeCenter = 0.75f;
        options.viewport.timeZoom = 4.0f;
        const auto plot = SpectrumRenderer::getPlotBounds(bounds, snap);
        const auto centreX = plot.x + plot.width * 0.5f;
        const auto cmds = SpectrumRenderer::render(snap, bounds, options);
        bool playheadAtCentre = false, markerAtCentre = false, curve = false;
        for (const auto& c : cmds)
        {
            if (c.kind == SpectrumDrawKind::playhead && c.isPath())
                playheadAtCentre = playheadAtCentre || std::abs(c.vertices.front().x - centreX) < 0.01f;
            if (c.kind == SpectrumDrawKind::onsetMarker && c.isPath())
                markerAtCentre = markerAtCentre || std::abs(c.vertices.front().x - centreX) < 0.01f;
            if (c.kind == SpectrumDrawKind::smearnovCurve && c.isPath())
            {
                curve = true;
                for (const auto& v : c.vertices)
                    // The curves live in a strip at the bottom of the plot.
                    assert(v.y >= plot.y + plot.height * 0.5f);
            }
        }
        assert(playheadAtCentre && markerAtCentre && curve);

        options.playheadNorm = 0.1f; // outside the zoomed window
        for (const auto& c : SpectrumRenderer::render(snap, bounds, options))
            assert(c.kind != SpectrumDrawKind::playhead);
    }

    // Gutter labels: frequency labels follow the zoom; band labels sit on the band.
    {
        auto snap = makeSnapshot(SpectralTraceMode::partials);
        const auto plot = SpectrumRenderer::getPlotBounds(bounds, snap);
        const auto labels = [&](SpectrumViewport viewport) {
            std::vector<std::string> result;
            for (const auto& c : SpectrumRenderer::render(snap, bounds, viewport))
                if (c.kind == SpectrumDrawKind::freqLabel)
                {
                    assert(c.bounds.y + 5.0f >= plot.y - 0.01f);
                    assert(c.bounds.y + 5.0f <= plot.y + plot.height + 0.01f);
                    result.push_back(c.label);
                }
            return result;
        };
        const auto full = labels({});
        SpectrumViewport zoomed;
        zoomed.frequencyCenter = 0.2f;
        zoomed.frequencyZoom = 4.0f;
        const auto low = labels(zoomed);
        assert(!full.empty() && !low.empty() && full != low);
        assert(std::find(full.begin(), full.end(), "10k") != full.end());
        assert(std::find(low.begin(), low.end(), "10k") == low.end());

        auto residual = makeSnapshot(SpectralTraceMode::residual);
        residual.residualBands = { { 4, 200.0, 400.0, { 0.2f, 0.8f } },
                                   { 5, 400.0, 2000.0, { 0.5f, 0.5f } } };
        const UiRect tall { 10.0f, 20.0f, 400.0f, 420.0f };
        const auto residualPlot = SpectrumRenderer::getPlotBounds(tall, residual);
        const auto logRange = std::log(20000.0 / 20.0);
        const auto yForHz = [&](double hz) {
            return residualPlot.y + residualPlot.height
                - residualPlot.height * static_cast<float>(std::log(hz / 20.0) / logRange);
        };
        bool found = false;
        for (const auto& c : SpectrumRenderer::render(residual, tall, SpectrumRenderOptions {}))
            if (c.kind == SpectrumDrawKind::bandLabel && c.index == 5 && c.label == "B5")
            {
                found = true;
                const auto centre = (yForHz(400.0) + yForHz(2000.0)) * 0.5f;
                assert(std::abs(c.bounds.y + 9.0f - centre) < 0.5f);
            }
        assert(found);
    }

    // A partial keeps its amplitude ramp along its length: per-stop alpha.
    {
        auto snap = makeSnapshot(SpectralTraceMode::partials);
        snap.partials.front().points = {
            { 0, 0.0f, 0.5f, 0.05f, 440.0, 440.0, 0.1, true },
            { 1, 0.5f, 0.5f, 0.50f, 440.0, 440.0, 0.5, true },
            { 2, 1.0f, 0.5f, 0.95f, 440.0, 440.0, 0.9, true } };
        float lowest = 1.0f, highest = 0.0f;
        for (const auto& c : SpectrumRenderer::render(snap, bounds, SpectrumRenderOptions {}))
            if (c.kind == SpectrumDrawKind::partialTrace)
                for (const auto& stop : c.stops)
                {
                    lowest = std::min(lowest, stop.alpha);
                    highest = std::max(highest, stop.alpha);
                }
        assert(highest - lowest > 0.3f);
    }

    // Isometric views cull out-of-range data instead of projecting it beside
    // the floor, and draw no planar time labels or planar residual selection.
    {
        auto snap = makeSnapshot(SpectralTraceMode::isometric);
        SpectralTrace early;
        early.partialIndex = 50;
        early.points = { { 0, 0.02f, 0.5f, 0.9f, 440.0, 440.0, 0.9, true },
                         { 1, 0.08f, 0.5f, 0.9f, 440.0, 440.0, 0.9, true } };
        snap.partials.push_back(early);
        SpectrumRenderOptions options;
        options.viewport.timeCenter = 0.75f;
        options.viewport.timeZoom = 4.0f;
        for (const auto& c : SpectrumRenderer::render(snap, bounds, options))
        {
            assert(c.index != 50 || (c.kind != SpectrumDrawKind::partialTrace
                                     && c.kind != SpectrumDrawKind::floorShadow));
            assert(c.kind != SpectrumDrawKind::axisLabel);
        }

        auto isoResidual = makeSnapshot(SpectralTraceMode::isometricResidual);
        SpectrumRenderOptions selectedBand;
        selectedBand.selectedBands = { 4 };
        selectedBand.residualLastFrame = 1;
        bool highlighted = false;
        for (const auto& c : SpectrumRenderer::render(isoResidual, bounds, selectedBand))
        {
            assert(c.kind != SpectrumDrawKind::selectionMarker);
            highlighted = highlighted || (c.kind == SpectrumDrawKind::glowHalo && c.index == 4);
        }
        assert(highlighted);
    }

    // Planar residual is a filled ridge, brighter in the residual view.
    {
        auto residual = makeSnapshot(SpectralTraceMode::residual);
        auto combined = makeSnapshot(SpectralTraceMode::combined);
        const auto fillAlpha = [&](const SpectralTraceSnapshot& snap) {
            for (const auto& c : SpectrumRenderer::render(snap, bounds, SpectrumRenderOptions {}))
                if (c.kind == SpectrumDrawKind::envelopeFill && c.isStrip() && c.index == 4)
                    return c.alpha;
            return 0.0f;
        };
        assert(fillAlpha(residual) > fillAlpha(combined) && fillAlpha(combined) > 0.0f);
    }

    // The Time... edit range is shaded in planar views only.
    {
        const auto shaded = [&bounds](ats::ui::SpectralTraceMode mode) {
            auto snap = makeSnapshot(mode);
            snap.frameCount = 101;
            SpectrumRenderOptions options;
            options.timeSelectionFirstFrame = 20;
            options.timeSelectionLastFrame = 60;
            for (const auto& c : SpectrumRenderer::render(snap, bounds, options))
                if (c.kind == SpectrumDrawKind::selectionMarker && c.index == -30 && c.filled)
                    return true;
            return false;
        };
        assert(shaded(SpectralTraceMode::combined));
        assert(shaded(SpectralTraceMode::partials));
        assert(!shaded(SpectralTraceMode::isometric));
    }

    // --- SpectrumLUT: the fixed blue/cyan/mint/lime ramp is theme independent ---
    {
        const auto c1 = SpectrumLUT::colorForAmp(0.5f, ThemeId::ember);
        const auto c2 = SpectrumLUT::colorForAmp(0.5f, ThemeId::porcelain);
        assert(c1 == c2);
        assert((c1 >> 24) == 0xff && (c2 >> 24) == 0xff); // fully opaque
    }

    // Frame geometry budget: a large model in a data-scaled mode must be
    // thinned so the painter stays near kMaxPathSegmentsPerFrame; a small one
    // is drawn at full resolution.
    {
        SpectralTraceInput large;
        large.source = SpectralTraceSource::current;
        large.sourceRevision = 1;
        large.displayShapeRevision = 1;
        large.durationSeconds = 20.0;
        large.partialCount = 400;
        large.frameCount = 2000;
        large.nyquistHz = 24000.0;
        large.displayAmpPeak = 1.0;
        large.displayResidualPeak = 1.0;
        for (int partial = 0; partial < 400; ++partial)
        {
            SpectralPartialInput trace;
            trace.partialIndex = partial;
            trace.frequencyShapeRank = partial + 1;
            for (int frame = 0; frame < 2000; ++frame)
                trace.frames.push_back({ 220.0 + partial * 40.0 + frame * 0.1,
                                         0.05 + 0.5 * ((partial * 7 + frame) % 13) / 13.0 });
            large.partials.push_back(std::move(trace));
        }
        const auto snapshot = SpectralTraceBuilder::build(large, SpectralTraceMode::isometric);
        const auto commands = SpectrumRenderer::render(snapshot, { 0.0f, 0.0f, 556.0f, 280.0f });
        const auto stride = SpectrumRenderer::pathSegmentStride(commands);
        std::size_t total = 0, drawn = 0, paths = 0;
        for (const auto& command : commands)
        {
            if (!command.isPath())
                continue;
            ++paths;
            const auto segments = command.vertices.size() - 1;
            total += segments;
            drawn += (segments + stride - 1) / stride;
        }
        assert(total > SpectrumRenderer::kMaxPathSegmentsPerFrame);
        assert(stride > 1);
        assert(drawn <= SpectrumRenderer::kMaxPathSegmentsPerFrame + paths);
        assert(SpectrumRenderer::pathSegmentStride(commands, total) == 1);
    }

    return 0;
}
