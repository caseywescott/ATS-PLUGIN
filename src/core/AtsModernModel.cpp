#include "AtsModernModel.h"

#include <cmath>
#include <iomanip>
#include <locale>
#include <set>
#include <sstream>

namespace ats
{

const char* toString(ModernAtsAnalysisMode mode)
{
    switch (mode)
    {
        case ModernAtsAnalysisMode::ClassicAts:
            return "classic_ats";
        case ModernAtsAnalysisMode::ModernAts:
            return "modern_ats";
        case ModernAtsAnalysisMode::Experimental:
            return "experimental";
    }
    return "classic_ats";
}

const char* toString(ModernAtsTrackerMode mode)
{
    switch (mode)
    {
        case ModernAtsTrackerMode::ClassicRecursiveFrequencySmr:
            return "classic_recursive_frequency_smr";
        case ModernAtsTrackerMode::ModernAssignment:
            return "modern_assignment";
        case ModernAtsTrackerMode::Experimental:
            return "experimental";
    }
    return "classic_recursive_frequency_smr";
}

const char* toString(ModernAtsChannelPolicy policy)
{
    switch (policy)
    {
        case ModernAtsChannelPolicy::MonoInput:
            return "mono_input";
        case ModernAtsChannelPolicy::DownmixToMonoAverage:
            return "downmix_to_mono_average";
        case ModernAtsChannelPolicy::PerChannel:
            return "per_channel";
        case ModernAtsChannelPolicy::MidSide:
            return "mid_side";
    }
    return "mono_input";
}

ModernAtsChannelPolicy defaultModernAtsChannelPolicyForInput(int inputChannelCount)
{
    return inputChannelCount <= 1 ? ModernAtsChannelPolicy::MonoInput
                                  : ModernAtsChannelPolicy::DownmixToMonoAverage;
}

const char* toString(ModernAtsFrameReason reason)
{
    switch (reason)
    {
        case ModernAtsFrameReason::Matched:
            return "matched";
        case ModernAtsFrameReason::Born:
            return "born";
        case ModernAtsFrameReason::Slept:
            return "slept";
        case ModernAtsFrameReason::Interpolated:
            return "interpolated";
        case ModernAtsFrameReason::Pruned:
            return "pruned";
        case ModernAtsFrameReason::Rejected:
            return "rejected";
    }
    return "matched";
}

const char* toString(ModernAtsLifecycleKind kind)
{
    switch (kind)
    {
        case ModernAtsLifecycleKind::Birth:
            return "birth";
        case ModernAtsLifecycleKind::Sleep:
            return "sleep";
        case ModernAtsLifecycleKind::Wake:
            return "wake";
        case ModernAtsLifecycleKind::Death:
            return "death";
        case ModernAtsLifecycleKind::GapFill:
            return "gap_fill";
        case ModernAtsLifecycleKind::Pruned:
            return "pruned";
    }
    return "birth";
}

namespace
{

bool finite(double value) { return std::isfinite(value); }

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

void appendIntArray(std::ostringstream& out, const std::vector<int>& values)
{
    out << '[';
    for (std::size_t i = 0; i < values.size(); ++i)
    {
        if (i != 0)
            out << ',';
        out << values[i];
    }
    out << ']';
}

void appendDoubleArray(std::ostringstream& out, const std::vector<double>& values)
{
    out << '[';
    for (std::size_t i = 0; i < values.size(); ++i)
    {
        if (i != 0)
            out << ',';
        out << number(values[i]);
    }
    out << ']';
}

} // namespace

bool validateModernAtsModel(const ModernAtsModel& model, std::string& errorMessage)
{
    errorMessage.clear();

    if (model.header.schemaVersion != kModernAtsSchemaVersion)
    {
        errorMessage = "Unsupported modern ATS schema version.";
        return false;
    }

    if (model.header.sampleRate <= 0 || model.header.hopSizeSamples <= 0 || model.header.windowSizeSamples <= 0)
    {
        errorMessage = "Header sample rate, hop size, and window size must be positive.";
        return false;
    }

    if (model.header.inputChannelCount <= 0)
    {
        errorMessage = "Header input channel count must be positive.";
        return false;
    }

    if (model.header.inputChannelCount == 1
        && model.header.channelPolicy != ModernAtsChannelPolicy::MonoInput)
    {
        errorMessage = "Mono inputs must use the mono input channel policy.";
        return false;
    }

    if (model.header.inputChannelCount > 1
        && model.header.channelPolicy == ModernAtsChannelPolicy::MonoInput)
    {
        errorMessage = "Multichannel inputs must record an explicit multichannel channel policy.";
        return false;
    }

    if (model.header.parameterHash.empty() || model.header.sourceHash.empty())
    {
        errorMessage = "Header parameter hash and source hash are required.";
        return false;
    }

    std::set<int> partialIds;
    for (const auto& partial : model.partials)
    {
        if (partial.id < 0)
        {
            errorMessage = "Partial ids must be non-negative.";
            return false;
        }
        if (partial.channel < 0 || partial.channel >= model.header.inputChannelCount)
        {
            errorMessage = "Partial channels must be non-negative and within the source channel count.";
            return false;
        }
        if (!partialIds.insert(partial.id).second)
        {
            errorMessage = "Partial ids must be unique.";
            return false;
        }
    }

    double previousFrameTime = -1.0;
    for (std::size_t frameIndex = 0; frameIndex < model.frames.size(); ++frameIndex)
    {
        const auto& frame = model.frames[frameIndex];
        if (!finite(frame.timeSeconds) || frame.timeSeconds < 0.0)
        {
            errorMessage = "Frame time must be finite and non-negative.";
            return false;
        }
        if (previousFrameTime > frame.timeSeconds)
        {
            errorMessage = "Frame times must be monotonic.";
            return false;
        }
        previousFrameTime = frame.timeSeconds;
        std::set<int> activeIds;
        for (const int partialId : frame.activePartialIds)
        {
            if (partialIds.find(partialId) == partialIds.end())
            {
                errorMessage = "Active partial ids must reference existing partials.";
                return false;
            }
            if (!activeIds.insert(partialId).second)
            {
                errorMessage = "Active partial ids must be unique within a frame.";
                return false;
            }
        }
        if (!frame.orphanResidualBandEnergy.empty()
            && frame.orphanResidualBandEnergy.size() != kModernAtsDefaultResidualBands)
        {
            errorMessage = "Orphan residual band energy must be empty or contain 25 Bark bands.";
            return false;
        }
        for (const double energy : frame.orphanResidualBandEnergy)
        {
            if (!finite(energy) || energy < 0.0)
            {
                errorMessage = "Orphan residual band energy must be finite and non-negative.";
                return false;
            }
        }
    }

    for (const auto& partial : model.partials)
    {
        if (!model.frames.empty() && partial.frames.size() != model.frames.size())
        {
            errorMessage = "Every partial must carry one frame payload per model frame.";
            return false;
        }
        for (const auto& frame : partial.frames)
        {
            if (!finite(frame.amplitude) || !finite(frame.frequencyHz) || !finite(frame.phaseRadians)
                || !finite(frame.smrDb) || !finite(frame.noiseEnergy) || !finite(frame.confidence))
            {
                errorMessage = "Partial frame values must be finite.";
                return false;
            }
            if (frame.amplitude < 0.0 || frame.frequencyHz < 0.0 || frame.noiseEnergy < 0.0)
            {
                errorMessage = "Partial amplitude, frequency, and noise energy must be non-negative.";
                return false;
            }
            if (frame.confidence < 0.0 || frame.confidence > 1.0)
            {
                errorMessage = "Partial frame confidence must be in [0, 1].";
                return false;
            }
        }
    }

    for (const auto& event : model.trackLifecycle)
    {
        if (event.partialId < 0 || event.frameIndex < 0
            || partialIds.find(event.partialId) == partialIds.end()
            || event.frameIndex >= static_cast<int>(model.frames.size()))
        {
            errorMessage = "Track lifecycle events must reference existing partials and frames.";
            return false;
        }
    }

    for (const auto& cost : model.assignmentCosts)
    {
        if (cost.frameIndex < 0 || cost.partialId < 0 || cost.peakIndex < 0
            || partialIds.find(cost.partialId) == partialIds.end()
            || cost.frameIndex >= static_cast<int>(model.frames.size()))
        {
            errorMessage = "Assignment costs must reference existing partials and frames.";
            return false;
        }
        if (!finite(cost.frequencyCost) || !finite(cost.amplitudeCost) || !finite(cost.smrCost)
            || !finite(cost.slopeCost) || !finite(cost.birthDeathCost) || !finite(cost.spuriousPeakCost)
            || !finite(cost.totalCost))
        {
            errorMessage = "Assignment cost values must be finite.";
            return false;
        }
    }

    for (const auto& transient : model.transients)
    {
        if (transient.frameIndex < 0 || transient.frameIndex >= static_cast<int>(model.frames.size())
            || transient.bufferIndex < -1 || !finite(transient.onsetSeconds)
            || !finite(transient.windowStartSeconds) || !finite(transient.windowEndSeconds)
            || !finite(transient.gain))
        {
            errorMessage = "Transient marker fields must reference existing frames and be finite.";
            return false;
        }
        if (transient.onsetSeconds < 0.0 || transient.windowStartSeconds < 0.0
            || transient.windowEndSeconds < 0.0)
        {
            errorMessage = "Transient marker times must be non-negative.";
            return false;
        }
        if (transient.windowStartSeconds > transient.onsetSeconds
            || transient.onsetSeconds > transient.windowEndSeconds)
        {
            errorMessage = "Transient onset must be inside its window.";
            return false;
        }
        if (transient.gain < 0.0)
        {
            errorMessage = "Transient marker gain must be non-negative.";
            return false;
        }
    }

    for (const auto& buffer : model.transientBuffers)
    {
        if (buffer.markerIndex < 0 || buffer.markerIndex >= static_cast<int>(model.transients.size())
            || buffer.sampleRate <= 0 || !finite(buffer.gain) || buffer.gain < 0.0)
        {
            errorMessage = "Transient buffer marker index, sample rate, and gain must be valid.";
            return false;
        }
        for (const float sample : buffer.samples)
        {
            if (!std::isfinite(sample))
            {
                errorMessage = "Transient buffer samples must be finite.";
                return false;
            }
        }
    }

    for (std::size_t transientIndex = 0; transientIndex < model.transients.size(); ++transientIndex)
    {
        const int bufferIndex = model.transients[transientIndex].bufferIndex;
        if (bufferIndex < 0)
            continue;
        if (bufferIndex >= static_cast<int>(model.transientBuffers.size())
            || model.transientBuffers[static_cast<std::size_t>(bufferIndex)].markerIndex
                   != static_cast<int>(transientIndex))
        {
            errorMessage = "Transient marker buffer indices must reference matching transient buffers.";
            return false;
        }
    }

    return true;
}

std::string serializeModernAtsModelToJson(const ModernAtsModel& model)
{
    std::ostringstream out;
    out.imbue(std::locale::classic());

    out << "{\"schema_version\":" << model.header.schemaVersion;
    out << ",\"header\":{";
    out << "\"analysis_mode\":";
    appendQuoted(out, toString(model.header.analysisMode));
    out << ",\"tracker_mode\":";
    appendQuoted(out, toString(model.header.trackerMode));
    out << ",\"sample_rate\":" << model.header.sampleRate;
    out << ",\"hop_size_samples\":" << model.header.hopSizeSamples;
    out << ",\"window_size_samples\":" << model.header.windowSizeSamples;
    out << ",\"input_channel_count\":" << model.header.inputChannelCount;
    out << ",\"channel_policy\":";
    appendQuoted(out, toString(model.header.channelPolicy));
    out << ",\"parameter_hash\":";
    appendQuoted(out, model.header.parameterHash);
    out << ",\"source_hash\":";
    appendQuoted(out, model.header.sourceHash);
    out << '}';

    out << ",\"frames\":[";
    for (std::size_t i = 0; i < model.frames.size(); ++i)
    {
        if (i != 0)
            out << ',';
        const auto& frame = model.frames[i];
        out << "{\"time_seconds\":" << number(frame.timeSeconds);
        out << ",\"active_partial_ids\":";
        appendIntArray(out, frame.activePartialIds);
        out << ",\"orphan_residual_band_energy\":";
        appendDoubleArray(out, frame.orphanResidualBandEnergy);
        out << '}';
    }
    out << ']';

    out << ",\"partials\":[";
    for (std::size_t i = 0; i < model.partials.size(); ++i)
    {
        if (i != 0)
            out << ',';
        const auto& partial = model.partials[i];
        out << "{\"id\":" << partial.id;
        out << ",\"label\":";
        appendQuoted(out, partial.label);
        out << ",\"channel\":" << partial.channel;
        out << ",\"frames\":[";
        for (std::size_t j = 0; j < partial.frames.size(); ++j)
        {
            if (j != 0)
                out << ',';
            const auto& frame = partial.frames[j];
            out << "{\"amplitude\":" << number(frame.amplitude);
            out << ",\"frequency_hz\":" << number(frame.frequencyHz);
            out << ",\"phase_radians\":" << number(frame.phaseRadians);
            out << ",\"smr_db\":" << number(frame.smrDb);
            out << ",\"noise_energy\":" << number(frame.noiseEnergy);
            out << ",\"confidence\":" << number(frame.confidence);
            out << ",\"reason\":";
            appendQuoted(out, toString(frame.reason));
            out << '}';
        }
        out << "]}";
    }
    out << ']';

    out << ",\"track_lifecycle\":[";
    for (std::size_t i = 0; i < model.trackLifecycle.size(); ++i)
    {
        if (i != 0)
            out << ',';
        const auto& event = model.trackLifecycle[i];
        out << "{\"partial_id\":" << event.partialId;
        out << ",\"frame_index\":" << event.frameIndex;
        out << ",\"kind\":";
        appendQuoted(out, toString(event.kind));
        out << ",\"detail\":";
        appendQuoted(out, event.detail);
        out << '}';
    }
    out << ']';

    out << ",\"assignment_costs\":[";
    for (std::size_t i = 0; i < model.assignmentCosts.size(); ++i)
    {
        if (i != 0)
            out << ',';
        const auto& cost = model.assignmentCosts[i];
        out << "{\"frame_index\":" << cost.frameIndex;
        out << ",\"partial_id\":" << cost.partialId;
        out << ",\"peak_index\":" << cost.peakIndex;
        out << ",\"frequency_cost\":" << number(cost.frequencyCost);
        out << ",\"amplitude_cost\":" << number(cost.amplitudeCost);
        out << ",\"smr_cost\":" << number(cost.smrCost);
        out << ",\"slope_cost\":" << number(cost.slopeCost);
        out << ",\"birth_death_cost\":" << number(cost.birthDeathCost);
        out << ",\"spurious_peak_cost\":" << number(cost.spuriousPeakCost);
        out << ",\"total_cost\":" << number(cost.totalCost);
        out << '}';
    }
    out << ']';

    out << ",\"transients\":[";
    for (std::size_t i = 0; i < model.transients.size(); ++i)
    {
        if (i != 0)
            out << ',';
        const auto& transient = model.transients[i];
        out << "{\"frame_index\":" << transient.frameIndex;
        out << ",\"buffer_index\":" << transient.bufferIndex;
        out << ",\"onset_seconds\":" << number(transient.onsetSeconds);
        out << ",\"window_start_seconds\":" << number(transient.windowStartSeconds);
        out << ",\"window_end_seconds\":" << number(transient.windowEndSeconds);
        out << ",\"gain\":" << number(transient.gain);
        out << '}';
    }
    out << ']';

    out << ",\"transient_buffers\":[";
    for (std::size_t i = 0; i < model.transientBuffers.size(); ++i)
    {
        if (i != 0)
            out << ',';
        const auto& buffer = model.transientBuffers[i];
        out << "{\"marker_index\":" << buffer.markerIndex;
        out << ",\"sample_rate\":" << buffer.sampleRate;
        out << ",\"gain\":" << number(buffer.gain);
        out << ",\"samples\":[";
        for (std::size_t j = 0; j < buffer.samples.size(); ++j)
        {
            if (j != 0)
                out << ',';
            out << number(buffer.samples[j]);
        }
        out << "]}";
    }
    out << ']';

    out << ",\"warnings\":[";
    for (std::size_t i = 0; i < model.warnings.size(); ++i)
    {
        if (i != 0)
            out << ',';
        appendQuoted(out, model.warnings[i]);
    }
    out << "]}";

    return out.str();
}

std::size_t estimateModernAtsModelStateBytes(const ModernAtsModel& model)
{
    return serializeModernAtsModelToJson(model).size();
}

} // namespace ats
