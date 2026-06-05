#include "AtsMorphRecipe.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <map>
#include <sstream>
#include <string_view>
#include <utility>
#include <vector>

namespace ats
{
namespace
{

constexpr std::array<const char*, static_cast<size_t>(MorphLane::Count)> kLaneNames = {
    "frequency",
    "amplitude",
    "residual_noise",
    "time",
    "transient",
    "phase",
};

constexpr std::array<const char*, 4> kCurveShapeNames = {
    "linear",
    "ease_in",
    "ease_out",
    "ease_in_out",
};

constexpr std::array<const char*, 5> kPhaseStrategyNames = {
    "oscillator_continuation",
    "source_a_donor",
    "source_b_donor",
    "relative_cluster_preservation",
    "direct_blend",
};

constexpr std::array<const char*, 4> kTransientPolicyNames = {
    "source_a",
    "source_b",
    "crossfade",
    "sustained_only_after_attack",
};

constexpr double kDoubleTolerance = 1.0e-9;
constexpr double kPi = 3.14159265358979323846264338327950288;
constexpr double kTwoPi = 2.0 * kPi;
constexpr int kResidualPreviewBandCount = 25;
constexpr int kResidualPreviewComponentsPerBand = 5;
constexpr double kResidualPreviewLowestHz = 20.0;
constexpr double kResidualPreviewHighestHz = 20000.0;

bool isValidFrequencyGrid(const MorphFrequencyGrid& grid)
{
    if (grid.bandCount <= 0 || static_cast<int>(grid.edgeFrequenciesHz.size()) != grid.bandCount + 1)
        return false;
    for (size_t i = 1; i < grid.edgeFrequenciesHz.size(); ++i)
    {
        if (!(std::isfinite(grid.edgeFrequenciesHz[i - 1]) && std::isfinite(grid.edgeFrequenciesHz[i]))
            || !(grid.edgeFrequenciesHz[i] > grid.edgeFrequenciesHz[i - 1]))
        {
            return false;
        }
    }
    return true;
}

double hzToBark(double hz)
{
    const double nonNegativeHz = std::max(0.0, hz);
    const double ratio = nonNegativeHz / 7500.0;
    return 13.0 * std::atan(0.00076 * nonNegativeHz) + 3.5 * std::atan(ratio * ratio);
}

double barkToHzInRange(double bark, double lowestFrequencyHz, double highestFrequencyHz)
{
    double lo = lowestFrequencyHz;
    double hi = highestFrequencyHz;
    for (int i = 0; i < 80; ++i)
    {
        const double mid = 0.5 * (lo + hi);
        if (hzToBark(mid) < bark)
            lo = mid;
        else
            hi = mid;
    }
    return 0.5 * (lo + hi);
}

double hzToMel(double hz)
{
    return 2595.0 * std::log10(1.0 + std::max(0.0, hz) / 700.0);
}

double melToHz(double mel)
{
    return 700.0 * (std::pow(10.0, mel / 2595.0) - 1.0);
}

bool isPositive(double value)
{
    return std::isfinite(value) && value > 0.0;
}

std::uint32_t residualPreviewHash(int bandIndex, int componentIndex)
{
    std::uint32_t x = static_cast<std::uint32_t>(bandIndex + 1) * 0x9e3779b9u;
    x ^= static_cast<std::uint32_t>(componentIndex + 17) * 0x85ebca6bu;
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

double residualPreviewUnitHash(int bandIndex, int componentIndex)
{
    constexpr double scale = 1.0 / 4294967295.0;
    return static_cast<double>(residualPreviewHash(bandIndex, componentIndex)) * scale;
}

double residualPreviewBandEdgeHz(int edgeIndex, double sampleRate)
{
    if (!isPositive(sampleRate))
        return 0.0;

    const double maxHz = std::min(kResidualPreviewHighestHz, std::max(1.0, sampleRate * 0.475));
    const double minHz = std::min(kResidualPreviewLowestHz, std::max(1.0, maxHz * 0.25));
    if (!(maxHz > minHz))
        return maxHz;

    const int clampedEdge = std::max(0, std::min(kResidualPreviewBandCount, edgeIndex));
    const double amount = static_cast<double>(clampedEdge) / static_cast<double>(kResidualPreviewBandCount);
    const double bark = hzToBark(minHz) + amount * (hzToBark(maxHz) - hzToBark(minHz));
    return barkToHzInRange(bark, minHz, maxHz);
}

std::pair<double, double> residualPreviewBandRangeHz(int bandIndex, double sampleRate)
{
    const int clampedBand = std::max(0, std::min(kResidualPreviewBandCount - 1, bandIndex));
    double lowHz = residualPreviewBandEdgeHz(clampedBand, sampleRate);
    double highHz = residualPreviewBandEdgeHz(clampedBand + 1, sampleRate);
    if (!(isPositive(lowHz) && isPositive(highHz)) || highHz <= lowHz)
        return { 0.0, 0.0 };
    return { lowHz, highHz };
}

double residualPreviewComponentFrequencyHz(int bandIndex, int componentIndex, double sampleRate)
{
    const auto range = residualPreviewBandRangeHz(bandIndex, sampleRate);
    if (!(range.second > range.first))
        return 0.0;

    const double slot = (static_cast<double>(componentIndex) + 0.5)
                        / static_cast<double>(kResidualPreviewComponentsPerBand);
    const double jitter = (residualPreviewUnitHash(bandIndex, componentIndex) - 0.5)
                          / static_cast<double>(kResidualPreviewComponentsPerBand);
    const double amount = std::max(0.05, std::min(0.95, slot + jitter));
    return range.first + amount * (range.second - range.first);
}

double clampUnitInterval(double value)
{
    if (!std::isfinite(value))
        return 0.0;
    return std::max(0.0, std::min(1.0, value));
}

double wrapPhaseRadians(double phase)
{
    if (!std::isfinite(phase))
        return 0.0;
    double wrapped = std::fmod(phase, kTwoPi);
    if (wrapped > kPi)
        wrapped -= kTwoPi;
    if (wrapped <= -kPi)
        wrapped += kTwoPi;
    return wrapped;
}

int clampLifecycleFrameIndex(int frameIndex)
{
    return std::max(0, frameIndex);
}

struct IndexedTransientMarker
{
    MorphTransientMarker marker;
    int originalIndex = -1;
};

std::vector<IndexedTransientMarker> makeIndexedTransientMarkers(const std::vector<MorphTransientMarker>& markers)
{
    std::vector<IndexedTransientMarker> indexed;
    indexed.reserve(markers.size());
    for (size_t i = 0; i < markers.size(); ++i)
        indexed.push_back({markers[i], static_cast<int>(i)});

    std::sort(indexed.begin(), indexed.end(), [](const IndexedTransientMarker& a, const IndexedTransientMarker& b) {
        if (a.marker.timeSeconds != b.marker.timeSeconds)
            return a.marker.timeSeconds < b.marker.timeSeconds;
        if (a.marker.frameIndex != b.marker.frameIndex)
            return a.marker.frameIndex < b.marker.frameIndex;
        if (a.marker.strength != b.marker.strength)
            return a.marker.strength > b.marker.strength;
        return a.originalIndex < b.originalIndex;
    });
    return indexed;
}

bool nearlyEqual(double a, double b)
{
    return std::abs(a - b) <= kDoubleTolerance * std::max(1.0, std::max(std::abs(a), std::abs(b)));
}

std::string escapeValue(const std::string& value)
{
    std::string escaped;
    escaped.reserve(value.size());
    for (char c : value)
    {
        switch (c)
        {
            case '\\':
                escaped += "\\\\";
                break;
            case '\n':
                escaped += "\\n";
                break;
            case '\r':
                escaped += "\\r";
                break;
            case '=':
                escaped += "\\e";
                break;
            default:
                escaped += c;
                break;
        }
    }
    return escaped;
}

bool unescapeValue(const std::string& value, std::string& unescaped, std::string* errorMessage)
{
    unescaped.clear();
    unescaped.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i)
    {
        const char c = value[i];
        if (c != '\\')
        {
            unescaped += c;
            continue;
        }

        if (i + 1 >= value.size())
        {
            if (errorMessage)
                *errorMessage = "dangling escape in serialized morph recipe";
            return false;
        }

        const char escaped = value[++i];
        switch (escaped)
        {
            case '\\':
                unescaped += '\\';
                break;
            case 'n':
                unescaped += '\n';
                break;
            case 'r':
                unescaped += '\r';
                break;
            case 'e':
                unescaped += '=';
                break;
            default:
                if (errorMessage)
                    *errorMessage = "unknown escape in serialized morph recipe";
                return false;
        }
    }
    return true;
}

std::string formatDouble(double value)
{
    std::ostringstream out;
    out << std::setprecision(17) << value;
    return out.str();
}

void writeKeyValue(std::ostringstream& out, const std::string& key, const std::string& value)
{
    out << key << '=' << escapeValue(value) << '\n';
}

void writeKeyValue(std::ostringstream& out, const std::string& key, int value)
{
    out << key << '=' << value << '\n';
}

void writeKeyValue(std::ostringstream& out, const std::string& key, bool value)
{
    out << key << '=' << (value ? 1 : 0) << '\n';
}

void writeKeyValue(std::ostringstream& out, const std::string& key, double value)
{
    out << key << '=' << formatDouble(value) << '\n';
}

std::string joinDoubles(const std::vector<double>& values)
{
    std::ostringstream out;
    for (size_t i = 0; i < values.size(); ++i)
    {
        if (i > 0)
            out << ',';
        out << formatDouble(values[i]);
    }
    return out.str();
}

bool parseDoubleList(const std::string& text, std::vector<double>& values, std::string* errorMessage)
{
    values.clear();
    if (text.empty())
        return true;

    size_t start = 0;
    while (start <= text.size())
    {
        const size_t separator = text.find(',', start);
        const size_t end = separator == std::string::npos ? text.size() : separator;
        const std::string item = text.substr(start, end - start);
        char* parseEnd = nullptr;
        const double value = std::strtod(item.c_str(), &parseEnd);
        if (item.empty() || parseEnd != item.c_str() + item.size())
        {
            if (errorMessage)
                *errorMessage = "invalid double list in serialized morph recipe";
            return false;
        }
        values.push_back(value);

        if (separator == std::string::npos)
            break;
        start = separator + 1;
    }
    return true;
}

bool readDouble(const std::map<std::string, std::string>& values,
                const std::string& key,
                double& out,
                std::string* errorMessage)
{
    const auto it = values.find(key);
    if (it == values.end())
    {
        if (errorMessage)
            *errorMessage = "missing serialized morph recipe key: " + key;
        return false;
    }

    const std::string& text = it->second;
    const char* begin = text.data();
    char* end = nullptr;
    out = std::strtod(begin, &end);
    if (end != begin + text.size())
    {
        if (errorMessage)
            *errorMessage = "invalid double for serialized morph recipe key: " + key;
        return false;
    }
    return true;
}

bool readInt(const std::map<std::string, std::string>& values,
             const std::string& key,
             int& out,
             std::string* errorMessage)
{
    const auto it = values.find(key);
    if (it == values.end())
    {
        if (errorMessage)
            *errorMessage = "missing serialized morph recipe key: " + key;
        return false;
    }

    const std::string& text = it->second;
    const auto* begin = text.data();
    const auto* end = text.data() + text.size();
    const auto result = std::from_chars(begin, end, out);
    if (result.ec != std::errc{} || result.ptr != end)
    {
        if (errorMessage)
            *errorMessage = "invalid integer for serialized morph recipe key: " + key;
        return false;
    }
    return true;
}

bool readBool(const std::map<std::string, std::string>& values,
              const std::string& key,
              bool& out,
              std::string* errorMessage)
{
    int encoded = 0;
    if (!readInt(values, key, encoded, errorMessage))
        return false;
    if (encoded != 0 && encoded != 1)
    {
        if (errorMessage)
            *errorMessage = "invalid boolean for serialized morph recipe key: " + key;
        return false;
    }
    out = encoded == 1;
    return true;
}

bool readString(const std::map<std::string, std::string>& values,
                const std::string& key,
                std::string& out,
                std::string* errorMessage)
{
    const auto it = values.find(key);
    if (it == values.end())
    {
        if (errorMessage)
            *errorMessage = "missing serialized morph recipe key: " + key;
        return false;
    }
    out = it->second;
    return true;
}

bool readOptionalInt(const std::map<std::string, std::string>& values,
                     const std::string& key,
                     int& out,
                     std::string* errorMessage)
{
    const auto it = values.find(key);
    if (it == values.end())
        return true;
    return readInt(values, key, out, errorMessage);
}

bool readOptionalBool(const std::map<std::string, std::string>& values,
                      const std::string& key,
                      bool& out,
                      std::string* errorMessage)
{
    const auto it = values.find(key);
    if (it == values.end())
        return true;
    return readBool(values, key, out, errorMessage);
}

bool readOptionalDouble(const std::map<std::string, std::string>& values,
                        const std::string& key,
                        double& out,
                        std::string* errorMessage)
{
    const auto it = values.find(key);
    if (it == values.end())
        return true;
    return readDouble(values, key, out, errorMessage);
}

bool readOptionalString(const std::map<std::string, std::string>& values,
                        const std::string& key,
                        std::string& out,
                        std::string* errorMessage)
{
    const auto it = values.find(key);
    if (it == values.end())
        return true;
    return readString(values, key, out, errorMessage);
}

void writeFrequencyGrid(std::ostringstream& out, const MorphFrequencyGrid& grid)
{
    writeKeyValue(out, "grid.type", std::string(morphFrequencyGridTypeName(grid.type)));
    writeKeyValue(out, "grid.band_count", grid.bandCount);
    writeKeyValue(out, "grid.lowest_frequency_hz", grid.lowestFrequencyHz);
    writeKeyValue(out, "grid.highest_frequency_hz", grid.highestFrequencyHz);
    writeKeyValue(out, "grid.edge_frequencies_hz", joinDoubles(grid.edgeFrequenciesHz));
}

bool readFrequencyGrid(const std::map<std::string, std::string>& values,
                       MorphFrequencyGrid& grid,
                       std::string* errorMessage)
{
    std::string typeName;
    if (!readString(values, "grid.type", typeName, errorMessage)
        || !morphFrequencyGridTypeFromName(typeName, grid.type)
        || !readInt(values, "grid.band_count", grid.bandCount, errorMessage)
        || !readDouble(values, "grid.lowest_frequency_hz", grid.lowestFrequencyHz, errorMessage)
        || !readDouble(values, "grid.highest_frequency_hz", grid.highestFrequencyHz, errorMessage))
    {
        if (errorMessage && typeName.size() > 0 && !morphFrequencyGridTypeFromName(typeName, grid.type))
            *errorMessage = "invalid frequency grid type in serialized morph recipe";
        return false;
    }

    std::string edgesText;
    if (!readString(values, "grid.edge_frequencies_hz", edgesText, errorMessage))
        return false;
    if (!parseDoubleList(edgesText, grid.edgeFrequenciesHz, errorMessage))
        return false;

    if (grid.bandCount <= 0 || static_cast<int>(grid.edgeFrequenciesHz.size()) != grid.bandCount + 1)
    {
        if (errorMessage)
            *errorMessage = "frequency grid edge count does not match band count";
        return false;
    }
    return true;
}

void writeUnmatchedPartialPolicies(std::ostringstream& out, const MorphUnmatchedPartialPolicies& policies)
{
    writeKeyValue(out, "unmatched.source_policy", std::string(morphUnmatchedPartialPolicyName(policies.sourcePolicy)));
    writeKeyValue(out, "unmatched.target_policy", std::string(morphUnmatchedPartialPolicyName(policies.targetPolicy)));
    writeKeyValue(out, "unmatched.discard_opt_in", policies.discardOptIn);
}

bool readUnmatchedPartialPolicies(const std::map<std::string, std::string>& values,
                                  MorphUnmatchedPartialPolicies& policies,
                                  std::string* errorMessage)
{
    std::string sourcePolicyName = morphUnmatchedPartialPolicyName(policies.sourcePolicy);
    std::string targetPolicyName = morphUnmatchedPartialPolicyName(policies.targetPolicy);
    if (!readOptionalString(values, "unmatched.source_policy", sourcePolicyName, errorMessage)
        || !readOptionalString(values, "unmatched.target_policy", targetPolicyName, errorMessage)
        || !readOptionalBool(values, "unmatched.discard_opt_in", policies.discardOptIn, errorMessage))
    {
        return false;
    }

    if (!morphUnmatchedPartialPolicyFromName(sourcePolicyName, policies.sourcePolicy))
    {
        if (errorMessage)
            *errorMessage = "invalid source unmatched partial policy in serialized morph recipe";
        return false;
    }
    if (!morphUnmatchedPartialPolicyFromName(targetPolicyName, policies.targetPolicy))
    {
        if (errorMessage)
            *errorMessage = "invalid target unmatched partial policy in serialized morph recipe";
        return false;
    }
    return validateUnmatchedPartialPolicies(policies, errorMessage);
}

void writeRenderGainSettings(std::ostringstream& out, const MorphRenderGainSettings& gains)
{
    writeKeyValue(out, "gain.partial", gains.partialGain);
    writeKeyValue(out, "gain.partial_noise", gains.partialNoiseGain);
    writeKeyValue(out, "gain.orphan_residual", gains.orphanResidualGain);
}

bool readRenderGainSettings(const std::map<std::string, std::string>& values,
                            MorphRenderGainSettings& gains,
                            std::string* errorMessage)
{
    return readOptionalDouble(values, "gain.partial", gains.partialGain, errorMessage)
           && readOptionalDouble(values, "gain.partial_noise", gains.partialNoiseGain, errorMessage)
           && readOptionalDouble(values, "gain.orphan_residual", gains.orphanResidualGain, errorMessage);
}

void writePhaseSettings(std::ostringstream& out, const MorphPhaseSettings& settings)
{
    writeKeyValue(out, "phase.strategy", std::string(morphPhaseStrategyName(settings.strategy)));
}

bool readPhaseSettings(const std::map<std::string, std::string>& values,
                       MorphPhaseSettings& settings,
                       std::string* errorMessage)
{
    std::string strategyName = morphPhaseStrategyName(settings.strategy);
    if (!readOptionalString(values, "phase.strategy", strategyName, errorMessage))
        return false;
    if (!morphPhaseStrategyFromName(strategyName, settings.strategy))
    {
        if (errorMessage)
            *errorMessage = "invalid phase strategy in serialized morph recipe";
        return false;
    }
    return true;
}

void writeLifecycleFadeSettings(std::ostringstream& out, const MorphLifecycleFadeSettings& settings)
{
    writeKeyValue(out, "lifecycle.birth_fade_frames", settings.birthFadeFrames);
    writeKeyValue(out, "lifecycle.death_fade_frames", settings.deathFadeFrames);
    writeKeyValue(out, "lifecycle.max_gain_step_per_frame", settings.maxGainStepPerFrame);
    writeKeyValue(out, "lifecycle.curve", std::string(morphCurveShapeName(settings.curve)));
}

bool readLifecycleFadeSettings(const std::map<std::string, std::string>& values,
                               MorphLifecycleFadeSettings& settings,
                               std::string* errorMessage)
{
    std::string curveName = morphCurveShapeName(settings.curve);
    if (!readOptionalInt(values, "lifecycle.birth_fade_frames", settings.birthFadeFrames, errorMessage)
        || !readOptionalInt(values, "lifecycle.death_fade_frames", settings.deathFadeFrames, errorMessage)
        || !readOptionalDouble(values,
                               "lifecycle.max_gain_step_per_frame",
                               settings.maxGainStepPerFrame,
                               errorMessage)
        || !readOptionalString(values, "lifecycle.curve", curveName, errorMessage))
    {
        return false;
    }
    if (!morphCurveShapeFromName(curveName, settings.curve))
    {
        if (errorMessage)
            *errorMessage = "invalid lifecycle fade curve in serialized morph recipe";
        return false;
    }
    return true;
}

void writeTransientSettings(std::ostringstream& out, const MorphTransientSettings& settings)
{
    writeKeyValue(out, "transient.policy", std::string(morphTransientPolicyName(settings.policy)));
    writeKeyValue(out, "transient.protection_window_seconds", settings.protectionWindowSeconds);
    writeKeyValue(out, "transient.max_onset_alignment_offset_seconds",
                  settings.maxOnsetAlignmentOffsetSeconds);
}

bool readTransientSettings(const std::map<std::string, std::string>& values,
                           MorphTransientSettings& settings,
                           std::string* errorMessage)
{
    std::string policyName = morphTransientPolicyName(settings.policy);
    if (!readOptionalString(values, "transient.policy", policyName, errorMessage)
        || !readOptionalDouble(values,
                               "transient.protection_window_seconds",
                               settings.protectionWindowSeconds,
                               errorMessage)
        || !readOptionalDouble(values,
                               "transient.max_onset_alignment_offset_seconds",
                               settings.maxOnsetAlignmentOffsetSeconds,
                               errorMessage))
    {
        return false;
    }
    if (!morphTransientPolicyFromName(policyName, settings.policy))
    {
        if (errorMessage)
            *errorMessage = "invalid transient policy in serialized morph recipe";
        return false;
    }
    return true;
}

void writeTransientMarkers(std::ostringstream& out,
                           const char* sourcePrefix,
                           const std::vector<MorphTransientMarker>& markers)
{
    const std::string prefix = std::string(sourcePrefix) + ".transient";
    writeKeyValue(out, prefix + ".count", static_cast<int>(markers.size()));
    for (size_t i = 0; i < markers.size(); ++i)
    {
        const std::string markerPrefix = prefix + "." + std::to_string(i) + ".";
        writeKeyValue(out, markerPrefix + "time_seconds", markers[i].timeSeconds);
        writeKeyValue(out, markerPrefix + "strength", markers[i].strength);
        writeKeyValue(out, markerPrefix + "frame_index", markers[i].frameIndex);
    }
}

bool readTransientMarkers(const std::map<std::string, std::string>& values,
                          const char* sourcePrefix,
                          std::vector<MorphTransientMarker>& markers,
                          std::string* errorMessage)
{
    markers.clear();

    const std::string prefix = std::string(sourcePrefix) + ".transient";
    int count = 0;
    if (!readOptionalInt(values, prefix + ".count", count, errorMessage))
        return false;
    if (count < 0)
    {
        if (errorMessage)
            *errorMessage = "invalid transient marker count in serialized morph recipe";
        return false;
    }

    markers.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i)
    {
        const std::string markerPrefix = prefix + "." + std::to_string(i) + ".";
        MorphTransientMarker marker;
        if (!readDouble(values, markerPrefix + "time_seconds", marker.timeSeconds, errorMessage)
            || !readDouble(values, markerPrefix + "strength", marker.strength, errorMessage)
            || !readInt(values, markerPrefix + "frame_index", marker.frameIndex, errorMessage))
        {
            return false;
        }
        markers.push_back(marker);
    }
    return true;
}

void writeCorrespondenceRecord(std::ostringstream& out, int index, const MorphCorrespondenceRecord& record)
{
    const std::string prefix = "correspondence." + std::to_string(index) + ".";
    writeKeyValue(out, prefix + "band_index", record.bandIndex);
    writeKeyValue(out, prefix + "rank", record.rank);
    writeKeyValue(out, prefix + "kind", std::string(morphCorrespondenceKindName(record.kind)));
    writeKeyValue(out, prefix + "source_partial_index", record.sourcePartialIndex);
    writeKeyValue(out, prefix + "source_frame_index", record.sourceFrameIndex);
    writeKeyValue(out, prefix + "source_frequency_hz", record.sourceFrequencyHz);
    writeKeyValue(out, prefix + "source_amplitude", record.sourceAmplitude);
    writeKeyValue(out, prefix + "target_partial_index", record.targetPartialIndex);
    writeKeyValue(out, prefix + "target_frame_index", record.targetFrameIndex);
    writeKeyValue(out, prefix + "target_frequency_hz", record.targetFrequencyHz);
    writeKeyValue(out, prefix + "target_amplitude", record.targetAmplitude);
    writeKeyValue(out, prefix + "has_cost_diagnostics", record.hasCostDiagnostics);
    writeKeyValue(out, prefix + "log_frequency_cost", record.logFrequencyCost);
    writeKeyValue(out, prefix + "amplitude_rank_cost", record.amplitudeRankCost);
    writeKeyValue(out, prefix + "confidence_cost", record.confidenceCost);
    writeKeyValue(out, prefix + "continuity_cost", record.continuityCost);
    writeKeyValue(out, prefix + "total_cost", record.totalCost);
}

bool readCorrespondenceRecords(const std::map<std::string, std::string>& values,
                               std::vector<MorphCorrespondenceRecord>& records,
                               std::string* errorMessage)
{
    records.clear();

    int count = 0;
    if (!readOptionalInt(values, "correspondence.count", count, errorMessage))
        return false;
    if (count < 0)
    {
        if (errorMessage)
            *errorMessage = "invalid correspondence record count in serialized morph recipe";
        return false;
    }

    records.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i)
    {
        const std::string prefix = "correspondence." + std::to_string(i) + ".";
        MorphCorrespondenceRecord record;
        std::string kindName;
        if (!readInt(values, prefix + "band_index", record.bandIndex, errorMessage)
            || !readInt(values, prefix + "rank", record.rank, errorMessage)
            || !readString(values, prefix + "kind", kindName, errorMessage)
            || !morphCorrespondenceKindFromName(kindName, record.kind)
            || !readInt(values, prefix + "source_partial_index", record.sourcePartialIndex, errorMessage)
            || !readInt(values, prefix + "source_frame_index", record.sourceFrameIndex, errorMessage)
            || !readDouble(values, prefix + "source_frequency_hz", record.sourceFrequencyHz, errorMessage)
            || !readDouble(values, prefix + "source_amplitude", record.sourceAmplitude, errorMessage)
            || !readInt(values, prefix + "target_partial_index", record.targetPartialIndex, errorMessage)
            || !readInt(values, prefix + "target_frame_index", record.targetFrameIndex, errorMessage)
            || !readDouble(values, prefix + "target_frequency_hz", record.targetFrequencyHz, errorMessage)
            || !readDouble(values, prefix + "target_amplitude", record.targetAmplitude, errorMessage))
        {
            if (errorMessage && !kindName.empty() && !morphCorrespondenceKindFromName(kindName, record.kind))
                *errorMessage = "invalid correspondence kind in serialized morph recipe";
            return false;
        }
        if (!readOptionalBool(values, prefix + "has_cost_diagnostics", record.hasCostDiagnostics, errorMessage)
            || !readOptionalDouble(values, prefix + "log_frequency_cost", record.logFrequencyCost, errorMessage)
            || !readOptionalDouble(values, prefix + "amplitude_rank_cost", record.amplitudeRankCost, errorMessage)
            || !readOptionalDouble(values, prefix + "confidence_cost", record.confidenceCost, errorMessage)
            || !readOptionalDouble(values, prefix + "continuity_cost", record.continuityCost, errorMessage)
            || !readOptionalDouble(values, prefix + "total_cost", record.totalCost, errorMessage))
        {
            return false;
        }
        records.push_back(record);
    }

    return true;
}

void writeSource(std::ostringstream& out, const char* prefix, const MorphSourceMetadata& source)
{
    writeKeyValue(out, std::string(prefix) + ".id", source.id);
    writeKeyValue(out, std::string(prefix) + ".hash", source.hash);
    writeKeyValue(out, std::string(prefix) + ".name", source.name);
    writeKeyValue(out, std::string(prefix) + ".sample_rate", source.sampleRate);
    writeKeyValue(out, std::string(prefix) + ".duration_seconds", source.durationSeconds);
    writeKeyValue(out, std::string(prefix) + ".frame_hop_seconds", source.frameHopSeconds);
    writeKeyValue(out, std::string(prefix) + ".lowest_frequency_hz", source.lowestFrequencyHz);
    writeKeyValue(out, std::string(prefix) + ".highest_frequency_hz", source.highestFrequencyHz);
    writeKeyValue(out, std::string(prefix) + ".channel_count", source.channelCount);
    writeKeyValue(out, std::string(prefix) + ".frame_count", source.frameCount);
    writeKeyValue(out, std::string(prefix) + ".partial_count", source.partialCount);
    writeKeyValue(out, std::string(prefix) + ".has_phase", source.hasPhase);
    writeKeyValue(out, std::string(prefix) + ".has_residual", source.hasResidual);
}

bool readSource(const std::map<std::string, std::string>& values,
                const char* prefix,
                MorphSourceMetadata& source,
                std::string* errorMessage)
{
    return readString(values, std::string(prefix) + ".id", source.id, errorMessage)
           && readString(values, std::string(prefix) + ".hash", source.hash, errorMessage)
           && readString(values, std::string(prefix) + ".name", source.name, errorMessage)
           && readDouble(values, std::string(prefix) + ".sample_rate", source.sampleRate, errorMessage)
           && readDouble(values, std::string(prefix) + ".duration_seconds", source.durationSeconds, errorMessage)
           && readDouble(values, std::string(prefix) + ".frame_hop_seconds", source.frameHopSeconds, errorMessage)
           && readDouble(values, std::string(prefix) + ".lowest_frequency_hz", source.lowestFrequencyHz, errorMessage)
           && readDouble(values, std::string(prefix) + ".highest_frequency_hz", source.highestFrequencyHz, errorMessage)
           && readInt(values, std::string(prefix) + ".channel_count", source.channelCount, errorMessage)
           && readInt(values, std::string(prefix) + ".frame_count", source.frameCount, errorMessage)
           && readInt(values, std::string(prefix) + ".partial_count", source.partialCount, errorMessage)
           && readBool(values, std::string(prefix) + ".has_phase", source.hasPhase, errorMessage)
           && readBool(values, std::string(prefix) + ".has_residual", source.hasResidual, errorMessage);
}

bool isKnownSourceKey(const std::string& key, const char* prefix)
{
    const std::string sourcePrefix = std::string(prefix) + ".";
    if (key.rfind(sourcePrefix, 0) != 0)
        return false;

    const std::string field = key.substr(sourcePrefix.size());
    return field == "id" || field == "hash" || field == "name" || field == "sample_rate"
           || field == "duration_seconds" || field == "frame_hop_seconds"
           || field == "lowest_frequency_hz" || field == "highest_frequency_hz"
           || field == "channel_count" || field == "frame_count" || field == "partial_count"
           || field == "has_phase" || field == "has_residual";
}

bool isKnownLaneKey(const std::string& key)
{
    constexpr std::string_view kLanePrefix = "lane.";
    if (key.rfind(kLanePrefix, 0) != 0)
        return false;

    for (const auto* laneName : kLaneNames)
    {
        const std::string lanePrefix = std::string(kLanePrefix) + laneName + ".";
        if (key == lanePrefix + "linked" || key == lanePrefix + "amount" || key == lanePrefix + "curve")
            return true;
    }
    return false;
}

bool isKnownFrequencyGridKey(const std::string& key)
{
    return key == "grid.type" || key == "grid.band_count" || key == "grid.lowest_frequency_hz"
           || key == "grid.highest_frequency_hz" || key == "grid.edge_frequencies_hz";
}

bool isKnownUnmatchedPartialPolicyKey(const std::string& key)
{
    return key == "unmatched.source_policy" || key == "unmatched.target_policy"
           || key == "unmatched.discard_opt_in";
}

bool isKnownRenderGainKey(const std::string& key)
{
    return key == "gain.partial" || key == "gain.partial_noise" || key == "gain.orphan_residual";
}

bool isKnownPhaseSettingsKey(const std::string& key)
{
    return key == "phase.strategy";
}

bool isKnownLifecycleFadeSettingsKey(const std::string& key)
{
    return key == "lifecycle.birth_fade_frames" || key == "lifecycle.death_fade_frames"
           || key == "lifecycle.max_gain_step_per_frame" || key == "lifecycle.curve";
}

bool isKnownTransientSettingsKey(const std::string& key)
{
    return key == "transient.policy" || key == "transient.protection_window_seconds"
           || key == "transient.max_onset_alignment_offset_seconds";
}

bool isKnownTransientMarkerKey(const std::string& key, const char* sourcePrefix)
{
    const std::string countKey = std::string(sourcePrefix) + ".transient.count";
    if (key == countKey)
        return true;

    const std::string prefix = std::string(sourcePrefix) + ".transient.";
    if (key.rfind(prefix, 0) != 0)
        return false;

    const size_t indexStart = prefix.size();
    const size_t indexEnd = key.find('.', indexStart);
    if (indexEnd == std::string::npos || indexEnd == indexStart)
        return false;

    int index = -1;
    const auto* begin = key.data() + indexStart;
    const auto* end = key.data() + indexEnd;
    const auto result = std::from_chars(begin, end, index);
    if (result.ec != std::errc{} || result.ptr != end || index < 0)
        return false;

    const std::string field = key.substr(indexEnd + 1);
    return field == "time_seconds" || field == "strength" || field == "frame_index";
}

bool isKnownCorrespondenceKey(const std::string& key)
{
    if (key == "correspondence.count")
        return true;

    constexpr std::string_view kPrefix = "correspondence.";
    if (key.rfind(kPrefix, 0) != 0)
        return false;

    const size_t indexStart = kPrefix.size();
    const size_t indexEnd = key.find('.', indexStart);
    if (indexEnd == std::string::npos || indexEnd == indexStart)
        return false;

    int index = -1;
    const auto* begin = key.data() + indexStart;
    const auto* end = key.data() + indexEnd;
    const auto result = std::from_chars(begin, end, index);
    if (result.ec != std::errc{} || result.ptr != end || index < 0)
        return false;

    const std::string field = key.substr(indexEnd + 1);
    return field == "band_index" || field == "rank" || field == "kind"
           || field == "source_partial_index" || field == "source_frame_index"
           || field == "source_frequency_hz" || field == "source_amplitude"
           || field == "target_partial_index" || field == "target_frame_index"
           || field == "target_frequency_hz" || field == "target_amplitude"
           || field == "has_cost_diagnostics" || field == "log_frequency_cost"
           || field == "amplitude_rank_cost" || field == "confidence_cost"
           || field == "continuity_cost" || field == "total_cost";
}

bool isKnownSerializedKey(const std::string& key)
{
    return key == "engine_version" || key == "default_morph_amount" || isKnownSourceKey(key, "source_a")
           || isKnownSourceKey(key, "source_b") || isKnownFrequencyGridKey(key) || isKnownLaneKey(key)
           || isKnownUnmatchedPartialPolicyKey(key) || isKnownRenderGainKey(key)
           || isKnownPhaseSettingsKey(key) || isKnownLifecycleFadeSettingsKey(key)
           || isKnownTransientSettingsKey(key) || isKnownTransientMarkerKey(key, "source_a")
           || isKnownTransientMarkerKey(key, "source_b")
           || isKnownCorrespondenceKey(key);
}

void addIssue(MorphCompatibilityReport& report,
              MorphCompatibilitySeverity severity,
              MorphCompatibilityCode code,
              const std::string& source,
              const std::string& message)
{
    report.issues.push_back({severity, code, source, message});
}

bool hasValidFrequencyBounds(const MorphSourceMetadata& source)
{
    return isPositive(source.lowestFrequencyHz) && isPositive(source.highestFrequencyHz)
           && source.highestFrequencyHz > source.lowestFrequencyHz;
}

bool assignmentSortLess(const MorphBandAssignment& a, const MorphBandAssignment& b)
{
    if (a.bandIndex != b.bandIndex)
        return a.bandIndex < b.bandIndex;
    if (a.frequencyHz != b.frequencyHz)
        return a.frequencyHz < b.frequencyHz;
    if (a.partialIndex != b.partialIndex)
        return a.partialIndex < b.partialIndex;
    if (a.frameIndex != b.frameIndex)
        return a.frameIndex < b.frameIndex;
    return a.amplitude < b.amplitude;
}

bool isCorrespondenceCandidate(const MorphBandAssignment& assignment)
{
    return assignment.kind == MorphBandAssignmentKind::Assigned && assignment.bandIndex >= 0
           && std::isfinite(assignment.frequencyHz) && std::isfinite(assignment.amplitude);
}

struct GreedyCandidate
{
    MorphBandAssignment assignment;
    int amplitudeRank = 0;
};

bool isGreedyCandidate(const MorphBandAssignment& assignment)
{
    return isCorrespondenceCandidate(assignment) && assignment.frequencyHz > 0.0;
}

void assignAmplitudeRanks(std::vector<GreedyCandidate>& candidates)
{
    std::vector<size_t> order(candidates.size());
    for (size_t i = 0; i < order.size(); ++i)
        order[i] = i;

    std::sort(order.begin(), order.end(), [&candidates](size_t a, size_t b) {
        const auto& left = candidates[a].assignment;
        const auto& right = candidates[b].assignment;
        if (left.amplitude != right.amplitude)
            return left.amplitude > right.amplitude;
        return assignmentSortLess(left, right);
    });

    for (size_t rank = 0; rank < order.size(); ++rank)
        candidates[order[rank]].amplitudeRank = static_cast<int>(rank);
}

bool candidateAmplitudeLess(const GreedyCandidate& a, const GreedyCandidate& b)
{
    if (a.assignment.amplitude != b.assignment.amplitude)
        return a.assignment.amplitude > b.assignment.amplitude;
    return assignmentSortLess(a.assignment, b.assignment);
}

std::vector<GreedyCandidate> makeGreedyCandidates(const std::vector<MorphBandAssignment>& assignments)
{
    std::vector<GreedyCandidate> candidates;
    candidates.reserve(assignments.size());
    for (const auto& assignment : assignments)
    {
        if (isGreedyCandidate(assignment))
            candidates.push_back({assignment, 0});
    }

    std::sort(candidates.begin(), candidates.end(), [](const GreedyCandidate& a, const GreedyCandidate& b) {
        return assignmentSortLess(a.assignment, b.assignment);
    });
    assignAmplitudeRanks(candidates);
    return candidates;
}

std::vector<GreedyCandidate> boundCandidatesForAssignmentMatrix(const std::vector<GreedyCandidate>& candidates,
                                                               int maxCandidatesPerBand)
{
    std::vector<GreedyCandidate> bounded = candidates;
    const int sanitizedLimit = std::max(1, maxCandidatesPerBand);
    if (static_cast<int>(bounded.size()) > sanitizedLimit)
    {
        std::sort(bounded.begin(), bounded.end(), candidateAmplitudeLess);
        bounded.resize(static_cast<size_t>(sanitizedLimit));
    }
    std::sort(bounded.begin(), bounded.end(), [](const GreedyCandidate& a, const GreedyCandidate& b) {
        return assignmentSortLess(a.assignment, b.assignment);
    });
    assignAmplitudeRanks(bounded);
    return bounded;
}

void copySourceAssignment(MorphCorrespondenceRecord& record, const MorphBandAssignment& assignment)
{
    record.sourcePartialIndex = assignment.partialIndex;
    record.sourceFrameIndex = assignment.frameIndex;
    record.sourceFrequencyHz = assignment.frequencyHz;
    record.sourceAmplitude = assignment.amplitude;
}

void copyTargetAssignment(MorphCorrespondenceRecord& record, const MorphBandAssignment& assignment)
{
    record.targetPartialIndex = assignment.partialIndex;
    record.targetFrameIndex = assignment.frameIndex;
    record.targetFrequencyHz = assignment.frequencyHz;
    record.targetAmplitude = assignment.amplitude;
}

double logFrequencyCost(double sourceFrequencyHz, double targetFrequencyHz)
{
    return std::abs(std::log(targetFrequencyHz / sourceFrequencyHz));
}

double nLog2N(int count)
{
    if (count <= 1)
        return static_cast<double>(std::max(0, count));
    return static_cast<double>(count) * std::log2(static_cast<double>(count));
}

double amplitudeRankCost(int sourceRank, int targetRank, double amplitudeRankWeight)
{
    return std::max(0.0, amplitudeRankWeight) * static_cast<double>(std::abs(sourceRank - targetRank));
}

void addGreedyCostDiagnostics(MorphCorrespondenceRecord& record,
                              const GreedyCandidate& source,
                              const GreedyCandidate& target,
                              double amplitudeRankWeight)
{
    record.hasCostDiagnostics = true;
    record.logFrequencyCost =
        logFrequencyCost(source.assignment.frequencyHz, target.assignment.frequencyHz);
    record.amplitudeRankCost = amplitudeRankCost(source.amplitudeRank, target.amplitudeRank, amplitudeRankWeight);
    record.totalCost = record.logFrequencyCost + record.amplitudeRankCost + record.confidenceCost
                       + record.continuityCost;
}

double boundedConfidence(double confidence)
{
    if (!std::isfinite(confidence))
        return 0.0;
    return std::max(0.0, std::min(1.0, confidence));
}

double confidenceCost(const GreedyCandidate& source, const GreedyCandidate& target, double confidenceWeight)
{
    const double missingConfidence = 2.0 - boundedConfidence(source.assignment.confidence)
                                     - boundedConfidence(target.assignment.confidence);
    return std::max(0.0, confidenceWeight) * missingConfidence;
}

std::map<int, int> makePreviousTargetBySource(const std::vector<MorphCorrespondenceRecord>& records)
{
    std::map<int, int> previous;
    for (const auto& record : records)
    {
        if (record.kind == MorphCorrespondenceKind::Matched)
            previous[record.sourcePartialIndex] = record.targetPartialIndex;
    }
    return previous;
}

double continuityCost(const GreedyCandidate& source,
                      const GreedyCandidate& target,
                      const std::map<int, int>& previousTargetBySource,
                      double continuityWeight)
{
    const auto previous = previousTargetBySource.find(source.assignment.partialIndex);
    if (previous == previousTargetBySource.end() || previous->second == target.assignment.partialIndex)
        return 0.0;
    return std::max(0.0, continuityWeight);
}

void addAssignmentMatrixCostDiagnostics(MorphCorrespondenceRecord& record,
                                        const GreedyCandidate& source,
                                        const GreedyCandidate& target,
                                        const MorphAssignmentMatrixOptions& options,
                                        const std::map<int, int>& previousTargetBySource)
{
    record.hasCostDiagnostics = true;
    record.logFrequencyCost = logFrequencyCost(source.assignment.frequencyHz, target.assignment.frequencyHz);
    record.amplitudeRankCost = amplitudeRankCost(source.amplitudeRank, target.amplitudeRank,
                                                 options.amplitudeRankWeight);
    record.confidenceCost = confidenceCost(source, target, options.confidenceWeight);
    record.continuityCost = continuityCost(source, target, previousTargetBySource, options.continuityWeight);
    record.totalCost = record.logFrequencyCost + record.amplitudeRankCost + record.confidenceCost
                       + record.continuityCost;
}

struct MorphPerformanceCandidateSummary
{
    int sourceCount = 0;
    int targetCount = 0;
    int bandCount = 0;
    int candidateEdgeCount = 0;
    int maxCandidateEdgesPerBand = 0;
};

MorphPerformanceCandidateSummary summarizePerformanceCandidates(
    const std::vector<MorphBandAssignment>& sourceAssignments,
    const std::vector<MorphBandAssignment>& targetAssignments)
{
    MorphPerformanceCandidateSummary summary;
    std::map<int, std::pair<int, int>> countsByBand;

    for (const auto& assignment : sourceAssignments)
    {
        if (isCorrespondenceCandidate(assignment))
        {
            ++summary.sourceCount;
            ++countsByBand[assignment.bandIndex].first;
        }
    }
    for (const auto& assignment : targetAssignments)
    {
        if (isCorrespondenceCandidate(assignment))
        {
            ++summary.targetCount;
            ++countsByBand[assignment.bandIndex].second;
        }
    }

    summary.bandCount = static_cast<int>(countsByBand.size());
    for (const auto& entry : countsByBand)
    {
        const int edgeCount = entry.second.first * entry.second.second;
        summary.candidateEdgeCount += edgeCount;
        summary.maxCandidateEdgesPerBand = std::max(summary.maxCandidateEdgesPerBand, edgeCount);
    }
    return summary;
}

double pairLogFrequencyCost(double sourceFrequencyHz, double targetFrequencyHz)
{
    if (sourceFrequencyHz <= 0.0 || targetFrequencyHz <= 0.0
        || !std::isfinite(sourceFrequencyHz) || !std::isfinite(targetFrequencyHz))
    {
        return 1.0e12;
    }
    return logFrequencyCost(sourceFrequencyHz, targetFrequencyHz);
}

double recordPairCost(const MorphCorrespondenceRecord& record)
{
    if (record.hasCostDiagnostics)
        return record.totalCost;
    return pairLogFrequencyCost(record.sourceFrequencyHz, record.targetFrequencyHz);
}

double swappedPairCost(const MorphCorrespondenceRecord& sourceRecord, const MorphCorrespondenceRecord& targetRecord)
{
    return pairLogFrequencyCost(sourceRecord.sourceFrequencyHz, targetRecord.targetFrequencyHz);
}

void copyTargetFields(MorphCorrespondenceRecord& destination, const MorphCorrespondenceRecord& source)
{
    destination.targetPartialIndex = source.targetPartialIndex;
    destination.targetFrameIndex = source.targetFrameIndex;
    destination.targetFrequencyHz = source.targetFrequencyHz;
    destination.targetAmplitude = source.targetAmplitude;
}

void refreshSmoothedCostDiagnostics(MorphCorrespondenceRecord& record)
{
    record.hasCostDiagnostics = true;
    record.logFrequencyCost = pairLogFrequencyCost(record.sourceFrequencyHz, record.targetFrequencyHz);
    record.amplitudeRankCost = 0.0;
    record.confidenceCost = 0.0;
    record.continuityCost = 0.0;
    record.totalCost = record.logFrequencyCost;
}

bool greedyCostIsBetter(double candidateTotalCost,
                        double candidateLogFrequencyCost,
                        const MorphBandAssignment& candidateTarget,
                        double bestTotalCost,
                        double bestLogFrequencyCost,
                        const MorphBandAssignment& bestTarget)
{
    if (candidateTotalCost != bestTotalCost)
        return candidateTotalCost < bestTotalCost;
    if (candidateLogFrequencyCost != bestLogFrequencyCost)
        return candidateLogFrequencyCost < bestLogFrequencyCost;
    return assignmentSortLess(candidateTarget, bestTarget);
}

struct MatrixSearchState
{
    double bestCost = std::numeric_limits<double>::infinity();
    int evaluatedAssignmentCount = 0;
    std::vector<std::pair<size_t, size_t>> bestPairs;
};

void searchAssignmentMatrix(size_t sourceOffset,
                            int matchesNeeded,
                            const std::vector<std::vector<double>>& costs,
                            std::vector<bool>& targetUsed,
                            std::vector<std::pair<size_t, size_t>>& currentPairs,
                            double currentCost,
                            MatrixSearchState& state)
{
    const size_t sourceCount = costs.size();
    const size_t targetCount = targetUsed.size();
    if (matchesNeeded == 0)
    {
        ++state.evaluatedAssignmentCount;
        if (currentCost < state.bestCost)
        {
            state.bestCost = currentCost;
            state.bestPairs = currentPairs;
        }
        return;
    }
    if (sourceOffset >= sourceCount || static_cast<int>(sourceCount - sourceOffset) < matchesNeeded)
        return;
    if (currentCost >= state.bestCost)
        return;

    if (static_cast<int>(sourceCount - sourceOffset - 1) >= matchesNeeded)
        searchAssignmentMatrix(sourceOffset + 1, matchesNeeded, costs, targetUsed, currentPairs, currentCost, state);

    for (size_t targetOffset = 0; targetOffset < targetCount; ++targetOffset)
    {
        if (targetUsed[targetOffset])
            continue;

        targetUsed[targetOffset] = true;
        currentPairs.push_back({sourceOffset, targetOffset});
        searchAssignmentMatrix(sourceOffset + 1,
                               matchesNeeded - 1,
                               costs,
                               targetUsed,
                               currentPairs,
                               currentCost + costs[sourceOffset][targetOffset],
                               state);
        currentPairs.pop_back();
        targetUsed[targetOffset] = false;
    }
}

bool isSameCandidateIdentity(const GreedyCandidate& a, const GreedyCandidate& b)
{
    return a.assignment.partialIndex == b.assignment.partialIndex
           && a.assignment.frameIndex == b.assignment.frameIndex;
}

bool candidateWasMatched(const GreedyCandidate& candidate,
                         const std::vector<GreedyCandidate>& matchedCandidates)
{
    for (const auto& matched : matchedCandidates)
    {
        if (isSameCandidateIdentity(candidate, matched))
            return true;
    }
    return false;
}

double matchedCostSum(const std::vector<MorphCorrespondenceRecord>& records)
{
    double sum = 0.0;
    for (const auto& record : records)
    {
        if (record.kind == MorphCorrespondenceKind::Matched)
            sum += record.hasCostDiagnostics ? record.totalCost
                                             : pairLogFrequencyCost(record.sourceFrequencyHz,
                                                                    record.targetFrequencyHz);
    }
    return sum;
}

double amplitudeToPower(double amplitude)
{
    return amplitude * amplitude;
}

double powerToAmplitude(double power)
{
    return std::sqrt(std::max(0.0, power));
}

double decibelFloorToAmplitude(double decibelFloor)
{
    return std::pow(10.0, decibelFloor / 20.0);
}

double amplitudeToDecibels(double amplitude, double decibelFloor)
{
    const double floorAmplitude = decibelFloorToAmplitude(decibelFloor);
    return 20.0 * std::log10(std::max(floorAmplitude, amplitude));
}

double decibelsToAmplitude(double decibels)
{
    return std::pow(10.0, decibels / 20.0);
}

double interpolatedPower(double sourcePower, double targetPower, double morphAmount)
{
    const double amount = std::max(0.0, std::min(1.0, morphAmount));
    return sourcePower + amount * (targetPower - sourcePower);
}

double nonNegativeAmplitudePower(double amplitude)
{
    if (!std::isfinite(amplitude) || amplitude < 0.0)
        return 0.0;
    return amplitudeToPower(amplitude);
}

double nonNegativePower(double power)
{
    if (!std::isfinite(power) || power < 0.0)
        return 0.0;
    return power;
}

double gainToPowerScale(double gain)
{
    if (!std::isfinite(gain) || gain < 0.0)
        return 0.0;
    return amplitudeToPower(gain);
}

double unmatchedSinusoidalOutputPower(MorphUnmatchedPartialPolicy policy,
                                      MorphCorrespondenceKind kind,
                                      double availablePower,
                                      double morphAmount)
{
    const double amount = std::max(0.0, std::min(1.0, morphAmount));
    switch (policy)
    {
        case MorphUnmatchedPartialPolicy::FadeOut:
            return kind == MorphCorrespondenceKind::UnmatchedSource ? availablePower * (1.0 - amount)
                                                                    : 0.0;
        case MorphUnmatchedPartialPolicy::FadeIn:
            return kind == MorphCorrespondenceKind::UnmatchedTarget ? availablePower * amount
                                                                    : 0.0;
        case MorphUnmatchedPartialPolicy::Hold:
            return availablePower;
        case MorphUnmatchedPartialPolicy::MergeIntoResidual:
        case MorphUnmatchedPartialPolicy::Discard:
            return 0.0;
    }
    return 0.0;
}

double unmatchedResidualOutputPower(MorphUnmatchedPartialPolicy policy,
                                    MorphCorrespondenceKind kind,
                                    double availablePower,
                                    double morphAmount)
{
    if (policy != MorphUnmatchedPartialPolicy::MergeIntoResidual)
        return 0.0;

    const double amount = std::max(0.0, std::min(1.0, morphAmount));
    if (kind == MorphCorrespondenceKind::UnmatchedSource)
        return availablePower * (1.0 - amount);
    if (kind == MorphCorrespondenceKind::UnmatchedTarget)
        return availablePower * amount;
    return 0.0;
}

void applyUnmatchedPolicyFlags(MorphUnmatchedPartialDecision& decision)
{
    switch (decision.policy)
    {
        case MorphUnmatchedPartialPolicy::FadeOut:
        case MorphUnmatchedPartialPolicy::FadeIn:
            decision.fadesPartial = true;
            break;
        case MorphUnmatchedPartialPolicy::Hold:
            decision.holdsPartial = true;
            break;
        case MorphUnmatchedPartialPolicy::MergeIntoResidual:
            decision.routesToResidual = true;
            decision.residualPower = amplitudeToPower(decision.amplitude);
            break;
        case MorphUnmatchedPartialPolicy::Discard:
            decision.discardsPartial = true;
            break;
    }
}

MorphUnmatchedPartialDecision makeUnmatchedDecision(const MorphCorrespondenceRecord& record,
                                                    MorphUnmatchedPartialPolicy policy)
{
    MorphUnmatchedPartialDecision decision;
    decision.policy = policy;
    decision.kind = record.kind;
    decision.bandIndex = record.bandIndex;
    decision.rank = record.rank;

    if (record.kind == MorphCorrespondenceKind::UnmatchedSource)
    {
        decision.partialIndex = record.sourcePartialIndex;
        decision.frameIndex = record.sourceFrameIndex;
        decision.frequencyHz = record.sourceFrequencyHz;
        decision.amplitude = record.sourceAmplitude;
    }
    else if (record.kind == MorphCorrespondenceKind::UnmatchedTarget)
    {
        decision.partialIndex = record.targetPartialIndex;
        decision.frameIndex = record.targetFrameIndex;
        decision.frequencyHz = record.targetFrequencyHz;
        decision.amplitude = record.targetAmplitude;
    }

    applyUnmatchedPolicyFlags(decision);
    return decision;
}

MorphFrequencyInterpolationResult makeFixedSnapshotFrequency(double frequencyHz, const MorphFrequencyGrid& grid)
{
    MorphFrequencyInterpolationResult result;
    result.frequencyHz = frequencyHz;
    result.sourceBandIndex = findFrequencyBandIndex(grid, frequencyHz);
    result.targetBandIndex = result.sourceBandIndex;
    result.outputBandIndex = result.sourceBandIndex;
    result.valid = std::isfinite(frequencyHz) && frequencyHz > 0.0;
    result.clampedToGridBounds = false;
    result.crossedBand = false;
    return result;
}

MorphAmplitudeInterpolationResult makeFixedSnapshotAmplitude(double amplitude)
{
    MorphAmplitudeInterpolationResult result;
    result.amplitude = std::isfinite(amplitude) && amplitude > 0.0 ? amplitude : 0.0;
    result.power = amplitudeToPower(result.amplitude);
    result.valid = true;
    return result;
}

MorphAmplitudeInterpolationResult evaluateUnmatchedSnapshotAmplitude(MorphUnmatchedPartialPolicy policy,
                                                                     MorphCorrespondenceKind kind,
                                                                     double availableAmplitude,
                                                                     double morphAmount,
                                                                     const MorphAmplitudeInterpolationOptions& options)
{
    switch (policy)
    {
        case MorphUnmatchedPartialPolicy::FadeOut:
            return kind == MorphCorrespondenceKind::UnmatchedSource
                       ? interpolateMorphAmplitude(availableAmplitude, 0.0, morphAmount, options)
                       : makeFixedSnapshotAmplitude(0.0);
        case MorphUnmatchedPartialPolicy::FadeIn:
            return kind == MorphCorrespondenceKind::UnmatchedTarget
                       ? interpolateMorphAmplitude(0.0, availableAmplitude, morphAmount, options)
                       : makeFixedSnapshotAmplitude(0.0);
        case MorphUnmatchedPartialPolicy::Hold:
            return makeFixedSnapshotAmplitude(availableAmplitude);
        case MorphUnmatchedPartialPolicy::MergeIntoResidual:
        case MorphUnmatchedPartialPolicy::Discard:
            return makeFixedSnapshotAmplitude(0.0);
    }
    return makeFixedSnapshotAmplitude(0.0);
}

MorphSnapshotPartial evaluateSnapshotPartial(const MorphRecipe& recipe,
                                             const MorphCorrespondenceRecord& record,
                                             double frequencyMorphAmount,
                                             double amplitudeMorphAmount,
                                             double phaseMorphAmount,
                                             int lifecycleFrameIndex,
                                             const MorphFrequencyInterpolationOptions& frequencyOptions,
                                             const MorphAmplitudeInterpolationOptions& amplitudeOptions)
{
    MorphSnapshotPartial partial;
    partial.bandIndex = record.bandIndex;
    partial.rank = record.rank;
    partial.sourcePartialIndex = record.sourcePartialIndex;
    partial.sourceFrameIndex = record.sourceFrameIndex;
    partial.targetPartialIndex = record.targetPartialIndex;
    partial.targetFrameIndex = record.targetFrameIndex;
    partial.kind = record.kind;

    if (record.kind == MorphCorrespondenceKind::Matched)
    {
        partial.frequencyInterpolation = interpolateMorphFrequency(recipe.frequencyGrid,
                                                                   record.sourceFrequencyHz,
                                                                   record.targetFrequencyHz,
                                                                   frequencyMorphAmount,
                                                                   frequencyOptions);
        partial.amplitudeInterpolation = interpolateMorphAmplitude(record.sourceAmplitude,
                                                                   record.targetAmplitude,
                                                                   amplitudeMorphAmount,
                                                                   amplitudeOptions);
    }
    else if (record.kind == MorphCorrespondenceKind::UnmatchedSource)
    {
        partial.unmatchedPolicy = recipe.unmatchedPartialPolicies.sourcePolicy;
        partial.frequencyInterpolation = makeFixedSnapshotFrequency(record.sourceFrequencyHz, recipe.frequencyGrid);
        partial.amplitudeInterpolation = evaluateUnmatchedSnapshotAmplitude(partial.unmatchedPolicy,
                                                                            record.kind,
                                                                            record.sourceAmplitude,
                                                                            amplitudeMorphAmount,
                                                                            amplitudeOptions);
    }
    else
    {
        partial.unmatchedPolicy = recipe.unmatchedPartialPolicies.targetPolicy;
        partial.frequencyInterpolation = makeFixedSnapshotFrequency(record.targetFrequencyHz, recipe.frequencyGrid);
        partial.amplitudeInterpolation = evaluateUnmatchedSnapshotAmplitude(partial.unmatchedPolicy,
                                                                            record.kind,
                                                                            record.targetAmplitude,
                                                                            amplitudeMorphAmount,
                                                                            amplitudeOptions);
    }

    partial.lifecycle = evaluateMorphLifecycleFade(record.kind, lifecycleFrameIndex, recipe.lifecycle);
    partial.frequencyHz = partial.frequencyInterpolation.frequencyHz;
    partial.amplitude = partial.amplitudeInterpolation.amplitude * partial.lifecycle.amplitudeScale;
    partial.power = amplitudeToPower(partial.amplitude);
    partial.routedToResidual = partial.unmatchedPolicy == MorphUnmatchedPartialPolicy::MergeIntoResidual
                               && record.kind != MorphCorrespondenceKind::Matched;
    partial.discarded = partial.unmatchedPolicy == MorphUnmatchedPartialPolicy::Discard
                        && record.kind != MorphCorrespondenceKind::Matched;

    MorphPhaseInput phaseInput;
    phaseInput.kind = record.kind;
    partial.phase = resolveMorphPhase(phaseInput, phaseMorphAmount, recipe.phase);
    partial.valid = partial.frequencyInterpolation.valid && partial.amplitudeInterpolation.valid;
    return partial;
}

void validateSourceMetadata(MorphCompatibilityReport& report, const char* sourceName, const MorphSourceMetadata& source)
{
    if (source.id.empty())
        addIssue(report, MorphCompatibilitySeverity::Error, MorphCompatibilityCode::MissingSourceId, sourceName,
                 "source id is required");
    if (source.hash.empty())
        addIssue(report, MorphCompatibilitySeverity::Error, MorphCompatibilityCode::MissingSourceHash, sourceName,
                 "source hash is required");
    if (!isPositive(source.sampleRate))
        addIssue(report, MorphCompatibilitySeverity::Error, MorphCompatibilityCode::InvalidSampleRate, sourceName,
                 "source sample rate must be positive");
    if (!isPositive(source.durationSeconds))
        addIssue(report, MorphCompatibilitySeverity::Error, MorphCompatibilityCode::InvalidDuration, sourceName,
                 "source duration must be positive");
    if (!isPositive(source.frameHopSeconds))
        addIssue(report, MorphCompatibilitySeverity::Error, MorphCompatibilityCode::InvalidFrameHop, sourceName,
                 "source frame hop must be positive");
    if (source.frameCount <= 0)
        addIssue(report, MorphCompatibilitySeverity::Error, MorphCompatibilityCode::InvalidFrameCount, sourceName,
                 "source frame count must be positive");
    if (source.partialCount <= 0)
        addIssue(report, MorphCompatibilitySeverity::Error, MorphCompatibilityCode::InvalidPartialCount, sourceName,
                 "source partial count must be positive");
    if (source.channelCount <= 0)
        addIssue(report, MorphCompatibilitySeverity::Error, MorphCompatibilityCode::InvalidChannelCount, sourceName,
                 "source channel count must be positive");
    if (!hasValidFrequencyBounds(source))
        addIssue(report, MorphCompatibilitySeverity::Error, MorphCompatibilityCode::InvalidFrequencyBounds, sourceName,
                 "source frequency bounds must be positive and ordered");
    if (!source.hasPhase)
        addIssue(report, MorphCompatibilitySeverity::Warning, MorphCompatibilityCode::MissingPhase, sourceName,
                 "source phase data is missing; renderer phase continuity will be used");
    if (!source.hasResidual)
        addIssue(report, MorphCompatibilitySeverity::Warning, MorphCompatibilityCode::MissingResidual, sourceName,
                 "source residual/noise data is missing; residual lane will be silent or synthesized later");
}

} // namespace

bool MorphFrequencyGrid::operator==(const MorphFrequencyGrid& other) const
{
    return type == other.type && bandCount == other.bandCount
           && lowestFrequencyHz == other.lowestFrequencyHz
           && highestFrequencyHz == other.highestFrequencyHz
           && edgeFrequenciesHz == other.edgeFrequenciesHz;
}

bool MorphSourceMetadata::operator==(const MorphSourceMetadata& other) const
{
    return id == other.id && hash == other.hash && name == other.name && sampleRate == other.sampleRate
           && durationSeconds == other.durationSeconds && frameHopSeconds == other.frameHopSeconds
           && lowestFrequencyHz == other.lowestFrequencyHz && highestFrequencyHz == other.highestFrequencyHz
           && channelCount == other.channelCount && frameCount == other.frameCount
           && partialCount == other.partialCount && hasPhase == other.hasPhase && hasResidual == other.hasResidual;
}

bool MorphLaneSetting::operator==(const MorphLaneSetting& other) const
{
    return linkedToMainMorph == other.linkedToMainMorph && amount == other.amount && curve == other.curve;
}

bool MorphUnmatchedPartialPolicies::operator==(const MorphUnmatchedPartialPolicies& other) const
{
    return sourcePolicy == other.sourcePolicy && targetPolicy == other.targetPolicy
           && discardOptIn == other.discardOptIn;
}

bool MorphGreedyNearestOptions::operator==(const MorphGreedyNearestOptions& other) const
{
    return amplitudeRankWeight == other.amplitudeRankWeight;
}

bool MorphCorrespondenceSmoothingOptions::operator==(const MorphCorrespondenceSmoothingOptions& other) const
{
    return hysteresisCostMargin == other.hysteresisCostMargin;
}

bool MorphFrequencyInterpolationOptions::operator==(const MorphFrequencyInterpolationOptions& other) const
{
    return domain == other.domain && clampToGridBounds == other.clampToGridBounds;
}

bool MorphAmplitudeInterpolationOptions::operator==(const MorphAmplitudeInterpolationOptions& other) const
{
    return domain == other.domain && decibelFloor == other.decibelFloor;
}

bool MorphRenderGainSettings::operator==(const MorphRenderGainSettings& other) const
{
    return partialGain == other.partialGain
           && partialNoiseGain == other.partialNoiseGain
           && orphanResidualGain == other.orphanResidualGain;
}

bool MorphPhaseSettings::operator==(const MorphPhaseSettings& other) const
{
    return strategy == other.strategy;
}

bool MorphPhaseInput::operator==(const MorphPhaseInput& other) const
{
    return kind == other.kind
           && sourcePhaseRadians == other.sourcePhaseRadians
           && targetPhaseRadians == other.targetPhaseRadians
           && hasSourcePhase == other.hasSourcePhase
           && hasTargetPhase == other.hasTargetPhase;
}

bool MorphPhaseResolution::operator==(const MorphPhaseResolution& other) const
{
    return phaseRadians == other.phaseRadians
           && valid == other.valid
           && usesRendererContinuation == other.usesRendererContinuation
           && usedSourcePhase == other.usedSourcePhase
           && usedTargetPhase == other.usedTargetPhase
           && interpolatedPhase == other.interpolatedPhase
           && isBirth == other.isBirth
           && isDeath == other.isDeath;
}

bool MorphLifecycleFadeSettings::operator==(const MorphLifecycleFadeSettings& other) const
{
    return birthFadeFrames == other.birthFadeFrames
           && deathFadeFrames == other.deathFadeFrames
           && maxGainStepPerFrame == other.maxGainStepPerFrame
           && curve == other.curve;
}

bool MorphLifecycleFadeResult::operator==(const MorphLifecycleFadeResult& other) const
{
    return kind == other.kind
           && lifecycleFrameIndex == other.lifecycleFrameIndex
           && amplitudeScale == other.amplitudeScale
           && nextAmplitudeScale == other.nextAmplitudeScale
           && frameGainStep == other.frameGainStep
           && isBirth == other.isBirth
           && isDeath == other.isDeath
           && allowsPhaseReset == other.allowsPhaseReset
           && clickRisk == other.clickRisk
           && zipperRisk == other.zipperRisk;
}

bool MorphTransientMarker::operator==(const MorphTransientMarker& other) const
{
    return timeSeconds == other.timeSeconds
           && strength == other.strength
           && frameIndex == other.frameIndex;
}

bool MorphTransientSettings::operator==(const MorphTransientSettings& other) const
{
    return policy == other.policy
           && protectionWindowSeconds == other.protectionWindowSeconds
           && maxOnsetAlignmentOffsetSeconds == other.maxOnsetAlignmentOffsetSeconds;
}

bool MorphTransientAlignmentWarning::operator==(const MorphTransientAlignmentWarning& other) const
{
    return sourceMarkerIndex == other.sourceMarkerIndex
           && targetMarkerIndex == other.targetMarkerIndex
           && sourceTimeSeconds == other.sourceTimeSeconds
           && targetTimeSeconds == other.targetTimeSeconds
           && offsetSeconds == other.offsetSeconds
           && exceedsTolerance == other.exceedsTolerance;
}

bool MorphTransientAnalysisReport::operator==(const MorphTransientAnalysisReport& other) const
{
    return comparedMarkerCount == other.comparedMarkerCount
           && warningCount == other.warningCount
           && warnings == other.warnings;
}

bool MorphAssignmentMatrixOptions::operator==(const MorphAssignmentMatrixOptions& other) const
{
    return advancedStrategyEnabled == other.advancedStrategyEnabled
           && maxCandidatesPerBand == other.maxCandidatesPerBand
           && amplitudeRankWeight == other.amplitudeRankWeight
           && confidenceWeight == other.confidenceWeight
           && continuityWeight == other.continuityWeight
           && previousFrameCorrespondence == other.previousFrameCorrespondence;
}

bool MorphCorrespondenceRecord::operator==(const MorphCorrespondenceRecord& other) const
{
    return bandIndex == other.bandIndex && rank == other.rank
           && sourcePartialIndex == other.sourcePartialIndex && sourceFrameIndex == other.sourceFrameIndex
           && sourceFrequencyHz == other.sourceFrequencyHz && sourceAmplitude == other.sourceAmplitude
           && targetPartialIndex == other.targetPartialIndex && targetFrameIndex == other.targetFrameIndex
           && targetFrequencyHz == other.targetFrequencyHz && targetAmplitude == other.targetAmplitude
           && kind == other.kind && hasCostDiagnostics == other.hasCostDiagnostics
           && logFrequencyCost == other.logFrequencyCost && amplitudeRankCost == other.amplitudeRankCost
           && confidenceCost == other.confidenceCost && continuityCost == other.continuityCost
           && totalCost == other.totalCost;
}

bool MorphCorrespondenceChurnMetrics::operator==(const MorphCorrespondenceChurnMetrics& other) const
{
    return frameTransitionCount == other.frameTransitionCount
           && comparedMatchCount == other.comparedMatchCount
           && changedMatchCount == other.changedMatchCount
           && churnRate == other.churnRate;
}

bool MorphAssignmentMatrixResult::operator==(const MorphAssignmentMatrixResult& other) const
{
    return strategy == other.strategy && records == other.records
           && sourceCandidateCount == other.sourceCandidateCount
           && targetCandidateCount == other.targetCandidateCount
           && candidateEdgeCount == other.candidateEdgeCount
           && evaluatedAssignmentCount == other.evaluatedAssignmentCount
           && totalMatchedCost == other.totalMatchedCost
           && wasCandidateBounded == other.wasCandidateBounded;
}

bool MorphFrequencyInterpolationResult::operator==(const MorphFrequencyInterpolationResult& other) const
{
    return frequencyHz == other.frequencyHz
           && sourceBandIndex == other.sourceBandIndex
           && targetBandIndex == other.targetBandIndex
           && outputBandIndex == other.outputBandIndex
           && valid == other.valid
           && crossedBand == other.crossedBand
           && clampedToGridBounds == other.clampedToGridBounds;
}

bool MorphAmplitudeInterpolationResult::operator==(const MorphAmplitudeInterpolationResult& other) const
{
    return amplitude == other.amplitude
           && power == other.power
           && valid == other.valid
           && usedDecibelFloor == other.usedDecibelFloor;
}

bool MorphBandEnergySnapshot::operator==(const MorphBandEnergySnapshot& other) const
{
    return bandIndex == other.bandIndex
           && sourceSinusoidalPower == other.sourceSinusoidalPower
           && targetSinusoidalPower == other.targetSinusoidalPower
           && outputSinusoidalPower == other.outputSinusoidalPower
           && outputResidualPower == other.outputResidualPower
           && expectedOutputPower == other.expectedOutputPower
           && powerDelta == other.powerDelta
           && exceedsTolerance == other.exceedsTolerance;
}

bool MorphBandEnergyOptions::operator==(const MorphBandEnergyOptions& other) const
{
    return morphAmount == other.morphAmount
           && powerDeltaTolerance == other.powerDeltaTolerance
           && unmatchedPolicies == other.unmatchedPolicies
           && amplitudeInterpolation == other.amplitudeInterpolation;
}

bool MorphBandEnergyReport::operator==(const MorphBandEnergyReport& other) const
{
    return bands == other.bands && warningBandCount == other.warningBandCount;
}

bool MorphResidualNoiseBand::operator==(const MorphResidualNoiseBand& other) const
{
    return bandIndex == other.bandIndex
           && partialNoisePower == other.partialNoisePower
           && orphanResidualPower == other.orphanResidualPower;
}

bool MorphResidualNoiseOptions::operator==(const MorphResidualNoiseOptions& other) const
{
    return morphAmount == other.morphAmount
           && gains == other.gains
           && interpolation == other.interpolation;
}

bool MorphResidualNoiseBandResult::operator==(const MorphResidualNoiseBandResult& other) const
{
    return bandIndex == other.bandIndex
           && sourcePartialNoisePower == other.sourcePartialNoisePower
           && targetPartialNoisePower == other.targetPartialNoisePower
           && outputPartialNoisePower == other.outputPartialNoisePower
           && sourceOrphanResidualPower == other.sourceOrphanResidualPower
           && targetOrphanResidualPower == other.targetOrphanResidualPower
           && outputOrphanResidualPower == other.outputOrphanResidualPower
           && outputTotalNoisePower == other.outputTotalNoisePower;
}

bool MorphResidualNoiseReport::operator==(const MorphResidualNoiseReport& other) const
{
    return bands == other.bands
           && outputPartialGain == other.outputPartialGain
           && outputPartialNoiseGain == other.outputPartialNoiseGain
           && outputOrphanResidualGain == other.outputOrphanResidualGain;
}

bool MorphSnapshotOptions::operator==(const MorphSnapshotOptions& other) const
{
    return morphAmount == other.morphAmount
           && lifecycleFrameIndex == other.lifecycleFrameIndex
           && frequency == other.frequency
           && amplitude == other.amplitude
           && sourceResidualNoiseBands == other.sourceResidualNoiseBands
           && targetResidualNoiseBands == other.targetResidualNoiseBands;
}

bool MorphSnapshotPartial::operator==(const MorphSnapshotPartial& other) const
{
    return bandIndex == other.bandIndex && rank == other.rank
           && sourcePartialIndex == other.sourcePartialIndex && sourceFrameIndex == other.sourceFrameIndex
           && targetPartialIndex == other.targetPartialIndex && targetFrameIndex == other.targetFrameIndex
           && kind == other.kind && unmatchedPolicy == other.unmatchedPolicy
           && frequencyHz == other.frequencyHz && amplitude == other.amplitude && power == other.power
           && valid == other.valid && routedToResidual == other.routedToResidual
           && discarded == other.discarded && frequencyInterpolation == other.frequencyInterpolation
           && amplitudeInterpolation == other.amplitudeInterpolation && phase == other.phase
           && lifecycle == other.lifecycle;
}

bool MorphRenderSnapshot::operator==(const MorphRenderSnapshot& other) const
{
    return recipeSchemaVersion == other.recipeSchemaVersion
           && engineVersion == other.engineVersion
           && morphAmount == other.morphAmount
           && laneAmounts == other.laneAmounts
           && requiresOfflinePreparation == other.requiresOfflinePreparation
           && safeForAudioThreadPlayback == other.safeForAudioThreadPlayback
           && partials == other.partials
           && residualNoise == other.residualNoise
           && energy == other.energy
           && transientAlignment == other.transientAlignment;
}

bool MorphSnapshotPlaybackSettings::operator==(const MorphSnapshotPlaybackSettings& other) const
{
    return sampleRate == other.sampleRate
           && durationSeconds == other.durationSeconds
           && residualCarrierHz == other.residualCarrierHz;
}

int MorphSnapshotPlaybackBuffer::getNumSamples() const
{
    return static_cast<int>(std::max(partialSamples.size(), residualSamples.size()));
}

bool MorphSnapshotPlaybackBuffer::operator==(const MorphSnapshotPlaybackBuffer& other) const
{
    return sampleRate == other.sampleRate
           && partialSamples == other.partialSamples
           && residualSamples == other.residualSamples;
}

bool MorphSnapshotPlaybackCursor::operator==(const MorphSnapshotPlaybackCursor& other) const
{
    return sampleIndex == other.sampleIndex;
}

bool MorphPerformanceSoftCaps::operator==(const MorphPerformanceSoftCaps& other) const
{
    return maxPartialsPerFrame == other.maxPartialsPerFrame
           && maxFrameCount == other.maxFrameCount
           && maxCandidateEdgesPerBand == other.maxCandidateEdgesPerBand
           && maxEvaluatedAssignments == other.maxEvaluatedAssignments
           && maxAnalysisMemoryBytes == other.maxAnalysisMemoryBytes
           && maxSnapshotMemoryBytes == other.maxSnapshotMemoryBytes;
}

bool MorphStrategyPerformanceEstimate::operator==(const MorphStrategyPerformanceEstimate& other) const
{
    return strategy == other.strategy
           && sourceCandidateCount == other.sourceCandidateCount
           && targetCandidateCount == other.targetCandidateCount
           && outputRecordCount == other.outputRecordCount
           && candidateEdgeCount == other.candidateEdgeCount
           && evaluatedAssignmentCount == other.evaluatedAssignmentCount
           && relativeCpuCost == other.relativeCpuCost
           && exceedsCandidateEdgeSoftCap == other.exceedsCandidateEdgeSoftCap
           && exceedsAssignmentSoftCap == other.exceedsAssignmentSoftCap;
}

bool MorphPerformanceProfile::operator==(const MorphPerformanceProfile& other) const
{
    return frameCount == other.frameCount
           && sourceCandidateCount == other.sourceCandidateCount
           && targetCandidateCount == other.targetCandidateCount
           && bandCount == other.bandCount
           && estimatedAnalysisMemoryBytes == other.estimatedAnalysisMemoryBytes
           && estimatedSnapshotMemoryBytes == other.estimatedSnapshotMemoryBytes
           && exceedsPartialSoftCap == other.exceedsPartialSoftCap
           && exceedsFrameSoftCap == other.exceedsFrameSoftCap
           && exceedsAnalysisMemorySoftCap == other.exceedsAnalysisMemorySoftCap
           && exceedsSnapshotMemorySoftCap == other.exceedsSnapshotMemorySoftCap
           && warningCount == other.warningCount
           && strategies == other.strategies;
}

bool MorphUnmatchedPartialDecision::operator==(const MorphUnmatchedPartialDecision& other) const
{
    return policy == other.policy && kind == other.kind && bandIndex == other.bandIndex && rank == other.rank
           && partialIndex == other.partialIndex && frameIndex == other.frameIndex
           && frequencyHz == other.frequencyHz && amplitude == other.amplitude
           && residualPower == other.residualPower && fadesPartial == other.fadesPartial
           && holdsPartial == other.holdsPartial && routesToResidual == other.routesToResidual
           && discardsPartial == other.discardsPartial;
}

bool MorphRecipeUnknownField::operator==(const MorphRecipeUnknownField& other) const
{
    return key == other.key && value == other.value;
}

bool MorphRecipe::operator==(const MorphRecipe& other) const
{
    return schemaVersion == other.schemaVersion && engineVersion == other.engineVersion
           && sourceA == other.sourceA && sourceB == other.sourceB
           && frequencyGrid == other.frequencyGrid
           && defaultMorphAmount == other.defaultMorphAmount && lanes == other.lanes
           && outputGains == other.outputGains
           && phase == other.phase
           && lifecycle == other.lifecycle
           && transient == other.transient
           && sourceATransients == other.sourceATransients
           && sourceBTransients == other.sourceBTransients
           && correspondenceRecords == other.correspondenceRecords
           && unmatchedPartialPolicies == other.unmatchedPartialPolicies
           && unknownFields == other.unknownFields;
}

bool MorphCompatibilityReport::isCompatible() const
{
    for (const auto& issue : issues)
    {
        if (issue.severity == MorphCompatibilitySeverity::Error)
            return false;
    }
    return true;
}

const char* morphLaneName(MorphLane lane)
{
    const auto index = static_cast<size_t>(lane);
    if (index >= kLaneNames.size())
        return "unknown";
    return kLaneNames[index];
}

bool morphLaneFromName(const std::string& name, MorphLane& lane)
{
    for (size_t i = 0; i < kLaneNames.size(); ++i)
    {
        if (name == kLaneNames[i])
        {
            lane = static_cast<MorphLane>(i);
            return true;
        }
    }
    return false;
}

const char* morphCurveShapeName(MorphCurveShape shape)
{
    const auto index = static_cast<size_t>(shape);
    if (index >= kCurveShapeNames.size())
        return "unknown";
    return kCurveShapeNames[index];
}

bool morphCurveShapeFromName(const std::string& name, MorphCurveShape& shape)
{
    for (size_t i = 0; i < kCurveShapeNames.size(); ++i)
    {
        if (name == kCurveShapeNames[i])
        {
            shape = static_cast<MorphCurveShape>(i);
            return true;
        }
    }
    return false;
}

const char* morphPhaseStrategyName(MorphPhaseStrategy strategy)
{
    const auto index = static_cast<size_t>(strategy);
    if (index >= kPhaseStrategyNames.size())
        return "unknown";
    return kPhaseStrategyNames[index];
}

bool morphPhaseStrategyFromName(const std::string& name, MorphPhaseStrategy& strategy)
{
    for (size_t i = 0; i < kPhaseStrategyNames.size(); ++i)
    {
        if (name == kPhaseStrategyNames[i])
        {
            strategy = static_cast<MorphPhaseStrategy>(i);
            return true;
        }
    }
    return false;
}

const char* morphTransientPolicyName(MorphTransientPolicy policy)
{
    const auto index = static_cast<size_t>(policy);
    if (index >= kTransientPolicyNames.size())
        return "unknown";
    return kTransientPolicyNames[index];
}

bool morphTransientPolicyFromName(const std::string& name, MorphTransientPolicy& policy)
{
    for (size_t i = 0; i < kTransientPolicyNames.size(); ++i)
    {
        if (name == kTransientPolicyNames[i])
        {
            policy = static_cast<MorphTransientPolicy>(i);
            return true;
        }
    }
    return false;
}

const char* morphFrequencyGridTypeName(MorphFrequencyGridType type)
{
    switch (type)
    {
        case MorphFrequencyGridType::Bark:
            return "bark";
        case MorphFrequencyGridType::Mel:
            return "mel";
    }
    return "unknown";
}

bool morphFrequencyGridTypeFromName(const std::string& name, MorphFrequencyGridType& type)
{
    if (name == "bark")
    {
        type = MorphFrequencyGridType::Bark;
        return true;
    }
    if (name == "mel")
    {
        type = MorphFrequencyGridType::Mel;
        return true;
    }
    return false;
}

const char* morphBandAssignmentKindName(MorphBandAssignmentKind kind)
{
    switch (kind)
    {
        case MorphBandAssignmentKind::Assigned:
            return "assigned";
        case MorphBandAssignmentKind::Silent:
            return "silent";
        case MorphBandAssignmentKind::Inactive:
            return "inactive";
        case MorphBandAssignmentKind::OutOfRangeLow:
            return "out_of_range_low";
        case MorphBandAssignmentKind::OutOfRangeHigh:
            return "out_of_range_high";
        case MorphBandAssignmentKind::InvalidGrid:
            return "invalid_grid";
    }
    return "unknown";
}

const char* morphCorrespondenceKindName(MorphCorrespondenceKind kind)
{
    switch (kind)
    {
        case MorphCorrespondenceKind::Matched:
            return "matched";
        case MorphCorrespondenceKind::UnmatchedSource:
            return "unmatched_source";
        case MorphCorrespondenceKind::UnmatchedTarget:
            return "unmatched_target";
    }
    return "unknown";
}

bool morphCorrespondenceKindFromName(const std::string& name, MorphCorrespondenceKind& kind)
{
    if (name == "matched")
    {
        kind = MorphCorrespondenceKind::Matched;
        return true;
    }
    if (name == "unmatched_source")
    {
        kind = MorphCorrespondenceKind::UnmatchedSource;
        return true;
    }
    if (name == "unmatched_target")
    {
        kind = MorphCorrespondenceKind::UnmatchedTarget;
        return true;
    }
    return false;
}

const char* morphUnmatchedPartialPolicyName(MorphUnmatchedPartialPolicy policy)
{
    switch (policy)
    {
        case MorphUnmatchedPartialPolicy::FadeOut:
            return "fade_out";
        case MorphUnmatchedPartialPolicy::FadeIn:
            return "fade_in";
        case MorphUnmatchedPartialPolicy::Hold:
            return "hold";
        case MorphUnmatchedPartialPolicy::MergeIntoResidual:
            return "merge_into_residual";
        case MorphUnmatchedPartialPolicy::Discard:
            return "discard";
    }
    return "unknown";
}

bool morphUnmatchedPartialPolicyFromName(const std::string& name, MorphUnmatchedPartialPolicy& policy)
{
    if (name == "fade_out")
    {
        policy = MorphUnmatchedPartialPolicy::FadeOut;
        return true;
    }
    if (name == "fade_in")
    {
        policy = MorphUnmatchedPartialPolicy::FadeIn;
        return true;
    }
    if (name == "hold")
    {
        policy = MorphUnmatchedPartialPolicy::Hold;
        return true;
    }
    if (name == "merge_into_residual")
    {
        policy = MorphUnmatchedPartialPolicy::MergeIntoResidual;
        return true;
    }
    if (name == "discard")
    {
        policy = MorphUnmatchedPartialPolicy::Discard;
        return true;
    }
    return false;
}

const char* morphCompatibilityCodeName(MorphCompatibilityCode code)
{
    switch (code)
    {
        case MorphCompatibilityCode::MissingSourceId:
            return "missing_source_id";
        case MorphCompatibilityCode::MissingSourceHash:
            return "missing_source_hash";
        case MorphCompatibilityCode::InvalidSampleRate:
            return "invalid_sample_rate";
        case MorphCompatibilityCode::SampleRateMismatch:
            return "sample_rate_mismatch";
        case MorphCompatibilityCode::InvalidDuration:
            return "invalid_duration";
        case MorphCompatibilityCode::InvalidFrameCount:
            return "invalid_frame_count";
        case MorphCompatibilityCode::InvalidPartialCount:
            return "invalid_partial_count";
        case MorphCompatibilityCode::InvalidFrameHop:
            return "invalid_frame_hop";
        case MorphCompatibilityCode::IncompatibleFrameGrid:
            return "incompatible_frame_grid";
        case MorphCompatibilityCode::InvalidChannelCount:
            return "invalid_channel_count";
        case MorphCompatibilityCode::ChannelCountMismatch:
            return "channel_count_mismatch";
        case MorphCompatibilityCode::InvalidFrequencyBounds:
            return "invalid_frequency_bounds";
        case MorphCompatibilityCode::NonOverlappingFrequencyBounds:
            return "non_overlapping_frequency_bounds";
        case MorphCompatibilityCode::MissingPhase:
            return "missing_phase";
        case MorphCompatibilityCode::MissingResidual:
            return "missing_residual";
    }
    return "unknown";
}

double applyMorphCurve(double amount, MorphCurveShape shape)
{
    const double clamped = clampUnitInterval(amount);
    switch (shape)
    {
        case MorphCurveShape::Linear:
            return clamped;
        case MorphCurveShape::EaseIn:
            return clamped * clamped;
        case MorphCurveShape::EaseOut:
        {
            const double inverse = 1.0 - clamped;
            return 1.0 - inverse * inverse;
        }
        case MorphCurveShape::EaseInOut:
            return clamped * clamped * (3.0 - 2.0 * clamped);
    }
    return clamped;
}

double evaluateMorphLaneAmount(const MorphRecipe& recipe, MorphLane lane, double mainMorphAmount)
{
    const auto index = static_cast<size_t>(lane);
    if (index >= recipe.lanes.size())
        return applyMorphCurve(mainMorphAmount, MorphCurveShape::Linear);

    const auto& setting = recipe.lanes[index];
    const double baseAmount = setting.linkedToMainMorph ? mainMorphAmount : setting.amount;
    return applyMorphCurve(baseAmount, setting.curve);
}

MorphLaneAmounts evaluateMorphLaneAmounts(const MorphRecipe& recipe, double mainMorphAmount)
{
    MorphLaneAmounts amounts{};
    for (size_t i = 0; i < amounts.size(); ++i)
        amounts[i] = evaluateMorphLaneAmount(recipe, static_cast<MorphLane>(i), mainMorphAmount);
    return amounts;
}

MorphPhaseResolution resolveMorphPhase(const MorphPhaseInput& input,
                                       double morphAmount,
                                       const MorphPhaseSettings& settings)
{
    MorphPhaseResolution resolution;
    resolution.valid = true;
    resolution.isBirth = input.kind == MorphCorrespondenceKind::UnmatchedTarget;
    resolution.isDeath = input.kind == MorphCorrespondenceKind::UnmatchedSource;

    const auto useSource = [&]() {
        if (!input.hasSourcePhase)
            return false;
        resolution.phaseRadians = wrapPhaseRadians(input.sourcePhaseRadians);
        resolution.usedSourcePhase = true;
        return true;
    };
    const auto useTarget = [&]() {
        if (!input.hasTargetPhase)
            return false;
        resolution.phaseRadians = wrapPhaseRadians(input.targetPhaseRadians);
        resolution.usedTargetPhase = true;
        return true;
    };
    const auto useRenderer = [&]() {
        resolution.phaseRadians = 0.0;
        resolution.usesRendererContinuation = true;
    };

    switch (settings.strategy)
    {
        case MorphPhaseStrategy::OscillatorContinuation:
            useRenderer();
            break;
        case MorphPhaseStrategy::SourceADonor:
            if (!useSource())
                useRenderer();
            break;
        case MorphPhaseStrategy::SourceBDonor:
            if (!useTarget())
                useRenderer();
            break;
        case MorphPhaseStrategy::RelativeClusterPreservation:
            if (!(useSource() || useTarget()))
                useRenderer();
            break;
        case MorphPhaseStrategy::DirectBlend:
            if (input.hasSourcePhase && input.hasTargetPhase)
            {
                const double source = wrapPhaseRadians(input.sourcePhaseRadians);
                const double target = wrapPhaseRadians(input.targetPhaseRadians);
                const double delta = wrapPhaseRadians(target - source);
                resolution.phaseRadians = wrapPhaseRadians(source + clampUnitInterval(morphAmount) * delta);
                resolution.usedSourcePhase = true;
                resolution.usedTargetPhase = true;
                resolution.interpolatedPhase = true;
            }
            else if (!(useSource() || useTarget()))
            {
                useRenderer();
            }
            break;
    }

    return resolution;
}

MorphLifecycleFadeResult evaluateMorphLifecycleFade(MorphCorrespondenceKind kind,
                                                    int lifecycleFrameIndex,
                                                    const MorphLifecycleFadeSettings& settings)
{
    MorphLifecycleFadeResult result;
    result.kind = kind;
    result.lifecycleFrameIndex = clampLifecycleFrameIndex(lifecycleFrameIndex);
    result.isBirth = kind == MorphCorrespondenceKind::UnmatchedTarget;
    result.isDeath = kind == MorphCorrespondenceKind::UnmatchedSource;

    if (!result.isBirth && !result.isDeath)
        return result;

    const int fadeFrames = result.isBirth ? settings.birthFadeFrames : settings.deathFadeFrames;
    const int sanitizedFadeFrames = std::max(0, fadeFrames);
    const auto progressAtFrame = [&](int frameIndex) {
        if (sanitizedFadeFrames <= 0)
            return 1.0;
        return applyMorphCurve(static_cast<double>(std::min(frameIndex, sanitizedFadeFrames))
                                   / static_cast<double>(sanitizedFadeFrames),
                               settings.curve);
    };

    const double progress = progressAtFrame(result.lifecycleFrameIndex);
    const double nextProgress = progressAtFrame(result.lifecycleFrameIndex + 1);
    if (result.isBirth)
    {
        result.amplitudeScale = progress;
        result.nextAmplitudeScale = nextProgress;
    }
    else
    {
        result.amplitudeScale = 1.0 - progress;
        result.nextAmplitudeScale = 1.0 - nextProgress;
    }
    result.frameGainStep = std::abs(result.nextAmplitudeScale - result.amplitudeScale);
    if (sanitizedFadeFrames <= 0 && result.lifecycleFrameIndex == 0)
    {
        result.frameGainStep = 1.0;
        result.clickRisk = true;
    }

    const double maxGainStep = std::isfinite(settings.maxGainStepPerFrame)
                                   ? std::max(0.0, settings.maxGainStepPerFrame)
                                   : 0.5;
    result.zipperRisk = result.frameGainStep > maxGainStep;
    result.allowsPhaseReset = result.lifecycleFrameIndex == 0;
    return result;
}

MorphTransientAnalysisReport analyzeMorphTransientAlignment(
    const std::vector<MorphTransientMarker>& sourceMarkers,
    const std::vector<MorphTransientMarker>& targetMarkers,
    const MorphTransientSettings& settings)
{
    MorphTransientAnalysisReport report;
    const auto source = makeIndexedTransientMarkers(sourceMarkers);
    const auto target = makeIndexedTransientMarkers(targetMarkers);
    report.comparedMarkerCount = static_cast<int>(std::min(source.size(), target.size()));

    const double tolerance = std::isfinite(settings.maxOnsetAlignmentOffsetSeconds)
                                 ? std::max(0.0, settings.maxOnsetAlignmentOffsetSeconds)
                                 : MorphTransientSettings{}.maxOnsetAlignmentOffsetSeconds;
    for (int i = 0; i < report.comparedMarkerCount; ++i)
    {
        const auto& sourceMarker = source[static_cast<size_t>(i)];
        const auto& targetMarker = target[static_cast<size_t>(i)];
        const double offset = targetMarker.marker.timeSeconds - sourceMarker.marker.timeSeconds;
        if (std::abs(offset) <= tolerance)
            continue;

        MorphTransientAlignmentWarning warning;
        warning.sourceMarkerIndex = sourceMarker.originalIndex;
        warning.targetMarkerIndex = targetMarker.originalIndex;
        warning.sourceTimeSeconds = sourceMarker.marker.timeSeconds;
        warning.targetTimeSeconds = targetMarker.marker.timeSeconds;
        warning.offsetSeconds = offset;
        warning.exceedsTolerance = true;
        report.warnings.push_back(warning);
    }

    report.warningCount = static_cast<int>(report.warnings.size());
    return report;
}

MorphFrequencyGrid makeBarkFrequencyGrid(int bandCount, double lowestFrequencyHz, double highestFrequencyHz)
{
    MorphFrequencyGrid grid;
    grid.type = MorphFrequencyGridType::Bark;
    grid.bandCount = bandCount;
    grid.lowestFrequencyHz = lowestFrequencyHz;
    grid.highestFrequencyHz = highestFrequencyHz;
    grid.edgeFrequenciesHz.clear();

    if (bandCount <= 0 || !std::isfinite(lowestFrequencyHz) || !std::isfinite(highestFrequencyHz)
        || lowestFrequencyHz < 0.0 || highestFrequencyHz <= lowestFrequencyHz)
    {
        return grid;
    }

    grid.edgeFrequenciesHz.reserve(static_cast<size_t>(bandCount + 1));
    const double lowBark = hzToBark(lowestFrequencyHz);
    const double highBark = hzToBark(highestFrequencyHz);
    for (int i = 0; i <= bandCount; ++i)
    {
        if (i == 0)
        {
            grid.edgeFrequenciesHz.push_back(lowestFrequencyHz);
            continue;
        }
        if (i == bandCount)
        {
            grid.edgeFrequenciesHz.push_back(highestFrequencyHz);
            continue;
        }

        const double t = static_cast<double>(i) / static_cast<double>(bandCount);
        const double bark = lowBark + t * (highBark - lowBark);
        grid.edgeFrequenciesHz.push_back(barkToHzInRange(bark, lowestFrequencyHz, highestFrequencyHz));
    }
    return grid;
}

MorphFrequencyGrid makeMelFrequencyGrid(int bandCount, double lowestFrequencyHz, double highestFrequencyHz)
{
    MorphFrequencyGrid grid;
    grid.type = MorphFrequencyGridType::Mel;
    grid.bandCount = bandCount;
    grid.lowestFrequencyHz = lowestFrequencyHz;
    grid.highestFrequencyHz = highestFrequencyHz;
    grid.edgeFrequenciesHz.clear();

    if (bandCount <= 0 || !std::isfinite(lowestFrequencyHz) || !std::isfinite(highestFrequencyHz)
        || lowestFrequencyHz < 0.0 || highestFrequencyHz <= lowestFrequencyHz)
    {
        return grid;
    }

    grid.edgeFrequenciesHz.reserve(static_cast<size_t>(bandCount + 1));
    const double lowMel = hzToMel(lowestFrequencyHz);
    const double highMel = hzToMel(highestFrequencyHz);
    for (int i = 0; i <= bandCount; ++i)
    {
        if (i == 0)
        {
            grid.edgeFrequenciesHz.push_back(lowestFrequencyHz);
            continue;
        }
        if (i == bandCount)
        {
            grid.edgeFrequenciesHz.push_back(highestFrequencyHz);
            continue;
        }

        const double t = static_cast<double>(i) / static_cast<double>(bandCount);
        const double mel = lowMel + t * (highMel - lowMel);
        grid.edgeFrequenciesHz.push_back(melToHz(mel));
    }
    return grid;
}

int findFrequencyBandIndex(const MorphFrequencyGrid& grid, double frequencyHz)
{
    if (!isValidFrequencyGrid(grid) || !std::isfinite(frequencyHz))
        return -1;

    if (frequencyHz < grid.edgeFrequenciesHz.front() || frequencyHz > grid.edgeFrequenciesHz.back())
        return -1;
    if (frequencyHz == grid.edgeFrequenciesHz.back())
        return grid.bandCount - 1;

    const auto upper = std::upper_bound(grid.edgeFrequenciesHz.begin(), grid.edgeFrequenciesHz.end(), frequencyHz);
    if (upper == grid.edgeFrequenciesHz.begin() || upper == grid.edgeFrequenciesHz.end())
        return -1;
    return static_cast<int>(std::distance(grid.edgeFrequenciesHz.begin(), upper)) - 1;
}

MorphBandAssignment assignPartialFrameToFrequencyBand(const MorphFrequencyGrid& grid,
                                                      const MorphPartialFrame& frame,
                                                      double silentAmplitudeThreshold)
{
    MorphBandAssignment assignment;
    assignment.partialIndex = frame.partialIndex;
    assignment.frameIndex = frame.frameIndex;
    assignment.frequencyHz = frame.frequencyHz;
    assignment.amplitude = frame.amplitude;

    if (!isValidFrequencyGrid(grid))
    {
        assignment.kind = MorphBandAssignmentKind::InvalidGrid;
        return assignment;
    }

    if (!frame.active || !std::isfinite(frame.frequencyHz) || !std::isfinite(frame.amplitude))
    {
        assignment.kind = MorphBandAssignmentKind::Inactive;
        return assignment;
    }

    const double silenceFloor = std::max(0.0, silentAmplitudeThreshold);
    if (frame.amplitude <= silenceFloor)
    {
        assignment.kind = MorphBandAssignmentKind::Silent;
        return assignment;
    }

    if (frame.frequencyHz < grid.edgeFrequenciesHz.front())
    {
        assignment.kind = MorphBandAssignmentKind::OutOfRangeLow;
        return assignment;
    }
    if (frame.frequencyHz > grid.edgeFrequenciesHz.back())
    {
        assignment.kind = MorphBandAssignmentKind::OutOfRangeHigh;
        return assignment;
    }

    assignment.bandIndex = findFrequencyBandIndex(grid, frame.frequencyHz);
    assignment.kind = assignment.bandIndex >= 0 ? MorphBandAssignmentKind::Assigned
                                                : MorphBandAssignmentKind::InvalidGrid;
    return assignment;
}

std::vector<MorphBandAssignment> assignPartialFramesToFrequencyBands(const MorphFrequencyGrid& grid,
                                                                     const std::vector<MorphPartialFrame>& frames,
                                                                     double silentAmplitudeThreshold)
{
    std::vector<MorphBandAssignment> assignments;
    assignments.reserve(frames.size());
    for (const auto& frame : frames)
        assignments.push_back(assignPartialFrameToFrequencyBand(grid, frame, silentAmplitudeThreshold));
    return assignments;
}

std::vector<MorphTrackCentroidAssignment> assignPartialTracksToCentroidBands(
    const MorphFrequencyGrid& grid,
    const std::vector<MorphPartialTrack>& tracks,
    double silentAmplitudeThreshold)
{
    std::vector<MorphTrackCentroidAssignment> assignments;
    assignments.reserve(tracks.size());

    for (const auto& track : tracks)
    {
        MorphTrackCentroidAssignment centroid;
        centroid.partialIndex = track.partialIndex;

        bool sawSilent = false;
        bool sawInactive = false;
        bool sawLow = false;
        bool sawHigh = false;
        bool sawInvalidGrid = false;
        double weightedFrequency = 0.0;

        for (auto frame : track.frames)
        {
            frame.partialIndex = track.partialIndex;
            const auto frameAssignment = assignPartialFrameToFrequencyBand(grid, frame, silentAmplitudeThreshold);
            switch (frameAssignment.kind)
            {
                case MorphBandAssignmentKind::Assigned:
                    weightedFrequency += frameAssignment.frequencyHz * frameAssignment.amplitude;
                    centroid.totalAmplitude += frameAssignment.amplitude;
                    ++centroid.contributingFrameCount;
                    break;
                case MorphBandAssignmentKind::Silent:
                    sawSilent = true;
                    break;
                case MorphBandAssignmentKind::Inactive:
                    sawInactive = true;
                    break;
                case MorphBandAssignmentKind::OutOfRangeLow:
                    sawLow = true;
                    break;
                case MorphBandAssignmentKind::OutOfRangeHigh:
                    sawHigh = true;
                    break;
                case MorphBandAssignmentKind::InvalidGrid:
                    sawInvalidGrid = true;
                    break;
            }
        }

        if (centroid.contributingFrameCount > 0 && centroid.totalAmplitude > 0.0)
        {
            centroid.centroidFrequencyHz = weightedFrequency / centroid.totalAmplitude;
            centroid.bandIndex = findFrequencyBandIndex(grid, centroid.centroidFrequencyHz);
            centroid.kind = centroid.bandIndex >= 0 ? MorphBandAssignmentKind::Assigned
                                                    : MorphBandAssignmentKind::InvalidGrid;
        }
        else if (sawInvalidGrid)
        {
            centroid.kind = MorphBandAssignmentKind::InvalidGrid;
        }
        else if (sawLow)
        {
            centroid.kind = MorphBandAssignmentKind::OutOfRangeLow;
        }
        else if (sawHigh)
        {
            centroid.kind = MorphBandAssignmentKind::OutOfRangeHigh;
        }
        else if (sawSilent)
        {
            centroid.kind = MorphBandAssignmentKind::Silent;
        }
        else if (sawInactive || track.frames.empty())
        {
            centroid.kind = MorphBandAssignmentKind::Inactive;
        }

        assignments.push_back(centroid);
    }

    return assignments;
}

std::vector<MorphCorrespondenceRecord> buildFrequencyRankCorrespondence(
    const std::vector<MorphBandAssignment>& sourceAssignments,
    const std::vector<MorphBandAssignment>& targetAssignments,
    double amplitudeRankWeight)
{
    const double ampWeight = std::isfinite(amplitudeRankWeight)
                                 ? std::clamp(amplitudeRankWeight, 0.0, 1.0)
                                 : 0.0;
    // Reorder one band's candidate slice [begin, begin+count) by a blend of its existing
    // frequency rank (index within the frequency-sorted slice) and its amplitude rank (position
    // by descending amplitude). Returns absolute indices into `cands`. Identity when ampWeight==0,
    // so the no-weight path is bit-for-bit the original frequency-rank pairing.
    const auto bandOrder = [ampWeight](const std::vector<MorphBandAssignment>& cands,
                                       size_t begin, size_t count)
    {
        std::vector<size_t> order(count);
        for (size_t i = 0; i < count; ++i)
            order[i] = begin + i;
        if (ampWeight <= 0.0 || count < 2)
            return order;

        std::vector<size_t> byAmp(order);
        std::stable_sort(byAmp.begin(), byAmp.end(), [&cands](size_t a, size_t b)
        {
            return cands[a].amplitude > cands[b].amplitude;
        });

        std::vector<double> key(count);
        for (size_t ampRank = 0; ampRank < count; ++ampRank)
        {
            const size_t freqRank = byAmp[ampRank] - begin;
            key[freqRank] = (1.0 - ampWeight) * static_cast<double>(freqRank)
                            + ampWeight * static_cast<double>(ampRank);
        }
        std::stable_sort(order.begin(), order.end(), [&key, begin](size_t a, size_t b)
        {
            return key[a - begin] < key[b - begin];
        });
        return order;
    };

    std::vector<MorphBandAssignment> sourceCandidates;
    std::vector<MorphBandAssignment> targetCandidates;
    sourceCandidates.reserve(sourceAssignments.size());
    targetCandidates.reserve(targetAssignments.size());

    for (const auto& assignment : sourceAssignments)
    {
        if (isCorrespondenceCandidate(assignment))
            sourceCandidates.push_back(assignment);
    }
    for (const auto& assignment : targetAssignments)
    {
        if (isCorrespondenceCandidate(assignment))
            targetCandidates.push_back(assignment);
    }

    std::sort(sourceCandidates.begin(), sourceCandidates.end(), assignmentSortLess);
    std::sort(targetCandidates.begin(), targetCandidates.end(), assignmentSortLess);

    std::vector<MorphCorrespondenceRecord> records;
    size_t sourceIndex = 0;
    size_t targetIndex = 0;
    while (sourceIndex < sourceCandidates.size() || targetIndex < targetCandidates.size())
    {
        int bandIndex = 0;
        if (targetIndex >= targetCandidates.size())
            bandIndex = sourceCandidates[sourceIndex].bandIndex;
        else if (sourceIndex >= sourceCandidates.size())
            bandIndex = targetCandidates[targetIndex].bandIndex;
        else
            bandIndex = std::min(sourceCandidates[sourceIndex].bandIndex, targetCandidates[targetIndex].bandIndex);

        const size_t sourceBandBegin = sourceIndex;
        while (sourceIndex < sourceCandidates.size() && sourceCandidates[sourceIndex].bandIndex == bandIndex)
            ++sourceIndex;
        const size_t targetBandBegin = targetIndex;
        while (targetIndex < targetCandidates.size() && targetCandidates[targetIndex].bandIndex == bandIndex)
            ++targetIndex;

        const size_t sourceBandCount = sourceIndex - sourceBandBegin;
        const size_t targetBandCount = targetIndex - targetBandBegin;
        const size_t matchedCount = std::min(sourceBandCount, targetBandCount);
        records.reserve(records.size() + std::max(sourceBandCount, targetBandCount));

        const auto sourceOrder = bandOrder(sourceCandidates, sourceBandBegin, sourceBandCount);
        const auto targetOrder = bandOrder(targetCandidates, targetBandBegin, targetBandCount);

        for (size_t rank = 0; rank < matchedCount; ++rank)
        {
            MorphCorrespondenceRecord record;
            record.bandIndex = bandIndex;
            record.rank = static_cast<int>(rank);
            record.kind = MorphCorrespondenceKind::Matched;
            copySourceAssignment(record, sourceCandidates[sourceOrder[rank]]);
            copyTargetAssignment(record, targetCandidates[targetOrder[rank]]);
            records.push_back(record);
        }

        for (size_t rank = matchedCount; rank < sourceBandCount; ++rank)
        {
            MorphCorrespondenceRecord record;
            record.bandIndex = bandIndex;
            record.rank = static_cast<int>(rank);
            record.kind = MorphCorrespondenceKind::UnmatchedSource;
            copySourceAssignment(record, sourceCandidates[sourceOrder[rank]]);
            records.push_back(record);
        }

        for (size_t rank = matchedCount; rank < targetBandCount; ++rank)
        {
            MorphCorrespondenceRecord record;
            record.bandIndex = bandIndex;
            record.rank = static_cast<int>(rank);
            record.kind = MorphCorrespondenceKind::UnmatchedTarget;
            copyTargetAssignment(record, targetCandidates[targetOrder[rank]]);
            records.push_back(record);
        }
    }

    return records;
}

std::vector<int> reassignCorrespondenceByLogFrequency(
    const std::vector<double>& sourceLogFrequencies,
    const std::vector<double>& targetLogFrequencies,
    const std::vector<int>& previousAssignment,
    double hysteresisMargin)
{
    const size_t k = sourceLogFrequencies.size();

    // Identity fallback used whenever the inputs are not a clean k==k pairing.
    auto identity = [k]() {
        std::vector<int> id(k);
        for (size_t i = 0; i < k; ++i)
            id[i] = static_cast<int>(i);
        return id;
    };

    if (k == 0 || targetLogFrequencies.size() != k)
        return previousAssignment.size() == k ? previousAssignment : identity();

    const double margin = (std::isfinite(hysteresisMargin) && hysteresisMargin > 0.0)
                              ? hysteresisMargin
                              : 0.0;

    // Build every (source i, target j) pair with a cost = log-frequency distance, discounted by the
    // hysteresis margin when (i, j) is the pairing from the previous frame so an established match
    // is only broken by a clearly closer alternative.
    struct Pair { double cost; int i; int j; };
    std::vector<Pair> pairs;
    pairs.reserve(k * k);
    for (size_t i = 0; i < k; ++i)
    {
        const double sf = sourceLogFrequencies[i];
        const int prevJ = (previousAssignment.size() == k) ? previousAssignment[i] : static_cast<int>(i);
        for (size_t j = 0; j < k; ++j)
        {
            const double tf = targetLogFrequencies[j];
            double cost = (std::isfinite(sf) && std::isfinite(tf)) ? std::abs(sf - tf)
                                                                   : std::numeric_limits<double>::max();
            if (prevJ == static_cast<int>(j) && cost != std::numeric_limits<double>::max())
                cost -= margin; // stickiness: prefer keeping the prior partner
            pairs.push_back({ cost, static_cast<int>(i), static_cast<int>(j) });
        }
    }

    std::stable_sort(pairs.begin(), pairs.end(), [](const Pair& a, const Pair& b) {
        return a.cost < b.cost;
    });

    std::vector<int> assignment(k, -1);
    std::vector<char> sourceUsed(k, 0);
    std::vector<char> targetUsed(k, 0);
    size_t assigned = 0;
    for (const auto& p : pairs)
    {
        if (assigned == k)
            break;
        if (sourceUsed[static_cast<size_t>(p.i)] || targetUsed[static_cast<size_t>(p.j)])
            continue;
        assignment[static_cast<size_t>(p.i)] = p.j;
        sourceUsed[static_cast<size_t>(p.i)] = 1;
        targetUsed[static_cast<size_t>(p.j)] = 1;
        ++assigned;
    }
    // Greedy on a complete bipartite graph always fills every slot, but guard anyway.
    for (size_t i = 0; i < k; ++i)
        if (assignment[i] < 0)
            assignment[i] = static_cast<int>(i);

    return assignment;
}

std::vector<MorphCorrespondenceRecord> buildGreedyNearestCorrespondence(
    const std::vector<MorphBandAssignment>& sourceAssignments,
    const std::vector<MorphBandAssignment>& targetAssignments,
    const MorphGreedyNearestOptions& options)
{
    const auto sourceCandidates = makeGreedyCandidates(sourceAssignments);
    const auto targetCandidates = makeGreedyCandidates(targetAssignments);

    std::vector<MorphCorrespondenceRecord> records;
    size_t sourceIndex = 0;
    size_t targetIndex = 0;
    while (sourceIndex < sourceCandidates.size() || targetIndex < targetCandidates.size())
    {
        int bandIndex = 0;
        if (targetIndex >= targetCandidates.size())
            bandIndex = sourceCandidates[sourceIndex].assignment.bandIndex;
        else if (sourceIndex >= sourceCandidates.size())
            bandIndex = targetCandidates[targetIndex].assignment.bandIndex;
        else
            bandIndex = std::min(sourceCandidates[sourceIndex].assignment.bandIndex,
                                 targetCandidates[targetIndex].assignment.bandIndex);

        const size_t sourceBandBegin = sourceIndex;
        while (sourceIndex < sourceCandidates.size()
               && sourceCandidates[sourceIndex].assignment.bandIndex == bandIndex)
        {
            ++sourceIndex;
        }
        const size_t targetBandBegin = targetIndex;
        while (targetIndex < targetCandidates.size()
               && targetCandidates[targetIndex].assignment.bandIndex == bandIndex)
        {
            ++targetIndex;
        }

        const size_t sourceBandCount = sourceIndex - sourceBandBegin;
        const size_t targetBandCount = targetIndex - targetBandBegin;
        std::vector<bool> sourceUsed(sourceBandCount, false);
        std::vector<bool> targetUsed(targetBandCount, false);
        int rank = 0;
        records.reserve(records.size() + std::max(sourceBandCount, targetBandCount));

        const size_t matchCount = std::min(sourceBandCount, targetBandCount);
        for (size_t matchIndex = 0; matchIndex < matchCount; ++matchIndex)
        {
            bool foundPair = false;
            size_t bestSourceOffset = 0;
            size_t bestTargetOffset = 0;
            double bestTotalCost = 0.0;
            double bestLogCost = 0.0;

            for (size_t sourceOffset = 0; sourceOffset < sourceBandCount; ++sourceOffset)
            {
                if (sourceUsed[sourceOffset])
                    continue;

                const auto& source = sourceCandidates[sourceBandBegin + sourceOffset];
                for (size_t targetOffset = 0; targetOffset < targetBandCount; ++targetOffset)
                {
                    if (targetUsed[targetOffset])
                        continue;

                    const auto& target = targetCandidates[targetBandBegin + targetOffset];
                    const double candidateLogCost =
                        logFrequencyCost(source.assignment.frequencyHz, target.assignment.frequencyHz);
                    const double candidateAmplitudeRankCost =
                        amplitudeRankCost(source.amplitudeRank, target.amplitudeRank, options.amplitudeRankWeight);
                    const double candidateTotalCost = candidateLogCost + candidateAmplitudeRankCost;
                    if (!foundPair
                        || greedyCostIsBetter(candidateTotalCost,
                                              candidateLogCost,
                                              target.assignment,
                                              bestTotalCost,
                                              bestLogCost,
                                              targetCandidates[targetBandBegin + bestTargetOffset].assignment)
                        || (candidateTotalCost == bestTotalCost && candidateLogCost == bestLogCost
                            && !assignmentSortLess(target.assignment,
                                                   targetCandidates[targetBandBegin + bestTargetOffset].assignment)
                            && assignmentSortLess(source.assignment,
                                                  sourceCandidates[sourceBandBegin + bestSourceOffset].assignment)))
                    {
                        foundPair = true;
                        bestSourceOffset = sourceOffset;
                        bestTargetOffset = targetOffset;
                        bestTotalCost = candidateTotalCost;
                        bestLogCost = candidateLogCost;
                    }
                }
            }

            if (!foundPair)
                break;

            MorphCorrespondenceRecord record;
            record.bandIndex = bandIndex;
            record.rank = rank++;
            record.kind = MorphCorrespondenceKind::Matched;
            sourceUsed[bestSourceOffset] = true;
            targetUsed[bestTargetOffset] = true;
            const auto& source = sourceCandidates[sourceBandBegin + bestSourceOffset];
            const auto& target = targetCandidates[targetBandBegin + bestTargetOffset];
            copySourceAssignment(record, source.assignment);
            copyTargetAssignment(record, target.assignment);
            addGreedyCostDiagnostics(record, source, target, options.amplitudeRankWeight);
            records.push_back(record);
        }

        for (size_t sourceOffset = 0; sourceOffset < sourceBandCount; ++sourceOffset)
        {
            if (sourceUsed[sourceOffset])
                continue;

            MorphCorrespondenceRecord record;
            record.bandIndex = bandIndex;
            record.rank = rank++;
            record.kind = MorphCorrespondenceKind::UnmatchedSource;
            copySourceAssignment(record, sourceCandidates[sourceBandBegin + sourceOffset].assignment);
            records.push_back(record);
        }

        for (size_t targetOffset = 0; targetOffset < targetBandCount; ++targetOffset)
        {
            if (targetUsed[targetOffset])
                continue;

            MorphCorrespondenceRecord record;
            record.bandIndex = bandIndex;
            record.rank = rank++;
            record.kind = MorphCorrespondenceKind::UnmatchedTarget;
            copyTargetAssignment(record, targetCandidates[targetBandBegin + targetOffset].assignment);
            records.push_back(record);
        }
    }

    return records;
}

MorphAssignmentMatrixResult buildAssignmentMatrixCorrespondence(
    const std::vector<MorphBandAssignment>& sourceAssignments,
    const std::vector<MorphBandAssignment>& targetAssignments,
    const MorphAssignmentMatrixOptions& options)
{
    MorphAssignmentMatrixResult result;
    const auto allSourceCandidates = makeGreedyCandidates(sourceAssignments);
    const auto allTargetCandidates = makeGreedyCandidates(targetAssignments);
    result.sourceCandidateCount = static_cast<int>(allSourceCandidates.size());
    result.targetCandidateCount = static_cast<int>(allTargetCandidates.size());

    if (!options.advancedStrategyEnabled)
    {
        MorphGreedyNearestOptions greedyOptions;
        greedyOptions.amplitudeRankWeight = options.amplitudeRankWeight;
        result.strategy = MorphCorrespondenceStrategy::GreedyNearest;
        result.records = buildGreedyNearestCorrespondence(sourceAssignments, targetAssignments, greedyOptions);
        result.totalMatchedCost = matchedCostSum(result.records);
        return result;
    }

    result.strategy = MorphCorrespondenceStrategy::AssignmentMatrix;
    const auto previousTargetBySource = makePreviousTargetBySource(options.previousFrameCorrespondence);
    size_t sourceIndex = 0;
    size_t targetIndex = 0;
    while (sourceIndex < allSourceCandidates.size() || targetIndex < allTargetCandidates.size())
    {
        int bandIndex = 0;
        if (targetIndex >= allTargetCandidates.size())
            bandIndex = allSourceCandidates[sourceIndex].assignment.bandIndex;
        else if (sourceIndex >= allSourceCandidates.size())
            bandIndex = allTargetCandidates[targetIndex].assignment.bandIndex;
        else
            bandIndex = std::min(allSourceCandidates[sourceIndex].assignment.bandIndex,
                                 allTargetCandidates[targetIndex].assignment.bandIndex);

        const size_t sourceBandBegin = sourceIndex;
        while (sourceIndex < allSourceCandidates.size()
               && allSourceCandidates[sourceIndex].assignment.bandIndex == bandIndex)
        {
            ++sourceIndex;
        }
        const size_t targetBandBegin = targetIndex;
        while (targetIndex < allTargetCandidates.size()
               && allTargetCandidates[targetIndex].assignment.bandIndex == bandIndex)
        {
            ++targetIndex;
        }

        const std::vector<GreedyCandidate> sourceBand(allSourceCandidates.begin() + sourceBandBegin,
                                                      allSourceCandidates.begin() + sourceIndex);
        const std::vector<GreedyCandidate> targetBand(allTargetCandidates.begin() + targetBandBegin,
                                                      allTargetCandidates.begin() + targetIndex);
        const auto matrixSources = boundCandidatesForAssignmentMatrix(sourceBand, options.maxCandidatesPerBand);
        const auto matrixTargets = boundCandidatesForAssignmentMatrix(targetBand, options.maxCandidatesPerBand);
        result.wasCandidateBounded = result.wasCandidateBounded
                                     || matrixSources.size() != sourceBand.size()
                                     || matrixTargets.size() != targetBand.size();

        const size_t matchCount = std::min(matrixSources.size(), matrixTargets.size());
        result.candidateEdgeCount += static_cast<int>(matrixSources.size() * matrixTargets.size());

        std::vector<GreedyCandidate> matchedSources;
        std::vector<GreedyCandidate> matchedTargets;
        int rank = 0;
        if (matchCount > 0)
        {
            std::vector<std::vector<double>> costs(matrixSources.size(),
                                                   std::vector<double>(matrixTargets.size(), 0.0));
            for (size_t sourceOffset = 0; sourceOffset < matrixSources.size(); ++sourceOffset)
            {
                for (size_t targetOffset = 0; targetOffset < matrixTargets.size(); ++targetOffset)
                {
                    const auto& source = matrixSources[sourceOffset];
                    const auto& target = matrixTargets[targetOffset];
                    costs[sourceOffset][targetOffset] =
                        logFrequencyCost(source.assignment.frequencyHz, target.assignment.frequencyHz)
                        + amplitudeRankCost(source.amplitudeRank,
                                            target.amplitudeRank,
                                            options.amplitudeRankWeight)
                        + confidenceCost(source, target, options.confidenceWeight)
                        + continuityCost(source, target, previousTargetBySource, options.continuityWeight);
                }
            }

            MatrixSearchState state;
            std::vector<bool> targetUsed(matrixTargets.size(), false);
            std::vector<std::pair<size_t, size_t>> currentPairs;
            searchAssignmentMatrix(0,
                                   static_cast<int>(matchCount),
                                   costs,
                                   targetUsed,
                                   currentPairs,
                                   0.0,
                                   state);
            result.evaluatedAssignmentCount += state.evaluatedAssignmentCount;

            for (const auto& pair : state.bestPairs)
            {
                const auto& source = matrixSources[pair.first];
                const auto& target = matrixTargets[pair.second];
                MorphCorrespondenceRecord record;
                record.bandIndex = bandIndex;
                record.rank = rank++;
                record.kind = MorphCorrespondenceKind::Matched;
                copySourceAssignment(record, source.assignment);
                copyTargetAssignment(record, target.assignment);
                addAssignmentMatrixCostDiagnostics(record, source, target, options, previousTargetBySource);
                result.records.push_back(record);
                matchedSources.push_back(source);
                matchedTargets.push_back(target);
            }
        }

        for (const auto& source : sourceBand)
        {
            if (candidateWasMatched(source, matchedSources))
                continue;

            MorphCorrespondenceRecord record;
            record.bandIndex = bandIndex;
            record.rank = rank++;
            record.kind = MorphCorrespondenceKind::UnmatchedSource;
            copySourceAssignment(record, source.assignment);
            result.records.push_back(record);
        }

        for (const auto& target : targetBand)
        {
            if (candidateWasMatched(target, matchedTargets))
                continue;

            MorphCorrespondenceRecord record;
            record.bandIndex = bandIndex;
            record.rank = rank++;
            record.kind = MorphCorrespondenceKind::UnmatchedTarget;
            copyTargetAssignment(record, target.assignment);
            result.records.push_back(record);
        }
    }

    result.totalMatchedCost = matchedCostSum(result.records);
    return result;
}

MorphFrequencyInterpolationResult interpolateMorphFrequency(const MorphFrequencyGrid& grid,
                                                            double sourceFrequencyHz,
                                                            double targetFrequencyHz,
                                                            double morphAmount,
                                                            const MorphFrequencyInterpolationOptions& options)
{
    MorphFrequencyInterpolationResult result;
    if (!isValidFrequencyGrid(grid) || !isPositive(sourceFrequencyHz) || !isPositive(targetFrequencyHz)
        || !std::isfinite(morphAmount))
    {
        return result;
    }

    result.valid = true;
    result.sourceBandIndex = findFrequencyBandIndex(grid, sourceFrequencyHz);
    result.targetBandIndex = findFrequencyBandIndex(grid, targetFrequencyHz);
    result.crossedBand = result.sourceBandIndex >= 0 && result.targetBandIndex >= 0
                         && result.sourceBandIndex != result.targetBandIndex;

    const double amount = std::max(0.0, std::min(1.0, morphAmount));
    if (amount <= 0.0)
    {
        result.frequencyHz = sourceFrequencyHz;
    }
    else if (amount >= 1.0)
    {
        result.frequencyHz = targetFrequencyHz;
    }
    else if (options.domain == MorphFrequencyInterpolationDomain::BarkCoordinate)
    {
        const double sourceBark = hzToBark(sourceFrequencyHz);
        const double targetBark = hzToBark(targetFrequencyHz);
        const double interpolatedBark = sourceBark + amount * (targetBark - sourceBark);
        result.frequencyHz = barkToHzInRange(interpolatedBark,
                                             grid.edgeFrequenciesHz.front(),
                                             grid.edgeFrequenciesHz.back());
    }
    else
    {
        const double sourceLog = std::log(sourceFrequencyHz);
        const double targetLog = std::log(targetFrequencyHz);
        result.frequencyHz = std::exp(sourceLog + amount * (targetLog - sourceLog));
    }

    if (options.clampToGridBounds)
    {
        const double clamped = std::max(grid.edgeFrequenciesHz.front(),
                                        std::min(grid.edgeFrequenciesHz.back(), result.frequencyHz));
        result.clampedToGridBounds = clamped != result.frequencyHz;
        result.frequencyHz = clamped;
    }

    result.outputBandIndex = findFrequencyBandIndex(grid, result.frequencyHz);
    return result;
}

MorphAmplitudeInterpolationResult interpolateMorphAmplitude(double sourceAmplitude,
                                                            double targetAmplitude,
                                                            double morphAmount,
                                                            const MorphAmplitudeInterpolationOptions& options)
{
    MorphAmplitudeInterpolationResult result;
    if (!std::isfinite(sourceAmplitude) || !std::isfinite(targetAmplitude) || !std::isfinite(morphAmount)
        || sourceAmplitude < 0.0 || targetAmplitude < 0.0)
    {
        return result;
    }

    result.valid = true;
    const double amount = std::max(0.0, std::min(1.0, morphAmount));
    if (amount <= 0.0)
    {
        result.amplitude = sourceAmplitude;
        result.power = amplitudeToPower(result.amplitude);
        return result;
    }
    if (amount >= 1.0)
    {
        result.amplitude = targetAmplitude;
        result.power = amplitudeToPower(result.amplitude);
        return result;
    }

    if (sourceAmplitude == 0.0 && targetAmplitude == 0.0)
    {
        result.amplitude = 0.0;
        result.power = 0.0;
        return result;
    }

    if (options.domain == MorphAmplitudeInterpolationDomain::Decibels)
    {
        const double floorDb = std::isfinite(options.decibelFloor) ? std::min(0.0, options.decibelFloor) : -120.0;
        const double floorAmplitude = decibelFloorToAmplitude(floorDb);
        result.usedDecibelFloor = sourceAmplitude < floorAmplitude || targetAmplitude < floorAmplitude;
        const double sourceDb = amplitudeToDecibels(sourceAmplitude, floorDb);
        const double targetDb = amplitudeToDecibels(targetAmplitude, floorDb);
        result.amplitude = decibelsToAmplitude(sourceDb + amount * (targetDb - sourceDb));
    }
    else
    {
        const double sourcePower = amplitudeToPower(sourceAmplitude);
        const double targetPower = amplitudeToPower(targetAmplitude);
        result.amplitude = powerToAmplitude(sourcePower + amount * (targetPower - sourcePower));
    }

    result.power = amplitudeToPower(result.amplitude);
    return result;
}

MorphBandEnergyReport calculateBandEnergyReport(const std::vector<MorphCorrespondenceRecord>& records,
                                                 const MorphBandEnergyOptions& options)
{
    MorphBandEnergyReport report;
    std::map<int, MorphBandEnergySnapshot> bandsByIndex;
    const double amount = std::max(0.0, std::min(1.0, options.morphAmount));

    for (const auto& record : records)
    {
        auto& band = bandsByIndex[record.bandIndex];
        band.bandIndex = record.bandIndex;

        if (record.kind == MorphCorrespondenceKind::Matched)
        {
            const double sourcePower = nonNegativeAmplitudePower(record.sourceAmplitude);
            const double targetPower = nonNegativeAmplitudePower(record.targetAmplitude);
            band.sourceSinusoidalPower += sourcePower;
            band.targetSinusoidalPower += targetPower;

            const auto interpolated = interpolateMorphAmplitude(record.sourceAmplitude,
                                                                record.targetAmplitude,
                                                                amount,
                                                                options.amplitudeInterpolation);
            if (interpolated.valid)
                band.outputSinusoidalPower += interpolated.power;
            continue;
        }

        if (record.kind == MorphCorrespondenceKind::UnmatchedSource)
        {
            const double sourcePower = nonNegativeAmplitudePower(record.sourceAmplitude);
            band.sourceSinusoidalPower += sourcePower;
            band.outputSinusoidalPower += unmatchedSinusoidalOutputPower(options.unmatchedPolicies.sourcePolicy,
                                                                         record.kind,
                                                                         sourcePower,
                                                                         amount);
            band.outputResidualPower += unmatchedResidualOutputPower(options.unmatchedPolicies.sourcePolicy,
                                                                     record.kind,
                                                                     sourcePower,
                                                                     amount);
        }
        else if (record.kind == MorphCorrespondenceKind::UnmatchedTarget)
        {
            const double targetPower = nonNegativeAmplitudePower(record.targetAmplitude);
            band.targetSinusoidalPower += targetPower;
            band.outputSinusoidalPower += unmatchedSinusoidalOutputPower(options.unmatchedPolicies.targetPolicy,
                                                                         record.kind,
                                                                         targetPower,
                                                                         amount);
            band.outputResidualPower += unmatchedResidualOutputPower(options.unmatchedPolicies.targetPolicy,
                                                                     record.kind,
                                                                     targetPower,
                                                                     amount);
        }
    }

    const double tolerance = std::max(0.0, options.powerDeltaTolerance);
    report.bands.reserve(bandsByIndex.size());
    for (auto& entry : bandsByIndex)
    {
        auto& band = entry.second;
        band.expectedOutputPower = interpolatedPower(band.sourceSinusoidalPower,
                                                     band.targetSinusoidalPower,
                                                     amount);
        band.powerDelta = (band.outputSinusoidalPower + band.outputResidualPower) - band.expectedOutputPower;
        band.exceedsTolerance = std::abs(band.powerDelta) > tolerance;
        if (band.exceedsTolerance)
            ++report.warningBandCount;
        report.bands.push_back(band);
    }

    return report;
}

MorphResidualNoiseReport morphResidualNoiseBands(const std::vector<MorphResidualNoiseBand>& sourceBands,
                                                 const std::vector<MorphResidualNoiseBand>& targetBands,
                                                 const MorphResidualNoiseOptions& options)
{
    MorphResidualNoiseReport report;
    report.outputPartialGain = std::max(0.0, std::isfinite(options.gains.partialGain)
                                                 ? options.gains.partialGain
                                                 : 0.0);
    report.outputPartialNoiseGain = std::max(0.0, std::isfinite(options.gains.partialNoiseGain)
                                                      ? options.gains.partialNoiseGain
                                                      : 0.0);
    report.outputOrphanResidualGain = std::max(0.0, std::isfinite(options.gains.orphanResidualGain)
                                                        ? options.gains.orphanResidualGain
                                                        : 0.0);

    std::map<int, MorphResidualNoiseBandResult> bandsByIndex;
    for (const auto& band : sourceBands)
    {
        auto& result = bandsByIndex[band.bandIndex];
        result.bandIndex = band.bandIndex;
        result.sourcePartialNoisePower += nonNegativePower(band.partialNoisePower);
        result.sourceOrphanResidualPower += nonNegativePower(band.orphanResidualPower);
    }
    for (const auto& band : targetBands)
    {
        auto& result = bandsByIndex[band.bandIndex];
        result.bandIndex = band.bandIndex;
        result.targetPartialNoisePower += nonNegativePower(band.partialNoisePower);
        result.targetOrphanResidualPower += nonNegativePower(band.orphanResidualPower);
    }

    const double amount = clampUnitInterval(options.morphAmount);
    const double partialNoiseScale = gainToPowerScale(options.gains.partialNoiseGain);
    const double orphanResidualScale = gainToPowerScale(options.gains.orphanResidualGain);
    report.bands.reserve(bandsByIndex.size());
    for (auto& entry : bandsByIndex)
    {
        auto& band = entry.second;
        const auto partialNoise = interpolateMorphAmplitude(powerToAmplitude(band.sourcePartialNoisePower),
                                                            powerToAmplitude(band.targetPartialNoisePower),
                                                            amount,
                                                            options.interpolation);
        const auto orphanResidual = interpolateMorphAmplitude(powerToAmplitude(band.sourceOrphanResidualPower),
                                                              powerToAmplitude(band.targetOrphanResidualPower),
                                                              amount,
                                                              options.interpolation);
        if (partialNoise.valid)
            band.outputPartialNoisePower = partialNoise.power * partialNoiseScale;
        if (orphanResidual.valid)
            band.outputOrphanResidualPower = orphanResidual.power * orphanResidualScale;
        band.outputTotalNoisePower = band.outputPartialNoisePower + band.outputOrphanResidualPower;
        report.bands.push_back(band);
    }

    return report;
}

MorphRenderSnapshot evaluateMorphRecipeSnapshot(const MorphRecipe& recipe, const MorphSnapshotOptions& options)
{
    MorphRenderSnapshot snapshot;
    snapshot.recipeSchemaVersion = recipe.schemaVersion;
    snapshot.engineVersion = recipe.engineVersion;
    snapshot.morphAmount = clampUnitInterval(options.morphAmount);
    snapshot.laneAmounts = evaluateMorphLaneAmounts(recipe, snapshot.morphAmount);

    const double frequencyAmount = snapshot.laneAmounts[static_cast<size_t>(MorphLane::Frequency)];
    const double amplitudeAmount = snapshot.laneAmounts[static_cast<size_t>(MorphLane::Amplitude)];
    const double residualNoiseAmount = snapshot.laneAmounts[static_cast<size_t>(MorphLane::ResidualNoise)];
    const double phaseAmount = snapshot.laneAmounts[static_cast<size_t>(MorphLane::Phase)];

    snapshot.partials.reserve(recipe.correspondenceRecords.size());
    for (const auto& record : recipe.correspondenceRecords)
    {
        snapshot.partials.push_back(evaluateSnapshotPartial(recipe,
                                                            record,
                                                            frequencyAmount,
                                                            amplitudeAmount,
                                                            phaseAmount,
                                                            options.lifecycleFrameIndex,
                                                            options.frequency,
                                                            options.amplitude));
    }

    MorphResidualNoiseOptions residualNoiseOptions;
    residualNoiseOptions.morphAmount = residualNoiseAmount;
    residualNoiseOptions.gains = recipe.outputGains;
    residualNoiseOptions.interpolation = options.amplitude;
    snapshot.residualNoise = morphResidualNoiseBands(options.sourceResidualNoiseBands,
                                                     options.targetResidualNoiseBands,
                                                     residualNoiseOptions);

    MorphBandEnergyOptions energyOptions;
    energyOptions.morphAmount = amplitudeAmount;
    energyOptions.unmatchedPolicies = recipe.unmatchedPartialPolicies;
    energyOptions.amplitudeInterpolation = options.amplitude;
    snapshot.energy = calculateBandEnergyReport(recipe.correspondenceRecords, energyOptions);
    snapshot.transientAlignment = analyzeMorphTransientAlignment(recipe.sourceATransients,
                                                                recipe.sourceBTransients,
                                                                recipe.transient);

    return snapshot;
}

MorphSnapshotPlaybackBuffer renderMorphSnapshotPlaybackBuffer(const MorphRenderSnapshot& snapshot,
                                                              const MorphSnapshotPlaybackSettings& settings)
{
    MorphSnapshotPlaybackBuffer playback;
    playback.sampleRate = isPositive(settings.sampleRate) ? settings.sampleRate
                                                          : MorphSnapshotPlaybackSettings{}.sampleRate;
    const double durationSeconds = isPositive(settings.durationSeconds)
                                       ? settings.durationSeconds
                                       : MorphSnapshotPlaybackSettings{}.durationSeconds;
    const int sampleCount = std::max(1, static_cast<int>(std::llround(playback.sampleRate * durationSeconds)));

    playback.partialSamples.assign(static_cast<size_t>(sampleCount), 0.0f);
    playback.residualSamples.assign(static_cast<size_t>(sampleCount), 0.0f);

    for (const auto& partial : snapshot.partials)
    {
        if (!partial.valid || partial.discarded || partial.routedToResidual || partial.amplitude <= 0.0
            || partial.frequencyHz <= 0.0 || !std::isfinite(partial.frequencyHz))
        {
            continue;
        }

        const double phaseStart = partial.phase.valid && !partial.phase.usesRendererContinuation
                                      ? partial.phase.phaseRadians
                                      : 0.0;
        const double phaseIncrement = kTwoPi * partial.frequencyHz / playback.sampleRate;
        // Phasor recurrence replaces a per-sample std::sin: rotate a unit vector
        // by phaseIncrement each step; its imaginary part is sin(phaseStart + inc*i).
        const double rotRe = std::cos(phaseIncrement);
        const double rotIm = std::sin(phaseIncrement);
        double re = std::cos(phaseStart);
        double im = std::sin(phaseStart);
        for (int i = 0; i < sampleCount; ++i)
        {
            playback.partialSamples[static_cast<size_t>(i)] +=
                static_cast<float>(im * partial.amplitude);
            const double newRe = re * rotRe - im * rotIm;
            im = re * rotIm + im * rotRe;
            re = newRe;
        }
    }

    std::map<int, double> residualPowerByBand;
    double unbandedResidualPower = 0.0;
    const auto addResidualPower = [&residualPowerByBand, &unbandedResidualPower](int bandIndex, double power) {
        const double safePower = nonNegativePower(power);
        if (safePower <= 0.0)
            return;

        if (bandIndex >= 0)
            residualPowerByBand[bandIndex] += safePower;
        else
            unbandedResidualPower += safePower;
    };

    for (const auto& band : snapshot.residualNoise.bands)
        addResidualPower(band.bandIndex, band.outputTotalNoisePower);
    for (const auto& band : snapshot.energy.bands)
        addResidualPower(band.bandIndex, band.outputResidualPower);

    for (const auto& bandPower : residualPowerByBand)
    {
        const double bandPowerTotal = nonNegativePower(bandPower.second);
        if (bandPowerTotal <= 0.0)
            continue;

        // Match do_synthesis level: ioscilator(ENG_RMS) * randi gives RMS = ENG_RMS/3
        // (randi RMS = sqrt(2)/3, carrier RMS = 1/sqrt(2), product = 1/3).
        // bandPowerTotal is ENG_RMS^2, so divide by 9 to get (ENG_RMS/3)^2 target power.
        const double componentAmplitude = std::sqrt(2.0 * bandPowerTotal
                                                    / (9.0 * static_cast<double>(kResidualPreviewComponentsPerBand)));
        for (int component = 0; component < kResidualPreviewComponentsPerBand; ++component)
        {
            const double frequencyHz = residualPreviewComponentFrequencyHz(bandPower.first,
                                                                           component,
                                                                           playback.sampleRate);
            if (!isPositive(frequencyHz))
                continue;

            const double phaseStart = residualPreviewUnitHash(bandPower.first, component + 101) * kTwoPi;
            const double phaseIncrement = kTwoPi * frequencyHz / playback.sampleRate;
            const double rotRe = std::cos(phaseIncrement);
            const double rotIm = std::sin(phaseIncrement);
            double re = std::cos(phaseStart);
            double im = std::sin(phaseStart);
            for (int i = 0; i < sampleCount; ++i)
            {
                playback.residualSamples[static_cast<size_t>(i)] +=
                    static_cast<float>(im * componentAmplitude);
                const double newRe = re * rotRe - im * rotIm;
                im = re * rotIm + im * rotRe;
                re = newRe;
            }
        }
    }

    const double residualAmplitude = powerToAmplitude(unbandedResidualPower) / 3.0;
    if (residualAmplitude > 0.0)
    {
        const double residualCarrierHz = isPositive(settings.residualCarrierHz)
                                             ? settings.residualCarrierHz
                                             : MorphSnapshotPlaybackSettings{}.residualCarrierHz;
        const double residualPhaseIncrement = kTwoPi * residualCarrierHz / playback.sampleRate;
        const double rotRe = std::cos(residualPhaseIncrement);
        const double rotIm = std::sin(residualPhaseIncrement);
        double re = 1.0; // phaseStart = 0
        double im = 0.0;
        for (int i = 0; i < sampleCount; ++i)
        {
            playback.residualSamples[static_cast<size_t>(i)] +=
                static_cast<float>(im * residualAmplitude);
            const double newRe = re * rotRe - im * rotIm;
            im = re * rotIm + im * rotRe;
            re = newRe;
        }
    }

    return playback;
}

int mixMorphSnapshotPlaybackBlock(const MorphSnapshotPlaybackBuffer& playback,
                                  MorphSnapshotPlaybackCursor& cursor,
                                  float partialGain,
                                  float residualGain,
                                  float outputTrim,
                                  bool residualOnly,
                                  float* output,
                                  int numSamples)
{
    if (output == nullptr || numSamples <= 0)
        return 0;

    const int totalSamples = playback.getNumSamples();
    int consumed = 0;
    for (int i = 0; i < numSamples; ++i)
    {
        float value = 0.0f;
        if (cursor.sampleIndex >= 0 && cursor.sampleIndex < totalSamples)
        {
            const int sampleIndex = cursor.sampleIndex;
            const float partial = sampleIndex < static_cast<int>(playback.partialSamples.size())
                                      ? playback.partialSamples[static_cast<size_t>(sampleIndex)]
                                      : 0.0f;
            const float residual = sampleIndex < static_cast<int>(playback.residualSamples.size())
                                       ? playback.residualSamples[static_cast<size_t>(sampleIndex)]
                                       : 0.0f;
            value = residualOnly ? residual * residualGain
                                 : partial * partialGain + residual * residualGain;
            value *= outputTrim;
            ++cursor.sampleIndex;
            ++consumed;
        }
        output[i] = value;
    }

    return consumed;
}

MorphPerformanceProfile profileMorphPerformance(const std::vector<MorphBandAssignment>& sourceAssignments,
                                                const std::vector<MorphBandAssignment>& targetAssignments,
                                                int frameCount,
                                                const MorphAssignmentMatrixOptions& assignmentOptions,
                                                const MorphPerformanceSoftCaps& softCaps)
{
    MorphPerformanceProfile profile;
    profile.frameCount = std::max(0, frameCount);

    const auto summary = summarizePerformanceCandidates(sourceAssignments, targetAssignments);
    profile.sourceCandidateCount = summary.sourceCount;
    profile.targetCandidateCount = summary.targetCount;
    profile.bandCount = summary.bandCount;

    const auto frequencyRankRecords = buildFrequencyRankCorrespondence(sourceAssignments, targetAssignments);
    const auto greedyRecords = buildGreedyNearestCorrespondence(sourceAssignments, targetAssignments);

    MorphAssignmentMatrixOptions matrixOptions = assignmentOptions;
    matrixOptions.advancedStrategyEnabled = true;
    const auto assignmentResult = buildAssignmentMatrixCorrespondence(sourceAssignments,
                                                                      targetAssignments,
                                                                      matrixOptions);

    const int largestRecordCount = std::max(static_cast<int>(frequencyRankRecords.size()),
                                            std::max(static_cast<int>(greedyRecords.size()),
                                                     static_cast<int>(assignmentResult.records.size())));
    profile.estimatedSnapshotMemoryBytes = static_cast<std::size_t>(std::max(0, largestRecordCount))
                                           * sizeof(MorphSnapshotPartial);
    profile.estimatedAnalysisMemoryBytes = static_cast<std::size_t>(profile.frameCount)
                                           * static_cast<std::size_t>(std::max(0, largestRecordCount))
                                           * sizeof(MorphCorrespondenceRecord);

    const int largestCandidateCount = std::max(profile.sourceCandidateCount, profile.targetCandidateCount);
    profile.exceedsPartialSoftCap = largestCandidateCount > softCaps.maxPartialsPerFrame;
    profile.exceedsFrameSoftCap = profile.frameCount > softCaps.maxFrameCount;
    profile.exceedsAnalysisMemorySoftCap = profile.estimatedAnalysisMemoryBytes > softCaps.maxAnalysisMemoryBytes;
    profile.exceedsSnapshotMemorySoftCap = profile.estimatedSnapshotMemoryBytes > softCaps.maxSnapshotMemoryBytes;

    const auto makeEstimate = [&](MorphCorrespondenceStrategy strategy,
                                  int outputRecordCount,
                                  int candidateEdgeCount,
                                  int evaluatedAssignmentCount,
                                  double relativeCpuCost) {
        MorphStrategyPerformanceEstimate estimate;
        estimate.strategy = strategy;
        estimate.sourceCandidateCount = profile.sourceCandidateCount;
        estimate.targetCandidateCount = profile.targetCandidateCount;
        estimate.outputRecordCount = outputRecordCount;
        estimate.candidateEdgeCount = candidateEdgeCount;
        estimate.evaluatedAssignmentCount = evaluatedAssignmentCount;
        estimate.relativeCpuCost = relativeCpuCost;
        estimate.exceedsCandidateEdgeSoftCap = candidateEdgeCount > 0
                                               && summary.maxCandidateEdgesPerBand
                                                      > softCaps.maxCandidateEdgesPerBand;
        estimate.exceedsAssignmentSoftCap = evaluatedAssignmentCount > softCaps.maxEvaluatedAssignments;
        return estimate;
    };

    profile.strategies.push_back(makeEstimate(
        MorphCorrespondenceStrategy::FrequencyRank,
        static_cast<int>(frequencyRankRecords.size()),
        0,
        0,
        nLog2N(profile.sourceCandidateCount) + nLog2N(profile.targetCandidateCount)
            + static_cast<double>(frequencyRankRecords.size())));
    profile.strategies.push_back(makeEstimate(
        MorphCorrespondenceStrategy::GreedyNearest,
        static_cast<int>(greedyRecords.size()),
        summary.candidateEdgeCount,
        0,
        static_cast<double>(summary.candidateEdgeCount + greedyRecords.size())));
    profile.strategies.push_back(makeEstimate(
        MorphCorrespondenceStrategy::AssignmentMatrix,
        static_cast<int>(assignmentResult.records.size()),
        assignmentResult.candidateEdgeCount,
        assignmentResult.evaluatedAssignmentCount,
        static_cast<double>(assignmentResult.candidateEdgeCount)
            + static_cast<double>(assignmentResult.evaluatedAssignmentCount)
                  * static_cast<double>(std::max(1, std::min(profile.sourceCandidateCount,
                                                             profile.targetCandidateCount)))));

    if (profile.exceedsPartialSoftCap)
        ++profile.warningCount;
    if (profile.exceedsFrameSoftCap)
        ++profile.warningCount;
    if (profile.exceedsAnalysisMemorySoftCap)
        ++profile.warningCount;
    if (profile.exceedsSnapshotMemorySoftCap)
        ++profile.warningCount;
    for (const auto& strategy : profile.strategies)
    {
        if (strategy.exceedsCandidateEdgeSoftCap)
            ++profile.warningCount;
        if (strategy.exceedsAssignmentSoftCap)
            ++profile.warningCount;
    }

    return profile;
}

std::vector<std::vector<MorphCorrespondenceRecord>> smoothCorrespondenceFrames(
    const std::vector<std::vector<MorphCorrespondenceRecord>>& frames,
    const MorphCorrespondenceSmoothingOptions& options)
{
    std::vector<std::vector<MorphCorrespondenceRecord>> smoothed = frames;
    if (smoothed.size() < 2)
        return smoothed;

    const double margin = std::max(0.0, options.hysteresisCostMargin);
    for (size_t frameIndex = 1; frameIndex < smoothed.size(); ++frameIndex)
    {
        std::map<int, int> previousTargetBySource;
        for (const auto& previousRecord : smoothed[frameIndex - 1])
        {
            if (previousRecord.kind == MorphCorrespondenceKind::Matched)
                previousTargetBySource[previousRecord.sourcePartialIndex] = previousRecord.targetPartialIndex;
        }

        auto& currentFrame = smoothed[frameIndex];
        for (size_t i = 0; i < currentFrame.size(); ++i)
        {
            auto& record = currentFrame[i];
            if (record.kind != MorphCorrespondenceKind::Matched)
                continue;

            const auto previousTarget = previousTargetBySource.find(record.sourcePartialIndex);
            if (previousTarget == previousTargetBySource.end()
                || previousTarget->second == record.targetPartialIndex)
            {
                continue;
            }

            size_t partnerIndex = currentFrame.size();
            for (size_t j = 0; j < currentFrame.size(); ++j)
            {
                if (i == j)
                    continue;
                const auto& candidate = currentFrame[j];
                if (candidate.kind == MorphCorrespondenceKind::Matched
                    && candidate.bandIndex == record.bandIndex
                    && candidate.targetPartialIndex == previousTarget->second)
                {
                    partnerIndex = j;
                    break;
                }
            }
            if (partnerIndex == currentFrame.size())
                continue;

            auto& partner = currentFrame[partnerIndex];
            const double currentCost = recordPairCost(record) + recordPairCost(partner);
            const double swappedCost = swappedPairCost(record, partner) + swappedPairCost(partner, record);
            if (swappedCost <= currentCost + margin)
            {
                MorphCorrespondenceRecord originalRecord = record;
                MorphCorrespondenceRecord originalPartner = partner;
                copyTargetFields(record, originalPartner);
                copyTargetFields(partner, originalRecord);
                refreshSmoothedCostDiagnostics(record);
                refreshSmoothedCostDiagnostics(partner);
            }
        }
    }

    return smoothed;
}

MorphCorrespondenceChurnMetrics calculateCorrespondenceChurnMetrics(
    const std::vector<std::vector<MorphCorrespondenceRecord>>& frames)
{
    MorphCorrespondenceChurnMetrics metrics;
    if (frames.size() < 2)
        return metrics;

    metrics.frameTransitionCount = static_cast<int>(frames.size() - 1);
    for (size_t frameIndex = 1; frameIndex < frames.size(); ++frameIndex)
    {
        std::map<int, int> previousTargetBySource;
        for (const auto& previousRecord : frames[frameIndex - 1])
        {
            if (previousRecord.kind == MorphCorrespondenceKind::Matched)
                previousTargetBySource[previousRecord.sourcePartialIndex] = previousRecord.targetPartialIndex;
        }

        for (const auto& currentRecord : frames[frameIndex])
        {
            if (currentRecord.kind != MorphCorrespondenceKind::Matched)
                continue;

            const auto previousTarget = previousTargetBySource.find(currentRecord.sourcePartialIndex);
            if (previousTarget == previousTargetBySource.end())
                continue;

            ++metrics.comparedMatchCount;
            if (previousTarget->second != currentRecord.targetPartialIndex)
                ++metrics.changedMatchCount;
        }
    }

    if (metrics.comparedMatchCount > 0)
    {
        metrics.churnRate = static_cast<double>(metrics.changedMatchCount)
                            / static_cast<double>(metrics.comparedMatchCount);
    }
    return metrics;
}

bool unmatchedPartialPoliciesRequireDiscardOptIn(const MorphUnmatchedPartialPolicies& policies)
{
    return policies.sourcePolicy == MorphUnmatchedPartialPolicy::Discard
           || policies.targetPolicy == MorphUnmatchedPartialPolicy::Discard;
}

bool validateUnmatchedPartialPolicies(const MorphUnmatchedPartialPolicies& policies, std::string* errorMessage)
{
    if (unmatchedPartialPoliciesRequireDiscardOptIn(policies) && !policies.discardOptIn)
    {
        if (errorMessage)
            *errorMessage = "discard unmatched partial policy requires explicit opt-in";
        return false;
    }
    return true;
}

std::vector<MorphUnmatchedPartialDecision> resolveUnmatchedPartialPolicies(
    const std::vector<MorphCorrespondenceRecord>& records,
    const MorphUnmatchedPartialPolicies& policies)
{
    std::vector<MorphUnmatchedPartialDecision> decisions;
    decisions.reserve(records.size());
    for (const auto& record : records)
    {
        if (record.kind == MorphCorrespondenceKind::UnmatchedSource)
        {
            decisions.push_back(makeUnmatchedDecision(record, policies.sourcePolicy));
        }
        else if (record.kind == MorphCorrespondenceKind::UnmatchedTarget)
        {
            decisions.push_back(makeUnmatchedDecision(record, policies.targetPolicy));
        }
    }
    return decisions;
}

MorphRecipe makeDefaultMorphRecipe()
{
    MorphRecipe recipe;
    recipe.schemaVersion = kMorphRecipeSchemaVersion;
    recipe.engineVersion = "morph-recipe-v1";
    recipe.frequencyGrid = makeBarkFrequencyGrid(25, 0.0, 22050.0);
    recipe.defaultMorphAmount = 0.0;
    for (auto& lane : recipe.lanes)
    {
        lane.linkedToMainMorph = true;
        lane.amount = 0.0;
    }
    return recipe;
}

MorphCompatibilityReport validateMorphSources(const MorphRecipe& recipe)
{
    MorphCompatibilityReport report;
    validateSourceMetadata(report, "source_a", recipe.sourceA);
    validateSourceMetadata(report, "source_b", recipe.sourceB);

    if (isPositive(recipe.sourceA.sampleRate) && isPositive(recipe.sourceB.sampleRate)
        && !nearlyEqual(recipe.sourceA.sampleRate, recipe.sourceB.sampleRate))
    {
        addIssue(report, MorphCompatibilitySeverity::Error, MorphCompatibilityCode::SampleRateMismatch, "recipe",
                 "source sample rates must match before morphing");
    }

    if (recipe.sourceA.channelCount > 0 && recipe.sourceB.channelCount > 0
        && recipe.sourceA.channelCount != recipe.sourceB.channelCount)
    {
        addIssue(report, MorphCompatibilitySeverity::Error, MorphCompatibilityCode::ChannelCountMismatch, "recipe",
                 "source channel counts must match before morphing");
    }

    if (isPositive(recipe.sourceA.frameHopSeconds) && isPositive(recipe.sourceB.frameHopSeconds)
        && !nearlyEqual(recipe.sourceA.frameHopSeconds, recipe.sourceB.frameHopSeconds))
    {
        addIssue(report, MorphCompatibilitySeverity::Warning, MorphCompatibilityCode::IncompatibleFrameGrid, "recipe",
                 "source frame grids differ; a later recipe rebuild must resample or align frame times");
    }

    if (hasValidFrequencyBounds(recipe.sourceA) && hasValidFrequencyBounds(recipe.sourceB)
        && (recipe.sourceA.highestFrequencyHz <= recipe.sourceB.lowestFrequencyHz
            || recipe.sourceB.highestFrequencyHz <= recipe.sourceA.lowestFrequencyHz))
    {
        addIssue(report, MorphCompatibilitySeverity::Error, MorphCompatibilityCode::NonOverlappingFrequencyBounds,
                 "recipe", "source frequency bounds do not overlap");
    }

    return report;
}

std::string serializeMorphRecipe(const MorphRecipe& recipe)
{
    std::ostringstream out;
    out << "ATS_MORPH_RECIPE " << recipe.schemaVersion << '\n';
    writeKeyValue(out, "engine_version", recipe.engineVersion);
    writeKeyValue(out, "default_morph_amount", recipe.defaultMorphAmount);
    writeFrequencyGrid(out, recipe.frequencyGrid);
    writeUnmatchedPartialPolicies(out, recipe.unmatchedPartialPolicies);
    writeRenderGainSettings(out, recipe.outputGains);
    writePhaseSettings(out, recipe.phase);
    writeLifecycleFadeSettings(out, recipe.lifecycle);
    writeTransientSettings(out, recipe.transient);
    writeTransientMarkers(out, "source_a", recipe.sourceATransients);
    writeTransientMarkers(out, "source_b", recipe.sourceBTransients);
    writeSource(out, "source_a", recipe.sourceA);
    writeSource(out, "source_b", recipe.sourceB);

    for (size_t i = 0; i < recipe.lanes.size(); ++i)
    {
        const std::string lanePrefix = std::string("lane.") + morphLaneName(static_cast<MorphLane>(i));
        writeKeyValue(out, lanePrefix + ".linked", recipe.lanes[i].linkedToMainMorph);
        writeKeyValue(out, lanePrefix + ".amount", recipe.lanes[i].amount);
        writeKeyValue(out, lanePrefix + ".curve", std::string(morphCurveShapeName(recipe.lanes[i].curve)));
    }

    writeKeyValue(out, "correspondence.count", static_cast<int>(recipe.correspondenceRecords.size()));
    for (size_t i = 0; i < recipe.correspondenceRecords.size(); ++i)
        writeCorrespondenceRecord(out, static_cast<int>(i), recipe.correspondenceRecords[i]);

    for (const auto& unknown : recipe.unknownFields)
    {
        if (!unknown.key.empty() && !isKnownSerializedKey(unknown.key))
            writeKeyValue(out, unknown.key, unknown.value);
    }
    return out.str();
}

bool deserializeMorphRecipe(const std::string& text, MorphRecipe& recipe, std::string* errorMessage)
{
    std::istringstream in(text);
    std::string line;
    if (!std::getline(in, line))
    {
        if (errorMessage)
            *errorMessage = "empty serialized morph recipe";
        return false;
    }

    constexpr std::string_view kHeaderPrefix = "ATS_MORPH_RECIPE ";
    if (line.rfind(kHeaderPrefix, 0) != 0)
    {
        if (errorMessage)
            *errorMessage = "serialized morph recipe header is missing";
        return false;
    }

    int schemaVersion = 0;
    const std::string versionText = line.substr(kHeaderPrefix.size());
    const auto* begin = versionText.data();
    const auto* end = versionText.data() + versionText.size();
    const auto versionResult = std::from_chars(begin, end, schemaVersion);
    if (versionResult.ec != std::errc{} || versionResult.ptr != end)
    {
        if (errorMessage)
            *errorMessage = "serialized morph recipe version is invalid";
        return false;
    }
    if (schemaVersion != kMorphRecipeSchemaVersion)
    {
        if (errorMessage)
            *errorMessage = "unsupported serialized morph recipe version";
        return false;
    }

    std::map<std::string, std::string> values;
    while (std::getline(in, line))
    {
        if (line.empty())
            continue;
        const auto separator = line.find('=');
        if (separator == std::string::npos)
        {
            if (errorMessage)
                *errorMessage = "serialized morph recipe line is missing '='";
            return false;
        }

        std::string value;
        if (!unescapeValue(line.substr(separator + 1), value, errorMessage))
            return false;
        values[line.substr(0, separator)] = value;
    }

    MorphRecipe parsed = makeDefaultMorphRecipe();
    parsed.schemaVersion = schemaVersion;
    if (!readString(values, "engine_version", parsed.engineVersion, errorMessage)
        || !readDouble(values, "default_morph_amount", parsed.defaultMorphAmount, errorMessage)
        || !readFrequencyGrid(values, parsed.frequencyGrid, errorMessage)
        || !readUnmatchedPartialPolicies(values, parsed.unmatchedPartialPolicies, errorMessage)
        || !readRenderGainSettings(values, parsed.outputGains, errorMessage)
        || !readPhaseSettings(values, parsed.phase, errorMessage)
        || !readLifecycleFadeSettings(values, parsed.lifecycle, errorMessage)
        || !readTransientSettings(values, parsed.transient, errorMessage)
        || !readTransientMarkers(values, "source_a", parsed.sourceATransients, errorMessage)
        || !readTransientMarkers(values, "source_b", parsed.sourceBTransients, errorMessage)
        || !readSource(values, "source_a", parsed.sourceA, errorMessage)
        || !readSource(values, "source_b", parsed.sourceB, errorMessage))
    {
        return false;
    }

    for (size_t i = 0; i < parsed.lanes.size(); ++i)
    {
        const std::string lanePrefix = std::string("lane.") + morphLaneName(static_cast<MorphLane>(i));
        std::string curveName = morphCurveShapeName(parsed.lanes[i].curve);
        if (!readBool(values, lanePrefix + ".linked", parsed.lanes[i].linkedToMainMorph, errorMessage)
            || !readDouble(values, lanePrefix + ".amount", parsed.lanes[i].amount, errorMessage)
            || !readOptionalString(values, lanePrefix + ".curve", curveName, errorMessage)
            || !morphCurveShapeFromName(curveName, parsed.lanes[i].curve))
        {
            if (errorMessage && !curveName.empty() && !morphCurveShapeFromName(curveName, parsed.lanes[i].curve))
                *errorMessage = "invalid morph lane curve in serialized morph recipe";
            return false;
        }
    }

    if (!readCorrespondenceRecords(values, parsed.correspondenceRecords, errorMessage))
        return false;

    parsed.unknownFields.clear();
    for (const auto& entry : values)
    {
        if (!isKnownSerializedKey(entry.first))
            parsed.unknownFields.push_back({entry.first, entry.second});
    }

    recipe = parsed;
    return true;
}

} // namespace ats
