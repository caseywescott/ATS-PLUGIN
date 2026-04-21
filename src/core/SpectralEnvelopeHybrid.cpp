#include "SpectralEnvelopeHybrid.h"

#include <charconv>
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

} // namespace ats
