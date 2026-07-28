#include "LegacyStateCodec.h"

#include <algorithm>
#include <limits>

namespace ats
{
namespace
{
constexpr std::size_t kMaxBlobBytes = 128u * 1024u * 1024u;
constexpr std::int32_t kMaxSlots = 16;
constexpr std::int32_t kMaxResidualBands = 256;

void writeString(ByteWriter& writer, const std::string& value)
{
    writer.writeU64(static_cast<std::uint64_t>(value.size()));
    if (!value.empty()) writer.writeBytes(value.data(), value.size());
}

bool readString(ByteReader& reader, std::string& value)
{
    std::uint64_t size = 0;
    if (!reader.readU64(size) || size > reader.remaining() || size > kMaxBlobBytes) return false;
    value.resize(static_cast<std::size_t>(size));
    return size == 0 || reader.readBytes(value.data(), static_cast<std::size_t>(size));
}

void writeBlob(ByteWriter& writer, const std::vector<std::byte>& blob)
{
    writer.writeU64(static_cast<std::uint64_t>(blob.size()));
    if (!blob.empty()) writer.writeBytes(blob.data(), blob.size());
}

bool readBlob(ByteReader& reader, std::vector<std::byte>& blob)
{
    std::uint64_t size = 0;
    if (!reader.readU64(size) || size > reader.remaining() || size > kMaxBlobBytes) return false;
    blob.resize(static_cast<std::size_t>(size));
    return size == 0 || reader.readBytes(blob.data(), static_cast<std::size_t>(size));
}

void writeAnalysis(ByteWriter& writer, const AnalysisParams& a)
{
    writer.writeF64(a.start); writer.writeF64(a.duration); writer.writeF64(a.lowest_freq);
    writer.writeF64(a.highest_freq); writer.writeF64(a.freq_dev);
    writer.writeI32(a.win_cycles); writer.writeI32(a.win_type); writer.writeI32(a.win_size);
    writer.writeF64(a.hop_size); writer.writeF64(a.lowest_mag);
    writer.writeI32(a.track_len); writer.writeI32(a.min_seg_len); writer.writeI32(a.min_gap_len);
    writer.writeF64(a.SMR_thres); writer.writeF64(a.min_seg_SMR);
    writer.writeF64(a.last_peak_cont); writer.writeF64(a.SMR_cont); writer.writeI32(a.type);
}

bool readAnalysis(ByteReader& reader, AnalysisParams& a)
{
    return reader.readF64(a.start) && reader.readF64(a.duration) && reader.readF64(a.lowest_freq) &&
           reader.readF64(a.highest_freq) && reader.readF64(a.freq_dev) &&
           reader.readI32(a.win_cycles) && reader.readI32(a.win_type) && reader.readI32(a.win_size) &&
           reader.readF64(a.hop_size) && reader.readF64(a.lowest_mag) &&
           reader.readI32(a.track_len) && reader.readI32(a.min_seg_len) && reader.readI32(a.min_gap_len) &&
           reader.readF64(a.SMR_thres) && reader.readF64(a.min_seg_SMR) &&
           reader.readF64(a.last_peak_cont) && reader.readF64(a.SMR_cont) && reader.readI32(a.type);
}

void writeSynth(ByteWriter& writer, const SynthParams& s)
{
    writer.writeF64(s.startTime); writer.writeF64(s.duration);
    writer.writeU8(s.useAllPartials ? 1 : 0); writer.writeU8(s.usePartialsPhase ? 1 : 0);
    writer.writeF64(s.partialsGain); writer.writeF64(s.residualGain);
    writer.writeF64(s.frequencyFactor); writer.writeF64(s.timeFactor); writer.writeF64(s.sampleRate);
}

bool readSynth(ByteReader& reader, SynthParams& s)
{
    std::uint8_t all = 0, phase = 0;
    if (!reader.readF64(s.startTime) || !reader.readF64(s.duration) || !reader.readU8(all) ||
        !reader.readU8(phase) || !reader.readF64(s.partialsGain) || !reader.readF64(s.residualGain) ||
        !reader.readF64(s.frequencyFactor) || !reader.readF64(s.timeFactor) || !reader.readF64(s.sampleRate))
        return false;
    s.useAllPartials = all != 0;
    s.usePartialsPhase = phase != 0;
    return true;
}

void writeResidual(ByteWriter& writer, const ResidualState& state)
{
    writer.writeU8(state.hasResidual ? 1 : 0);
    writer.writeI32(state.activeBandCount); writer.writeI32(state.activeBandFrameCount);
    writer.writeF64(state.totalEnergy); writer.writeI32(state.peakBandIndex); writer.writeF64(state.peakBandEnergy);
    writer.writeI32(static_cast<std::int32_t>(state.bands.size()));
    for (const auto& band : state.bands)
    {
        writer.writeI32(band.bandIndex); writer.writeF64(band.partialNoisePower); writer.writeF64(band.orphanResidualPower);
    }
}

bool readResidual(ByteReader& reader, ResidualState& state)
{
    std::uint8_t has = 0;
    std::int32_t count = 0;
    if (!reader.readU8(has) || !reader.readI32(state.activeBandCount) || !reader.readI32(state.activeBandFrameCount) ||
        !reader.readF64(state.totalEnergy) || !reader.readI32(state.peakBandIndex) || !reader.readF64(state.peakBandEnergy) ||
        !reader.readI32(count) || count < 0 || count > kMaxResidualBands)
        return false;
    state.hasResidual = has != 0;
    state.bands.resize(static_cast<std::size_t>(count));
    for (auto& band : state.bands)
        if (!reader.readI32(band.bandIndex) || !reader.readF64(band.partialNoisePower) || !reader.readF64(band.orphanResidualPower)) return false;
    return true;
}
}

std::vector<std::byte> LegacyStateCodec::encode(const LegacyState& state, std::uint32_t version)
{
    if (version < kMinimumReadableVersion || version > kCurrentVersion) return {};
    ByteWriter writer;
    writer.reserve(4096);
    writer.writeU32(StateHeader::kMagic); writer.writeU32(version);
    writeAnalysis(writer, state.analysis); writeSynth(writer, state.synth);
    if (version >= 13)
    {
        writer.writeI32(static_cast<std::int32_t>(state.frequencyShape.mode));
        writer.writeF64(state.frequencyShape.amount); writer.writeF64(state.frequencyShape.pivotHz);
    }
    if (version >= 3) writer.writeF64(state.outputTrim);
    writeBlob(writer, state.atsModel);
    if (version >= 4) writer.writeU8(state.residualOnly ? 1 : 0);
    if (version >= 11) writer.writeI32(state.classicPreviewMode);
    if (version >= 5) writeString(writer, state.morphRecipe);
    if (version >= 6) writer.writeF64(state.morphAmount);
    if (version >= 7)
    {
        writer.writeF64(state.morphPerformanceMacro); writer.writeF64(state.morphSmoothingMs);
        for (std::size_t i = 0; i < 6; ++i) { writer.writeF64(state.morphLaneAmounts[i]); writer.writeU8(state.morphLaneLinked[i] ? 1 : 0); }
    }
    if (version >= 8)
    {
        writer.writeI32(state.morphGridType); writer.writeI32(state.morphCorrespondenceStrategy);
        writer.writeI32(state.morphUnmatchedSourcePolicy); writer.writeI32(state.morphUnmatchedTargetPolicy);
        writer.writeU8(state.morphDiscardOptIn ? 1 : 0); writer.writeI32(state.morphFrequencyDomain);
        writer.writeI32(state.morphAmplitudeDomain); writer.writeI32(state.morphPhaseStrategy); writer.writeI32(state.morphTransientPolicy);
    }
    if (version >= 9)
    {
        writer.writeI32(3);
        for (const auto& slot : state.variations)
        {
            writer.writeU8(slot.hasSnapshot ? 1 : 0); writeString(writer, slot.name);
            writer.writeF64(slot.morphAmount); writer.writeF64(slot.performanceMacro); writer.writeF64(slot.smoothingMs);
            for (std::size_t i = 0; i < 6; ++i) { writer.writeF64(slot.laneAmounts[i]); writer.writeU8(slot.laneLinked[i] ? 1 : 0); }
        }
    }
    if (version >= 10) for (const auto& path : state.morphSourcePaths) writeString(writer, path);
    if (version >= 12)
    {
        writer.writeI32(2);
        for (const auto& residual : state.residualStates) writeResidual(writer, residual);
    }
    if (version >= 14)
    {
        const auto& s = state.smearnov;
        writer.writeF64(s.blurMs); writer.writeF64(s.sharpen); writer.writeF64(s.exaggerateDb); writer.writeF64(s.maxDb);
        writer.writeI32(s.targets); writer.writeI32(s.scope); writer.writeF64(s.weightDetFlux); writer.writeF64(s.weightResFlux);
        writer.writeF64(s.weightHfcFlux); writer.writeF64(s.weightBirth); writer.writeF64(s.weightPhaseDev);
        writer.writeF64(s.thresholdLambda); writer.writeF64(s.thresholdAutoScale); writer.writeF64(s.preMarginMs);
        writer.writeI32(s.peakWindowFrames); writer.writeI32(s.minGapFrames); writer.writeI32(s.maxAttackFrames);
        writer.writeU8(s.usePhaseDeviation ? 1 : 0); writer.writeU8(s.useRpcaFallback ? 1 : 0); writer.writeI32(s.controlTab);
    }
    if (version >= 15) writer.writeF64(state.smearnov.mix);
    if (version >= 16) writer.writeI32(state.morphQuality);
    if (version >= 17) for (const auto& blob : state.morphSoundData) writeBlob(writer, blob);
    return std::move(writer).take();
}

std::optional<LegacyState> LegacyStateCodec::decode(const void* data, std::size_t size)
{
    if (data == nullptr || size < 8) return std::nullopt;
    ByteReader reader(static_cast<const std::byte*>(data), size);
    std::uint32_t magic = 0, version = 0;
    if (!reader.readU32(magic) || !reader.readU32(version) || magic != StateHeader::kMagic ||
        version < kMinimumReadableVersion || version > kCurrentVersion)
        return std::nullopt;

    LegacyState state;
    if (!readAnalysis(reader, state.analysis) || !readSynth(reader, state.synth)) return std::nullopt;
    if (version >= 13)
    {
        std::int32_t mode = 0;
        if (!reader.readI32(mode) || !reader.readF64(state.frequencyShape.amount) || !reader.readF64(state.frequencyShape.pivotHz)) return std::nullopt;
        state.frequencyShape.mode = static_cast<FrequencyShapeMode>(std::clamp(mode, 0, 3));
    }
    if (version >= 3 && !reader.readF64(state.outputTrim)) return std::nullopt;
    if (!readBlob(reader, state.atsModel)) return std::nullopt;
    if (version >= 4)
    {
        std::uint8_t value = 0; if (!reader.readU8(value)) return std::nullopt; state.residualOnly = value != 0;
    }
    if (version >= 11 && !reader.readI32(state.classicPreviewMode)) return std::nullopt;
    if (version >= 5 && !readString(reader, state.morphRecipe)) return std::nullopt;
    if (version >= 6 && !reader.readF64(state.morphAmount)) return std::nullopt;
    if (version >= 7)
    {
        if (!reader.readF64(state.morphPerformanceMacro) || !reader.readF64(state.morphSmoothingMs)) return std::nullopt;
        for (std::size_t i = 0; i < 6; ++i) { std::uint8_t linked = 0; if (!reader.readF64(state.morphLaneAmounts[i]) || !reader.readU8(linked)) return std::nullopt; state.morphLaneLinked[i] = linked != 0; }
    }
    if (version >= 8)
    {
        std::uint8_t discard = 0;
        if (!reader.readI32(state.morphGridType) || !reader.readI32(state.morphCorrespondenceStrategy) ||
            !reader.readI32(state.morphUnmatchedSourcePolicy) || !reader.readI32(state.morphUnmatchedTargetPolicy) ||
            !reader.readU8(discard) || !reader.readI32(state.morphFrequencyDomain) || !reader.readI32(state.morphAmplitudeDomain) ||
            !reader.readI32(state.morphPhaseStrategy) || !reader.readI32(state.morphTransientPolicy)) return std::nullopt;
        state.morphDiscardOptIn = discard != 0;
    }
    if (version >= 9)
    {
        std::int32_t count = 0; if (!reader.readI32(count) || count < 0 || count > kMaxSlots) return std::nullopt;
        for (std::int32_t slotIndex = 0; slotIndex < count; ++slotIndex)
        {
            VariationState slot; std::uint8_t present = 0;
            if (!reader.readU8(present) || !readString(reader, slot.name) || !reader.readF64(slot.morphAmount) || !reader.readF64(slot.performanceMacro) || !reader.readF64(slot.smoothingMs)) return std::nullopt;
            slot.hasSnapshot = present != 0;
            for (std::size_t i = 0; i < 6; ++i) { std::uint8_t linked = 0; if (!reader.readF64(slot.laneAmounts[i]) || !reader.readU8(linked)) return std::nullopt; slot.laneLinked[i] = linked != 0; }
            if (slotIndex < 3) state.variations[static_cast<std::size_t>(slotIndex)] = std::move(slot);
        }
    }
    if (version >= 10) for (auto& path : state.morphSourcePaths) if (!readString(reader, path)) return std::nullopt;
    if (version >= 12)
    {
        std::int32_t count = 0; if (!reader.readI32(count) || count < 0 || count > kMaxSlots) return std::nullopt;
        for (std::int32_t i = 0; i < count; ++i) { ResidualState residual; if (!readResidual(reader, residual)) return std::nullopt; if (i < 2) state.residualStates[static_cast<std::size_t>(i)] = std::move(residual); }
    }
    if (version >= 14)
    {
        auto& s = state.smearnov; std::uint8_t phase = 0, rpca = 0;
        if (!reader.readF64(s.blurMs) || !reader.readF64(s.sharpen) || !reader.readF64(s.exaggerateDb) || !reader.readF64(s.maxDb) ||
            !reader.readI32(s.targets) || !reader.readI32(s.scope) || !reader.readF64(s.weightDetFlux) || !reader.readF64(s.weightResFlux) ||
            !reader.readF64(s.weightHfcFlux) || !reader.readF64(s.weightBirth) || !reader.readF64(s.weightPhaseDev) || !reader.readF64(s.thresholdLambda) ||
            !reader.readF64(s.thresholdAutoScale) || !reader.readF64(s.preMarginMs) || !reader.readI32(s.peakWindowFrames) || !reader.readI32(s.minGapFrames) ||
            !reader.readI32(s.maxAttackFrames) || !reader.readU8(phase) || !reader.readU8(rpca) || !reader.readI32(s.controlTab)) return std::nullopt;
        s.usePhaseDeviation = phase != 0; s.useRpcaFallback = rpca != 0;
    }
    if (version >= 15 && !reader.readF64(state.smearnov.mix)) return std::nullopt;
    if (version >= 16 && !reader.readI32(state.morphQuality)) return std::nullopt;
    if (version >= 17) for (auto& blob : state.morphSoundData) if (!readBlob(reader, blob)) return std::nullopt;
    return state;
}

} // namespace ats
