#include "TimbreDescriptors.h"

#include <algorithm>
#include <string>
#include <unordered_map>

namespace ats
{

const char* toString(const TimbreDescriptorDomain domain)
{
    switch (domain)
    {
        case TimbreDescriptorDomain::Partials: return "partials";
        case TimbreDescriptorDomain::ResidualBands: return "residual_bands";
        case TimbreDescriptorDomain::Combined: return "combined";
        case TimbreDescriptorDomain::FittedEnvelope: return "fitted_envelope";
        case TimbreDescriptorDomain::WaveformSpectrum: return "waveform_spectrum";
        case TimbreDescriptorDomain::TemporalSeries: return "temporal_series";
    }
    return "unknown";
}

const char* toString(const TimbreDescriptorStatus status)
{
    switch (status)
    {
        case TimbreDescriptorStatus::Available: return "available";
        case TimbreDescriptorStatus::EmptyInput: return "empty_input";
        case TimbreDescriptorStatus::TooSparse: return "too_sparse";
        case TimbreDescriptorStatus::MissingResidual: return "missing_residual";
        case TimbreDescriptorStatus::MissingEnvelope: return "missing_envelope";
        case TimbreDescriptorStatus::MissingReliableF0: return "missing_reliable_f0";
        case TimbreDescriptorStatus::MissingWaveform: return "missing_waveform";
        case TimbreDescriptorStatus::Undefined: return "undefined";
        case TimbreDescriptorStatus::Unsupported: return "unsupported";
    }
    return "unknown";
}

bool parseTimbreDescriptorDomain(const std::string& text, TimbreDescriptorDomain& out)
{
    if (text == "partials") { out = TimbreDescriptorDomain::Partials; return true; }
    if (text == "residual_bands") { out = TimbreDescriptorDomain::ResidualBands; return true; }
    if (text == "combined") { out = TimbreDescriptorDomain::Combined; return true; }
    if (text == "fitted_envelope") { out = TimbreDescriptorDomain::FittedEnvelope; return true; }
    if (text == "waveform_spectrum") { out = TimbreDescriptorDomain::WaveformSpectrum; return true; }
    if (text == "temporal_series") { out = TimbreDescriptorDomain::TemporalSeries; return true; }
    return false;
}

bool parseTimbreDescriptorStatus(const std::string& text, TimbreDescriptorStatus& out)
{
    if (text == "available") { out = TimbreDescriptorStatus::Available; return true; }
    if (text == "empty_input") { out = TimbreDescriptorStatus::EmptyInput; return true; }
    if (text == "too_sparse") { out = TimbreDescriptorStatus::TooSparse; return true; }
    if (text == "missing_residual") { out = TimbreDescriptorStatus::MissingResidual; return true; }
    if (text == "missing_envelope") { out = TimbreDescriptorStatus::MissingEnvelope; return true; }
    if (text == "missing_reliable_f0") { out = TimbreDescriptorStatus::MissingReliableF0; return true; }
    if (text == "missing_waveform") { out = TimbreDescriptorStatus::MissingWaveform; return true; }
    if (text == "undefined") { out = TimbreDescriptorStatus::Undefined; return true; }
    if (text == "unsupported") { out = TimbreDescriptorStatus::Unsupported; return true; }
    return false;
}

namespace
{

// The stable id table mirrors tests/fixtures/timbre_descriptor_manifest.json.
// The Python manifest is the source of truth; this table exists so the C++
// serialization layer can match formula_version against the manifest without
// re-reading JSON at runtime.
const std::vector<TimbreDescriptorMetadata>& metadataTable()
{
    static const std::vector<TimbreDescriptorMetadata> table = {
        { "partial_energy", 1, TimbreDescriptorDomain::Partials, "linear_energy", "energy" },
        { "residual_energy", 1, TimbreDescriptorDomain::ResidualBands, "linear_energy", "energy_residual_band" },
        { "combined_energy", 1, TimbreDescriptorDomain::Combined, "linear_energy", "energy_combined" },
        { "noisiness", 1, TimbreDescriptorDomain::Combined, "ratio_0_1", "energy_combined" },
        { "orphan_residual_ratio", 1, TimbreDescriptorDomain::ResidualBands, "ratio_0_1", "energy_residual_band" },
        { "bandwise_noisiness", 1, TimbreDescriptorDomain::Combined, "ratio_0_1_per_band", "energy_combined" },
        { "partial_centroid_hz", 1, TimbreDescriptorDomain::Partials, "hz", "energy" },
        { "residual_centroid_hz", 1, TimbreDescriptorDomain::ResidualBands, "hz", "energy_residual_band" },
        { "combined_centroid_hz", 1, TimbreDescriptorDomain::Combined, "hz", "energy_combined" },
        { "combined_centroid_axis", 1, TimbreDescriptorDomain::Combined, "axis_position", "energy_combined" },
        { "combined_spread_hz", 1, TimbreDescriptorDomain::Combined, "hz", "energy_combined" },
        { "combined_skewness", 1, TimbreDescriptorDomain::Combined, "dimensionless", "energy_combined" },
        { "combined_kurtosis", 1, TimbreDescriptorDomain::Combined, "dimensionless", "energy_combined" },
        { "rolloff_85_hz", 1, TimbreDescriptorDomain::Combined, "hz", "energy_combined" },
        { "rolloff_95_hz", 1, TimbreDescriptorDomain::Combined, "hz", "energy_combined" },
        { "flatness", 1, TimbreDescriptorDomain::Combined, "ratio_0_1", "uniform_grid" },
        { "crest", 1, TimbreDescriptorDomain::Combined, "ratio_ge_1", "uniform_grid" },
        { "entropy", 1, TimbreDescriptorDomain::Combined, "nats_normalized", "uniform_grid" },
        { "spectral_decrease", 1, TimbreDescriptorDomain::Combined, "per_index", "uniform_grid" },
        { "strong_peak_ratio", 1, TimbreDescriptorDomain::Combined, "ratio_0_1", "uniform_grid" },
        { "spectral_complexity", 1, TimbreDescriptorDomain::Combined, "peak_count", "uniform_grid" },
        { "tilt_db_per_octave", 1, TimbreDescriptorDomain::FittedEnvelope, "db_per_octave", "uniform_envelope_grid" },
        { "envelope_deviation_rms_db", 1, TimbreDescriptorDomain::FittedEnvelope, "db", "energy" },
        { "adjacent_partial_irregularity", 1, TimbreDescriptorDomain::Partials, "ratio_0_inf", "amplitude_linear" },
        { "spectral_contrast_db", 1, TimbreDescriptorDomain::FittedEnvelope, "db", "uniform_envelope_grid" },
        { "resonance_peaks", 1, TimbreDescriptorDomain::FittedEnvelope, "peak_list", "uniform_envelope_grid" },
        { "partial_vs_residual_centroid_offset_hz", 1, TimbreDescriptorDomain::Combined, "hz", "energy_combined" },
        { "dominant_partial_fraction", 1, TimbreDescriptorDomain::Partials, "ratio_0_1", "energy" },
        { "spectrum_flatness_vector", 1, TimbreDescriptorDomain::Combined, "ratio_0_1_per_band", "uniform_grid" },
        { "positive_flux", 1, TimbreDescriptorDomain::TemporalSeries, "ratio_per_frame", "frame_energy_weighted" },
        { "spectral_variation", 1, TimbreDescriptorDomain::TemporalSeries, "cosine_distance", "frame_energy_weighted" },
        { "temporal_centroid_seconds", 1, TimbreDescriptorDomain::TemporalSeries, "seconds", "frame_energy_weighted" },
        { "log_attack_time", 1, TimbreDescriptorDomain::TemporalSeries, "log10_seconds", "frame_energy_weighted" },
        { "attack_slope_db_per_second", 1, TimbreDescriptorDomain::TemporalSeries, "db_per_second", "frame_energy_weighted" },
        { "decay_slope_db_per_second", 1, TimbreDescriptorDomain::TemporalSeries, "db_per_second", "frame_energy_weighted" },
        { "effective_duration_seconds", 1, TimbreDescriptorDomain::TemporalSeries, "seconds", "frame_energy_weighted" },
        { "centroid_velocity_hz_per_second", 1, TimbreDescriptorDomain::TemporalSeries, "hz_per_second", "frame_energy_weighted" },
        { "centroid_acceleration_hz_per_second_sq", 1, TimbreDescriptorDomain::TemporalSeries, "hz_per_second_squared", "frame_energy_weighted" },
        { "frame_energy_coefficient_of_variation", 1, TimbreDescriptorDomain::TemporalSeries, "ratio_0_inf", "frame_energy_weighted" },
        { "partial_onset_asynchrony_seconds", 1, TimbreDescriptorDomain::TemporalSeries, "seconds", "track_energy_weighted" },
        { "partial_release_asynchrony_seconds", 1, TimbreDescriptorDomain::TemporalSeries, "seconds", "track_energy_weighted" },
        { "partial_am_rate_hz", 1, TimbreDescriptorDomain::TemporalSeries, "hz", "track_energy_weighted" },
        { "partial_fm_rate_hz", 1, TimbreDescriptorDomain::TemporalSeries, "hz", "track_energy_weighted" },
        { "residual_spectral_variation", 1, TimbreDescriptorDomain::TemporalSeries, "cosine_distance", "frame_energy_weighted" },
        { "residual_stationarity", 1, TimbreDescriptorDomain::TemporalSeries, "ratio_0_1", "frame_energy_weighted" },
        { "harmonicity", 1, TimbreDescriptorDomain::Partials, "ratio_0_1", "harmonic_energy" },
        { "inharmonicity", 1, TimbreDescriptorDomain::Partials, "ratio_0_inf", "harmonic_energy" },
        { "odd_even_ratio", 1, TimbreDescriptorDomain::Partials, "ratio_0_inf", "harmonic_energy" },
        { "tristimulus_1", 1, TimbreDescriptorDomain::Partials, "ratio_0_1", "harmonic_energy" },
        { "tristimulus_2", 1, TimbreDescriptorDomain::Partials, "ratio_0_1", "harmonic_energy" },
        { "tristimulus_3", 1, TimbreDescriptorDomain::Partials, "ratio_0_1", "harmonic_energy" },
        { "harmonic_spectral_variation", 1, TimbreDescriptorDomain::TemporalSeries, "cosine_distance", "harmonic_energy" },
        { "f0_jitter", 1, TimbreDescriptorDomain::TemporalSeries, "ratio_0_inf", "track_energy_weighted" },
        { "amplitude_shimmer", 1, TimbreDescriptorDomain::TemporalSeries, "ratio_0_inf", "track_energy_weighted" },
        { "cepstral_peak_prominence_db", 1, TimbreDescriptorDomain::WaveformSpectrum, "db", "none" },
        { "high_frequency_content", 1, TimbreDescriptorDomain::WaveformSpectrum, "linear_energy", "none" },
        { "roughness_sensory_dissonance", 1, TimbreDescriptorDomain::Partials, "dimensionless", "energy" },
        { "sharpness_zwicker", 1, TimbreDescriptorDomain::ResidualBands, "acum", "energy_residual_band" },
        { "residual_waveform_kurtosis", 1, TimbreDescriptorDomain::ResidualBands, "dimensionless", "none" },
        { "residual_zero_crossing_rate", 1, TimbreDescriptorDomain::ResidualBands, "per_second", "none" },
    };
    return table;
}

const std::unordered_map<std::string, const TimbreDescriptorMetadata*>& metadataIndex()
{
    static const std::unordered_map<std::string, const TimbreDescriptorMetadata*> index = [] {
        std::unordered_map<std::string, const TimbreDescriptorMetadata*> map;
        const auto& table = metadataTable();
        map.reserve(table.size());
        for (const auto& entry : table)
            map.emplace(entry.id, &entry);
        return map;
    }();
    return index;
}

} // namespace

const TimbreDescriptorMetadata* findTimbreDescriptorMetadata(const std::string& id)
{
    const auto& index = metadataIndex();
    const auto it = index.find(id);
    return it == index.end() ? nullptr : it->second;
}

const std::vector<TimbreDescriptorMetadata>& allTimbreDescriptorMetadata()
{
    return metadataTable();
}

} // namespace ats
