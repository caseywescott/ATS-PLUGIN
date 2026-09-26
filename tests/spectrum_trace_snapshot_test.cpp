#include "../src/ui/SpectrumViewModel.h"

#include <cassert>
#include <cmath>

int main()
{
    ats::ui::SpectralTraceInput input;
    input.source = ats::ui::SpectralTraceSource::sourceA;
    input.sourceRevision = 4;
    input.displayShapeRevision = 2;
    input.durationSeconds = 1.0;
    for (int partial = 0; partial < 4; ++partial)
    {
        ats::ui::SpectralPartialInput trace;
        trace.partialIndex = partial;
        trace.frequencyShapeRank = partial + 1;
        for (int frame = 0; frame < 9; ++frame)
            trace.frames.push_back({ 100.0 + partial * 100.0 + frame, 0.1 + partial * 0.2 });
        trace.frames[3] = { 0.0, 0.0 };
        input.partials.push_back(std::move(trace));
    }
    ats::ui::SpectralResidualBandInput band;
    band.bandIndex = 3;
    band.lowHz = 250.0;
    band.highHz = 350.0;
    band.energy = { 0.01, 0.02, 0.03, 0.04, 0.05 };
    input.residualBands.push_back(band);
    input.onsets = { { 0.75, 1.0 }, { 0.25, 0.8 } };
    input.envelope = { 0.1, 0.9, 0.4 };

    const ats::ui::SpectralTraceLimits limits { 2, 4, 3 };
    const auto snapshot = ats::ui::SpectralTraceBuilder::build(input,
                                                                 ats::ui::SpectralTraceMode::combined,
                                                                 limits);
    assert(snapshot.source == ats::ui::SpectralTraceSource::sourceA);
    assert(snapshot.partials.size() == 2);
    assert(snapshot.partials[0].points.size() <= 9);
    assert(snapshot.partials[0].points.front().active);
    assert(snapshot.residualBands.size() == 1);
    assert(snapshot.residualBands[0].energyNorm.size() == 16);
    assert(snapshot.onsetNorms.size() == 2 && snapshot.onsetNorms[0] == 0.75f);
    assert(snapshot.envelopeNorm.size() == 3 && snapshot.envelopeNorm[1] == 0.9f);
    for (const auto& trace : snapshot.partials)
        for (const auto& point : trace.points)
            assert(std::isfinite(point.freqNorm) && std::isfinite(point.ampNorm));

    ats::ui::SpectralTraceInput gainUpInput;
    gainUpInput.displayAmpPeak = 1.0;
    ats::ui::SpectralPartialInput gainUpPartial;
    gainUpPartial.partialIndex = 0;
    gainUpPartial.frames.push_back({ 440.0, 4.0 });
    gainUpInput.partials.push_back(std::move(gainUpPartial));
    const auto gainUp = ats::ui::SpectralTraceBuilder::build(
        gainUpInput, ats::ui::SpectralTraceMode::partials);
    assert(gainUp.partials.size() == 1 && gainUp.partials.front().peakAmpNorm > 1.0f);
    assert(gainUp.partials.front().points.front().ampNorm > 1.0f);

    const auto residualOnly = ats::ui::SpectralTraceBuilder::build(
        input, ats::ui::SpectralTraceMode::residual, limits);
    assert(residualOnly.partials.size() == snapshot.partials.size() && residualOnly.hasResidual());

    ats::ui::SpectralTraceInput residualInput;
    residualInput.residualBands.push_back({ 0, 800.0, 1200.0, { 1.0, 0.5 } });
    const auto residualBounds = ats::ui::SpectralTraceBuilder::build(
        residualInput, ats::ui::SpectralTraceMode::residual);
    assert(residualBounds.minFrequencyHz < 800.0);
    assert(residualBounds.maxFrequencyHz > 1200.0);

    ats::ui::SpectrumViewModel view;
    assert(view.accept(snapshot));
    auto stale = snapshot;
    stale.sourceRevision = 3;
    assert(!view.accept(std::move(stale)));
    auto shapeStale = snapshot;
    shapeStale.displayShapeRevision = 1;
    assert(!view.accept(std::move(shapeStale)));
    auto newer = snapshot;
    newer.sourceRevision = 5;
    assert(view.accept(std::move(newer)));

    ats::ui::SpectrumViewModel shapeView;
    auto shaped = snapshot;
    shaped.frequencyShapeMode = ats::ui::FrequencyShapeMode::Stretch;
    shaped.frequencyShapeAmount = 1.4;
    shaped.frequencyShapePivotHz = 440.0;
    shaped.resolvedFrequencyShapePivotHz = 440.0;
    assert(shapeView.accept(shaped));
    const auto pivotCache = shapeView.cacheRevision();
    shaped.frequencyShapePivotHz = 880.0;
    shaped.resolvedFrequencyShapePivotHz = 880.0;
    assert(shapeView.accept(std::move(shaped)));
    assert(shapeView.cacheRevision() != pivotCache);
    return 0;
}
