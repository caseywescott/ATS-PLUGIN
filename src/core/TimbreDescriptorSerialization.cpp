#include "TimbreDescriptorSerialization.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <ostream>
#include <sstream>
#include <string>
#include <utility>

namespace ats
{

namespace
{

void appendEscapedString(std::string& out, const std::string& value)
{
    out.push_back('"');
    for (const char c : value)
    {
        switch (c)
        {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20)
                {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x",
                                  static_cast<unsigned int>(static_cast<unsigned char>(c)));
                    out += buf;
                }
                else
                {
                    out.push_back(c);
                }
                break;
        }
    }
    out.push_back('"');
}

void appendKey(std::string& out, const char* key)
{
    out.push_back('"');
    out.append(key);
    out += "\": ";
}

void appendBool(std::string& out, const bool value)
{
    out += value ? "true" : "false";
}

void appendInt(std::string& out, const long long value)
{
    out += std::to_string(value);
}

void appendFiniteDouble(std::string& out, const double value)
{
    if (!std::isfinite(value))
    {
        out += "null";
        return;
    }
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.17g", value);
    out += buf;
}

void appendValueOrNull(std::string& out, const double value, const TimbreDescriptorStatus status)
{
    if (status == TimbreDescriptorStatus::Available && std::isfinite(value))
    {
        appendFiniteDouble(out, value);
    }
    else
    {
        out += "null";
    }
}

void appendDoubleVector(std::string& out, const std::vector<double>& values)
{
    out.push_back('[');
    for (std::size_t i = 0; i < values.size(); ++i)
    {
        if (i != 0)
            out += ", ";
        appendFiniteDouble(out, values[i]);
    }
    out.push_back(']');
}

void appendCoverage(std::string& out, const TimbreDescriptorCoverage& coverage)
{
    out += "{";
    appendKey(out, "active_point_count");
    appendInt(out, coverage.activePointCount);
    out += ", ";
    appendKey(out, "supported_frequency_span_hz");
    appendFiniteDouble(out, coverage.supportedFrequencySpanHz);
    out += ", ";
    appendKey(out, "represented_energy_ratio");
    appendFiniteDouble(out, coverage.representedEnergyRatio);
    out += "}";
}

void appendDescriptorEntry(
    std::string& out,
    const char* descriptorId,
    const TimbreDescriptorValue& value)
{
    const TimbreDescriptorMetadata* metadata = findTimbreDescriptorMetadata(descriptorId);
    appendEscapedString(out, descriptorId);
    out += ": {";
    appendKey(out, "value");
    appendValueOrNull(out, value.value, value.status);
    out += ", ";
    appendKey(out, "domain");
    // The manifest is authoritative for descriptor domain; the in-struct field
    // is used by analysis-side helpers but the report must agree with the
    // declared contract so the regression checker can validate it.
    appendEscapedString(out,
        metadata != nullptr ? toString(metadata->domain) : toString(value.domain));
    out += ", ";
    appendKey(out, "status");
    appendEscapedString(out, toString(value.status));
    out += ", ";
    appendKey(out, "confidence");
    appendFiniteDouble(out, value.confidence);
    out += ", ";
    appendKey(out, "formula_version");
    appendInt(out, metadata != nullptr ? metadata->formulaVersion : 0);
    if (metadata != nullptr)
    {
        out += ", ";
        appendKey(out, "units");
        appendEscapedString(out, std::string(metadata->units));
        out += ", ";
        appendKey(out, "weighting");
        appendEscapedString(out, std::string(metadata->weighting));
    }
    out += ", ";
    appendKey(out, "coverage");
    appendCoverage(out, value.coverage);
    out += "}";
}

void appendVectorDescriptor(
    std::string& out,
    const char* descriptorId,
    const TimbreDescriptorVector& value)
{
    const TimbreDescriptorMetadata* metadata = findTimbreDescriptorMetadata(descriptorId);
    appendEscapedString(out, descriptorId);
    out += ": {";
    appendKey(out, "value");
    if (value.status == TimbreDescriptorStatus::Available)
        appendDoubleVector(out, value.values);
    else
        out += "null";
    out += ", ";
    appendKey(out, "domain");
    appendEscapedString(out,
        metadata != nullptr ? toString(metadata->domain) : toString(value.domain));
    out += ", ";
    appendKey(out, "status");
    appendEscapedString(out, toString(value.status));
    out += ", ";
    appendKey(out, "confidence");
    appendFiniteDouble(out, value.confidence);
    out += ", ";
    appendKey(out, "formula_version");
    appendInt(out, metadata != nullptr ? metadata->formulaVersion : 0);
    if (metadata != nullptr)
    {
        out += ", ";
        appendKey(out, "units");
        appendEscapedString(out, std::string(metadata->units));
        out += ", ";
        appendKey(out, "weighting");
        appendEscapedString(out, std::string(metadata->weighting));
    }
    out += ", ";
    appendKey(out, "coverage");
    appendCoverage(out, value.coverage);
    out += "}";
}

void appendResonancePeaks(
    std::string& out,
    const std::vector<TimbreResonancePeak>& peaks,
    const TimbreDescriptorStatus status,
    const double confidence)
{
    const TimbreDescriptorMetadata* metadata = findTimbreDescriptorMetadata("resonance_peaks");
    appendEscapedString(out, "resonance_peaks");
    out += ": {";
    appendKey(out, "value");
    if (status == TimbreDescriptorStatus::Available)
    {
        out += "[";
        for (std::size_t i = 0; i < peaks.size(); ++i)
        {
            if (i != 0)
                out += ", ";
            out += "{";
            appendKey(out, "frequency_hz");
            appendFiniteDouble(out, peaks[i].frequencyHz);
            out += ", ";
            appendKey(out, "axis_position");
            appendFiniteDouble(out, peaks[i].axisPosition);
            out += ", ";
            appendKey(out, "prominence_db");
            appendFiniteDouble(out, peaks[i].prominenceDb);
            out += ", ";
            appendKey(out, "bandwidth_axis");
            appendFiniteDouble(out, peaks[i].bandwidthAxis);
            out += "}";
        }
        out += "]";
    }
    else
    {
        out += "null";
    }
    out += ", ";
    appendKey(out, "domain");
    appendEscapedString(out, "fitted_envelope");
    out += ", ";
    appendKey(out, "status");
    appendEscapedString(out, toString(status));
    out += ", ";
    appendKey(out, "confidence");
    appendFiniteDouble(out, confidence);
    out += ", ";
    appendKey(out, "formula_version");
    appendInt(out, metadata != nullptr ? metadata->formulaVersion : 0);
    out += "}";
}

// Pair an in-struct field with its stable id so the JSON layout is fixed and
// adding a new descriptor only requires a single insertion.
struct DescriptorBinding
{
    const char* id;
    const TimbreDescriptorValue& value;
};

const std::vector<DescriptorBinding> bindFrameValues(const TimbreDescriptorFrame& frame)
{
    return {
        { "partial_energy",                       frame.partialEnergy },
        { "residual_energy",                      frame.residualEnergy },
        { "combined_energy",                      frame.combinedEnergy },
        { "noisiness",                            frame.noisiness },
        { "orphan_residual_ratio",                frame.orphanResidualRatio },
        { "partial_centroid_hz",                  frame.partialCentroidHz },
        { "residual_centroid_hz",                 frame.residualCentroidHz },
        { "combined_centroid_hz",                 frame.combinedCentroidHz },
        { "combined_centroid_axis",               frame.combinedCentroidAxis },
        { "combined_spread_hz",                   frame.combinedSpreadHz },
        { "combined_skewness",                    frame.combinedSkewness },
        { "combined_kurtosis",                    frame.combinedKurtosis },
        { "rolloff_85_hz",                        frame.rolloff85Hz },
        { "rolloff_95_hz",                        frame.rolloff95Hz },
        { "flatness",                             frame.flatness },
        { "crest",                                frame.crest },
        { "entropy",                              frame.entropyNormalized },
        { "spectral_decrease",                    frame.spectralDecrease },
        { "strong_peak_ratio",                    frame.strongPeakRatio },
        { "spectral_complexity",                  frame.spectralComplexity },
        { "tilt_db_per_octave",                   frame.tiltDbPerOctave },
        { "envelope_deviation_rms_db",            frame.envelopeDeviationRmsDb },
        { "adjacent_partial_irregularity",        frame.adjacentPartialIrregularity },
        { "spectral_contrast_db",                 frame.spectralContrastDb },
        { "partial_vs_residual_centroid_offset_hz", frame.partialVsResidualCentroidOffsetHz },
        { "dominant_partial_fraction",            frame.dominantPartialFraction },
        { "harmonicity",                          frame.harmonicity },
        { "inharmonicity",                        frame.inharmonicity },
        { "odd_even_ratio",                       frame.oddEvenRatio },
        { "tristimulus_1",                        frame.tristimulus[0] },
        { "tristimulus_2",                        frame.tristimulus[1] },
        { "tristimulus_3",                        frame.tristimulus[2] },
        { "cepstral_peak_prominence_db",          frame.cepstralPeakProminenceDb },
        { "high_frequency_content",               frame.highFrequencyContent },
    };
}

void appendFrame(std::string& out, const TimbreDescriptorFrame& frame)
{
    out += "{";
    appendKey(out, "frame_index");
    appendInt(out, static_cast<long long>(frame.frameIndex));
    out += ", ";
    appendKey(out, "source_model_id");
    appendEscapedString(out, frame.sourceModelId);
    out += ", ";
    appendKey(out, "descriptors");
    out += "{";

    const auto bindings = bindFrameValues(frame);
    bool first = true;
    for (const auto& binding : bindings)
    {
        if (!first)
            out += ", ";
        first = false;
        appendDescriptorEntry(out, binding.id, binding.value);
    }

    // Vector descriptors
    out += ", ";
    appendVectorDescriptor(out, "bandwise_noisiness", frame.bandwiseNoisiness);
    out += ", ";
    appendVectorDescriptor(out, "spectrum_flatness_vector", frame.spectrumFlatnessVector);

    // Resonance peak list
    out += ", ";
    appendResonancePeaks(out, frame.resonancePeaks,
                          frame.resonancePeaksStatus,
                          frame.resonancePeaksConfidence);

    out += "}}";
}

const std::vector<DescriptorBinding> bindSeriesScalarValues(const TimbreDescriptorSeries& series)
{
    return {
        { "temporal_centroid_seconds",           series.temporalCentroidSeconds },
        { "log_attack_time",                     series.logAttackTime },
        { "attack_slope_db_per_second",          series.attackSlopeDbPerSecond },
        { "decay_slope_db_per_second",           series.decaySlopeDbPerSecond },
        { "effective_duration_seconds",          series.effectiveDurationSeconds },
        { "centroid_velocity_hz_per_second",     series.centroidVelocityHzPerSecond },
        { "centroid_acceleration_hz_per_second_sq", series.centroidAccelerationHzPerSecondSq },
        { "frame_energy_coefficient_of_variation", series.frameEnergyCoefficientOfVariation },
        { "partial_onset_asynchrony_seconds",    series.partialOnsetAsynchronySeconds },
        { "partial_release_asynchrony_seconds",  series.partialReleaseAsynchronySeconds },
        { "partial_am_rate_hz",                  series.partialAmRateHz },
        { "partial_fm_rate_hz",                  series.partialFmRateHz },
        { "residual_spectral_variation",         series.residualSpectralVariation },
        { "residual_stationarity",               series.residualStationarity },
        { "harmonic_spectral_variation",         series.harmonicSpectralVariation },
        { "f0_jitter",                           series.f0Jitter },
        { "amplitude_shimmer",                   series.amplitudeShimmer },
    };
}

void appendTrajectory(
    std::string& out,
    const char* descriptorId,
    const std::vector<TimbreDescriptorValue>& values)
{
    appendEscapedString(out, descriptorId);
    out += ": [";
    for (std::size_t i = 0; i < values.size(); ++i)
    {
        if (i != 0)
            out += ", ";
        out += "{";
        appendKey(out, "value");
        appendValueOrNull(out, values[i].value, values[i].status);
        out += ", ";
        appendKey(out, "status");
        appendEscapedString(out, toString(values[i].status));
        out += ", ";
        appendKey(out, "confidence");
        appendFiniteDouble(out, values[i].confidence);
        out += "}";
    }
    out += "]";
}

void appendSeries(std::string& out, const TimbreDescriptorSeries& series)
{
    out += "{";
    appendKey(out, "source_model_id");
    appendEscapedString(out, series.sourceModelId);
    out += ", ";
    appendKey(out, "frame_hop_seconds");
    appendFiniteDouble(out, series.frameHopSeconds);
    out += ", ";
    appendKey(out, "scalars");
    out += "{";
    const auto scalars = bindSeriesScalarValues(series);
    bool first = true;
    for (const auto& binding : scalars)
    {
        if (!first)
            out += ", ";
        first = false;
        appendDescriptorEntry(out, binding.id, binding.value);
    }
    out += "}";
    out += ", ";
    appendKey(out, "trajectories");
    out += "{";
    appendTrajectory(out, "positive_flux", series.positiveFlux);
    out += ", ";
    appendTrajectory(out, "spectral_variation", series.spectralVariation);
    out += ", ";
    appendTrajectory(out, "combined_centroid_hz", series.centroidHzTrajectory);
    out += ", ";
    appendTrajectory(out, "noisiness", series.noisinessTrajectory);
    out += "}";
    out += "}";
}

} // namespace

const char* toReportAxisString(const EnvelopeFrequencyAxis axis)
{
    switch (axis)
    {
        case EnvelopeFrequencyAxis::LinearHz: return "linear_hz";
        case EnvelopeFrequencyAxis::Bark: return "bark";
        case EnvelopeFrequencyAxis::Mel: return "mel";
        case EnvelopeFrequencyAxis::Erb: return "erb";
        case EnvelopeFrequencyAxis::NormalizedNyquist: return "normalized_nyquist";
        case EnvelopeFrequencyAxis::CustomNormalized: return "custom_normalized";
    }
    return "linear_hz";
}

std::string serializeTimbreDescriptorFrame(
    const TimbreDescriptorFrame& frame,
    const TimbreDescriptorOptions& /*options*/)
{
    std::string out;
    appendFrame(out, frame);
    return out;
}

std::string serializeTimbreDescriptorReport(const TimbreDescriptorReport& report)
{
    std::string out;
    out += "{";
    appendKey(out, "schema_version");
    appendInt(out, TimbreDescriptorReport::kSchemaVersion);
    out += ", ";
    appendKey(out, "source_model_id");
    appendEscapedString(out, report.sourceModelId);
    out += ", ";
    appendKey(out, "weighting");
    appendEscapedString(out, report.weighting);
    out += ", ";
    appendKey(out, "comparison_axis");
    appendEscapedString(out, toReportAxisString(report.comparisonAxis.type));
    out += ", ";
    appendKey(out, "comparison_grid_size");
    appendInt(out, report.comparisonGridSize);
    out += ", ";
    appendKey(out, "options");
    out += "{";
    appendKey(out, "amplitude_floor");
    appendFiniteDouble(out, report.amplitudeFloor);
    out += ", ";
    appendKey(out, "minimum_peak_prominence_db");
    appendFiniteDouble(out, report.minimumPeakProminenceDb);
    out += ", ";
    appendKey(out, "rolloff_low_fraction");
    appendFiniteDouble(out, report.rolloffLowFraction);
    out += ", ";
    appendKey(out, "rolloff_high_fraction");
    appendFiniteDouble(out, report.rolloffHighFraction);
    out += ", ";
    appendKey(out, "include_residual_in_combined");
    appendBool(out, report.includeResidualInCombined);
    out += ", ";
    appendKey(out, "enable_harmonic_descriptors");
    appendBool(out, report.enableHarmonicDescriptors);
    out += ", ";
    appendKey(out, "enable_waveform_descriptors");
    appendBool(out, report.enableWaveformDescriptors);
    out += "}";
    out += ", ";
    appendKey(out, "frames");
    out += "[";
    for (std::size_t i = 0; i < report.frames.size(); ++i)
    {
        if (i != 0)
            out += ", ";
        appendFrame(out, report.frames[i]);
    }
    out += "]";
    out += ", ";
    appendKey(out, "series");
    appendSeries(out, report.series);
    out += "}";
    return out;
}

bool writeTimbreDescriptorReport(
    const TimbreDescriptorReport& report,
    std::ostream& stream,
    std::string* errorMessage)
{
    const std::string text = serializeTimbreDescriptorReport(report);
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!stream.good())
    {
        if (errorMessage != nullptr)
            *errorMessage = "stream write failed";
        return false;
    }
    return true;
}

bool writeTimbreDescriptorReport(
    const TimbreDescriptorReport& report,
    const std::string& filePath,
    std::string* errorMessage)
{
    std::ofstream stream(filePath, std::ios::out | std::ios::trunc | std::ios::binary);
    if (!stream.is_open())
    {
        if (errorMessage != nullptr)
            *errorMessage = "could not open file for writing: " + filePath;
        return false;
    }
    return writeTimbreDescriptorReport(report, stream, errorMessage);
}

} // namespace ats
