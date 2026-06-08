#include "TimbreFrameAdapter.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace ats
{

namespace
{

bool isFiniteAndPositive(double value)
{
    return std::isfinite(value) && value > 0.0;
}

bool isFiniteAndNonNegative(double value)
{
    return std::isfinite(value) && value >= 0.0;
}

} // namespace

ValidatedFrameSamples validateEnvelopeFrameInput(
    const EnvelopeFrameInput& frame,
    const double amplitudeFloor)
{
    ValidatedFrameSamples out;
    out.sourceModelId = frame.sourceModelId;
    out.frameIndex = frame.frameIndex;
    out.sampleRate = frame.sampleRate;
    out.waveformAvailable = frame.waveformAvailable && !frame.waveformSpectrum.empty();
    out.residualBandsPresent = !frame.residualBands.empty();
    out.fundamentalFrequencyHz = frame.fundamentalFrequencyHz;
    out.f0Confidence = frame.f0Confidence;

    const double floor = std::max(amplitudeFloor, 0.0);

    out.partials.reserve(frame.partials.size());
    for (const auto& partial : frame.partials)
    {
        if (!partial.selected)
        {
            ++out.invalidPartialCount;
            continue;
        }
        if (!isFiniteAndPositive(partial.frequencyHz)
            || !std::isfinite(partial.amplitude)
            || partial.amplitude < floor)
        {
            ++out.invalidPartialCount;
            continue;
        }

        PartialEnergySample sample;
        sample.frequencyHz = partial.frequencyHz;
        sample.amplitude = partial.amplitude;
        sample.energy = partial.amplitude * partial.amplitude;
        sample.confidence = std::isfinite(partial.confidence)
            ? std::clamp(partial.confidence, 0.0, 1.0)
            : 0.0;
        sample.selected = partial.selected;
        out.partials.push_back(sample);
    }

    out.residualBands.reserve(frame.residualBands.size());
    for (const auto& band : frame.residualBands)
    {
        if (!isFiniteAndPositive(band.frequencyHz)
            || !isFiniteAndNonNegative(band.energy))
        {
            ++out.invalidResidualCount;
            continue;
        }
        if (band.energy < floor)
        {
            // Treat sub-floor band energy as zero but still record the band
            // so coverage statistics see the band exists.
            ResidualEnergySample sample;
            sample.frequencyHz = band.frequencyHz;
            sample.energy = 0.0;
            sample.confidence = std::isfinite(band.confidence)
                ? std::clamp(band.confidence, 0.0, 1.0)
                : 0.0;
            sample.orphanBand = band.orphanBand;
            out.residualBands.push_back(sample);
            continue;
        }

        ResidualEnergySample sample;
        sample.frequencyHz = band.frequencyHz;
        sample.energy = band.energy;
        sample.confidence = std::isfinite(band.confidence)
            ? std::clamp(band.confidence, 0.0, 1.0)
            : 0.0;
        sample.orphanBand = band.orphanBand;
        out.residualBands.push_back(sample);
    }

    std::sort(out.partials.begin(), out.partials.end(),
              [](const PartialEnergySample& a, const PartialEnergySample& b) {
                  return a.frequencyHz < b.frequencyHz;
              });
    std::sort(out.residualBands.begin(), out.residualBands.end(),
              [](const ResidualEnergySample& a, const ResidualEnergySample& b) {
                  return a.frequencyHz < b.frequencyHz;
              });

    double partialMin = std::numeric_limits<double>::infinity();
    double partialMax = -std::numeric_limits<double>::infinity();
    for (const auto& sample : out.partials)
    {
        out.partialEnergyTotal += sample.energy;
        partialMin = std::min(partialMin, sample.frequencyHz);
        partialMax = std::max(partialMax, sample.frequencyHz);
    }
    if (out.partials.empty())
    {
        partialMin = 0.0;
        partialMax = 0.0;
    }
    out.minPartialFrequencyHz = partialMin;
    out.maxPartialFrequencyHz = partialMax;

    double residualMin = std::numeric_limits<double>::infinity();
    double residualMax = -std::numeric_limits<double>::infinity();
    for (const auto& sample : out.residualBands)
    {
        out.residualEnergyTotal += sample.energy;
        residualMin = std::min(residualMin, sample.frequencyHz);
        residualMax = std::max(residualMax, sample.frequencyHz);
    }
    if (out.residualBands.empty())
    {
        residualMin = 0.0;
        residualMax = 0.0;
    }
    out.minResidualFrequencyHz = residualMin;
    out.maxResidualFrequencyHz = residualMax;

    return out;
}

void populateEnvelopeFrameInput(
    const PartialExtractionInput& partials,
    const ResidualExtractionInput* residual,
    const double sampleRate,
    std::string sourceModelId,
    const bool waveformAvailable,
    const double fundamentalFrequencyHz,
    const double f0Confidence,
    const double amplitudeFloor,
    EnvelopeFrameInput& out)
{
    out = {};
    out.sourceModelId = std::move(sourceModelId);
    out.frameIndex = static_cast<std::uint64_t>(std::max(partials.frameIndex, 0));
    out.sampleRate = sampleRate;
    out.waveformAvailable = waveformAvailable;
    out.fundamentalFrequencyHz = fundamentalFrequencyHz;
    out.f0Confidence = f0Confidence;

    const double floor = std::max(amplitudeFloor, 0.0);

    if (partials.partialCount > 0
        && partials.amplitudeByPartial != nullptr
        && partials.frequencyByPartial != nullptr)
    {
        out.partials.reserve(static_cast<std::size_t>(partials.partialCount));
        for (int partialIndex = 0; partialIndex < partials.partialCount; ++partialIndex)
        {
            const double* ampSeries = partials.amplitudeByPartial[partialIndex];
            const double* freqSeries = partials.frequencyByPartial[partialIndex];
            if (ampSeries == nullptr || freqSeries == nullptr)
                continue;

            const double amplitude = ampSeries[partials.frameIndex];
            const double frequency = freqSeries[partials.frameIndex];

            if (!isFiniteAndPositive(frequency)
                || !std::isfinite(amplitude)
                || amplitude < floor)
            {
                continue;
            }

            EnvelopePartialPoint point;
            point.frequencyHz = frequency;
            point.amplitude = amplitude;
            point.smr = partials.defaultSmr;
            point.confidence = std::clamp(partials.defaultConfidence, 0.0, 1.0);
            point.selected = true;
            out.partials.push_back(point);
        }
    }

    if (residual != nullptr
        && residual->bandCount > 0
        && residual->energyByBand != nullptr
        && residual->bandCenterFrequencyHz)
    {
        out.residualBands.reserve(static_cast<std::size_t>(residual->bandCount));
        for (int bandIndex = 0; bandIndex < residual->bandCount; ++bandIndex)
        {
            const double* series = residual->energyByBand[bandIndex];
            if (series == nullptr)
                continue;

            const double energy = series[residual->frameIndex];
            const double frequency = residual->bandCenterFrequencyHz(bandIndex);
            if (!isFiniteAndPositive(frequency)
                || !isFiniteAndNonNegative(energy))
            {
                continue;
            }
            if (energy <= 0.0)
                continue;

            EnvelopeResidualBandPoint point;
            point.frequencyHz = frequency;
            point.energy = energy;
            point.confidence = std::clamp(residual->defaultConfidence, 0.0, 1.0);
            point.orphanBand = residual->bandIsOrphan
                ? residual->bandIsOrphan(bandIndex)
                : false;
            out.residualBands.push_back(point);
        }
    }
}

ValidatedTrackSet validatePartialTracks(
    const std::vector<PartialTrack>& tracks,
    const double amplitudeFloor)
{
    ValidatedTrackSet out;
    const double floor = std::max(amplitudeFloor, 0.0);

    out.tracks.reserve(tracks.size());
    for (const auto& track : tracks)
    {
        PartialTrack kept;
        kept.trackId = track.trackId;
        kept.samples.reserve(track.samples.size());

        bool sawPresent = false;
        for (const auto& sample : track.samples)
        {
            PartialTrackSample copy = sample;
            if (!std::isfinite(copy.timeSeconds))
                copy.present = false;
            if (!std::isfinite(copy.amplitude) || copy.amplitude < floor)
                copy.present = false;
            if (!std::isfinite(copy.frequencyHz) || copy.frequencyHz <= 0.0)
                copy.present = false;
            if (copy.present)
                sawPresent = true;
            kept.samples.push_back(copy);
        }

        if (kept.samples.empty() || !sawPresent)
        {
            ++out.droppedTrackCount;
            continue;
        }

        out.tracks.push_back(std::move(kept));
    }
    return out;
}

} // namespace ats
