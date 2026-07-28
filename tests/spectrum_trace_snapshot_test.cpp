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

    const ats::ui::SpectralTraceLimits limits { 2, 4, 3 };
    const auto snapshot = ats::ui::SpectralTraceBuilder::build(input,
                                                                 ats::ui::SpectralTraceMode::combined,
                                                                 limits);
    assert(snapshot.source == ats::ui::SpectralTraceSource::sourceA);
    assert(snapshot.partials.size() == 2);
    assert(snapshot.partials[0].points.size() <= 4);
    assert(snapshot.partials[0].points.front().active);
    assert(snapshot.residualBands.size() == 1);
    assert(snapshot.residualBands[0].energyNorm.size() == 3);
    for (const auto& trace : snapshot.partials)
        for (const auto& point : trace.points)
            assert(std::isfinite(point.freqNorm) && std::isfinite(point.ampNorm));

    const auto residualOnly = ats::ui::SpectralTraceBuilder::build(
        input, ats::ui::SpectralTraceMode::residual, limits);
    assert(residualOnly.partials.empty() && residualOnly.hasResidual());

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
    return 0;
}
