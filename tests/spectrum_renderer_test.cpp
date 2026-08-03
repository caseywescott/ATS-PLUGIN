#include "../src/ui/SpectrumRenderer.h"

#include <cassert>

int main()
{
    ats::ui::SpectralTraceSnapshot snapshot;
    snapshot.mode = ats::ui::SpectralTraceMode::combined;
    snapshot.residualBands.push_back({ 4, 200.0, 400.0, { 0.2f, 0.8f } });
    ats::ui::SpectralTrace trace;
    trace.partialIndex = 7;
    trace.points = { { 0, 0.0f, 0.2f, 0.4f, 220.0, 220.0, 0.2, true },
                     { 1, 1.0f, 0.8f, 0.9f, 440.0, 440.0, 0.8, true } };
    snapshot.partials.push_back(trace);
    snapshot.onsetNorms = { 0.25f, 0.75f };
    snapshot.envelopeNorm = { 0.1f, 0.8f, 0.4f };

    const ats::ui::UiRect bounds { 10.0f, 20.0f, 400.0f, 200.0f };
    auto ember = ats::ui::SpectrumRenderer::render(snapshot, bounds,
        { ats::ui::ThemeId::ember, 7, 0.25f, true });
    auto porcelain = ats::ui::SpectrumRenderer::render(snapshot, bounds,
        { ats::ui::ThemeId::porcelain, 7, 0.25f, true });
    assert(!ember.empty() && ember.front().kind == ats::ui::SpectrumDrawKind::background);
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
        if (ember[i].kind == ats::ui::SpectrumDrawKind::residualBand && residualIndex == ember.size()) residualIndex = i;
        if (ember[i].kind == ats::ui::SpectrumDrawKind::selectedPartial && traceIndex == ember.size()) traceIndex = i;
        if (ember[i].kind == ats::ui::SpectrumDrawKind::playhead) hasPlayhead = true;
        if (ember[i].kind == ats::ui::SpectrumDrawKind::selectedPartial) hasSelected = true;
        if (ember[i].kind == ats::ui::SpectrumDrawKind::axisTick) hasAxes = true;
        if (ember[i].kind == ats::ui::SpectrumDrawKind::onsetMarker) hasOnsets = true;
        if (ember[i].kind == ats::ui::SpectrumDrawKind::envelopeTrace) hasEnvelope = true;
    }
    assert(residualIndex < traceIndex && hasPlayhead && hasSelected && hasAxes && hasOnsets && hasEnvelope);
    assert(ember.back().kind == ats::ui::SpectrumDrawKind::legend);
    return 0;
}
