#include "ParameterRegistry.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdlib>

namespace ats
{
namespace
{
using D = ParameterDescriptor;

bool hasPrefix(std::string_view value, std::string_view prefix) noexcept
{
    return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
}

constexpr std::array<std::string_view, 2> kBarkMel { "Bark", "Mel" };
constexpr std::array<std::string_view, 4> kCorrespondence { "Frequency rank", "Greedy nearest", "Assignment matrix", "Optimal transport" };
constexpr std::array<std::string_view, 3> kQuality { "Max", "Default", "Economy" };
constexpr std::array<std::string_view, 5> kPolicy { "Fade out", "Fade in", "Hold", "Merge residual", "Discard" };
constexpr std::array<std::string_view, 2> kLogBark { "Log frequency", "Bark coordinate" };
constexpr std::array<std::string_view, 2> kPowerDb { "Power", "Decibels" };
constexpr std::array<std::string_view, 5> kPhase { "Oscillator continuation", "Source A donor", "Source B donor", "Relative cluster", "Direct blend" };
constexpr std::array<std::string_view, 4> kTransient { "Source A attack", "Source B attack", "Crossfade attacks", "Sustain after attack" };
constexpr std::array<std::string_view, 3> kSmearTargets { "Partials", "Residual", "Both" };
constexpr std::array<std::string_view, 3> kSmearScope { "All", "Selection", "Sensitivity" };
constexpr std::array<std::string_view, 4> kShape { "Linear", "Stretch", "Compress", "Harmonic" };
constexpr std::array<std::string_view, 2> kClassic { "Classic combined", "Layer mix" };
constexpr std::array<std::string_view, 4> kAnalysisType { "1 amp+freq", "2 +phase", "3 +noise", "4 full" };
constexpr std::array<std::string_view, 4> kWindow { "Blackman", "Blackman-Harris", "Hanning", "Von Hann" };

constexpr D f(std::string_view id, std::string_view name, float lo, float hi, float def, float interval = 0.0001f, float skew = 1.0f)
{ return { id, name, ParameterType::floatValue, lo, hi, def, interval, skew }; }
constexpr D i(std::string_view id, std::string_view name, float lo, float hi, float def)
{ return { id, name, ParameterType::integer, lo, hi, def, 1.0f, 1.0f }; }
constexpr D b(std::string_view id, std::string_view name, bool def)
{ return { id, name, ParameterType::boolean, 0.0f, 1.0f, def ? 1.0f : 0.0f, 1.0f, 1.0f }; }
template <std::size_t N>
constexpr D c(std::string_view id, std::string_view name, const std::array<std::string_view, N>& values, std::uint16_t def)
{ return { id, name, ParameterType::choice, 0.0f, static_cast<float>(N - 1), static_cast<float>(def), 1.0f, 1.0f, values.data(), static_cast<std::uint16_t>(N) }; }

std::vector<D> build()
{
    std::vector<D> d;
    d.reserve(81);
    d.insert(d.end(), {
        f("partGain", "Partials gain", 0, 4, 1, 0.0001f, 0.35f),
        f("resGain", "Residual gain", 0, 4, 1, 0.0001f, 0.35f),
        f("morphAmount", "Morph amount", 0, 1, 0),
        f("morphPerformanceMacro", "Morph performance macro", 0, 1, 0.5f),
        f("morphSmoothingMs", "Morph smoothing (ms)", 0, 250, 40, 0.01f, 0.35f),
        c("morphGridType", "Morph grid type", kBarkMel, 0),
        c("morphCorrespondenceStrategy", "Morph correspondence strategy", kCorrespondence, 3),
        c("morphQuality", "Morph quality", kQuality, 1),
        c("morphUnmatchedSourcePolicy", "Source-only partial policy", kPolicy, 0),
        c("morphUnmatchedTargetPolicy", "Target-only partial policy", kPolicy, 1),
        b("morphDiscardOptIn", "Enable discard policy", false),
        c("morphFrequencyInterpolationDomain", "Frequency interpolation domain", kLogBark, 0),
        c("morphAmplitudeInterpolationDomain", "Amplitude interpolation domain", kPowerDb, 0),
        c("morphPhaseStrategy", "Morph phase strategy", kPhase, 0),
        c("morphTransientPolicy", "Morph transient policy", kTransient, 0),
        f("smearnovBlurMs", "Smearnov blur (ms)", 0, 120, 0, 0.01f, 0.45f),
        f("smearnovSharpenAmount", "Smearnov sharpen", 0, 4, 0, 0.0001f, 0.5f),
        f("smearnovExaggerateDb", "Smearnov exaggerate (dB)", -12, 12, 0, 0.01f),
        f("smearnovMix", "Smearnov mix (%)", 0, 100, 100, 0.1f),
        f("smearnovMaxDb", "Smearnov max gain (dB)", 0, 24, 12, 0.01f),
        c("smearnovTargets", "Smearnov targets", kSmearTargets, 2),
        c("smearnovScope", "Smearnov scope", kSmearScope, 0),
        f("smearnovWeightDetFlux", "Smearnov deterministic flux weight", 0, 4, 1),
        f("smearnovWeightResFlux", "Smearnov residual flux weight", 0, 4, 0.8f),
        f("smearnovWeightHfcFlux", "Smearnov HFC flux weight", 0, 4, 0.6f),
        f("smearnovWeightBirth", "Smearnov birth weight", 0, 4, 0.7f),
        f("smearnovWeightPhaseDev", "Smearnov phase-deviation weight", 0, 4, 0.4f),
        f("smearnovThresholdLambda", "Smearnov threshold lambda", 0, 24, 0, 0.001f, 0.45f),
        f("smearnovThresholdAutoScale", "Smearnov auto-threshold scale", 0, 8, 1.5f, 0.001f),
        i("smearnovPeakWindowFrames", "Smearnov peak window (frames)", 1, 64, 6),
        i("smearnovMinGapFrames", "Smearnov min gap override (frames)", 0, 512, 0),
        f("smearnovPreMarginMs", "Smearnov pre-margin (ms)", 0, 50, 5, 0.01f, 0.5f),
        i("smearnovMaxAttackFrames", "Smearnov max attack (frames)", 0, 1024, 0),
        b("smearnovUsePhaseDeviation", "Smearnov use phase deviation", false),
        b("smearnovUseRpcaFallback", "Smearnov RPCA fallback", false),
        f("timbreBrightnessDb", "Brightness (dB)", -12, 12, 0, 0.01f),
        f("timbreTiltDbPerOct", "Tilt (dB/oct)", -6, 6, 0, 0.01f),
        f("timbreNoiseBalance", "Tonal/Noise balance", 0, 1, 0.5f, 0.001f),
    });

    constexpr std::array<std::string_view, 6> laneNames { "Morph lane Frequency", "Morph lane Amplitude", "Morph lane Residual", "Morph lane Time", "Morph lane Transient", "Morph lane Phase" };
    constexpr std::array<std::string_view, 6> linkNames { "Link Frequency lane", "Link Amplitude lane", "Link Residual lane", "Link Time lane", "Link Transient lane", "Link Phase lane" };
    constexpr std::array<std::string_view, 6> amounts { "morphLaneFrequencyAmount", "morphLaneAmplitudeAmount", "morphLaneResidualAmount", "morphLaneTimeAmount", "morphLaneTransientAmount", "morphLanePhaseAmount" };
    constexpr std::array<std::string_view, 6> links { "morphLaneFrequencyLinked", "morphLaneAmplitudeLinked", "morphLaneResidualLinked", "morphLaneTimeLinked", "morphLaneTransientLinked", "morphLanePhaseLinked" };
    for (std::size_t n = 0; n < laneNames.size(); ++n)
    {
        d.push_back(f(amounts[n], laneNames[n], 0, 1, 0));
        d.push_back(b(links[n], linkNames[n], true));
    }

    d.insert(d.end(), {
        f("freq", "Frequency factor", 0.25f, 4, 1, 0.0001f, 0.35f),
        c("freqShapeMode", "Frequency shaping mode", kShape, 0),
        f("freqShapeAmount", "Frequency shaping amount", 0.25f, 4, 1, 0.0001f, 0.35f),
        f("freqShapePivotHz", "Frequency shaping pivot (Hz)", 0, 4000, 0, 0.01f, 0.45f),
        f("timeStretch", "Time factor", 0.25f, 4, 1, 0.0001f, 0.35f),
        f("synStart", "Synth start (s)", 0, 600, 0, 0.001f, 0.4f),
        f("synDur", "Synth duration (s, 0=full)", 0, 600, 0, 0.001f, 0.4f),
        b("usePhase", "Use partial phases", false),
        b("useAllPartials", "Use all partials", true),
        b("residualOnly", "Residual only (mute partials)", false),
        c("classicPreviewMode", "Both playback mode", kClassic, 0),
        f("outputTrim", "Output trim", 0, 2, 1),
        c("analysisType", "Analysis type", kAnalysisType, 3),
        f("analysisStart", "Analysis start (s)", 0, 600, 0, 0.001f, 0.4f),
        f("analysisDuration", "Analysis duration (s, 0=full)", 0, 600, 0, 0.001f, 0.4f),
        f("analysisLowestFreq", "Lowest frequency (Hz)", 1, 2000, 20, 0.01f, 0.35f),
        f("analysisHighestFreq", "Highest frequency (Hz)", 20, 24000, 20000, 0.01f, 0.35f),
        f("analysisFreqDev", "Frequency deviation", 0.001f, 0.99f, 0.1f, 0.0001f, 0.5f),
        i("analysisWinCycles", "Window cycles", 1, 8, 4),
        c("analysisWinType", "Window type", kWindow, 1),
        f("analysisHopSize", "Hop size ratio", 0.01f, 1, 0.25f, 0.0001f, 0.5f),
        f("analysisLowestMag", "Peak threshold (dB)", -120, 0, -60, 0.01f),
        i("analysisTrackLen", "Track memory (frames)", 1, 64, 3),
        i("analysisMinSegLen", "Min segment length (frames)", 1, 128, 3),
        i("analysisMinGapLen", "Min gap length (frames)", 0, 128, 3),
        f("analysisSmrThreshold", "SMR threshold (dB)", 0, 99.999f, 30, 0.01f),
        f("analysisMinSegSmr", "Min segment SMR (dB)", 0, 99.999f, 60, 0.01f),
        f("analysisLastPeakContribution", "Last peak tracking weight", 0, 1, 0),
        f("analysisSmrContribution", "SMR tracking weight", 0, 1, 0),
        b("analysisMultiRes", "Analysis multi-resolution", false),
        b("useIfftSynth", "Use IFFT synthesis", false)
    });
    return d;
}
}

float ParameterDescriptor::clamp(float value) const noexcept
{ return std::clamp(value, minimum, maximum); }

float ParameterDescriptor::normalize(float value) const noexcept
{
    if (maximum <= minimum) return 0.0f;
    const float linear = (clamp(value) - minimum) / (maximum - minimum);
    return skew == 1.0f ? linear : std::pow(linear, 1.0f / skew);
}

float ParameterDescriptor::denormalize(float normalized) const noexcept
{
    const float n = std::clamp(normalized, 0.0f, 1.0f);
    const float linear = skew == 1.0f ? n : std::pow(n, skew);
    return minimum + linear * (maximum - minimum);
}

float ParameterDescriptor::snap(float value) const noexcept
{
    const float clamped = clamp(value);
    if (type == ParameterType::boolean || type == ParameterType::choice || type == ParameterType::integer)
        return std::round(clamped);
    if (interval <= 0.0f) return clamped;
    return clamp(minimum + std::round((clamped - minimum) / interval) * interval);
}

std::string ParameterDescriptor::text(float value) const
{
    const auto snapped = snap(value);
    if (type == ParameterType::choice && choices != nullptr && snapped >= 0 && snapped < choiceCount)
        return std::string(choices[static_cast<std::size_t>(snapped)]);
    if (type == ParameterType::boolean)
        return snapped >= 0.5f ? "On" : "Off";
    char buffer[32] {};
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), snapped, std::chars_format::general, 7);
    return result.ec == std::errc{} ? std::string(buffer, result.ptr) : std::string {};
}

bool ParameterDescriptor::fromText(std::string_view value, float& result) const
{
    if (type == ParameterType::choice && choices != nullptr)
        for (std::uint16_t i = 0; i < choiceCount; ++i)
            if (choices[i] == value) { result = static_cast<float>(i); return true; }
    if (type == ParameterType::boolean)
    {
        if (value == "On" || value == "true" || value == "1") { result = 1; return true; }
        if (value == "Off" || value == "false" || value == "0") { result = 0; return true; }
        return false;
    }
    char* end = nullptr;
    const std::string copy(value);
    const float parsed = std::strtof(copy.c_str(), &end);
    if (end == copy.c_str() || *end != '\0' || !std::isfinite(parsed)) return false;
    result = snap(parsed);
    return true;
}

const std::vector<ParameterDescriptor>& ParameterRegistry::descriptors()
{
    static const auto table = [] {
        auto values = build();
        for (std::size_t index = 0; index < values.size(); ++index)
        {
            auto& descriptor = values[index];
            descriptor.denseIndex = static_cast<std::uint16_t>(index);
            // The dense index is deliberately also the host numeric ID. It is
            // deterministic, collision-free, and keeps the durable string ID
            // as the actual ABI. Future parameters append after this table.
            descriptor.hostNumericId = static_cast<std::uint32_t>(index + 1);

            if (hasPrefix(descriptor.id, "smearnov"))
                descriptor.group = "Smearnov";
            else if (hasPrefix(descriptor.id, "morphLane") || hasPrefix(descriptor.id, "timbre"))
                descriptor.group = "Timbre / Morph lanes";
            else if (hasPrefix(descriptor.id, "analysis"))
                descriptor.group = "Analysis";
            else if (descriptor.id == "freq" || hasPrefix(descriptor.id, "freqShape") ||
                     descriptor.id == "timeStretch" || descriptor.id == "synStart" ||
                     descriptor.id == "synDur" || descriptor.id == "usePhase" ||
                     descriptor.id == "useAllPartials" || descriptor.id == "residualOnly" ||
                     descriptor.id == "useIfftSynth")
                descriptor.group = "Synthesis";
            else
                descriptor.group = "Output / Morph";

            if (descriptor.id == "partGain" || descriptor.id == "resGain" || descriptor.id == "outputTrim")
            {
                descriptor.smoothing = ParameterSmoothing::onePole20Ms;
                descriptor.updatePolicy = ParameterUpdatePolicy::realtime;
            }
        }
        return values;
    }();
    return table;
}

const ParameterDescriptor* ParameterRegistry::find(std::string_view id) noexcept
{
    const auto& table = descriptors();
    const auto it = std::find_if(table.begin(), table.end(), [id](const auto& d) { return d.id == id; });
    return it == table.end() ? nullptr : &*it;
}

std::size_t ParameterRegistry::indexOf(std::string_view id) noexcept
{
    const auto& table = descriptors();
    const auto it = std::find_if(table.begin(), table.end(), [id](const auto& d) { return d.id == id; });
    return it == table.end() ? table.size() : static_cast<std::size_t>(it - table.begin());
}

ParameterStore::ParameterStore() : size_(ParameterRegistry::descriptors().size()), values_(std::make_unique<std::atomic<float>[]>(size_))
{ resetToDefaults(); }

float ParameterStore::read(std::size_t index) const noexcept
{ return index < size_ ? values_[index].load(std::memory_order_acquire) : 0.0f; }

float ParameterStore::read(std::string_view id) const noexcept
{ return read(ParameterRegistry::indexOf(id)); }

bool ParameterStore::write(std::size_t index, float value) noexcept
{
    if (index >= size_) return false;
    const auto& descriptor = ParameterRegistry::descriptors()[index];
    const auto snapped = descriptor.snap(value);
    values_[index].store(snapped, std::memory_order_release);
    return true;
}

bool ParameterStore::write(std::string_view id, float value) noexcept
{ return write(ParameterRegistry::indexOf(id), value); }

std::vector<float> ParameterStore::snapshot() const
{
    std::vector<float> values(size_);
    for (std::size_t i = 0; i < size_; ++i) values[i] = read(i);
    return values;
}

void ParameterStore::resetToDefaults() noexcept
{
    const auto& table = ParameterRegistry::descriptors();
    for (std::size_t i = 0; i < size_; ++i) values_[i].store(table[i].defaultValue, std::memory_order_release);
}

} // namespace ats
