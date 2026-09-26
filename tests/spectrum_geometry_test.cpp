#include "../src/ui/SpectrumGeometry.h"
#include "../src/ui/SpectrumInteractionModel.h"

#include <cassert>
#include <cmath>

using namespace ats::ui;

int main()
{
    const UiRect local { 0.0f, 0.0f, 1200.0f, 900.0f };
    SpectralTraceSnapshot partials;
    partials.mode = SpectralTraceMode::partials;
    partials.frameCount = 20;
    const auto screen = SpectrumGeometry::getScreenBounds(local, partials.mode);
    const auto plot = SpectrumGeometry::getPlotBounds(local, partials.mode, 0, partials.frameCount);
    assert(screen.x == 5.0f && screen.y == 35.0f);
    assert(plot.x > screen.x && plot.width > 0.0f && plot.height > 0.0f);
    assert(SpectrumGeometry::showsFreqLabelGutter(partials.mode, partials.frameCount));
    assert(!SpectrumGeometry::showsBandLabelGutter(partials.mode, 4, partials.frameCount));
    assert(SpectrumGeometry::kMinZoom == 1.0f && SpectrumGeometry::kMaxZoom == 32.0f);
    assert(0.085f + 0.055f + 0.44f + 0.30f * 1.25f <= 1.0f);

    SpectrumViewport viewport;
    const auto center = SpectrumGeometry::positionForPoint(
        SpectralTracePoint { 0, 0.5f, 0.5f, 0.0f }, plot, viewport);
    assert(std::abs(center.x - (plot.x + plot.width * 0.5f)) < 1.0e-4f);
    assert(std::abs(center.y - (plot.y + plot.height * 0.5f)) < 1.0e-4f);

    const auto iso = SpectrumGeometry::positionForIsoCoords(0.3f, 0.4f, 0.0f, plot, viewport);
    assert(std::abs(SpectrumGeometry::timeNormFromIsoPoint(iso, plot, viewport) - 0.3f) < 1.0e-4f);
    float time = 0.0f, freq = 0.0f;
    SpectrumGeometry::isoTimeFreqFromPoint(iso, plot, viewport, time, freq);
    assert(std::abs(time - 0.3f) < 1.0e-4f && std::abs(freq - 0.4f) < 1.0e-4f);

    SpectralTraceSnapshot snapshot;
    snapshot.mode = SpectralTraceMode::partials;
    snapshot.frameCount = 2;
    snapshot.minFrequencyHz = 100.0;
    snapshot.maxFrequencyHz = 1000.0;
    snapshot.partials.push_back({ 4, 0.8f, 440.0, 440.0, 0,
                                  { { 0, 0.25f, 0.37f, 0.55f, 0.8f, 440.0, 440.0, true },
                                    { 1, 0.75f, 0.72f, 0.60f, 0.8f, 440.0, 440.0, true } } });
    SpectrumInteractionModel interaction;
    interaction.setMode(SpectralTraceMode::partials);
    interaction.setSnapshot(&snapshot, { 0.0f, 0.0f, 800.0f, 500.0f });
    const auto crossing = SpectrumGeometry::positionForPoint(
        { 0, 0.5f, 0.545f, 0.0f },
        SpectrumGeometry::getPlotBounds({ 0.0f, 0.0f, 800.0f, 500.0f },
                                        snapshot.mode, 0, snapshot.frameCount), {});
    assert(interaction.traceIntersectsLasso(snapshot.partials.front(),
                                             { crossing.x - 3.0f, crossing.y - 3.0f, 6.0f, 6.0f }));

    snapshot.mode = SpectralTraceMode::isometric;
    SpectrumInteractionModel isoInteraction;
    isoInteraction.setMode(SpectralTraceMode::isometric);
    isoInteraction.setSnapshot(&snapshot, { 0.0f, 0.0f, 800.0f, 500.0f });
    const auto isoPlot = SpectrumGeometry::getPlotBounds(
        { 0.0f, 0.0f, 800.0f, 500.0f }, snapshot.mode, 0, snapshot.frameCount);
    const auto isoHit = SpectrumGeometry::positionForIsoCoords(0.3f, 0.4f, 0.0f, isoPlot, {});
    assert(std::abs(isoInteraction.timeNormFromScreenPoint(isoHit) - 0.3f) < 1.0e-4f);
    assert(std::abs(isoInteraction.freqNormFromScreenPoint(isoHit) - 0.4f) < 1.0e-4f);

    SpectrumInteractionModel navigation;
    assert(navigation.zoom(4.0f, 0.5f, 0.5f));
    assert(navigation.scrollTimeTo(0.9f));
    assert(navigation.navigation().timeEnd <= 1.0f + 1.0e-6f);
    assert(navigation.navigation().timeStart > 0.5f);
    assert(navigation.scrollFrequencyTo(0.1f));
    assert(navigation.navigation().frequencyStart >= 0.0f);
    return 0;
}
