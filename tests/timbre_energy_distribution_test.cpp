#include "TimbreEnergyDistribution.h"
#include "TimbreFrameAdapter.h"
#include "SpectralEnvelope.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <random>
#include <string>
#include <vector>

namespace
{

int fail(const std::string& message)
{
    std::cerr << "timbre_energy_distribution_test: " << message << '\n';
    return 1;
}

ats::EnvelopeAxisSpec linearAxis()
{
    ats::EnvelopeAxisSpec axis;
    axis.type = ats::EnvelopeFrequencyAxis::LinearHz;
    axis.minFrequencyHz = 20.0;
    axis.maxFrequencyHz = 10000.0;
    axis.sampleRate = 48000.0;
    return axis;
}

ats::EnvelopeAxisSpec barkAxis()
{
    ats::EnvelopeAxisSpec axis;
    axis.type = ats::EnvelopeFrequencyAxis::Bark;
    axis.minFrequencyHz = 20.0;
    axis.maxFrequencyHz = 10000.0;
    axis.sampleRate = 48000.0;
    return axis;
}

std::vector<ats::PartialEnergySample> makePartials(
    const std::vector<std::pair<double, double>>& freqAmp)
{
    std::vector<ats::PartialEnergySample> out;
    out.reserve(freqAmp.size());
    for (const auto& [frequency, amplitude] : freqAmp)
    {
        ats::PartialEnergySample sample;
        sample.frequencyHz = frequency;
        sample.amplitude = amplitude;
        sample.energy = amplitude * amplitude;
        sample.confidence = 1.0;
        sample.selected = true;
        out.push_back(sample);
    }
    return out;
}

std::vector<ats::ResidualEnergySample> makeBands(
    const std::vector<std::pair<double, double>>& freqEnergy)
{
    std::vector<ats::ResidualEnergySample> out;
    out.reserve(freqEnergy.size());
    for (const auto& [frequency, energy] : freqEnergy)
    {
        ats::ResidualEnergySample sample;
        sample.frequencyHz = frequency;
        sample.energy = energy;
        sample.confidence = 1.0;
        sample.orphanBand = false;
        out.push_back(sample);
    }
    return out;
}

} // namespace

int main()
{
    const auto axis = linearAxis();

    {
        // Empty inputs return EmptyInput status and zero energy.
        const auto distribution = ats::buildPartialEnergyDistribution({}, axis);
        if (distribution.status != ats::TimbreDescriptorStatus::EmptyInput)
            return fail("empty partials must be EmptyInput");
        if (distribution.totalEnergy != 0.0)
            return fail("empty partials must produce zero energy");

        const auto combined = ats::buildCombinedEnergyDistribution({}, {}, axis);
        if (combined.status != ats::TimbreDescriptorStatus::EmptyInput)
            return fail("empty combined must be EmptyInput");
    }

    {
        // Order independence: shuffling the input partial list produces the
        // same total energy and the same sorted samples.
        std::mt19937 random(123);
        std::vector<std::pair<double, double>> base = {
            {  500.0, 0.5 },
            { 1000.0, 0.7 },
            { 2000.0, 0.3 },
            { 4000.0, 0.4 },
        };
        const auto reference = ats::buildPartialEnergyDistribution(
            makePartials(base), axis);

        for (int trial = 0; trial < 5; ++trial)
        {
            std::shuffle(base.begin(), base.end(), random);
            const auto shuffled = ats::buildPartialEnergyDistribution(
                makePartials(base), axis);
            if (shuffled.samples.size() != reference.samples.size())
                return fail("order independence: sample count differs");
            if (std::abs(shuffled.totalEnergy - reference.totalEnergy) > 1.0e-12)
                return fail("order independence: total energy differs");
            for (std::size_t i = 0; i < shuffled.samples.size(); ++i)
            {
                if (std::abs(shuffled.samples[i].frequencyHz
                             - reference.samples[i].frequencyHz) > 1.0e-12)
                {
                    return fail("order independence: sample order differs");
                }
                if (std::abs(shuffled.samples[i].energy
                             - reference.samples[i].energy) > 1.0e-12)
                {
                    return fail("order independence: sample energy differs");
                }
            }
        }
    }

    {
        // Gain scaling: doubling every amplitude must quadruple total energy
        // but leave the distribution shape (energy/total) unchanged.
        const std::vector<std::pair<double, double>> partialsA = {
            { 500.0, 0.5 },
            { 1500.0, 1.0 },
            { 3000.0, 0.25 },
        };
        std::vector<std::pair<double, double>> partialsB = partialsA;
        for (auto& [frequency, amplitude] : partialsB)
        {
            (void) frequency;
            amplitude *= 2.0;
        }
        const auto a = ats::buildPartialEnergyDistribution(makePartials(partialsA), axis);
        const auto b = ats::buildPartialEnergyDistribution(makePartials(partialsB), axis);

        if (std::abs(b.totalEnergy - 4.0 * a.totalEnergy) > 1.0e-9)
            return fail("gain scaling: total energy not quadrupled");
        for (std::size_t i = 0; i < a.samples.size(); ++i)
        {
            const double normA = a.samples[i].energy / a.totalEnergy;
            const double normB = b.samples[i].energy / b.totalEnergy;
            if (std::abs(normA - normB) > 1.0e-9)
                return fail("gain scaling: shape (normalized energy) changed");
        }
    }

    {
        // Energy conservation: projection preserves total energy for in-range
        // samples and the represented-energy-ratio matches.
        const auto distribution = ats::buildPartialEnergyDistribution(
            makePartials({ { 500.0, 0.6 }, { 1500.0, 0.8 }, { 4500.0, 0.4 } }),
            axis);
        const auto grid = ats::projectEnergyOntoUniformGrid(distribution, axis, 32);
        if (grid.status != ats::TimbreDescriptorStatus::Available)
            return fail("projection status must be Available");
        if (std::abs(grid.totalEnergyAfter - distribution.totalEnergy) > 1.0e-9)
            return fail("projection did not conserve energy");
        if (std::abs(grid.representedEnergyRatio - 1.0) > 1.0e-9)
            return fail("represented energy ratio must be 1 for in-range samples");
        if (grid.activeBinCount <= 0)
            return fail("projection produced no active bins");
    }

    {
        // Out-of-range samples are clamped to the boundary bins and counted in
        // representedEnergyRatio. (Clamp-style projection still sums to the
        // input total because we keep boundary samples on the edge bins; this
        // matches the spec's "preserve total energy within tolerance" rule.)
        ats::EnvelopeAxisSpec narrow = axis;
        narrow.minFrequencyHz = 1000.0;
        narrow.maxFrequencyHz = 5000.0;
        const auto distribution = ats::buildCombinedEnergyDistribution(
            makePartials({ { 500.0, 1.0 }, { 2500.0, 1.0 }, { 9000.0, 1.0 } }),
            {},
            narrow);
        if (distribution.status != ats::TimbreDescriptorStatus::Available)
            return fail("distribution should be available");
        const auto grid = ats::projectEnergyOntoUniformGrid(distribution, narrow, 16);
        if (grid.status != ats::TimbreDescriptorStatus::Available)
            return fail("narrow-axis projection status must be Available");
        if (std::abs(grid.totalEnergyAfter - distribution.totalEnergy) > 1.0e-9)
            return fail("narrow-axis projection did not conserve total energy");
    }

    {
        // Bark axis works equivalently and produces sorted samples.
        const auto distribution = ats::buildPartialEnergyDistribution(
            makePartials({ { 1000.0, 0.5 }, { 2000.0, 0.5 }, { 4000.0, 0.5 } }),
            barkAxis());
        for (std::size_t i = 1; i < distribution.samples.size(); ++i)
        {
            if (distribution.samples[i - 1].frequencyHz > distribution.samples[i].frequencyHz)
                return fail("bark axis: samples not sorted by frequency");
            if (distribution.samples[i - 1].axisPosition
                > distribution.samples[i].axisPosition + 1.0e-12)
            {
                return fail("bark axis: axis position not monotonic");
            }
        }
    }

    {
        // Combined distribution sums partial and residual contributions.
        const auto distribution = ats::buildCombinedEnergyDistribution(
            makePartials({ { 500.0, 1.0 }, { 1500.0, 1.0 } }),
            makeBands({ { 250.0, 0.25 }, { 2500.0, 0.75 } }),
            axis);
        const double expectedTotal = 1.0 + 1.0 + 0.25 + 0.75;
        if (std::abs(distribution.totalEnergy - expectedTotal) > 1.0e-12)
            return fail("combined distribution: total energy mismatch");
        if (distribution.samples.size() != 4)
            return fail("combined distribution: expected 4 samples");
    }

    {
        // Helper: amplitudeToEnergy is the canonical conversion.
        static_assert(ats::amplitudeToEnergy(0.5) == 0.25, "amplitudeToEnergy mismatch");
    }

    std::cout << "timbre_energy_distribution_test: ok\n";
    return 0;
}
