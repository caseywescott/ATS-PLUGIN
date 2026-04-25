#include "AtsModernMorph.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <set>
#include <sstream>

namespace ats
{

namespace
{

constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 6.28318530717958647692;

double clamp01(double value) { return std::max(0.0, std::min(1.0, value)); }

std::string number(double value)
{
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(17) << value;
    return out.str();
}

void appendQuoted(std::ostringstream& out, const std::string& value)
{
    out << '"';
    for (const char c : value)
    {
        switch (c)
        {
            case '\\':
                out << "\\\\";
                break;
            case '"':
                out << "\\\"";
                break;
            case '\b':
                out << "\\b";
                break;
            case '\f':
                out << "\\f";
                break;
            case '\n':
                out << "\\n";
                break;
            case '\r':
                out << "\\r";
                break;
            case '\t':
                out << "\\t";
                break;
            default:
                if (static_cast<unsigned char>(c) < 0x20)
                {
                    out << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                        << static_cast<int>(static_cast<unsigned char>(c)) << std::dec
                        << std::setfill(' ');
                }
                else
                {
                    out << c;
                }
                break;
        }
    }
    out << '"';
}

const ModernAtsPartial* findPartial(const ModernAtsModel& model, int id)
{
    for (const auto& partial : model.partials)
    {
        if (partial.id == id)
            return &partial;
    }
    return nullptr;
}

double averageFrequency(const ModernAtsPartial& partial)
{
    double total = 0.0;
    int count = 0;
    for (const auto& frame : partial.frames)
    {
        if (frame.frequencyHz > 0.0 && frame.amplitude > 0.0
            && frame.reason != ModernAtsFrameReason::Slept
            && frame.reason != ModernAtsFrameReason::Rejected
            && frame.reason != ModernAtsFrameReason::Pruned)
        {
            total += frame.frequencyHz;
            ++count;
        }
    }
    return count > 0 ? total / (double)count : 0.0;
}

double averageSmr(const ModernAtsPartial& partial)
{
    double total = 0.0;
    int count = 0;
    for (const auto& frame : partial.frames)
    {
        if (frame.amplitude > 0.0)
        {
            total += frame.smrDb;
            ++count;
        }
    }
    return count > 0 ? total / (double)count : 0.0;
}

double frequencySlope(const ModernAtsPartial& partial)
{
    if (partial.frames.size() < 2)
        return 0.0;

    int first = -1;
    int last = -1;
    for (int i = 0; i < static_cast<int>(partial.frames.size()); ++i)
    {
        if (partial.frames[(std::size_t)i].frequencyHz > 0.0
            && partial.frames[(std::size_t)i].amplitude > 0.0)
        {
            if (first < 0)
                first = i;
            last = i;
        }
    }
    if (first < 0 || last <= first)
        return 0.0;
    return (partial.frames[(std::size_t)last].frequencyHz - partial.frames[(std::size_t)first].frequencyHz)
           / (double)(last - first);
}

double centsDistance(double a, double b)
{
    if (a <= 0.0 || b <= 0.0)
        return 1.0e9;
    return 1200.0 * std::abs(std::log2(a / b));
}

int harmonicNumberFor(const ModernAtsPartial& partial, double f0)
{
    const double f = averageFrequency(partial);
    if (f <= 0.0 || f0 <= 0.0)
        return 0;
    return std::max(1, (int)std::llround(f / f0));
}

const ModernAtsPartialFrame* frameAt(const ModernAtsPartial* partial, int frameIndex)
{
    if (partial == nullptr || partial->frames.empty())
        return nullptr;
    const int clamped = std::max(0, std::min(frameIndex, static_cast<int>(partial->frames.size()) - 1));
    return &partial->frames[(std::size_t)clamped];
}

double interpolatePhase(double a, double b, double t)
{
    double delta = std::fmod(b - a, kTwoPi);
    if (delta > kPi)
        delta -= kTwoPi;
    if (delta < -kPi)
        delta += kTwoPi;
    return a + delta * t;
}

bool frameIsActive(const ModernAtsPartialFrame* frame)
{
    return frame != nullptr && frame->amplitude > 0.0 && frame->frequencyHz > 0.0
           && frame->reason != ModernAtsFrameReason::Slept
           && frame->reason != ModernAtsFrameReason::Pruned
           && frame->reason != ModernAtsFrameReason::Rejected;
}

ModernAtsPartialFrame sleptMorphFrame()
{
    ModernAtsPartialFrame frame;
    frame.reason = ModernAtsFrameReason::Slept;
    frame.confidence = 0.0;
    return frame;
}

ModernAtsPartialFrame morphFrame(const ModernAtsPartialFrame* source,
                                 const ModernAtsPartialFrame* target,
                                 MorphUnmatchedPolicy sourcePolicy,
                                 MorphUnmatchedPolicy targetPolicy,
                                 double frequencyT,
                                 double amplitudeT,
                                 double noiseT)
{
    const bool sourceActive = frameIsActive(source);
    const bool targetActive = frameIsActive(target);
    if (!sourceActive && !targetActive)
        return sleptMorphFrame();

    if (sourceActive && !targetActive)
    {
        if (sourcePolicy == MorphUnmatchedPolicy::Discard)
            return sleptMorphFrame();
        ModernAtsPartialFrame out = *source;
        const double fade = sourcePolicy == MorphUnmatchedPolicy::Hold ? 1.0 : 1.0 - amplitudeT;
        out.amplitude *= std::max(0.0, fade);
        out.noiseEnergy *= std::max(0.0, 1.0 - noiseT);
        out.confidence *= std::max(0.0, fade);
        out.reason = out.amplitude > 0.0 ? ModernAtsFrameReason::Matched : ModernAtsFrameReason::Slept;
        return out;
    }

    if (!sourceActive && targetActive)
    {
        if (targetPolicy == MorphUnmatchedPolicy::Discard)
            return sleptMorphFrame();
        ModernAtsPartialFrame out = *target;
        const double fade = targetPolicy == MorphUnmatchedPolicy::Hold ? 1.0 : amplitudeT;
        out.amplitude *= std::max(0.0, fade);
        out.noiseEnergy *= std::max(0.0, noiseT);
        out.confidence *= std::max(0.0, fade);
        out.reason = out.amplitude > 0.0 ? ModernAtsFrameReason::Matched : ModernAtsFrameReason::Slept;
        return out;
    }

    ModernAtsPartialFrame out;
    out.amplitude = source->amplitude + (target->amplitude - source->amplitude) * amplitudeT;
    out.frequencyHz = source->frequencyHz + (target->frequencyHz - source->frequencyHz) * frequencyT;
    out.phaseRadians = interpolatePhase(source->phaseRadians, target->phaseRadians, frequencyT);
    out.smrDb = source->smrDb + (target->smrDb - source->smrDb) * amplitudeT;
    out.noiseEnergy = source->noiseEnergy + (target->noiseEnergy - source->noiseEnergy) * noiseT;
    out.confidence = source->confidence + (target->confidence - source->confidence) * amplitudeT;
    out.reason = ModernAtsFrameReason::Matched;
    return out;
}

void appendEnvelope(std::ostringstream& out, const MorphEnvelope& envelope)
{
    out << "{\"id\":";
    appendQuoted(out, envelope.id);
    out << ",\"points\":[";
    for (std::size_t i = 0; i < envelope.points.size(); ++i)
    {
        if (i != 0)
            out << ',';
        out << "{\"position\":" << number(envelope.points[i].position)
            << ",\"value\":" << number(envelope.points[i].value) << '}';
    }
    out << "]}";
}

std::vector<float> mixTransientSamples(const ModernAtsTransientBuffer* source,
                                       const ModernAtsTransientBuffer* target,
                                       double targetMix)
{
    if (source == nullptr && target == nullptr)
        return {};
    const std::size_t n = std::max(source != nullptr ? source->samples.size() : std::size_t(0),
                                  target != nullptr ? target->samples.size() : std::size_t(0));
    std::vector<float> out(n, 0.0f);
    for (std::size_t i = 0; i < n; ++i)
    {
        const double s = source != nullptr && i < source->samples.size() ? source->samples[i] : 0.0;
        const double t = target != nullptr && i < target->samples.size() ? target->samples[i] : 0.0;
        out[i] = static_cast<float>(s * (1.0 - targetMix) + t * targetMix);
    }
    return out;
}

const ModernAtsTransientBuffer* transientBufferFor(const ModernAtsModel& model, int markerIndex)
{
    for (const auto& buffer : model.transientBuffers)
    {
        if (buffer.markerIndex == markerIndex)
            return &buffer;
    }
    return nullptr;
}

} // namespace

const char* toString(MorphUnmatchedPolicy policy)
{
    switch (policy)
    {
        case MorphUnmatchedPolicy::FadeOut:
            return "fade_out";
        case MorphUnmatchedPolicy::FadeIn:
            return "fade_in";
        case MorphUnmatchedPolicy::Nearest:
            return "nearest";
        case MorphUnmatchedPolicy::Hold:
            return "hold";
        case MorphUnmatchedPolicy::Discard:
            return "discard";
    }
    return "fade_out";
}

const char* toString(MorphCorrespondenceStrategy strategy)
{
    switch (strategy)
    {
        case MorphCorrespondenceStrategy::Explicit:
            return "explicit";
        case MorphCorrespondenceStrategy::HarmonicNumber:
            return "harmonic_number";
        case MorphCorrespondenceStrategy::NearestFrequency:
            return "nearest_frequency";
        case MorphCorrespondenceStrategy::Automatic:
            return "automatic";
    }
    return "automatic";
}

const char* toString(MorphTransientMode mode)
{
    switch (mode)
    {
        case MorphTransientMode::SourceOnly:
            return "source_only";
        case MorphTransientMode::TargetOnly:
            return "target_only";
        case MorphTransientMode::BlendBoth:
            return "blend_both";
        case MorphTransientMode::FollowMorph:
            return "follow_morph";
    }
    return "follow_morph";
}

double evaluateMorphEnvelope(const MorphEnvelope& envelope, double position, double fallbackValue)
{
    if (envelope.points.empty())
        return fallbackValue;

    const double p = clamp01(position);
    if (p <= envelope.points.front().position)
        return envelope.points.front().value;
    for (std::size_t i = 1; i < envelope.points.size(); ++i)
    {
        if (p <= envelope.points[i].position)
        {
            const auto& a = envelope.points[i - 1];
            const auto& b = envelope.points[i];
            const double span = std::max(1.0e-12, b.position - a.position);
            const double t = clamp01((p - a.position) / span);
            return a.value + (b.value - a.value) * t;
        }
    }
    return envelope.points.back().value;
}

MorphEnvelope makeConstantMorphEnvelope(const std::string& id, double value)
{
    return { id, { { 0.0, value }, { 1.0, value } } };
}

MorphEnvelope makeLinearMorphEnvelope(const std::string& id, double startValue, double endValue)
{
    return { id, { { 0.0, startValue }, { 1.0, endValue } } };
}

MorphEnvelopeSet makeDefaultMorphEnvelopeSet()
{
    MorphEnvelopeSet envelopes;
    envelopes.frequency = makeLinearMorphEnvelope("frequency", 0.0, 1.0);
    envelopes.amplitude = makeLinearMorphEnvelope("amplitude", 0.0, 1.0);
    envelopes.partialNoise = makeLinearMorphEnvelope("partial_noise", 0.0, 1.0);
    envelopes.orphanResidual = makeLinearMorphEnvelope("orphan_residual", 0.0, 1.0);
    envelopes.time = makeLinearMorphEnvelope("time", 0.0, 1.0);
    envelopes.transientTiming = makeLinearMorphEnvelope("transient_timing", 0.0, 1.0);
    envelopes.transientGain = makeConstantMorphEnvelope("transient_gain", 1.0);
    envelopes.transientSourceMix = makeLinearMorphEnvelope("transient_source_mix", 0.0, 1.0);
    return envelopes;
}

bool validateMorphRecipe(const MorphRecipe& recipe, std::string& errorMessage)
{
    errorMessage.clear();
    if (recipe.schemaVersion != kModernAtsMorphRecipeVersion)
    {
        errorMessage = "Unsupported morph recipe schema.";
        return false;
    }
    if (recipe.recipeId.empty() || recipe.sourceModelId.empty() || recipe.targetModelId.empty())
    {
        errorMessage = "Morph recipe id and source model ids are required.";
        return false;
    }
    return true;
}

std::string serializeMorphRecipeToJson(const MorphRecipe& recipe)
{
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "{\"schema_version\":" << recipe.schemaVersion;
    out << ",\"recipe_id\":";
    appendQuoted(out, recipe.recipeId);
    out << ",\"source_model_id\":";
    appendQuoted(out, recipe.sourceModelId);
    out << ",\"target_model_id\":";
    appendQuoted(out, recipe.targetModelId);
    out << ",\"correspondence_strategy\":";
    appendQuoted(out, toString(recipe.correspondenceStrategy));
    out << ",\"source_unmatched_policy\":";
    appendQuoted(out, toString(recipe.sourceUnmatchedPolicy));
    out << ",\"target_unmatched_policy\":";
    appendQuoted(out, toString(recipe.targetUnmatchedPolicy));
    out << ",\"transient_mode\":";
    appendQuoted(out, toString(recipe.transientMode));
    out << ",\"protect_transient_windows\":" << (recipe.protectTransientWindows ? "true" : "false");
    out << ",\"source_fundamental_hz\":" << number(recipe.sourceFundamentalHz);
    out << ",\"target_fundamental_hz\":" << number(recipe.targetFundamentalHz);
    out << ",\"source_harmonicity\":" << number(recipe.sourceHarmonicity);
    out << ",\"target_harmonicity\":" << number(recipe.targetHarmonicity);
    out << ",\"max_nearest_cents\":" << number(recipe.maxNearestCents);

    out << ",\"envelopes\":{";
    out << "\"frequency\":";
    appendEnvelope(out, recipe.envelopes.frequency);
    out << ",\"amplitude\":";
    appendEnvelope(out, recipe.envelopes.amplitude);
    out << ",\"partial_noise\":";
    appendEnvelope(out, recipe.envelopes.partialNoise);
    out << ",\"orphan_residual\":";
    appendEnvelope(out, recipe.envelopes.orphanResidual);
    out << ",\"time\":";
    appendEnvelope(out, recipe.envelopes.time);
    out << ",\"transient_timing\":";
    appendEnvelope(out, recipe.envelopes.transientTiming);
    out << ",\"transient_gain\":";
    appendEnvelope(out, recipe.envelopes.transientGain);
    out << ",\"transient_source_mix\":";
    appendEnvelope(out, recipe.envelopes.transientSourceMix);
    out << '}';

    out << ",\"correspondences\":[";
    for (std::size_t i = 0; i < recipe.correspondences.size(); ++i)
    {
        if (i != 0)
            out << ',';
        const auto& c = recipe.correspondences[i];
        out << "{\"source_partial_id\":" << c.sourcePartialId;
        out << ",\"target_partial_id\":" << c.targetPartialId;
        out << ",\"label\":";
        appendQuoted(out, c.label);
        out << ",\"source_label\":";
        appendQuoted(out, c.sourceLabel);
        out << ",\"target_label\":";
        appendQuoted(out, c.targetLabel);
        out << ",\"source_unmatched_policy\":";
        appendQuoted(out, toString(c.sourceUnmatchedPolicy));
        out << ",\"target_unmatched_policy\":";
        appendQuoted(out, toString(c.targetUnmatchedPolicy));
        out << '}';
    }
    out << "]}";
    return out.str();
}

std::vector<MorphPartialCorrespondence> buildAutomaticCorrespondence(
    const ModernAtsModel& source,
    const ModernAtsModel& target,
    const MorphRecipe& recipe)
{
    if (recipe.correspondenceStrategy == MorphCorrespondenceStrategy::Explicit
        && !recipe.correspondences.empty())
        return recipe.correspondences;

    std::vector<MorphPartialCorrespondence> result;
    std::set<int> usedSource;
    std::set<int> usedTarget;
    const bool useHarmonic =
        (recipe.correspondenceStrategy == MorphCorrespondenceStrategy::HarmonicNumber
         || (recipe.correspondenceStrategy == MorphCorrespondenceStrategy::Automatic
             && recipe.sourceHarmonicity >= 0.75 && recipe.targetHarmonicity >= 0.75
             && recipe.sourceFundamentalHz > 0.0 && recipe.targetFundamentalHz > 0.0));

    if (useHarmonic)
    {
        struct HarmonicCandidate
        {
            int harmonic = 0;
            int sourceId = -1;
            int targetId = -1;
            double cost = 0.0;
        };

        std::vector<HarmonicCandidate> candidates;
        for (const auto& sourcePartial : source.partials)
        {
            const int sourceHarmonic = harmonicNumberFor(sourcePartial, recipe.sourceFundamentalHz);
            if (sourceHarmonic <= 0)
                continue;
            for (const auto& targetPartial : target.partials)
            {
                const int targetHarmonic = harmonicNumberFor(targetPartial, recipe.targetFundamentalHz);
                if (targetHarmonic != sourceHarmonic)
                    continue;
                const double cents = centsDistance(averageFrequency(sourcePartial), averageFrequency(targetPartial));
                const double slopeCost = std::abs(frequencySlope(sourcePartial) - frequencySlope(targetPartial)) / 200.0;
                const double smrCost = std::abs(averageSmr(sourcePartial) - averageSmr(targetPartial)) / 100.0;
                candidates.push_back({ sourceHarmonic, sourcePartial.id, targetPartial.id, cents + slopeCost + smrCost });
            }
        }

        std::sort(candidates.begin(), candidates.end(), [](const HarmonicCandidate& a, const HarmonicCandidate& b) {
            if (a.cost == b.cost)
            {
                if (a.harmonic == b.harmonic)
                    return a.sourceId < b.sourceId;
                return a.harmonic < b.harmonic;
            }
            return a.cost < b.cost;
        });

        for (const auto& candidate : candidates)
        {
            if (usedSource.count(candidate.sourceId) || usedTarget.count(candidate.targetId))
                continue;
            const auto* sourcePartial = findPartial(source, candidate.sourceId);
            const auto* targetPartial = findPartial(target, candidate.targetId);
            result.push_back({ candidate.sourceId,
                               candidate.targetId,
                               "h" + std::to_string(candidate.harmonic),
                               sourcePartial != nullptr ? sourcePartial->label : std::string(),
                               targetPartial != nullptr ? targetPartial->label : std::string(),
                               recipe.sourceUnmatchedPolicy,
                               recipe.targetUnmatchedPolicy });
            usedSource.insert(candidate.sourceId);
            usedTarget.insert(candidate.targetId);
        }
    }
    else
    {
        struct Candidate
        {
            int sourceId = -1;
            int targetId = -1;
            double cost = 0.0;
        };

        std::vector<Candidate> candidates;
        for (const auto& s : source.partials)
        {
            for (const auto& t : target.partials)
            {
                const double cents = centsDistance(averageFrequency(s), averageFrequency(t));
                if (cents > recipe.maxNearestCents)
                    continue;
                const double slopeCost = std::abs(frequencySlope(s) - frequencySlope(t)) / 200.0;
                const double smrCost = std::abs(averageSmr(s) - averageSmr(t)) / 100.0;
                candidates.push_back({ s.id, t.id, cents + slopeCost + smrCost });
            }
        }

        std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
            if (a.cost == b.cost)
                return a.sourceId < b.sourceId;
            return a.cost < b.cost;
        });

        for (const auto& candidate : candidates)
        {
            if (usedSource.count(candidate.sourceId) || usedTarget.count(candidate.targetId))
                continue;
            const auto* s = findPartial(source, candidate.sourceId);
            const auto* t = findPartial(target, candidate.targetId);
            result.push_back({ candidate.sourceId,
                               candidate.targetId,
                               "nearest_" + std::to_string(candidate.sourceId) + "_"
                                   + std::to_string(candidate.targetId),
                               s != nullptr ? s->label : std::string(),
                               t != nullptr ? t->label : std::string(),
                               recipe.sourceUnmatchedPolicy,
                               recipe.targetUnmatchedPolicy });
            usedSource.insert(candidate.sourceId);
            usedTarget.insert(candidate.targetId);
        }
    }

    for (const auto& partial : source.partials)
    {
        if (!usedSource.count(partial.id))
            result.push_back({ partial.id,
                               -1,
                               "source_unmatched_" + std::to_string(partial.id),
                               partial.label,
                               {},
                               recipe.sourceUnmatchedPolicy,
                               recipe.targetUnmatchedPolicy });
    }
    for (const auto& partial : target.partials)
    {
        if (!usedTarget.count(partial.id))
            result.push_back({ -1,
                               partial.id,
                               "target_unmatched_" + std::to_string(partial.id),
                               {},
                               partial.label,
                               recipe.sourceUnmatchedPolicy,
                               recipe.targetUnmatchedPolicy });
    }

    return result;
}

ModernAtsModel morphModernAtsModels(const ModernAtsModel& source,
                                    const ModernAtsModel& target,
                                    const MorphRecipe& recipe,
                                    double morphPosition,
                                    MorphFixtureMetrics* metrics)
{
    const double pos = clamp01(morphPosition);
    const double freqT = clamp01(evaluateMorphEnvelope(recipe.envelopes.frequency, pos, pos));
    const double ampT = clamp01(evaluateMorphEnvelope(recipe.envelopes.amplitude, pos, pos));
    const double noiseT = clamp01(evaluateMorphEnvelope(recipe.envelopes.partialNoise, pos, pos));
    const double orphanT = clamp01(evaluateMorphEnvelope(recipe.envelopes.orphanResidual, pos, pos));
    const double timeT = clamp01(evaluateMorphEnvelope(recipe.envelopes.time, pos, pos));
    const double transientTimingT = clamp01(evaluateMorphEnvelope(recipe.envelopes.transientTiming, pos, pos));
    const double transientGain = std::max(0.0, evaluateMorphEnvelope(recipe.envelopes.transientGain, pos, 1.0));
    const double transientSourceMix = clamp01(evaluateMorphEnvelope(recipe.envelopes.transientSourceMix, pos, pos));

    const auto correspondences = buildAutomaticCorrespondence(source, target, recipe);
    const int sourceFrames = static_cast<int>(source.frames.size());
    const int targetFrames = static_cast<int>(target.frames.size());
    const int outFrames = std::max(1, std::max(sourceFrames, targetFrames));

    ModernAtsModel out;
    out.header.schemaVersion = kModernAtsSchemaVersion;
    out.header.analysisMode = ModernAtsAnalysisMode::ModernAts;
    out.header.trackerMode = ModernAtsTrackerMode::ModernAssignment;
    out.header.sampleRate = source.header.sampleRate > 0 ? source.header.sampleRate : target.header.sampleRate;
    out.header.hopSizeSamples =
        source.header.hopSizeSamples > 0 ? source.header.hopSizeSamples : target.header.hopSizeSamples;
    out.header.windowSizeSamples =
        source.header.windowSizeSamples > 0 ? source.header.windowSizeSamples : target.header.windowSizeSamples;
    out.header.parameterHash = recipe.recipeId + "@" + number(pos);
    out.header.sourceHash = recipe.sourceModelId + "+" + recipe.targetModelId;

    for (int frameIndex = 0; frameIndex < outFrames; ++frameIndex)
    {
        const double norm = outFrames > 1 ? (double)frameIndex / (double)(outFrames - 1) : 0.0;
        const int sourceIndex =
            std::min(std::max(0, (int)std::llround(norm * (double)std::max(0, sourceFrames - 1))), std::max(0, sourceFrames - 1));
        const int targetIndex =
            std::min(std::max(0, (int)std::llround(norm * (double)std::max(0, targetFrames - 1))), std::max(0, targetFrames - 1));

        ModernAtsFrame frame;
        const double sourceTime = sourceFrames > 0 ? source.frames[(std::size_t)sourceIndex].timeSeconds
                                                   : (double)frameIndex * out.header.hopSizeSamples
                                                         / (double)out.header.sampleRate;
        const double targetTime = targetFrames > 0 ? target.frames[(std::size_t)targetIndex].timeSeconds : sourceTime;
        frame.timeSeconds = sourceTime + (targetTime - sourceTime) * timeT;

        const auto& sourceEnergy = sourceFrames > 0 ? source.frames[(std::size_t)sourceIndex].orphanResidualBandEnergy
                                                    : std::vector<double>();
        const auto& targetEnergy = targetFrames > 0 ? target.frames[(std::size_t)targetIndex].orphanResidualBandEnergy
                                                    : std::vector<double>();
        frame.orphanResidualBandEnergy.assign(kModernAtsDefaultResidualBands, 0.0);
        for (int band = 0; band < kModernAtsDefaultResidualBands; ++band)
        {
            const double s = band < static_cast<int>(sourceEnergy.size()) ? sourceEnergy[(std::size_t)band] : 0.0;
            const double t = band < static_cast<int>(targetEnergy.size()) ? targetEnergy[(std::size_t)band] : 0.0;
            frame.orphanResidualBandEnergy[(std::size_t)band] = s + (t - s) * orphanT;
        }

        out.frames.push_back(frame);
    }

    int nextPartialId = 0;
    for (const auto& correspondence : correspondences)
    {
        const auto* sourcePartial = findPartial(source, correspondence.sourcePartialId);
        const auto* targetPartial = findPartial(target, correspondence.targetPartialId);
        ModernAtsPartial partial;
        partial.id = nextPartialId++;
        partial.label = correspondence.label.empty() ? "morph_" + std::to_string(partial.id)
                                                     : correspondence.label;
        partial.channel = 0;

        for (int frameIndex = 0; frameIndex < outFrames; ++frameIndex)
        {
            const double norm = outFrames > 1 ? (double)frameIndex / (double)(outFrames - 1) : 0.0;
            const int sourceIndex =
                std::min(std::max(0, (int)std::llround(norm * (double)std::max(0, sourceFrames - 1))), std::max(0, sourceFrames - 1));
            const int targetIndex =
                std::min(std::max(0, (int)std::llround(norm * (double)std::max(0, targetFrames - 1))), std::max(0, targetFrames - 1));
            partial.frames.push_back(morphFrame(frameAt(sourcePartial, sourceIndex),
                                                frameAt(targetPartial, targetIndex),
                                                correspondence.sourceUnmatchedPolicy,
                                                correspondence.targetUnmatchedPolicy,
                                                freqT,
                                                ampT,
                                                noiseT));
            if (partial.frames.back().amplitude > 0.0 && partial.frames.back().frequencyHz > 0.0)
                out.frames[(std::size_t)frameIndex].activePartialIds.push_back(partial.id);
        }

        bool hasActive = false;
        for (const auto& frame : partial.frames)
            hasActive = hasActive || frameIsActive(&frame);
        if (hasActive)
            out.partials.push_back(partial);
    }

    const int transientCount = std::max(source.transients.size(), target.transients.size());
    for (int i = 0; i < transientCount; ++i)
    {
        const auto* sourceTransient =
            i < static_cast<int>(source.transients.size()) ? &source.transients[(std::size_t)i] : nullptr;
        const auto* targetTransient =
            i < static_cast<int>(target.transients.size()) ? &target.transients[(std::size_t)i] : nullptr;

        double targetMix = transientSourceMix;
        if (recipe.transientMode == MorphTransientMode::SourceOnly)
            targetMix = 0.0;
        else if (recipe.transientMode == MorphTransientMode::TargetOnly)
            targetMix = 1.0;
        else if (recipe.transientMode == MorphTransientMode::FollowMorph)
            targetMix = pos;

        if (sourceTransient == nullptr && targetTransient == nullptr)
            continue;
        if (sourceTransient == nullptr)
            targetMix = 1.0;
        if (targetTransient == nullptr)
            targetMix = 0.0;

        const ModernAtsTransientMarker& s = sourceTransient != nullptr ? *sourceTransient : *targetTransient;
        const ModernAtsTransientMarker& t = targetTransient != nullptr ? *targetTransient : *sourceTransient;

        const int markerIndex = static_cast<int>(out.transients.size());
        ModernAtsTransientMarker marker;
        marker.frameIndex = (int)std::llround((double)s.frameIndex + ((double)t.frameIndex - (double)s.frameIndex)
                                                                    * transientTimingT);
        marker.bufferIndex = markerIndex;
        marker.onsetSeconds = s.onsetSeconds + (t.onsetSeconds - s.onsetSeconds) * transientTimingT;
        marker.windowStartSeconds =
            s.windowStartSeconds + (t.windowStartSeconds - s.windowStartSeconds) * transientTimingT;
        marker.windowEndSeconds = s.windowEndSeconds + (t.windowEndSeconds - s.windowEndSeconds) * transientTimingT;
        marker.gain = (s.gain * (1.0 - targetMix) + t.gain * targetMix) * transientGain;
        if (recipe.protectTransientWindows)
        {
            const double minWindow = 0.01;
            marker.windowStartSeconds = std::min(marker.windowStartSeconds, marker.onsetSeconds);
            marker.windowEndSeconds = std::max(marker.windowEndSeconds, marker.onsetSeconds + minWindow);
        }
        out.transients.push_back(marker);

        const auto* sourceBuffer = transientBufferFor(source, i);
        const auto* targetBuffer = transientBufferFor(target, i);
        const auto samples = mixTransientSamples(sourceBuffer, targetBuffer, targetMix);
        if (!samples.empty())
        {
            ModernAtsTransientBuffer buffer;
            buffer.markerIndex = markerIndex;
            buffer.sampleRate = sourceBuffer != nullptr ? sourceBuffer->sampleRate
                                                        : targetBuffer != nullptr ? targetBuffer->sampleRate : out.header.sampleRate;
            buffer.gain = (sourceBuffer != nullptr ? sourceBuffer->gain : 1.0) * (1.0 - targetMix)
                          + (targetBuffer != nullptr ? targetBuffer->gain : 1.0) * targetMix;
            buffer.samples = samples;
            out.transientBuffers.push_back(buffer);
        }
    }

    if (metrics != nullptr)
    {
        metrics->outputPartials = static_cast<int>(out.partials.size());
        metrics->outputFrames = static_cast<int>(out.frames.size());
        metrics->transientMarkers = static_cast<int>(out.transients.size());
        metrics->attackWindowSeconds = 0.0;
        for (const auto& marker : out.transients)
            metrics->attackWindowSeconds += std::max(0.0, marker.windowEndSeconds - marker.windowStartSeconds);
        metrics->orphanResidualEnergy = 0.0;
        for (const auto& frame : out.frames)
        {
            for (const double energy : frame.orphanResidualBandEnergy)
                metrics->orphanResidualEnergy += energy;
        }
    }

    return out;
}

} // namespace ats
