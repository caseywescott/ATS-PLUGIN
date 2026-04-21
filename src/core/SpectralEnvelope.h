#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ats
{

enum class EnvelopeValueStorage
{
    Coefficients,
    Samples
};

enum class EnvelopeFrequencyAxis
{
    LinearHz,
    Bark,
    Mel,
    Erb,
    NormalizedNyquist,
    CustomNormalized
};

enum class EnvelopeFallbackReason
{
    None,
    TooFewPoints,
    IllConditioned,
    OutOfRange,
    Unsupported,
    Unknown
};

enum EnvelopeWarningFlags : std::uint32_t
{
    EnvelopeWarningNone = 0,
    EnvelopeWarningAmplitudeFloor = 1u << 0u,
    EnvelopeWarningClipped = 1u << 1u,
    EnvelopeWarningFallback = 1u << 2u,
    EnvelopeWarningSparseFrame = 1u << 3u,
    EnvelopeWarningOutOfRange = 1u << 4u
};

struct EnvelopeAxisSpec
{
    EnvelopeFrequencyAxis type = EnvelopeFrequencyAxis::LinearHz;
    double minFrequencyHz = 0.0;
    double maxFrequencyHz = 0.0;
    double sampleRate = 0.0;
    double customMin = 0.0;
    double customMax = 1.0;
};

struct EnvelopeDiagnostics
{
    int fitPointCount = 0;
    double weightedError = 0.0;
    double conditionEstimate = 0.0;
    int clippedOrFlooredCount = 0;
    EnvelopeFallbackReason fallbackReason = EnvelopeFallbackReason::None;
    std::uint32_t warningFlags = EnvelopeWarningNone;
    std::vector<std::string> warnings;

    [[nodiscard]] bool hasWarning(EnvelopeWarningFlags flag) const;
    void addWarning(EnvelopeWarningFlags flag, std::string message);
};

struct FrameEnvelope
{
    std::string estimatorId;
    std::uint32_t estimatorVersion = 0;
    std::string sourceModelId;
    std::uint64_t frameIndex = 0;
    double sampleRate = 0.0;
    EnvelopeAxisSpec frequencyAxis;
    int envelopeOrder = 0;
    double regularization = 0.0;
    std::string penaltyType;
    EnvelopeValueStorage storage = EnvelopeValueStorage::Coefficients;
    std::vector<double> values;
    EnvelopeDiagnostics diagnostics;
    std::uint64_t smoothingGroup = 0;
    double smoothingAmount = 0.0;

    [[nodiscard]] std::string serialize() const;
    static bool deserialize(const std::string& text, FrameEnvelope& out, std::string* errorMessage = nullptr);
};

[[nodiscard]] const char* toString(EnvelopeValueStorage storage);
[[nodiscard]] const char* toString(EnvelopeFrequencyAxis axis);
[[nodiscard]] const char* toString(EnvelopeFallbackReason reason);

bool parseEnvelopeValueStorage(const std::string& text, EnvelopeValueStorage& out);
bool parseEnvelopeFrequencyAxis(const std::string& text, EnvelopeFrequencyAxis& out);
bool parseEnvelopeFallbackReason(const std::string& text, EnvelopeFallbackReason& out);

} // namespace ats
