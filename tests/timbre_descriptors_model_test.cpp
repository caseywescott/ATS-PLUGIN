#include "TimbreDescriptors.h"

#include <cstring>
#include <iostream>
#include <string>
#include <unordered_set>

namespace
{

int fail(const std::string& message)
{
    std::cerr << "timbre_descriptors_model_test: " << message << '\n';
    return 1;
}

bool roundtripDomain(const ats::TimbreDescriptorDomain domain)
{
    const char* text = ats::toString(domain);
    if (text == nullptr || std::strlen(text) == 0)
        return false;
    ats::TimbreDescriptorDomain parsed;
    if (!ats::parseTimbreDescriptorDomain(text, parsed))
        return false;
    return parsed == domain;
}

bool roundtripStatus(const ats::TimbreDescriptorStatus status)
{
    const char* text = ats::toString(status);
    if (text == nullptr || std::strlen(text) == 0)
        return false;
    ats::TimbreDescriptorStatus parsed;
    if (!ats::parseTimbreDescriptorStatus(text, parsed))
        return false;
    return parsed == status;
}

} // namespace

int main()
{
    // Domain round-trips
    const ats::TimbreDescriptorDomain domains[] = {
        ats::TimbreDescriptorDomain::Partials,
        ats::TimbreDescriptorDomain::ResidualBands,
        ats::TimbreDescriptorDomain::Combined,
        ats::TimbreDescriptorDomain::FittedEnvelope,
        ats::TimbreDescriptorDomain::WaveformSpectrum,
        ats::TimbreDescriptorDomain::TemporalSeries,
    };
    for (const auto domain : domains)
    {
        if (!roundtripDomain(domain))
            return fail("domain string round-trip failed");
    }

    // Status round-trips
    const ats::TimbreDescriptorStatus statuses[] = {
        ats::TimbreDescriptorStatus::Available,
        ats::TimbreDescriptorStatus::EmptyInput,
        ats::TimbreDescriptorStatus::TooSparse,
        ats::TimbreDescriptorStatus::MissingResidual,
        ats::TimbreDescriptorStatus::MissingEnvelope,
        ats::TimbreDescriptorStatus::MissingReliableF0,
        ats::TimbreDescriptorStatus::MissingWaveform,
        ats::TimbreDescriptorStatus::Undefined,
        ats::TimbreDescriptorStatus::Unsupported,
    };
    for (const auto status : statuses)
    {
        if (!roundtripStatus(status))
            return fail("status string round-trip failed");
    }

    // Parse rejects unknown strings
    ats::TimbreDescriptorDomain domainSink;
    if (ats::parseTimbreDescriptorDomain("nope", domainSink))
        return fail("parseTimbreDescriptorDomain accepted unknown string");
    ats::TimbreDescriptorStatus statusSink;
    if (ats::parseTimbreDescriptorStatus("nope", statusSink))
        return fail("parseTimbreDescriptorStatus accepted unknown string");

    // Metadata table contains the canonical MVP ids, each id is unique, and
    // findTimbreDescriptorMetadata returns the same pointer as the table.
    const auto& table = ats::allTimbreDescriptorMetadata();
    if (table.empty())
        return fail("descriptor metadata table is empty");
    std::unordered_set<std::string> seen;
    for (const auto& entry : table)
    {
        if (entry.id == nullptr || std::strlen(entry.id) == 0)
            return fail("descriptor metadata entry has empty id");
        if (!seen.insert(entry.id).second)
            return fail(std::string("duplicate descriptor id: ") + entry.id);
        if (entry.formulaVersion < 1)
            return fail(std::string("descriptor ") + entry.id + " has bad formula version");
        const auto* lookup = ats::findTimbreDescriptorMetadata(entry.id);
        if (lookup != &entry)
            return fail(std::string("metadata lookup mismatch for ") + entry.id);
    }

    // Unknown ids return nullptr.
    if (ats::findTimbreDescriptorMetadata("not_a_descriptor") != nullptr)
        return fail("findTimbreDescriptorMetadata returned non-null for unknown id");

    // Canonical MVP ids must be present, since downstream commits and tests
    // rely on them.
    const char* requiredIds[] = {
        "partial_energy",
        "residual_energy",
        "combined_energy",
        "noisiness",
        "orphan_residual_ratio",
        "bandwise_noisiness",
        "combined_centroid_hz",
        "combined_spread_hz",
        "rolloff_85_hz",
        "rolloff_95_hz",
        "flatness",
        "crest",
        "tilt_db_per_octave",
        "envelope_deviation_rms_db",
        "adjacent_partial_irregularity",
        "spectral_contrast_db",
        "resonance_peaks",
        "partial_vs_residual_centroid_offset_hz",
        "dominant_partial_fraction",
        "spectrum_flatness_vector",
        "positive_flux",
        "spectral_variation",
        "log_attack_time",
        "centroid_velocity_hz_per_second",
        "centroid_acceleration_hz_per_second_sq",
        "frame_energy_coefficient_of_variation",
        "partial_onset_asynchrony_seconds",
        "partial_release_asynchrony_seconds",
        "partial_am_rate_hz",
        "partial_fm_rate_hz",
        "residual_spectral_variation",
        "residual_stationarity",
        "harmonicity",
        "inharmonicity",
        "odd_even_ratio",
        "tristimulus_1",
        "tristimulus_2",
        "tristimulus_3",
        "harmonic_spectral_variation",
        "f0_jitter",
        "amplitude_shimmer",
        "cepstral_peak_prominence_db",
        "high_frequency_content",
    };
    for (const char* id : requiredIds)
    {
        if (ats::findTimbreDescriptorMetadata(id) == nullptr)
            return fail(std::string("required descriptor id missing: ") + id);
    }

    // Default-constructed values must be EmptyInput with zeroed coverage.
    ats::TimbreDescriptorValue defaulted;
    if (defaulted.status != ats::TimbreDescriptorStatus::EmptyInput)
        return fail("default TimbreDescriptorValue status must be EmptyInput");
    if (defaulted.coverage.activePointCount != 0)
        return fail("default coverage must have zero active points");

    ats::TimbreDescriptorFrame frame;
    if (frame.partialEnergy.status != ats::TimbreDescriptorStatus::EmptyInput)
        return fail("default frame partialEnergy must be EmptyInput");
    if (frame.resonancePeaksStatus != ats::TimbreDescriptorStatus::EmptyInput)
        return fail("default frame resonancePeaksStatus must be EmptyInput");
    for (const auto& tristimulusValue : frame.tristimulus)
    {
        if (tristimulusValue.status != ats::TimbreDescriptorStatus::EmptyInput)
            return fail("default tristimulus entry must be EmptyInput");
    }

    ats::TimbreDescriptorSeries series;
    if (series.frameHopSeconds != 0.0)
        return fail("default series frameHopSeconds must be 0");
    if (!series.frames.empty())
        return fail("default series frames must be empty");

    // Options defaults
    ats::TimbreDescriptorOptions options;
    if (options.comparisonGridSize != 64)
        return fail("default comparisonGridSize must be 64");
    if (options.amplitudeFloor <= 0.0)
        return fail("default amplitudeFloor must be positive");
    if (options.rolloffLowFraction >= options.rolloffHighFraction)
        return fail("default rolloff fractions must be ordered");
    if (options.enableHarmonicDescriptors)
        return fail("harmonic descriptors must be opt-in by default");
    if (options.minimumF0ConfidenceForHarmonic < 0.0
        || options.minimumF0ConfidenceForHarmonic > 1.0)
    {
        return fail("minimumF0ConfidenceForHarmonic must be in [0, 1]");
    }

    std::cout << "timbre_descriptors_model_test: ok ("
              << table.size() << " descriptors)\n";
    return 0;
}
