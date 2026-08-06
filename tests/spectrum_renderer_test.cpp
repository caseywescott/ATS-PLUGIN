#include "../src/ui/SpectrumRenderer.h"

#include <cassert>

using namespace ats::ui;

static SpectralTraceSnapshot makeSnapshot(SpectralTraceMode mode)
{
    SpectralTraceSnapshot snap;
    snap.mode = mode;
    snap.minFrequencyHz = 20.0;
    snap.maxFrequencyHz = 20000.0;
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

    // --- combined mode (baseline) ---
    {
        auto snap = makeSnapshot(SpectralTraceMode::combined);
        auto ember = SpectrumRenderer::render(snap, bounds,
            { ThemeId::ember, 7, 0.25f, true });
        auto porcelain = SpectrumRenderer::render(snap, bounds,
            { ThemeId::porcelain, 7, 0.25f, true });
        assert(!ember.empty() && ember.front().kind == SpectrumDrawKind::background);
        assert(ember.front().color == porcelain.front().color);
        assert(ember.front().color == 0xff101513u);

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
        assert(residualIndex < traceIndex && hasPlayhead && hasSelected
            && hasAxes && hasOnsets && hasEnvelope);
        assert(ember.back().kind == SpectrumDrawKind::legend);
        // combined should have ridgeline for partials
        assert(hasKind(ember, SpectrumDrawKind::ridgeline));
        // selected partial should have glow halo
        assert(hasKind(ember, SpectrumDrawKind::glowHalo));
    }

    // --- partials mode: LUT-colored, ridgeline, no residualBand ---
    {
        auto snap = makeSnapshot(SpectralTraceMode::partials);
        snap.residualBands.clear(); // builder removes residual in partials mode
        auto cmds = SpectrumRenderer::render(snap, bounds, { ThemeId::ember, -1, 0.0f, false });
        assert(!cmds.empty() && cmds.front().kind == SpectrumDrawKind::background);
        assert(cmds.back().kind == SpectrumDrawKind::legend);
        assert(!hasKind(cmds, SpectrumDrawKind::residualBand));
        assert(hasKind(cmds, SpectrumDrawKind::partialTrace));
        assert(hasKind(cmds, SpectrumDrawKind::ridgeline));
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
    }

    // --- isometric mode: produces isometricTile ---
    {
        auto snap = makeSnapshot(SpectralTraceMode::isometric);
        snap.residualBands.clear();
        auto cmds = SpectrumRenderer::render(snap, bounds, { ThemeId::ember, -1, 0.0f, false });
        assert(cmds.back().kind == SpectrumDrawKind::legend);
        assert(hasKind(cmds, SpectrumDrawKind::isometricTile));
        assert(!hasKind(cmds, SpectrumDrawKind::partialTrace));
        assert(!hasKind(cmds, SpectrumDrawKind::residualBand));
    }

    // --- isometricResidual: produces isometricTile from bands ---
    {
        auto snap = makeSnapshot(SpectralTraceMode::isometricResidual);
        snap.partials.clear();
        auto cmds = SpectrumRenderer::render(snap, bounds, { ThemeId::ember, -1, 0.0f, false });
        assert(cmds.back().kind == SpectrumDrawKind::legend);
        assert(hasKind(cmds, SpectrumDrawKind::isometricTile));
    }

    // --- isometricEnvelope: produces isometricTile from envelope ---
    {
        auto snap = makeSnapshot(SpectralTraceMode::isometricEnvelope);
        snap.partials.clear();
        snap.residualBands.clear();
        auto cmds = SpectrumRenderer::render(snap, bounds, { ThemeId::ember, -1, 0.0f, false });
        assert(cmds.back().kind == SpectrumDrawKind::legend);
        assert(hasKind(cmds, SpectrumDrawKind::isometricTile));
    }

    // --- isometricEnvelopeRdc: same plus RDC floor tiles ---
    {
        auto snap = makeSnapshot(SpectralTraceMode::isometricEnvelopeRdc);
        snap.partials.clear();
        snap.residualBands.clear();
        auto cmds = SpectrumRenderer::render(snap, bounds, { ThemeId::ember, -1, 0.0f, false });
        assert(cmds.back().kind == SpectrumDrawKind::legend);
        assert(hasKind(cmds, SpectrumDrawKind::isometricTile));
        // RDC floor should produce multiple tiles
        int isoCount = 0;
        for (const auto& c : cmds) if (c.kind == SpectrumDrawKind::isometricTile) ++isoCount;
        assert(isoCount > 20); // at least one full RDC floor grid row
    }

    // --- SpectrumLUT: porcelain and ember produce different colors ---
    {
        const auto c1 = SpectrumLUT::colorForAmp(0.5f, ThemeId::ember);
        const auto c2 = SpectrumLUT::colorForAmp(0.5f, ThemeId::porcelain);
        assert(c1 != c2);
        assert((c1 >> 24) == 0xff && (c2 >> 24) == 0xff); // fully opaque
    }

    return 0;
}
