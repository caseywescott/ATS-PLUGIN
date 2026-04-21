#include "SpectralEnvelopeHybrid.h"

#include <charconv>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <unordered_map>
#include <utility>

namespace ats
{
namespace
{

constexpr const char* kRecipeHeader = "ATS_SPECTRAL_ENVELOPE_RECIPE_V1";

std::string escapeString(const std::string& text)
{
    std::string out;
    out.reserve(text.size());
    constexpr char hex[] = "0123456789ABCDEF";
    for (const unsigned char ch : text)
    {
        if (ch == '%' || ch == '\n' || ch == '\t' || ch == '\r')
        {
            out.push_back('%');
            out.push_back(hex[(ch >> 4u) & 0x0Fu]);
            out.push_back(hex[ch & 0x0Fu]);
        }
        else
        {
            out.push_back(static_cast<char>(ch));
        }
    }
    return out;
}

int hexValue(const char ch)
{
    if (ch >= '0' && ch <= '9')
        return ch - '0';
    if (ch >= 'A' && ch <= 'F')
        return 10 + ch - 'A';
    if (ch >= 'a' && ch <= 'f')
        return 10 + ch - 'a';
    return -1;
}

bool unescapeString(const std::string& text, std::string& out)
{
    out.clear();
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] != '%')
        {
            out.push_back(text[i]);
            continue;
        }
        if (i + 2 >= text.size())
            return false;
        const int hi = hexValue(text[i + 1]);
        const int lo = hexValue(text[i + 2]);
        if (hi < 0 || lo < 0)
            return false;
        out.push_back(static_cast<char>((hi << 4) | lo));
        i += 2;
    }
    return true;
}

std::string doubleToString(const double value)
{
    std::ostringstream os;
    os.imbue(std::locale::classic());
    os << std::setprecision(std::numeric_limits<double>::max_digits10) << value;
    return os.str();
}

bool parseDouble(const std::string& text, double& out)
{
    std::istringstream is(text);
    is.imbue(std::locale::classic());
    is >> out;
    return !is.fail() && is.eof();
}

bool parseUint32(const std::string& text, std::uint32_t& out)
{
    const char* begin = text.data();
    const char* end = text.data() + text.size();
    const auto result = std::from_chars(begin, end, out);
    return result.ec == std::errc() && result.ptr == end;
}

bool parseInt(const std::string& text, int& out)
{
    const char* begin = text.data();
    const char* end = text.data() + text.size();
    const auto result = std::from_chars(begin, end, out);
    return result.ec == std::errc() && result.ptr == end;
}

bool parseBool(const std::string& text, bool& out)
{
    if (text == "true")
    {
        out = true;
        return true;
    }
    if (text == "false")
    {
        out = false;
        return true;
    }
    return false;
}

void appendField(std::ostringstream& os, const char* key, const std::string& value)
{
    os << key << '\t' << value << '\n';
}

bool requireField(const std::unordered_map<std::string, std::string>& fields,
                  const char* key,
                  std::string& out,
                  std::string* errorMessage)
{
    const auto it = fields.find(key);
    if (it == fields.end())
    {
        if (errorMessage)
            *errorMessage = std::string("missing field: ") + key;
        return false;
    }
    out = it->second;
    return true;
}

bool toEnvelopeMapping(const HybridFrequencyMapping mapping, EnvelopeFrequencyMapping& out)
{
    switch (mapping)
    {
        case HybridFrequencyMapping::DirectHz:
            out = EnvelopeFrequencyMapping::DirectHz;
            return true;
        case HybridFrequencyMapping::NormalizedNyquist:
            out = EnvelopeFrequencyMapping::NormalizedNyquist;
            return true;
        case HybridFrequencyMapping::PerceptualWarp:
            out = EnvelopeFrequencyMapping::PerceptualWarp;
            return true;
        case HybridFrequencyMapping::RangeWarp:
            out = EnvelopeFrequencyMapping::RangeWarp;
            return true;
        case HybridFrequencyMapping::Correspondence:
        case HybridFrequencyMapping::HarmonicNumber:
            return false;
    }
    return false;
}

bool clampOrRejectToTargetAxis(const double frequencyHz,
                               const FrameEnvelope& targetEnvelope,
                               const EnvelopeExtrapolationPolicy extrapolation,
                               double& mappedFrequencyHz,
                               bool& inRange)
{
    EnvelopeEvaluationOptions direct;
    direct.mapping = EnvelopeFrequencyMapping::DirectHz;
    direct.extrapolation = extrapolation;
    direct.targetAxis = targetEnvelope.frequencyAxis;
    return mapEnvelopeLookupFrequency(frequencyHz, targetEnvelope, direct, mappedFrequencyHz, inRange);
}

bool isValidCorrespondencePoint(const HybridCorrespondencePoint& point)
{
    return std::isfinite(point.sourceFrequencyHz) &&
           std::isfinite(point.targetFrequencyHz) &&
           std::isfinite(point.confidence) &&
           point.sourceFrequencyHz > 0.0 &&
           point.targetFrequencyHz > 0.0 &&
           point.confidence > 0.0;
}

bool mapCorrespondenceFrequency(const double sourceFrequencyHz,
                                const std::vector<HybridCorrespondencePoint>& correspondence,
                                const EnvelopeExtrapolationPolicy extrapolation,
                                double& targetFrequencyHz,
                                bool& inRange)
{
    std::vector<HybridCorrespondencePoint> valid;
    valid.reserve(correspondence.size());
    for (const auto& point : correspondence)
    {
        if (isValidCorrespondencePoint(point))
            valid.push_back(point);
    }

    if (valid.empty() || !std::isfinite(sourceFrequencyHz) || sourceFrequencyHz <= 0.0)
        return false;

    std::sort(valid.begin(), valid.end(), [](const auto& a, const auto& b) {
        return a.sourceFrequencyHz < b.sourceFrequencyHz;
    });

    constexpr double kExactToleranceHz = 1.0e-9;
    for (const auto& point : valid)
    {
        if (std::abs(point.sourceFrequencyHz - sourceFrequencyHz) <= kExactToleranceHz)
        {
            targetFrequencyHz = point.targetFrequencyHz;
            inRange = true;
            return true;
        }
    }

    const auto& first = valid.front();
    const auto& last = valid.back();
    inRange = sourceFrequencyHz >= first.sourceFrequencyHz && sourceFrequencyHz <= last.sourceFrequencyHz;
    if (!inRange)
    {
        if (extrapolation == EnvelopeExtrapolationPolicy::Reject)
            return false;
        targetFrequencyHz = sourceFrequencyHz < first.sourceFrequencyHz ? first.targetFrequencyHz : last.targetFrequencyHz;
        return true;
    }

    for (std::size_t i = 1; i < valid.size(); ++i)
    {
        const auto& lo = valid[i - 1];
        const auto& hi = valid[i];
        if (sourceFrequencyHz < lo.sourceFrequencyHz || sourceFrequencyHz > hi.sourceFrequencyHz)
            continue;

        const double span = hi.sourceFrequencyHz - lo.sourceFrequencyHz;
        if (span <= 0.0)
            continue;

        const double t = (sourceFrequencyHz - lo.sourceFrequencyHz) / span;
        targetFrequencyHz = lo.targetFrequencyHz + t * (hi.targetFrequencyHz - lo.targetFrequencyHz);
        return std::isfinite(targetFrequencyHz) && targetFrequencyHz > 0.0;
    }

    return false;
}

void accumulateNormalizationStats(const std::vector<HybridPartialOutput>& outputs,
                                  HybridNormalizationStats& stats)
{
    for (const auto& output : outputs)
    {
        if (!output.ok ||
            !std::isfinite(output.sourceAmplitude) ||
            !std::isfinite(output.outputAmplitude) ||
            output.sourceAmplitude < 0.0 ||
            output.outputAmplitude < 0.0)
        {
            continue;
        }

        ++stats.processedCount;
        stats.sourceEnergy += output.sourceAmplitude * output.sourceAmplitude;
        stats.outputEnergyBefore += output.outputAmplitude * output.outputAmplitude;
    }

    if (stats.processedCount == 0)
        return;

    const double count = static_cast<double>(stats.processedCount);
    stats.sourceRms = std::sqrt(stats.sourceEnergy / count);
    stats.outputRmsBefore = std::sqrt(stats.outputEnergyBefore / count);
}

double loudnessLikeMean(const std::vector<HybridPartialOutput>& outputs,
                        const bool useSourceAmplitude)
{
    constexpr double kPower = 0.6;
    double sum = 0.0;
    std::size_t count = 0;
    for (const auto& output : outputs)
    {
        const double amplitude = useSourceAmplitude ? output.sourceAmplitude : output.outputAmplitude;
        if (!output.ok || !std::isfinite(amplitude) || amplitude < 0.0)
            continue;
        sum += std::pow(amplitude, kPower);
        ++count;
    }

    if (count == 0)
        return 0.0;
    return sum / static_cast<double>(count);
}

void finishNormalizationStats(const std::vector<HybridPartialOutput>& outputs,
                              HybridNormalizationStats& stats)
{
    stats.outputEnergyAfter = 0.0;
    for (const auto& output : outputs)
    {
        if (!output.ok || !std::isfinite(output.outputAmplitude) || output.outputAmplitude < 0.0)
            continue;
        stats.outputEnergyAfter += output.outputAmplitude * output.outputAmplitude;
    }

    if (stats.processedCount > 0)
        stats.outputRmsAfter = std::sqrt(stats.outputEnergyAfter / static_cast<double>(stats.processedCount));
}

} // namespace

const char* toString(const HybridFrequencyMapping mapping)
{
    switch (mapping)
    {
        case HybridFrequencyMapping::DirectHz:
            return "direct_hz";
        case HybridFrequencyMapping::NormalizedNyquist:
            return "normalized_nyquist";
        case HybridFrequencyMapping::PerceptualWarp:
            return "perceptual_warp";
        case HybridFrequencyMapping::RangeWarp:
            return "range_warp";
        case HybridFrequencyMapping::Correspondence:
            return "correspondence";
        case HybridFrequencyMapping::HarmonicNumber:
            return "harmonic_number";
    }
    return "direct_hz";
}

const char* toString(const HybridNormalizationPolicy policy)
{
    switch (policy)
    {
        case HybridNormalizationPolicy::None:
            return "none";
        case HybridNormalizationPolicy::FrameRms:
            return "frame_rms";
        case HybridNormalizationPolicy::EnergyPreserving:
            return "energy_preserving";
        case HybridNormalizationPolicy::LoudnessLike:
            return "loudness_like";
    }
    return "none";
}

const char* toString(const HybridResidualPolicy policy)
{
    switch (policy)
    {
        case HybridResidualPolicy::Unchanged:
            return "unchanged";
        case HybridResidualPolicy::TargetColored:
            return "target_colored";
        case HybridResidualPolicy::DecolorThenColor:
            return "decolor_then_color";
        case HybridResidualPolicy::Crossfade:
            return "crossfade";
        case HybridResidualPolicy::IndependentMorph:
            return "independent_morph";
    }
    return "unchanged";
}

const char* toString(const HybridTransientPolicy policy)
{
    switch (policy)
    {
        case HybridTransientPolicy::PreserveSource:
            return "preserve_source";
        case HybridTransientPolicy::TargetColor:
            return "target_color";
        case HybridTransientPolicy::CrossfadeSource:
            return "crossfade_source";
        case HybridTransientPolicy::Bypass:
            return "bypass";
    }
    return "preserve_source";
}

bool parseHybridFrequencyMapping(const std::string& text, HybridFrequencyMapping& out)
{
    if (text == "direct_hz")
    {
        out = HybridFrequencyMapping::DirectHz;
        return true;
    }
    if (text == "normalized_nyquist")
    {
        out = HybridFrequencyMapping::NormalizedNyquist;
        return true;
    }
    if (text == "perceptual_warp")
    {
        out = HybridFrequencyMapping::PerceptualWarp;
        return true;
    }
    if (text == "range_warp")
    {
        out = HybridFrequencyMapping::RangeWarp;
        return true;
    }
    if (text == "correspondence")
    {
        out = HybridFrequencyMapping::Correspondence;
        return true;
    }
    if (text == "harmonic_number")
    {
        out = HybridFrequencyMapping::HarmonicNumber;
        return true;
    }
    return false;
}

bool parseHybridNormalizationPolicy(const std::string& text, HybridNormalizationPolicy& out)
{
    if (text == "none")
    {
        out = HybridNormalizationPolicy::None;
        return true;
    }
    if (text == "frame_rms")
    {
        out = HybridNormalizationPolicy::FrameRms;
        return true;
    }
    if (text == "energy_preserving")
    {
        out = HybridNormalizationPolicy::EnergyPreserving;
        return true;
    }
    if (text == "loudness_like")
    {
        out = HybridNormalizationPolicy::LoudnessLike;
        return true;
    }
    return false;
}

bool parseHybridResidualPolicy(const std::string& text, HybridResidualPolicy& out)
{
    if (text == "unchanged")
    {
        out = HybridResidualPolicy::Unchanged;
        return true;
    }
    if (text == "target_colored")
    {
        out = HybridResidualPolicy::TargetColored;
        return true;
    }
    if (text == "decolor_then_color")
    {
        out = HybridResidualPolicy::DecolorThenColor;
        return true;
    }
    if (text == "crossfade")
    {
        out = HybridResidualPolicy::Crossfade;
        return true;
    }
    if (text == "independent_morph")
    {
        out = HybridResidualPolicy::IndependentMorph;
        return true;
    }
    return false;
}

bool parseHybridTransientPolicy(const std::string& text, HybridTransientPolicy& out)
{
    if (text == "preserve_source")
    {
        out = HybridTransientPolicy::PreserveSource;
        return true;
    }
    if (text == "target_color")
    {
        out = HybridTransientPolicy::TargetColor;
        return true;
    }
    if (text == "crossfade_source")
    {
        out = HybridTransientPolicy::CrossfadeSource;
        return true;
    }
    if (text == "bypass")
    {
        out = HybridTransientPolicy::Bypass;
        return true;
    }
    return false;
}

std::string SpectralEnvelopeTransferRecipe::serialize() const
{
    std::ostringstream os;
    os.imbue(std::locale::classic());
    os << kRecipeHeader << '\n';
    appendField(os, "version", std::to_string(version));
    appendField(os, "recipe_id", escapeString(recipeId));
    appendField(os, "source_model_id", escapeString(sourceModelId));
    appendField(os, "target_model_id", escapeString(targetModelId));
    appendField(os, "estimator_id", escapeString(estimatorId));
    appendField(os, "estimator_version", std::to_string(estimatorVersion));
    appendField(os, "envelope_order", std::to_string(envelopeOrder));
    appendField(os, "regularization", doubleToString(regularization));
    appendField(os, "penalty", toString(penalty));
    appendField(os, "warp_axis", toString(warpAxis));
    appendField(os, "source_decolor_amount", doubleToString(sourceDecolorAmount));
    appendField(os, "target_color_amount", doubleToString(targetColorAmount));
    appendField(os, "envelope_mix", doubleToString(envelopeMix));
    appendField(os, "amplitude_floor", doubleToString(amplitudeFloor));
    appendField(os, "mapping", toString(mapping));
    appendField(os, "extrapolation", extrapolation == EnvelopeExtrapolationPolicy::Clamp ? "clamp" : "reject");
    appendField(os, "partial_normalization", toString(partialNormalization));
    appendField(os, "residual_normalization", toString(residualNormalization));
    appendField(os, "residual_policy", toString(residualPolicy));
    appendField(os, "transient_policy", toString(transientPolicy));
    appendField(os, "harmonic_mapping_enabled", harmonicMappingEnabled ? "true" : "false");
    appendField(os, "harmonicity_reliable", harmonicityReliable ? "true" : "false");
    appendField(os, "source_f0_hz", doubleToString(sourceF0Hz));
    appendField(os, "target_f0_hz", doubleToString(targetF0Hz));
    appendField(os, "correspondence_count", std::to_string(correspondence.size()));
    for (std::size_t i = 0; i < correspondence.size(); ++i)
    {
        const auto prefix = "correspondence_" + std::to_string(i) + "_";
        appendField(os, (prefix + "source_hz").c_str(), doubleToString(correspondence[i].sourceFrequencyHz));
        appendField(os, (prefix + "target_hz").c_str(), doubleToString(correspondence[i].targetFrequencyHz));
        appendField(os, (prefix + "confidence").c_str(), doubleToString(correspondence[i].confidence));
    }
    os << "END\n";
    return os.str();
}

bool SpectralEnvelopeTransferRecipe::deserialize(const std::string& text,
                                                 SpectralEnvelopeTransferRecipe& out,
                                                 std::string* errorMessage)
{
    std::istringstream is(text);
    is.imbue(std::locale::classic());

    std::string line;
    if (!std::getline(is, line) || line != kRecipeHeader)
    {
        if (errorMessage)
            *errorMessage = "invalid recipe header";
        return false;
    }

    std::unordered_map<std::string, std::string> fields;
    bool sawEnd = false;
    while (std::getline(is, line))
    {
        if (line == "END")
        {
            sawEnd = true;
            break;
        }
        const auto tab = line.find('\t');
        if (tab == std::string::npos)
        {
            if (errorMessage)
                *errorMessage = "invalid field line: " + line;
            return false;
        }
        fields.emplace(line.substr(0, tab), line.substr(tab + 1));
    }
    if (!sawEnd)
    {
        if (errorMessage)
            *errorMessage = "missing END marker";
        return false;
    }

    SpectralEnvelopeTransferRecipe parsed;
    std::string value;
    if (!requireField(fields, "version", value, errorMessage) || !parseUint32(value, parsed.version))
        return false;
    if (parsed.version != SpectralEnvelopeTransferRecipe::kVersion)
    {
        if (errorMessage)
            *errorMessage = "unsupported recipe version";
        return false;
    }
    if (!requireField(fields, "recipe_id", value, errorMessage) || !unescapeString(value, parsed.recipeId))
        return false;
    if (!requireField(fields, "source_model_id", value, errorMessage) || !unescapeString(value, parsed.sourceModelId))
        return false;
    if (!requireField(fields, "target_model_id", value, errorMessage) || !unescapeString(value, parsed.targetModelId))
        return false;
    if (!requireField(fields, "estimator_id", value, errorMessage) || !unescapeString(value, parsed.estimatorId))
        return false;
    if (!requireField(fields, "estimator_version", value, errorMessage) || !parseUint32(value, parsed.estimatorVersion))
        return false;
    if (!requireField(fields, "envelope_order", value, errorMessage) || !parseInt(value, parsed.envelopeOrder))
        return false;
    if (!requireField(fields, "regularization", value, errorMessage) || !parseDouble(value, parsed.regularization))
        return false;
    if (!requireField(fields, "penalty", value, errorMessage) || !parseCepstrumPenalty(value, parsed.penalty))
        return false;
    if (!requireField(fields, "warp_axis", value, errorMessage) || !parseEnvelopeFrequencyAxis(value, parsed.warpAxis))
        return false;
    if (!requireField(fields, "source_decolor_amount", value, errorMessage) || !parseDouble(value, parsed.sourceDecolorAmount))
        return false;
    if (!requireField(fields, "target_color_amount", value, errorMessage) || !parseDouble(value, parsed.targetColorAmount))
        return false;
    if (!requireField(fields, "envelope_mix", value, errorMessage) || !parseDouble(value, parsed.envelopeMix))
        return false;
    if (!requireField(fields, "amplitude_floor", value, errorMessage) || !parseDouble(value, parsed.amplitudeFloor))
        return false;
    if (!requireField(fields, "mapping", value, errorMessage) || !parseHybridFrequencyMapping(value, parsed.mapping))
        return false;
    if (!requireField(fields, "extrapolation", value, errorMessage))
        return false;
    if (value == "clamp")
        parsed.extrapolation = EnvelopeExtrapolationPolicy::Clamp;
    else if (value == "reject")
        parsed.extrapolation = EnvelopeExtrapolationPolicy::Reject;
    else
        return false;
    if (!requireField(fields, "partial_normalization", value, errorMessage) || !parseHybridNormalizationPolicy(value, parsed.partialNormalization))
        return false;
    if (!requireField(fields, "residual_normalization", value, errorMessage) || !parseHybridNormalizationPolicy(value, parsed.residualNormalization))
        return false;
    if (!requireField(fields, "residual_policy", value, errorMessage) || !parseHybridResidualPolicy(value, parsed.residualPolicy))
        return false;
    if (!requireField(fields, "transient_policy", value, errorMessage) || !parseHybridTransientPolicy(value, parsed.transientPolicy))
        return false;
    if (!requireField(fields, "harmonic_mapping_enabled", value, errorMessage) || !parseBool(value, parsed.harmonicMappingEnabled))
        return false;
    if (!requireField(fields, "harmonicity_reliable", value, errorMessage) || !parseBool(value, parsed.harmonicityReliable))
        return false;
    if (!requireField(fields, "source_f0_hz", value, errorMessage) || !parseDouble(value, parsed.sourceF0Hz))
        return false;
    if (!requireField(fields, "target_f0_hz", value, errorMessage) || !parseDouble(value, parsed.targetF0Hz))
        return false;

    int correspondenceCount = 0;
    if (!requireField(fields, "correspondence_count", value, errorMessage) || !parseInt(value, correspondenceCount) || correspondenceCount < 0)
        return false;
    parsed.correspondence.reserve(static_cast<std::size_t>(correspondenceCount));
    for (int i = 0; i < correspondenceCount; ++i)
    {
        const auto prefix = "correspondence_" + std::to_string(i) + "_";
        HybridCorrespondencePoint point;
        if (!requireField(fields, (prefix + "source_hz").c_str(), value, errorMessage) || !parseDouble(value, point.sourceFrequencyHz))
            return false;
        if (!requireField(fields, (prefix + "target_hz").c_str(), value, errorMessage) || !parseDouble(value, point.targetFrequencyHz))
            return false;
        if (!requireField(fields, (prefix + "confidence").c_str(), value, errorMessage) || !parseDouble(value, point.confidence))
            return false;
        parsed.correspondence.push_back(point);
    }

    out = std::move(parsed);
    return true;
}

double dbToAmplitude(const double db)
{
    return std::pow(10.0, db / 20.0);
}

HybridPartialOutput applyLogDomainEnvelopeTransfer(const double sourceAmplitude,
                                                   const double frequencyHz,
                                                   const double sourceEnvelopeDb,
                                                   const double targetEnvelopeDb,
                                                   const SpectralEnvelopeTransferRecipe& recipe)
{
    HybridPartialOutput out;
    out.frequencyHz = frequencyHz;
    out.targetFrequencyHz = frequencyHz;
    out.sourceAmplitude = sourceAmplitude;
    out.sourceEnvelopeDb = sourceEnvelopeDb;
    out.targetEnvelopeDb = targetEnvelopeDb;

    if (!std::isfinite(sourceAmplitude) || !std::isfinite(frequencyHz) ||
        !std::isfinite(sourceEnvelopeDb) || !std::isfinite(targetEnvelopeDb) ||
        sourceAmplitude < 0.0 || frequencyHz <= 0.0)
    {
        return out;
    }

    const double sourceDb = amplitudeToDb(sourceAmplitude, recipe.amplitudeFloor);
    const double mix = std::clamp(recipe.envelopeMix, 0.0, 1.0);
    const double mixedEnvelopeDb = sourceEnvelopeDb + mix * (targetEnvelopeDb - sourceEnvelopeDb);
    const double outputDb = sourceDb
                            - recipe.sourceDecolorAmount * sourceEnvelopeDb
                            + recipe.targetColorAmount * mixedEnvelopeDb;

    out.sourceAmplitudeDb = sourceDb;
    out.transferGainDb = outputDb - sourceDb;
    out.outputAmplitude = dbToAmplitude(outputDb);
    out.ok = std::isfinite(out.outputAmplitude);
    return out;
}

HybridFrequencyMappingResult mapHybridTargetFrequency(const double sourceFrequencyHz,
                                                      const FrameEnvelope& sourceEnvelope,
                                                      const FrameEnvelope& targetEnvelope,
                                                      const SpectralEnvelopeTransferRecipe& recipe)
{
    HybridFrequencyMappingResult result;
    result.sourceFrequencyHz = sourceFrequencyHz;

    if (!std::isfinite(sourceFrequencyHz) || sourceFrequencyHz <= 0.0)
    {
        result.errorMessage = "invalid source frequency";
        return result;
    }

    EnvelopeFrequencyMapping envelopeMapping;
    if (toEnvelopeMapping(recipe.mapping, envelopeMapping))
    {
        EnvelopeEvaluationOptions options;
        options.mapping = envelopeMapping;
        options.extrapolation = recipe.extrapolation;
        options.sourceAxis = sourceEnvelope.frequencyAxis;
        options.targetAxis = targetEnvelope.frequencyAxis;
        options.perceptualAxis = recipe.warpAxis;

        result.ok = mapEnvelopeLookupFrequency(sourceFrequencyHz,
                                               targetEnvelope,
                                               options,
                                               result.targetFrequencyHz,
                                               result.inRange);
        if (!result.ok)
            result.errorMessage = "axis-based mapping failed";
        return result;
    }

    double mappedFrequencyHz = 0.0;
    bool inRange = false;
    switch (recipe.mapping)
    {
        case HybridFrequencyMapping::Correspondence:
            if (!mapCorrespondenceFrequency(sourceFrequencyHz,
                                            recipe.correspondence,
                                            recipe.extrapolation,
                                            mappedFrequencyHz,
                                            inRange))
            {
                result.errorMessage = "correspondence mapping failed";
                return result;
            }
            break;

        case HybridFrequencyMapping::HarmonicNumber:
            if (!recipe.harmonicMappingEnabled || !recipe.harmonicityReliable)
            {
                result.errorMessage = "harmonic mapping requires reliable harmonicity metadata";
                return result;
            }
            if (!std::isfinite(recipe.sourceF0Hz) || !std::isfinite(recipe.targetF0Hz) ||
                recipe.sourceF0Hz <= 0.0 || recipe.targetF0Hz <= 0.0)
            {
                result.errorMessage = "harmonic mapping requires positive source and target F0";
                return result;
            }
            mappedFrequencyHz = (sourceFrequencyHz / recipe.sourceF0Hz) * recipe.targetF0Hz;
            inRange = true;
            break;

        case HybridFrequencyMapping::DirectHz:
        case HybridFrequencyMapping::NormalizedNyquist:
        case HybridFrequencyMapping::PerceptualWarp:
        case HybridFrequencyMapping::RangeWarp:
            result.errorMessage = "unsupported mapping";
            return result;
    }

    result.ok = clampOrRejectToTargetAxis(mappedFrequencyHz,
                                          targetEnvelope,
                                          recipe.extrapolation,
                                          result.targetFrequencyHz,
                                          result.inRange);
    result.inRange = result.inRange && inRange;
    if (!result.ok)
        result.errorMessage = "mapped frequency outside target envelope";
    return result;
}

std::vector<HybridPartialOutput> applyEnvelopeTransferDirect(const std::vector<HybridPartialInput>& partials,
                                                            const FrameEnvelope& sourceEnvelope,
                                                            const FrameEnvelope& targetEnvelope,
                                                            const SpectralEnvelopeTransferRecipe& recipe)
{
    std::vector<HybridPartialOutput> out;
    out.reserve(partials.size());

    EnvelopeEvaluationOptions sourceEval;
    sourceEval.mapping = EnvelopeFrequencyMapping::DirectHz;
    sourceEval.extrapolation = recipe.extrapolation;
    sourceEval.targetAxis = sourceEnvelope.frequencyAxis;

    EnvelopeEvaluationOptions targetEval;
    targetEval.mapping = EnvelopeFrequencyMapping::DirectHz;
    targetEval.extrapolation = recipe.extrapolation;
    targetEval.targetAxis = targetEnvelope.frequencyAxis;

    for (const auto& partial : partials)
    {
        const auto sourceSample = evaluateEnvelopeAtFrequency(sourceEnvelope, partial.frequencyHz, sourceEval);
        const auto targetSample = evaluateEnvelopeAtFrequency(targetEnvelope, partial.frequencyHz, targetEval);
        if (!sourceSample.ok || !targetSample.ok)
        {
            HybridPartialOutput failed;
            failed.frequencyHz = partial.frequencyHz;
            failed.sourceAmplitude = partial.amplitude;
            out.push_back(failed);
            continue;
        }

        out.push_back(applyLogDomainEnvelopeTransfer(partial.amplitude,
                                                     partial.frequencyHz,
                                                     sourceSample.valueDb,
                                                     targetSample.valueDb,
                                                     recipe));
    }

    return out;
}

std::vector<HybridPartialOutput> applyEnvelopeTransferMapped(const std::vector<HybridPartialInput>& partials,
                                                            const FrameEnvelope& sourceEnvelope,
                                                            const FrameEnvelope& targetEnvelope,
                                                            const SpectralEnvelopeTransferRecipe& recipe)
{
    std::vector<HybridPartialOutput> out;
    out.reserve(partials.size());

    EnvelopeEvaluationOptions sourceEval;
    sourceEval.mapping = EnvelopeFrequencyMapping::DirectHz;
    sourceEval.extrapolation = recipe.extrapolation;
    sourceEval.targetAxis = sourceEnvelope.frequencyAxis;

    EnvelopeEvaluationOptions targetEval;
    targetEval.mapping = EnvelopeFrequencyMapping::DirectHz;
    targetEval.extrapolation = recipe.extrapolation;
    targetEval.targetAxis = targetEnvelope.frequencyAxis;

    for (const auto& partial : partials)
    {
        const auto mapping = mapHybridTargetFrequency(partial.frequencyHz, sourceEnvelope, targetEnvelope, recipe);
        const auto sourceSample = evaluateEnvelopeAtFrequency(sourceEnvelope, partial.frequencyHz, sourceEval);
        if (!mapping.ok || !sourceSample.ok)
        {
            HybridPartialOutput failed;
            failed.frequencyHz = partial.frequencyHz;
            failed.targetFrequencyHz = mapping.targetFrequencyHz;
            failed.sourceAmplitude = partial.amplitude;
            out.push_back(failed);
            continue;
        }

        const auto targetSample = evaluateEnvelopeAtFrequency(targetEnvelope, mapping.targetFrequencyHz, targetEval);
        if (!targetSample.ok)
        {
            HybridPartialOutput failed;
            failed.frequencyHz = partial.frequencyHz;
            failed.targetFrequencyHz = mapping.targetFrequencyHz;
            failed.sourceAmplitude = partial.amplitude;
            out.push_back(failed);
            continue;
        }

        auto transferred = applyLogDomainEnvelopeTransfer(partial.amplitude,
                                                          partial.frequencyHz,
                                                          sourceSample.valueDb,
                                                          targetSample.valueDb,
                                                          recipe);
        transferred.targetFrequencyHz = mapping.targetFrequencyHz;
        out.push_back(transferred);
    }

    return out;
}

HybridNormalizationStats applyHybridGainNormalization(std::vector<HybridPartialOutput>& outputs,
                                                      const HybridNormalizationPolicy policy,
                                                      const double amplitudeFloor)
{
    HybridNormalizationStats stats;
    accumulateNormalizationStats(outputs, stats);
    if (stats.processedCount == 0)
        return stats;

    stats.ok = true;
    if (policy == HybridNormalizationPolicy::None)
    {
        finishNormalizationStats(outputs, stats);
        return stats;
    }

    const double floor = std::max(amplitudeFloor, std::numeric_limits<double>::min());
    double scale = 1.0;
    switch (policy)
    {
        case HybridNormalizationPolicy::None:
            break;

        case HybridNormalizationPolicy::FrameRms:
            if (stats.outputRmsBefore <= floor)
            {
                stats.ok = false;
                finishNormalizationStats(outputs, stats);
                return stats;
            }
            scale = stats.sourceRms <= floor ? 0.0 : stats.sourceRms / stats.outputRmsBefore;
            break;

        case HybridNormalizationPolicy::EnergyPreserving:
            if (stats.outputEnergyBefore <= floor)
            {
                stats.ok = false;
                finishNormalizationStats(outputs, stats);
                return stats;
            }
            scale = stats.sourceEnergy <= floor ? 0.0 : std::sqrt(stats.sourceEnergy / stats.outputEnergyBefore);
            break;

        case HybridNormalizationPolicy::LoudnessLike:
        {
            constexpr double kPower = 0.6;
            const double sourceMean = loudnessLikeMean(outputs, true);
            const double outputMean = loudnessLikeMean(outputs, false);
            if (outputMean <= floor)
            {
                stats.ok = false;
                finishNormalizationStats(outputs, stats);
                return stats;
            }
            scale = sourceMean <= floor ? 0.0 : std::pow(sourceMean / outputMean, 1.0 / kPower);
            break;
        }
    }

    if (!std::isfinite(scale) || scale < 0.0)
    {
        stats.ok = false;
        finishNormalizationStats(outputs, stats);
        return stats;
    }

    stats.gainDb = amplitudeToDb(scale, floor);
    for (auto& output : outputs)
    {
        if (!output.ok || !std::isfinite(output.outputAmplitude) || output.outputAmplitude < 0.0)
            continue;
        output.outputAmplitude *= scale;
        output.transferGainDb += stats.gainDb;
    }

    stats.applied = true;
    finishNormalizationStats(outputs, stats);
    return stats;
}

} // namespace ats
