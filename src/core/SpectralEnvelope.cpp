#include "SpectralEnvelope.h"

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

constexpr const char* kHeader = "ATS_FRAME_ENVELOPE_V1";

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

bool parseInt64(const std::string& text, std::uint64_t& out)
{
    const char* begin = text.data();
    const char* end = text.data() + text.size();
    const auto result = std::from_chars(begin, end, out);
    return result.ec == std::errc() && result.ptr == end;
}

bool parseInt32(const std::string& text, std::uint32_t& out)
{
    const char* begin = text.data();
    const char* end = text.data() + text.size();
    const auto result = std::from_chars(begin, end, out);
    return result.ec == std::errc() && result.ptr == end;
}

bool parseSignedInt(const std::string& text, int& out)
{
    const char* begin = text.data();
    const char* end = text.data() + text.size();
    const auto result = std::from_chars(begin, end, out);
    return result.ec == std::errc() && result.ptr == end;
}

bool parseDouble(const std::string& text, double& out)
{
    std::istringstream is(text);
    is.imbue(std::locale::classic());
    is >> out;
    return !is.fail() && is.eof();
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

const char* toString(const EnvelopeValueStorage storage)
{
    switch (storage)
    {
        case EnvelopeValueStorage::Coefficients:
            return "coefficients";
        case EnvelopeValueStorage::Samples:
            return "samples";
    }
    return "coefficients";
}

const char* toString(const EnvelopeFrequencyAxis axis)
{
    switch (axis)
    {
        case EnvelopeFrequencyAxis::LinearHz:
            return "linear_hz";
        case EnvelopeFrequencyAxis::Bark:
            return "bark";
        case EnvelopeFrequencyAxis::Mel:
            return "mel";
        case EnvelopeFrequencyAxis::Erb:
            return "erb";
        case EnvelopeFrequencyAxis::NormalizedNyquist:
            return "normalized_nyquist";
        case EnvelopeFrequencyAxis::CustomNormalized:
            return "custom_normalized";
    }
    return "linear_hz";
}

const char* toString(const EnvelopeFallbackReason reason)
{
    switch (reason)
    {
        case EnvelopeFallbackReason::None:
            return "none";
        case EnvelopeFallbackReason::TooFewPoints:
            return "too_few_points";
        case EnvelopeFallbackReason::IllConditioned:
            return "ill_conditioned";
        case EnvelopeFallbackReason::OutOfRange:
            return "out_of_range";
        case EnvelopeFallbackReason::Unsupported:
            return "unsupported";
        case EnvelopeFallbackReason::Unknown:
            return "unknown";
    }
    return "unknown";
}

bool parseEnvelopeValueStorage(const std::string& text, EnvelopeValueStorage& out)
{
    if (text == "coefficients")
    {
        out = EnvelopeValueStorage::Coefficients;
        return true;
    }
    if (text == "samples")
    {
        out = EnvelopeValueStorage::Samples;
        return true;
    }
    return false;
}

bool parseEnvelopeFrequencyAxis(const std::string& text, EnvelopeFrequencyAxis& out)
{
    if (text == "linear_hz")
    {
        out = EnvelopeFrequencyAxis::LinearHz;
        return true;
    }
    if (text == "bark")
    {
        out = EnvelopeFrequencyAxis::Bark;
        return true;
    }
    if (text == "mel")
    {
        out = EnvelopeFrequencyAxis::Mel;
        return true;
    }
    if (text == "erb")
    {
        out = EnvelopeFrequencyAxis::Erb;
        return true;
    }
    if (text == "normalized_nyquist")
    {
        out = EnvelopeFrequencyAxis::NormalizedNyquist;
        return true;
    }
    if (text == "custom_normalized")
    {
        out = EnvelopeFrequencyAxis::CustomNormalized;
        return true;
    }
    return false;
}

bool parseEnvelopeFallbackReason(const std::string& text, EnvelopeFallbackReason& out)
{
    if (text == "none")
    {
        out = EnvelopeFallbackReason::None;
        return true;
    }
    if (text == "too_few_points")
    {
        out = EnvelopeFallbackReason::TooFewPoints;
        return true;
    }
    if (text == "ill_conditioned")
    {
        out = EnvelopeFallbackReason::IllConditioned;
        return true;
    }
    if (text == "out_of_range")
    {
        out = EnvelopeFallbackReason::OutOfRange;
        return true;
    }
    if (text == "unsupported")
    {
        out = EnvelopeFallbackReason::Unsupported;
        return true;
    }
    if (text == "unknown")
    {
        out = EnvelopeFallbackReason::Unknown;
        return true;
    }
    return false;
}

bool EnvelopeDiagnostics::hasWarning(const EnvelopeWarningFlags flag) const
{
    return (warningFlags & static_cast<std::uint32_t>(flag)) != 0u;
}

void EnvelopeDiagnostics::addWarning(const EnvelopeWarningFlags flag, std::string message)
{
    warningFlags |= static_cast<std::uint32_t>(flag);
    warnings.push_back(std::move(message));
}

std::string FrameEnvelope::serialize() const
{
    std::ostringstream os;
    os.imbue(std::locale::classic());
    os << kHeader << '\n';
    appendField(os, "estimator_id", escapeString(estimatorId));
    appendField(os, "estimator_version", std::to_string(estimatorVersion));
    appendField(os, "source_model_id", escapeString(sourceModelId));
    appendField(os, "frame_index", std::to_string(frameIndex));
    appendField(os, "sample_rate", doubleToString(sampleRate));
    appendField(os, "axis_type", toString(frequencyAxis.type));
    appendField(os, "axis_min_hz", doubleToString(frequencyAxis.minFrequencyHz));
    appendField(os, "axis_max_hz", doubleToString(frequencyAxis.maxFrequencyHz));
    appendField(os, "axis_sample_rate", doubleToString(frequencyAxis.sampleRate));
    appendField(os, "axis_custom_min", doubleToString(frequencyAxis.customMin));
    appendField(os, "axis_custom_max", doubleToString(frequencyAxis.customMax));
    appendField(os, "envelope_order", std::to_string(envelopeOrder));
    appendField(os, "regularization", doubleToString(regularization));
    appendField(os, "penalty_type", escapeString(penaltyType));
    appendField(os, "storage", toString(storage));
    appendField(os, "value_count", std::to_string(values.size()));
    for (std::size_t i = 0; i < values.size(); ++i)
        appendField(os, ("value_" + std::to_string(i)).c_str(), doubleToString(values[i]));

    appendField(os, "diag_fit_point_count", std::to_string(diagnostics.fitPointCount));
    appendField(os, "diag_weighted_error", doubleToString(diagnostics.weightedError));
    appendField(os, "diag_condition_estimate", doubleToString(diagnostics.conditionEstimate));
    appendField(os, "diag_clipped_or_floored_count", std::to_string(diagnostics.clippedOrFlooredCount));
    appendField(os, "diag_fallback_reason", toString(diagnostics.fallbackReason));
    appendField(os, "diag_warning_flags", std::to_string(diagnostics.warningFlags));
    appendField(os, "diag_warning_count", std::to_string(diagnostics.warnings.size()));
    for (std::size_t i = 0; i < diagnostics.warnings.size(); ++i)
        appendField(os, ("diag_warning_" + std::to_string(i)).c_str(), escapeString(diagnostics.warnings[i]));

    appendField(os, "uncertainty_valid", uncertainty.valid ? "true" : "false");
    appendField(os, "uncertainty_noise_variance", doubleToString(uncertainty.noiseVariance));
    appendField(os, "uncertainty_effective_df", doubleToString(uncertainty.effectiveDegreesOfFreedom));
    appendField(os, "uncertainty_coefficient_count", std::to_string(uncertainty.coefficientCount));
    appendField(os, "uncertainty_cholesky_count", std::to_string(uncertainty.choleskyFactor.size()));
    for (std::size_t i = 0; i < uncertainty.choleskyFactor.size(); ++i)
        appendField(os, ("uncertainty_cholesky_" + std::to_string(i)).c_str(), doubleToString(uncertainty.choleskyFactor[i]));

    appendField(os, "smoothing_group", std::to_string(smoothingGroup));
    appendField(os, "smoothing_amount", doubleToString(smoothingAmount));
    os << "END\n";
    return os.str();
}

bool FrameEnvelope::deserialize(const std::string& text, FrameEnvelope& out, std::string* errorMessage)
{
    std::istringstream is(text);
    is.imbue(std::locale::classic());

    std::string line;
    if (!std::getline(is, line) || line != kHeader)
    {
        if (errorMessage)
            *errorMessage = "invalid envelope header";
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

    FrameEnvelope parsed;
    std::string value;

    if (!requireField(fields, "estimator_id", value, errorMessage) || !unescapeString(value, parsed.estimatorId))
        return false;
    if (!requireField(fields, "estimator_version", value, errorMessage) || !parseInt32(value, parsed.estimatorVersion))
        return false;
    if (!requireField(fields, "source_model_id", value, errorMessage) || !unescapeString(value, parsed.sourceModelId))
        return false;
    if (!requireField(fields, "frame_index", value, errorMessage) || !parseInt64(value, parsed.frameIndex))
        return false;
    if (!requireField(fields, "sample_rate", value, errorMessage) || !parseDouble(value, parsed.sampleRate))
        return false;
    if (!requireField(fields, "axis_type", value, errorMessage) || !parseEnvelopeFrequencyAxis(value, parsed.frequencyAxis.type))
        return false;
    if (!requireField(fields, "axis_min_hz", value, errorMessage) || !parseDouble(value, parsed.frequencyAxis.minFrequencyHz))
        return false;
    if (!requireField(fields, "axis_max_hz", value, errorMessage) || !parseDouble(value, parsed.frequencyAxis.maxFrequencyHz))
        return false;
    if (!requireField(fields, "axis_sample_rate", value, errorMessage) || !parseDouble(value, parsed.frequencyAxis.sampleRate))
        return false;
    if (!requireField(fields, "axis_custom_min", value, errorMessage) || !parseDouble(value, parsed.frequencyAxis.customMin))
        return false;
    if (!requireField(fields, "axis_custom_max", value, errorMessage) || !parseDouble(value, parsed.frequencyAxis.customMax))
        return false;
    if (!requireField(fields, "envelope_order", value, errorMessage) || !parseSignedInt(value, parsed.envelopeOrder))
        return false;
    if (!requireField(fields, "regularization", value, errorMessage) || !parseDouble(value, parsed.regularization))
        return false;
    if (!requireField(fields, "penalty_type", value, errorMessage) || !unescapeString(value, parsed.penaltyType))
        return false;
    if (!requireField(fields, "storage", value, errorMessage) || !parseEnvelopeValueStorage(value, parsed.storage))
        return false;

    std::uint64_t count = 0;
    if (!requireField(fields, "value_count", value, errorMessage) || !parseInt64(value, count))
        return false;
    parsed.values.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t i = 0; i < count; ++i)
    {
        const std::string key = "value_" + std::to_string(i);
        double sample = 0.0;
        if (!requireField(fields, key.c_str(), value, errorMessage) || !parseDouble(value, sample))
            return false;
        parsed.values.push_back(sample);
    }

    if (!requireField(fields, "diag_fit_point_count", value, errorMessage) || !parseSignedInt(value, parsed.diagnostics.fitPointCount))
        return false;
    if (!requireField(fields, "diag_weighted_error", value, errorMessage) || !parseDouble(value, parsed.diagnostics.weightedError))
        return false;
    if (!requireField(fields, "diag_condition_estimate", value, errorMessage) || !parseDouble(value, parsed.diagnostics.conditionEstimate))
        return false;
    if (!requireField(fields, "diag_clipped_or_floored_count", value, errorMessage) || !parseSignedInt(value, parsed.diagnostics.clippedOrFlooredCount))
        return false;
    if (!requireField(fields, "diag_fallback_reason", value, errorMessage) || !parseEnvelopeFallbackReason(value, parsed.diagnostics.fallbackReason))
        return false;
    if (!requireField(fields, "diag_warning_flags", value, errorMessage) || !parseInt32(value, parsed.diagnostics.warningFlags))
        return false;

    std::uint64_t warningCount = 0;
    if (!requireField(fields, "diag_warning_count", value, errorMessage) || !parseInt64(value, warningCount))
        return false;
    parsed.diagnostics.warnings.reserve(static_cast<std::size_t>(warningCount));
    for (std::uint64_t i = 0; i < warningCount; ++i)
    {
        const std::string key = "diag_warning_" + std::to_string(i);
        std::string message;
        if (!requireField(fields, key.c_str(), value, errorMessage) || !unescapeString(value, message))
            return false;
        parsed.diagnostics.warnings.push_back(std::move(message));
    }

    if (requireField(fields, "uncertainty_valid", value, nullptr))
    {
        parsed.uncertainty.valid = (value == "true");
        if (!requireField(fields, "uncertainty_noise_variance", value, errorMessage) || !parseDouble(value, parsed.uncertainty.noiseVariance))
            return false;
        if (!requireField(fields, "uncertainty_effective_df", value, errorMessage) || !parseDouble(value, parsed.uncertainty.effectiveDegreesOfFreedom))
            return false;
        if (!requireField(fields, "uncertainty_coefficient_count", value, errorMessage) || !parseSignedInt(value, parsed.uncertainty.coefficientCount))
            return false;

        std::uint64_t choleskyCount = 0;
        if (!requireField(fields, "uncertainty_cholesky_count", value, errorMessage) || !parseInt64(value, choleskyCount))
            return false;
        parsed.uncertainty.choleskyFactor.reserve(static_cast<std::size_t>(choleskyCount));
        for (std::uint64_t i = 0; i < choleskyCount; ++i)
        {
            const std::string key = "uncertainty_cholesky_" + std::to_string(i);
            double sample = 0.0;
            if (!requireField(fields, key.c_str(), value, errorMessage) || !parseDouble(value, sample))
                return false;
            parsed.uncertainty.choleskyFactor.push_back(sample);
        }
    }

    if (!requireField(fields, "smoothing_group", value, errorMessage) || !parseInt64(value, parsed.smoothingGroup))
        return false;
    if (!requireField(fields, "smoothing_amount", value, errorMessage) || !parseDouble(value, parsed.smoothingAmount))
        return false;

    out = std::move(parsed);
    return true;
}

} // namespace ats
