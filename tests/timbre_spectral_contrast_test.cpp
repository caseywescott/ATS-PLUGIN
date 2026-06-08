#include "SpectralEnvelope.h"
#include "TimbreDescriptors.h"
#include "TimbreEnergyDistribution.h"
#include "TimbreFrameAdapter.h"
#include "TimbreFrameAnalysis.h"

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace
{

int fail(const std::string& message)
{
    std::cerr << "timbre_spectral_contrast_test: " << message << '\n';
    return 1;
}

ats::TimbreDescriptorOptions makeOptions()
{
    ats::TimbreDescriptorOptions opts;
    opts.comparisonAxis.type = ats::EnvelopeFrequencyAxis::LinearHz;
    opts.comparisonAxis.minFrequencyHz = 200.0;
    opts.comparisonAxis.maxFrequencyHz = 8200.0;
    opts.comparisonAxis.sampleRate = 48000.0;
    opts.comparisonGridSize = 128;
    return opts;
}

ats::FrameEnvelope stubEnvelope()
{
    ats::FrameEnvelope envelope;
    envelope.estimatorId = "stub/sampler";
    envelope.estimatorVersion = 1;
    envelope.sampleRate = 48000.0;
    envelope.storage = ats::EnvelopeValueStorage::Samples;
    envelope.values = { 1.0 };
    return envelope;
}

struct PeakSpec
{
    double centerHz = 0.0;
    double prominenceDb = 0.0;
    double bandwidthHz = 200.0;
};

ats::EnvelopeSamplerFn raisedCosinePeaksSampler(
    double baselineDb,
    std::vector<PeakSpec> peaks)
{
    return [baselineDb, peaks](const ats::FrameEnvelope&,
                                double frequencyHz,
                                bool* ok) -> double {
        if (frequencyHz <= 0.0)
        {
            if (ok != nullptr) *ok = false;
            return 0.0;
        }
        double db = baselineDb;
        for (const auto& peak : peaks)
        {
            const double diff = frequencyHz - peak.centerHz;
            const double bw = std::max(peak.bandwidthHz, 1.0e-6);
            if (std::abs(diff) >= bw)
                continue;
            const double shape = 0.5 * (1.0 + std::cos(M_PI * diff / bw));
            db += peak.prominenceDb * shape;
        }
        if (ok != nullptr) *ok = true;
        return std::pow(10.0, db / 20.0);
    };
}

ats::EnvelopeSamplerFn constantDbSampler(double db)
{
    const double amp = std::pow(10.0, db / 20.0);
    return [amp](const ats::FrameEnvelope&, double freq, bool* ok) -> double {
        if (freq <= 0.0)
        {
            if (ok != nullptr) *ok = false;
            return 0.0;
        }
        if (ok != nullptr) *ok = true;
        return amp;
    };
}

bool nearAny(const std::vector<ats::TimbreResonancePeak>& peaks,
              double expectedHz,
              double toleranceHz)
{
    for (const auto& peak : peaks)
    {
        if (std::abs(peak.frequencyHz - expectedHz) <= toleranceHz)
            return true;
    }
    return false;
}

} // namespace

int main()
{
    const auto options = makeOptions();
    const auto envelope = stubEnvelope();
    const ats::SpectralContrastOptions contrast;

    // ---- Single resonance: peak at 1500 Hz, +12 dB prominence ----
    {
        const auto sampler = raisedCosinePeaksSampler(
            -40.0, { { 1500.0, 12.0, 250.0 } });
        ats::TimbreDescriptorFrame d;
        ats::analyzeSpectralContrastAndResonancePeaks(
            envelope, options, contrast, sampler, d);
        if (d.resonancePeaksStatus != ats::TimbreDescriptorStatus::Available)
            return fail("single resonance: peaks must be Available");
        if (d.resonancePeaks.size() != 1)
            return fail("single resonance: must report exactly 1 peak");
        if (!nearAny(d.resonancePeaks, 1500.0, 100.0))
            return fail("single resonance: peak must be near 1500 Hz");
        if (d.resonancePeaks.front().prominenceDb < 10.0)
            return fail("single resonance: prominence must be >= 10 dB");
        if (d.spectralContrastDb.value < 10.0)
            return fail("single resonance: contrast must be >= 10 dB");
    }

    // ---- Multiple well-separated resonances ----
    {
        const auto sampler = raisedCosinePeaksSampler(
            -40.0,
            { { 800.0,  10.0, 200.0 },
              { 2400.0, 12.0, 350.0 },
              { 5000.0, 8.0,  500.0 } });
        ats::TimbreDescriptorFrame d;
        ats::analyzeSpectralContrastAndResonancePeaks(
            envelope, options, contrast, sampler, d);
        if (d.resonancePeaks.size() < 3)
            return fail("multiple resonances: must report >= 3 peaks");
        if (!nearAny(d.resonancePeaks, 800.0, 100.0))
            return fail("multiple resonances: missing 800 Hz peak");
        if (!nearAny(d.resonancePeaks, 2400.0, 200.0))
            return fail("multiple resonances: missing 2400 Hz peak");
        if (!nearAny(d.resonancePeaks, 5000.0, 250.0))
            return fail("multiple resonances: missing 5000 Hz peak");
        if (d.spectralContrastDb.status != ats::TimbreDescriptorStatus::Available)
            return fail("multiple resonances: contrast must be Available");
        if (d.spectralContrastDb.value < 8.0)
            return fail("multiple resonances: mean prominence too small");
    }

    // ---- Close peaks: two peaks within minimumSeparationAxis collapse to one ----
    {
        ats::SpectralContrastOptions strict;
        strict.minimumSeparationAxis = 0.2; // axis is [0, 1] so 0.2 is large
        const auto sampler = raisedCosinePeaksSampler(
            -40.0,
            { { 2400.0, 12.0, 200.0 },
              { 2600.0, 10.0, 200.0 } });
        ats::TimbreDescriptorFrame d;
        ats::analyzeSpectralContrastAndResonancePeaks(
            envelope, options, strict, sampler, d);
        if (d.resonancePeaks.size() != 1)
            return fail("close peaks: must collapse to a single peak");
        // Stronger of the two peaks should win.
        if (std::abs(d.resonancePeaks.front().frequencyHz - 2400.0) > 200.0)
            return fail("close peaks: stronger peak should be reported");
    }

    // ---- Flat envelope: TooSparse / no peaks ----
    {
        const auto sampler = constantDbSampler(-20.0);
        ats::TimbreDescriptorFrame d;
        ats::analyzeSpectralContrastAndResonancePeaks(
            envelope, options, contrast, sampler, d);
        if (d.resonancePeaksStatus != ats::TimbreDescriptorStatus::TooSparse)
            return fail("flat envelope: peaks must be TooSparse");
        if (!d.resonancePeaks.empty())
            return fail("flat envelope: peak list must be empty");
        if (d.spectralContrastDb.status != ats::TimbreDescriptorStatus::TooSparse)
            return fail("flat envelope: contrast must be TooSparse");
    }

    // ---- Unreliable envelope: missing sampler -> MissingEnvelope ----
    {
        ats::TimbreDescriptorFrame d;
        ats::analyzeSpectralContrastAndResonancePeaks(
            envelope, options, contrast, {}, d);
        if (d.resonancePeaksStatus != ats::TimbreDescriptorStatus::MissingEnvelope)
            return fail("missing sampler: status must be MissingEnvelope");
        if (d.spectralContrastDb.status != ats::TimbreDescriptorStatus::MissingEnvelope)
            return fail("missing sampler: contrast status must be MissingEnvelope");
        if (d.subbandCentroidsHz.status != ats::TimbreDescriptorStatus::MissingEnvelope)
            return fail("missing sampler: subband status must be MissingEnvelope");
    }

    // ---- Subband centroid correctness: with one peak in each sub-band, the
    //      sub-band centroids should land near the peak frequencies. ----
    {
        const auto sampler = raisedCosinePeaksSampler(
            -40.0,
            { { 1000.0, 24.0, 100.0 },
              { 3000.0, 24.0, 100.0 },
              { 5000.0, 24.0, 100.0 },
              { 7000.0, 24.0, 100.0 } });
        ats::SpectralContrastOptions cOpts;
        cOpts.subbandCount = 4;
        ats::TimbreDescriptorFrame d;
        ats::analyzeSpectralContrastAndResonancePeaks(
            envelope, options, cOpts, sampler, d);
        if (d.subbandCentroidsHz.status != ats::TimbreDescriptorStatus::Available)
            return fail("subband centroids: must be Available");
        if (d.subbandCentroidsHz.values.size() != 4u)
            return fail("subband centroids: vector must have 4 values");
        // Sub-bands cover axis quartiles of [200, 8200]: [200, 2200], [2200, 4200],
        // [4200, 6200], [6200, 8200]. Peaks at 1000, 3000, 5000, 7000 -> centroids
        // should be near those.
        const std::vector<double> expected = { 1000.0, 3000.0, 5000.0, 7000.0 };
        for (std::size_t band = 0; band < expected.size(); ++band)
        {
            const double value = d.subbandCentroidsHz.values[band];
            if (std::abs(value - expected[band]) > 250.0)
                return fail("subband centroid mismatch");
        }
    }

    std::cout << "timbre_spectral_contrast_test: ok\n";
    return 0;
}
