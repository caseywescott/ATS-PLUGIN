#pragma once

#include "../core/AtsStateCodec.h"
#include "../core/AtsFrequencyShape.h"
#include "../core/AtsTransientSmear.h"
#include "../core/JobScheduler.h"
#include "../core/MidiNoteRouter.h"
#include "../core/ParameterRegistry.h"
#include "../core/PlaybackBuffers.h"
#include "../core/UndoHistory.h"
#include "../core/AtsMathExpr.h"
#include "../core/AtsMorphRecipe.h"
#include "../core/AtsMorphTransportCorrespondence.h"
#include "../dsp/MorphTrajectoryRenderer.h"
#include "../engine/MorphTimelineService.h"
#include "../engine/ResidualEditService.h"
#include "../engine/PartialEditService.h"
#include "../engine/SourceImportService.h"
#include "../engine/AtsEngine.h"
#include "../state/FactoryPresetState.h"
#include "../state/LegacyStateCodec.h"
#include "../state/LegacyStateParameters.h"
#include "../state/ParameterPresetCodec.h"
#include "../state/PresetService.h"
#include "../platform/FileService.h"
#include "../ui/Editor.h"

#include <algorithm>
#include <atomic>
#include <array>
#include <cmath>
#include <cstddef>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <future>
#include <fstream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ats::host
{

enum class HostFormat : std::uint8_t { vst3, au, standalone };
enum class SourceIdentity : std::uint8_t { current, sourceA, sourceB, morphTimeline };

struct HostIdentity
{
    std::string_view productName = "ATS Resynth";
    std::string_view company = "ATSPlugin";
    std::string_view bundleId = "com.atsplugin.resynth";
    std::string_view vst3ManufacturerCode = "AtPl";
    std::string_view vst3ProductCode = "Atr1";
    std::string_view category = "Audio effect";
    double latencySeconds = 0.0;
    double tailSeconds = 0.0;
    bool midiInput = true;
    bool midiOutput = false;

    [[nodiscard]] static constexpr HostIdentity current() noexcept { return {}; }
};

struct HostBusLayout
{
    std::uint32_t inputChannels = 0;
    std::uint32_t outputChannels = 2;
};

enum class HostState : std::uint8_t { cold, prepared, released };

struct HostPrepareSpec
{
    double sampleRate = 0.0;
    std::uint32_t maximumBlockSize = 0;
    std::uint32_t outputChannels = 0;
    std::uint32_t inputChannels = 0;
};

struct HostParameterInfo
{
    std::string_view id;
    std::string_view name;
    std::string_view group;
    ParameterType type = ParameterType::floatValue;
    float minimum = 0.0f;
    float maximum = 1.0f;
    float defaultValue = 0.0f;
    float interval = 0.0001f;
    float skew = 1.0f;
    std::uint32_t hostNumericId = 0;
    bool automatable = true;
    bool readOnly = false;
    const std::string_view* choices = nullptr;
    std::uint16_t choiceCount = 0;
};

// The Session page has state that is intentionally independent of any native
// editor instance.  Native wrappers forward typed editor commands here, so a
// controller recreation or a view close cannot leave preview / Smearnov work
// owned by the UI object.
struct SessionRuntimeSnapshot
{
    int previewSource = 0;
    int previewLayer = 0;
    bool previewPlaying = false;
    float normalizedPlayhead = 0.0f;
    bool liveSmearEnabled = false;
    std::array<bool, 3> variationStored {};
    bool smearnovRunning = false;
    bool smearedSourceAvailable = false;
    std::uint64_t timelineRevision = 0;
    std::uint64_t timbreRevision = 0;
    std::uint64_t onsetRevision = 0;
    std::vector<float> timeline { 0.0f, 1.0f };
    std::vector<float> timbrePlan;
    std::vector<float> onsetOverlay;
};

using HostStateChunk = std::vector<std::byte>;

inline std::vector<std::byte> serializeAtsSoundForState(const ATS_SOUND* sound) noexcept
{
    if (sound == nullptr)
        return {};
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path = std::filesystem::temp_directory_path() /
                      ("ats-state-" + std::to_string(stamp) + ".ats");
    // ats_save dereferences pha/band_energy unconditionally for the types that
    // claim them, and the source's recorded atsType can be stale or unset (an
    // audio analysis leaves it at the amp+freq default). Derive the type from
    // what the sound actually carries, or residual edits are dropped on the way
    // into the state chunk and undo sees an unchanged state.
    const int type = 1 + (sound->pha != nullptr ? 1 : 0) + (sound->band_energy != nullptr ? 2 : 0);
    if (ats_save(const_cast<ATS_SOUND*>(sound), path.u8string().c_str(), 0.0, type) < 0)
        return {};
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    std::vector<std::byte> bytes;
    if (stream)
    {
        const auto size = stream.tellg();
        if (size >= 0)
        {
            bytes.resize(static_cast<std::size_t>(size));
            stream.seekg(0);
            stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            if (!stream)
                bytes.clear();
        }
    }
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    return bytes;
}

inline std::optional<engine::ImportedSource> deserializeAtsSoundForState(
    const std::vector<std::byte>& bytes, int slot, std::string pathHint = {})
{
    if (bytes.empty())
        return std::nullopt;
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path = std::filesystem::temp_directory_path() /
                      ("ats-state-restore-" + std::to_string(stamp) + ".ats");
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream)
        return std::nullopt;
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    stream.close();
    auto* raw = static_cast<ATS_SOUND*>(std::calloc(1, sizeof(ATS_SOUND)));
    int type = 0;
    const auto loaded = raw != nullptr && ats_load(raw, path.u8string().c_str(), &type) >= 0;
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    if (!loaded)
    {
        if (raw != nullptr) std::free(raw);
        return std::nullopt;
    }
    engine::ImportedSource source;
    source.path = { std::move(pathHint) };
    source.slot = slot;
    source.kind = engine::SourceImportKind::ats;
    source.name = source.path.utf8;
    source.atsType = type;
    source.sound.reset(raw);
    return source;
}

inline ResidualState residualStateForSound(const ATS_SOUND* sound)
{
    ResidualState state;
    if (sound == nullptr || sound->band_energy == nullptr || sound->frames <= 0)
        return state;
    for (int band = 0; band < ATSA_CRITICAL_BANDS; ++band)
    {
        ResidualBandState bandState;
        bandState.bandIndex = band;
        for (int frame = 0; frame < sound->frames; ++frame)
        {
            const auto energy = std::max(0.0, sound->band_energy[band][frame]);
            bandState.orphanResidualPower += energy;
            state.totalEnergy += energy;
            if (energy > 0.0)
                ++state.activeBandFrameCount;
        }
        if (bandState.orphanResidualPower > 0.0)
        {
            state.hasResidual = true;
            ++state.activeBandCount;
            if (bandState.orphanResidualPower > state.peakBandEnergy)
            {
                state.peakBandEnergy = bandState.orphanResidualPower;
                state.peakBandIndex = band;
            }
        }
        state.bands.push_back(bandState);
    }
    return state;
}

class HostAdapter
{
public:
    static constexpr std::size_t kMaximumStateChunkBytes = 64u * 1024u * 1024u;

    HostAdapter() = default;
    ~HostAdapter()
    {
        cancelAnalysis();
        if (analysisFuture_.valid())
            analysisFuture_.wait();
        cancelSmearnov();
        if (smearnovFuture_.valid())
            smearnovFuture_.wait();
    }
    HostAdapter(const HostAdapter&) = delete;
    HostAdapter& operator=(const HostAdapter&) = delete;

    [[nodiscard]] HostState state() const noexcept { return state_; }
    [[nodiscard]] HostFormat format() const noexcept { return format_; }
    [[nodiscard]] const HostIdentity& identity() const noexcept { return identity_; }
    [[nodiscard]] const HostPrepareSpec& prepareSpec() const noexcept { return spec_; }
    [[nodiscard]] HostBusLayout busLayout() const noexcept
    { return { spec_.inputChannels, spec_.outputChannels }; }
    [[nodiscard]] bool editorAttached() const noexcept { return editorAttached_; }
    [[nodiscard]] bool editorVisible() const noexcept { return editorVisible_; }
    [[nodiscard]] float editorScale() const noexcept { return editorScale_; }
    [[nodiscard]] std::array<float, 2> editorLogicalSize() const noexcept
    { return { editorWidth_, editorHeight_ }; }
    [[nodiscard]] const ParameterStore& parameters() const noexcept { return parameters_; }
    [[nodiscard]] ParameterStore& parameters() noexcept { return parameters_; }
    [[nodiscard]] const HostStateChunk& stateChunk() const noexcept { return stateChunk_; }
    [[nodiscard]] ui::TelemetrySnapshot telemetry() const
    {
        auto snapshot = telemetry_.snapshot();
        snapshot.analysisRunning = analysisRunning_.load(std::memory_order_acquire);
        snapshot.progress = analysisProgress_.load(std::memory_order_acquire);
        if (snapshot.analysisRunning)
            snapshot.state = ui::UiState::analyzing;
        return snapshot;
    }

    [[nodiscard]] ui::EditorSnapshot editorSnapshot() const
    {
        ui::EditorSnapshot snapshot;
        snapshot.revision = editorSnapshotRevision_.fetch_add(1, std::memory_order_relaxed) + 1;
        snapshot.telemetry = telemetry();
        snapshot.telemetry.canUndo = history_.canUndo();
        snapshot.telemetry.canRedo = history_.canRedo();
        for (const auto& descriptor : ParameterRegistry::descriptors())
            if (descriptor.denseIndex < snapshot.parameters.size())
                snapshot.parameters[descriptor.denseIndex] = parameters_.read(descriptor.denseIndex);

        const auto publishSource = [&snapshot](std::size_t index,
                                                const engine::ImportedSource* source) {
            if (source == nullptr)
                return;
            snapshot.sourceAvailable[index] = true;
            snapshot.sourceNames[index] = source->name;
            snapshot.sourceResidualAvailable[index] = source->sound
                && source->sound.get()->band_energy != nullptr;
            if (source->sound)
            {
                snapshot.sourceDurations[index] = static_cast<float>(source->sound.get()->dur);
                snapshot.sourcePartialCounts[index] = source->sound.get()->partials;
            }
            else if (source->sampleRate > 0)
                snapshot.sourceDurations[index] = static_cast<float>(source->monoSamples.size()) /
                                                  static_cast<float>(source->sampleRate);
        };
        publishSource(0, currentSource_ ? &*currentSource_ : nullptr);
        publishSource(1, sources_[0] ? &*sources_[0] : nullptr);
        publishSource(2, sources_[1] ? &*sources_[1] : nullptr);
        snapshot.timeline = timeline_;
        snapshot.timbrePlan = timbrePlan_;
        snapshot.onsets = onsetOverlay_;

        const auto* spectrumSource = spectralViewSource_ == 0
            ? (currentSource_ ? &*currentSource_ : nullptr)
            : (spectralViewSource_ <= 2 && sources_[static_cast<std::size_t>(spectralViewSource_ - 1)]
                   ? &*sources_[static_cast<std::size_t>(spectralViewSource_ - 1)] : nullptr);
        const auto publishFrequencyShape = [this](ui::SpectralTraceInput& input,
                                                  const ATS_SOUND* sound) {
            FrequencyShapeSettings settings;
            settings.mode = static_cast<FrequencyShapeMode>(std::clamp(
                static_cast<int>(std::lround(parameters_.read("freqShapeMode"))), 0, 3));
            settings.amount = static_cast<double>(parameters_.read("freqShapeAmount"));
            settings.pivotHz = static_cast<double>(parameters_.read("freqShapePivotHz"));
            const auto resolved = resolveFrequencyShapeSettingsForSound(sound, settings);
            input.frequencyShapeMode = static_cast<ui::FrequencyShapeMode>(
                static_cast<int>(settings.mode));
            input.frequencyShapeAmount = settings.amount;
            input.frequencyShapePivotHz = settings.pivotHz;
            input.resolvedFrequencyShapePivotHz = resolved.resolvedPivotHz;
            input.displayShapeRevision = frequencyShapeRevision();
        };
        const auto publishDisplayReferences = [this](ui::SpectralTraceInput& input,
                                                       const ATS_SOUND* sound,
                                                       int sourceId) {
            const auto index = static_cast<std::size_t>(std::clamp(sourceId, 0, 2));
            auto& ampReference = spectrumAmpDisplayReferences_[index];
            auto& residualReference = spectrumResidualDisplayReferences_[index];
            if (!(ampReference > 0.0) || !std::isfinite(ampReference))
            {
                ampReference = 0.0;
                if (sound != nullptr && sound->amp != nullptr)
                    for (int partial = 0; partial < sound->partials; ++partial)
                        if (sound->amp[partial] != nullptr)
                            for (int frame = 0; frame < sound->frames; ++frame)
                                ampReference = std::max(ampReference,
                                                         sound->amp[partial][frame]);
            }
            if (!(residualReference > 0.0) || !std::isfinite(residualReference))
            {
                residualReference = 0.0;
                if (sound != nullptr && sound->band_energy != nullptr)
                    for (int band = 0; band < ATSA_CRITICAL_BANDS; ++band)
                        if (sound->band_energy[band] != nullptr)
                            for (int frame = 0; frame < sound->frames; ++frame)
                                residualReference = std::max(residualReference,
                                                              sound->band_energy[band][frame]);
            }
            input.displayAmpPeak = ampReference;
            input.displayResidualPeak = residualReference;
        };
        const auto buildSpectrum = [&](const engine::ImportedSource* source, int sourceId,
                                       bool preserveDisplayReference = true)
            -> std::optional<ui::SpectralTraceSnapshot> {
            if (source == nullptr || !source->sound)
                return std::nullopt;
            {
            const auto* sound = source->sound.get();
            ui::SpectralTraceInput input;
            input.source = static_cast<ui::SpectralTraceSource>(std::clamp(sourceId, 0, 2));
            input.sourceRevision = sourceId == 0
                ? currentSourceRevision_
                : sourceRevisions_[static_cast<std::size_t>(sourceId - 1)];
            input.durationSeconds = sound->dur;
            input.partialCount = static_cast<std::size_t>(std::max(0, sound->partials));
            input.frameCount = static_cast<std::size_t>(std::max(0, sound->frames));
            input.nyquistHz = sound->srate > 0.0 ? sound->srate * 0.5 : 22050.0;
            publishFrequencyShape(input, sound);
            if (preserveDisplayReference)
                publishDisplayReferences(input, sound, sourceId);
            input.smearnovDetectionCurve = smearnovDetectionCurve_;
            input.smearnovThresholdCurve = smearnovThresholdCurve_;
            if (!onsetHandleOverlay_.empty())
                for (const auto& onset : onsetHandleOverlay_)
                    input.onsets.push_back({ onset.norm, onset.strength,
                                             onset.preNorm, onset.peakNorm });
            else
                for (const auto onset : onsetOverlay_)
                    input.onsets.push_back({ onset, 0.0, onset, onset });
            input.partials.reserve(static_cast<std::size_t>(std::max(0, sound->partials)));
            const auto shapeRanks = buildFrequencyShapeRanks(sound);
            for (int partial = 0; partial < sound->partials; ++partial)
            {
                ui::SpectralPartialInput trace;
                trace.partialIndex = partial;
                trace.frequencyShapeRank = partial < static_cast<int>(shapeRanks.size())
                    ? shapeRanks[static_cast<std::size_t>(partial)] : 0;
                trace.frames.reserve(static_cast<std::size_t>(std::max(0, sound->frames)));
                for (int frame = 0; frame < sound->frames; ++frame)
                {
                    const auto amplitude = sound->amp != nullptr && sound->amp[partial] != nullptr
                        ? sound->amp[partial][frame] : 0.0;
                    const auto frequency = sound->frq != nullptr && sound->frq[partial] != nullptr
                        ? sound->frq[partial][frame] : 0.0;
                    trace.frames.push_back({ frequency, amplitude });
                }
                input.partials.push_back(std::move(trace));
            }
            if (sound->band_energy)
                for (int band = 0; band < ATSA_CRITICAL_BANDS; ++band)
                {
                    ui::SpectralResidualBandInput residual;
                    residual.bandIndex = band;
                    residual.energy.reserve(static_cast<std::size_t>(std::max(0, sound->frames)));
                    for (int frame = 0; frame < sound->frames; ++frame)
                    {
                        const auto energy = sound->band_energy[band] != nullptr
                            ? sound->band_energy[band][frame] : 0.0;
                        residual.energy.push_back(energy);
                    }
                    residual.lowHz = ats::criticalBandEdgeHz(band);
                    residual.highHz = ats::criticalBandEdgeHz(band + 1);
                    input.residualBands.push_back(std::move(residual));
                }
            return ui::SpectralTraceBuilder::build(
                input, ui::SpectralTraceMode::combined);
            }
        };
        if (spectrumSource && spectrumSource->sound)
        {
            snapshot.spectrum = buildSpectrum(spectrumSource, spectralViewSource_);
            const auto overlaySource = spectralViewSource_ == 0 && sources_[0]
                ? &*sources_[0]
                : spectralViewSource_ == 1 && sources_[1]
                    ? &*sources_[1]
                    : spectralViewSource_ == 2 && sources_[0]
                        ? &*sources_[0] : nullptr;
            const auto overlaySourceId = spectralViewSource_ == 0 ? 1
                : spectralViewSource_ == 1 ? 2 : 1;
            // JUCE's complementary envelope surface is normalized from its
            // own source peak, rather than inheriting the visible source's
            // latched ruler.
            snapshot.spectrumOverlay = buildSpectrum(overlaySource, overlaySourceId, false);
        }
        return snapshot;
    }
    [[nodiscard]] SessionRuntimeSnapshot sessionSnapshot() const
    {
        SessionRuntimeSnapshot snapshot;
        snapshot.previewSource = previewSource_.load(std::memory_order_acquire);
        snapshot.previewLayer = previewLayer_.load(std::memory_order_acquire);
        snapshot.previewPlaying = previewPlaying_.load(std::memory_order_acquire);
        snapshot.normalizedPlayhead = normalizedPlayhead_.load(std::memory_order_acquire);
        snapshot.liveSmearEnabled = liveSmearEnabled_.load(std::memory_order_acquire);
        snapshot.variationStored = variationStored_;
        snapshot.smearnovRunning = smearnovRunning_.load(std::memory_order_acquire);
        snapshot.smearedSourceAvailable = smearedSourceAvailable_.load(std::memory_order_acquire);
        snapshot.timelineRevision = timelineRevision_;
        snapshot.timbreRevision = timbreRevision_;
        snapshot.onsetRevision = onsetRevision_;
        snapshot.timeline = timeline_;
        snapshot.timbrePlan = timbrePlan_;
        snapshot.onsetOverlay = onsetOverlay_;
        return snapshot;
    }
    [[nodiscard]] const engine::ImportedSource* source(int slot) const noexcept
    {
        return slot >= 0 && slot < static_cast<int>(sources_.size()) && sources_[static_cast<std::size_t>(slot)]
                   ? &*sources_[static_cast<std::size_t>(slot)]
                   : nullptr;
    }

    [[nodiscard]] const engine::ImportedSource* source(SourceIdentity identity) const noexcept
    {
        switch (identity)
        {
        case SourceIdentity::current: return currentSource_ ? &*currentSource_ : nullptr;
        case SourceIdentity::sourceA: return source(0);
        case SourceIdentity::sourceB: return source(1);
        case SourceIdentity::morphTimeline: return nullptr;
        }
        return nullptr;
    }

    static constexpr int sourceActionSlot(SourceIdentity identity) noexcept
    {
        switch (identity)
        {
        case SourceIdentity::current: return 3;
        case SourceIdentity::sourceA: return 0;
        case SourceIdentity::sourceB: return 1;
        case SourceIdentity::morphTimeline: return 2;
        }
        return -1;
    }

    static constexpr std::optional<SourceIdentity> sourceIdentityForSlot(int slot) noexcept
    {
        switch (slot)
        {
        case 0: return SourceIdentity::sourceA;
        case 1: return SourceIdentity::sourceB;
        case 2: return SourceIdentity::morphTimeline;
        case 3: return SourceIdentity::current;
        default: return std::nullopt;
        }
    }

    // The wrapper calls this from its UI/action domain. Decoding is kept out
    // of process() and accepts only host-neutral paths and slots.
    bool importSource(const Path& path, int slot, engine::SourceImportKind kind,
                      Error& error)
    {
        error = {};
        auto imported = engine::SourceImportService::import(
            sourceGenerations_.next(), sourceCancellation_.token(), { path, slot, kind });
        if (imported.status != JobResult<engine::ImportedSource>::Status::success)
        {
            error = imported.error;
            return false;
        }
        if (slot == 3)
        {
            currentSource_ = std::move(imported.value);
            ++currentSourceRevision_;
            spectrumAmpDisplayReferences_[0] = 0.0;
            spectrumResidualDisplayReferences_[0] = 0.0;
        }
        else
        {
            sources_[static_cast<std::size_t>(slot)] = std::move(imported.value);
            ++sourceRevisions_[static_cast<std::size_t>(slot)];
            spectrumAmpDisplayReferences_[static_cast<std::size_t>(slot + 1)] = 0.0;
            spectrumResidualDisplayReferences_[static_cast<std::size_t>(slot + 1)] = 0.0;
        }
        auto snapshot = telemetry();
        snapshot.hasSourceA = sources_[0].has_value();
        snapshot.hasSourceB = sources_[1].has_value();
        snapshot.hasCurrentModel = currentSource_ && currentSource_->sound;
        snapshot.state = snapshot.hasCurrentModel ? ui::UiState::analyzed : ui::UiState::readyToAnalyze;
        snapshot.status = snapshot.hasCurrentModel ? "ATS model imported" : "audio source decoded";
        snapshot.error.clear();
        telemetry_.publish(std::move(snapshot));
        return true;
    }

    bool importSource(const Path& path, SourceIdentity identity,
                      engine::SourceImportKind kind, Error& error)
    {
        if (identity == SourceIdentity::morphTimeline)
        {
            error = { "invalid_source_identity", "morph timeline is generated, not imported" };
            return false;
        }
        return importSource(path, sourceActionSlot(identity), kind, error);
    }

    // Non-real-time action entry point. It converts an imported WAV/AIFF slot
    // into an ATS model and, when the host is prepared, publishes a rendered
    // block that process() can consume on the audio thread.
    bool analyzeSource(int slot, Error& error)
    {
        error = {};
        const auto* selectedSource = slot == 3
            ? (currentSource_ ? &*currentSource_ : nullptr)
            : (slot >= 0 && slot < static_cast<int>(sources_.size()) && sources_[static_cast<std::size_t>(slot)]
                   ? &*sources_[static_cast<std::size_t>(slot)] : nullptr);
        if (selectedSource == nullptr)
        {
            error = { "missing_source", "select an imported audio source before analysis" };
            return false;
        }
        auto& source = *const_cast<engine::ImportedSource*>(selectedSource);
        if (source.kind != engine::SourceImportKind::audio || source.monoSamples.empty() || source.sampleRate <= 0)
        {
            error = { "source_not_audio", "selected source does not contain audio samples to analyse" };
            return false;
        }
        const auto generation = sourceGenerations_.next();
        const auto parameters = analysisParamsForSource(source);
        auto analysed = AtsEngine::analyze(generation, sourceCancellation_.token(),
            { source.monoSamples.data(), source.monoSamples.size(), source.sampleRate,
              parameters });
        if (analysed.status != JobResult<AnalysisOutput>::Status::success)
        {
            error = analysed.error;
            return false;
        }
        source.sound = std::move(analysed.value.sound);
        source.residual = std::move(analysed.value.residual);
        if (slot == 3)
            ++currentSourceRevision_;
        if (state_ != HostState::prepared)
            return true;

        const auto playbackSlot = playback_.tryAcquireWriteSlot();
        if (playbackSlot == PlaybackBufferPool::kNoBuffer)
        {
            error = { "playback_busy", "no playback buffer is available for the analysed source" };
            return false;
        }
        const auto rendered = AtsEngine::render(generation, sourceCancellation_.token(), source.sound.get(), {},
                                                playback_.writeView(playbackSlot));
        if (rendered.status != JobResult<RenderStats>::Status::success ||
            !playback_.publish(playbackSlot, generation))
        {
            error = rendered.error ? rendered.error
                                   : Error { "playback_publish_failed", "could not publish analysed playback" };
            return false;
        }
        return true;
    }

    bool analyzeSource(SourceIdentity identity, Error& error)
    {
        if (identity == SourceIdentity::morphTimeline)
        {
            error = { "invalid_source_identity", "morph timeline has no analyzable source" };
            return false;
        }
        return analyzeSource(sourceActionSlot(identity), error);
    }

    // Queues a copy of the decoded source so no worker observes mutable UI
    // ownership. Completion is committed by pollAnalysis() on the host/UI
    // domain; process() remains strictly audio-thread-only.
    bool queueAnalysisSource(int slot, Error& error)
    {
        error = {};
        if (analysisRunning_.load(std::memory_order_acquire))
        {
            error = { "analysis_busy", "an analysis job is already running" };
            return false;
        }
        const auto* selectedSource = slot == 3
            ? (currentSource_ ? &*currentSource_ : nullptr)
            : (slot >= 0 && slot < static_cast<int>(sources_.size()) && sources_[static_cast<std::size_t>(slot)]
                   ? &*sources_[static_cast<std::size_t>(slot)] : nullptr);
        if (selectedSource == nullptr)
        {
            error = { "missing_source", "select an imported audio source before analysis" };
            return false;
        }
        const auto& source = *selectedSource;
        if (source.kind != engine::SourceImportKind::audio || source.monoSamples.empty() || source.sampleRate <= 0)
        {
            error = { "source_not_audio", "selected source does not contain audio samples to analyse" };
            return false;
        }
        analysisCancellation_.cancel();
        analysisCancellation_ = CancellationSource {};
        analysisSlot_ = slot;
        analysisSourceRevision_ = slot == 3 ? currentSourceRevision_
                                            : sourceRevisions_[static_cast<std::size_t>(slot)];
        analysisProgress_.store(0.0f, std::memory_order_release);
        const auto generation = sourceGenerations_.next();
        auto samples = source.monoSamples;
        const auto sampleRate = source.sampleRate;
        const auto parameters = analysisParamsForSource(source);
        const auto token = analysisCancellation_.token();
        {
            auto promise = std::make_shared<std::promise<JobResult<AnalysisOutput>>>();
            analysisFuture_ = promise->get_future();
            analysisScheduler_.submitVoid(
                [this, generation, token, samples = std::move(samples), sampleRate, parameters,
                 promise]() mutable {
                    auto result = AtsEngine::analyze(generation, token,
                                                     { samples.data(), samples.size(), sampleRate, parameters },
                                                     [this](double progress) {
                                                         analysisProgress_.store(
                                                             static_cast<float>(progress),
                                                             std::memory_order_release);
                                                     });
                    promise->set_value(std::move(result));
                });
        }
        analysisRunning_.store(true, std::memory_order_release);
        auto snapshot = telemetry();
        snapshot.state = ui::UiState::analyzing;
        snapshot.analysisRunning = true;
        snapshot.progress = 0.0f;
        snapshot.status = std::string("analysing source ") + (slot == 0 ? "A" : "B");
        snapshot.error.clear();
        telemetry_.publish(std::move(snapshot));
        return true;
    }

    bool pollAnalysis(Error& error)
    {
        error = {};
        Error smearnovError;
        const bool smearnovCompleted = pollSmearnov(smearnovError);
        if (smearnovCompleted && smearnovError)
            error = std::move(smearnovError);
        if (!analysisRunning_.load(std::memory_order_acquire) || !analysisFuture_.valid() ||
            analysisFuture_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return smearnovCompleted;
        auto analysed = analysisFuture_.get();
        analysisRunning_.store(false, std::memory_order_release);
        analysisProgress_.store(analysed.status == JobResult<AnalysisOutput>::Status::success ? 1.0f : 0.0f,
                                std::memory_order_release);
        if (analysed.status != JobResult<AnalysisOutput>::Status::success)
        {
            error = analysed.error;
            auto snapshot = telemetry();
            snapshot.analysisRunning = false;
            snapshot.progress = 0.0f;
            snapshot.state = analysed.status == JobResult<AnalysisOutput>::Status::cancelled
                ? ui::UiState::readyToAnalyze : ui::UiState::error;
            snapshot.status = analysed.status == JobResult<AnalysisOutput>::Status::cancelled
                ? "analysis cancelled" : "analysis failed";
            snapshot.error = error.text;
            telemetry_.publish(std::move(snapshot));
            return true;
        }
        const auto* analysedSource = analysisSlot_ == 3
            ? (currentSource_ ? &*currentSource_ : nullptr)
            : (analysisSlot_ >= 0 && analysisSlot_ < static_cast<int>(sources_.size()) &&
               sources_[static_cast<std::size_t>(analysisSlot_)]
                   ? &*sources_[static_cast<std::size_t>(analysisSlot_)] : nullptr);
        if (analysedSource == nullptr ||
            (analysisSlot_ == 3 ? currentSourceRevision_ != analysisSourceRevision_
                                 : sourceRevisions_[static_cast<std::size_t>(analysisSlot_)] != analysisSourceRevision_))
        {
            error = { "stale_analysis", "source changed while analysis was running" };
            auto snapshot = telemetry();
            snapshot.analysisRunning = false;
            snapshot.state = ui::UiState::readyToAnalyze;
            snapshot.status = "analysis result discarded";
            snapshot.error = error.text;
            telemetry_.publish(std::move(snapshot));
            return true;
        }
        auto& source = *const_cast<engine::ImportedSource*>(analysedSource);
        source.sound = std::move(analysed.value.sound);
        source.residual = std::move(analysed.value.residual);
        if (analysisSlot_ == 3)
            ++currentSourceRevision_;
        const auto displayReferenceIndex = analysisSlot_ == 3 ? 0 : analysisSlot_ + 1;
        spectrumAmpDisplayReferences_[static_cast<std::size_t>(displayReferenceIndex)] = 0.0;
        spectrumResidualDisplayReferences_[static_cast<std::size_t>(displayReferenceIndex)] = 0.0;
        if (state_ == HostState::prepared)
        {
            const int previewSlot = analysisSlot_ == 3 ? 0 : analysisSlot_;
            (void) renderSourceForPreview(previewSlot, error);
        }
        auto snapshot = telemetry();
        snapshot.analysisRunning = false;
        snapshot.progress = 1.0f;
        snapshot.state = error ? ui::UiState::error : ui::UiState::analyzed;
        snapshot.hasSourceA = sources_[0].has_value();
        snapshot.hasSourceB = sources_[1].has_value();
        // Preserve the legacy telemetry flag for hosts that use it as the
        // generic "analyzed model available" indicator. Typed source APIs and
        // EditorSnapshot source roles remain distinct.
        snapshot.hasCurrentModel = (currentSource_ && currentSource_->sound) ||
                                   (analysisSlot_ != 3 && source.sound.get() != nullptr);
        snapshot.hasResidual = source.sound.get() != nullptr && source.sound.get()->res != nullptr;
        snapshot.hasPhase = source.sound.get() != nullptr && source.sound.get()->pha != nullptr;
        snapshot.status = error ? "analysis render failed" : "analysis complete";
        snapshot.error = error.text;
        telemetry_.publish(std::move(snapshot));
        // Auto-switch the spectrum view to the freshly analysed source when the
        // view is still pointing at currentSource_ (slot 0), which no normal UI
        // workflow ever populates, so the spectrum would otherwise stay blank.
        if (!error && analysisSlot_ >= 0 && analysisSlot_ <= 1 && spectralViewSource_ == 0)
            spectralViewSource_ = analysisSlot_ + 1;
        return true;
    }

    void cancelAnalysis() noexcept
    {
        analysisCancellation_.cancel();
        if (analysisRunning_.load(std::memory_order_acquire))
        {
            auto snapshot = telemetry();
            snapshot.status = "cancelling analysis";
            snapshot.error.clear();
            telemetry_.publish(std::move(snapshot));
        }
    }
    [[nodiscard]] bool analysisRunning() const noexcept
    { return analysisRunning_.load(std::memory_order_acquire); }

    bool applyAnalysisAction(const ui::EditorAction& action, Error& error)
    {
        error = {};
        if (const auto* profile = std::get_if<ui::ApplyAnalysisProfileAction>(&action))
        {
            if (profile->profile < 0 || profile->profile > 3)
            {
                error = { "invalid_analysis_profile", "analysis profile is invalid" };
                return true;
            }
            applyAnalysisProfile(profile->profile);
            auto snapshot = telemetry();
            snapshot.status = std::string("analysis profile: ") + analysisProfileName(profile->profile);
            snapshot.error.clear();
            telemetry_.publish(std::move(snapshot));
            return true;
        }
        if (const auto* analyse = std::get_if<ui::AnalyzeMorphSourceAction>(&action))
        {
            (void) queueAnalysisSource(analyse->slot, error);
            return true;
        }
        if (std::holds_alternative<ui::AnalyzeCurrentAction>(action))
        {
            (void) queueAnalysisSource(0, error);
            return true;
        }
        if (std::holds_alternative<ui::CancelJobsAction>(action))
        {
            cancelAnalysis();
            return true;
        }
        return false;
    }

    // Applies the Session subset of EditorAction at the shared host boundary.
    // File import, analysis, and parameter automation deliberately remain
    // wrapper-specific because their host transactions have separate rules.
    // Returns false for a non-Session action so callers can continue their
    // normal action routing without accidentally swallowing it.
    bool applySessionAction(const ui::EditorAction& action, Error& error)
    {
        error = {};
        if (const auto* preview = std::get_if<ui::PreviewAction>(&action))
        {
            if (preview->source < 0 || preview->source > 3 ||
                preview->layer < 0 || preview->layer > 3)
            {
                error = { "invalid_preview", "preview source or audition layer is invalid" };
                return true;
            }
            previewSource_.store(preview->source, std::memory_order_release);
            previewLayer_.store(preview->layer, std::memory_order_release);
            if (!preview->start)
            {
                previewPlaying_.store(false, std::memory_order_release);
                return true;
            }
            bool rendered = false;
            if (preview->source == 2)
                rendered = renderMorphForPreview(error);
            else if (preview->source == 3)
                rendered = renderCurrentForPreview(error);
            else
                rendered = renderSourceForPreview(preview->source, error);
            if (!rendered)
                return true;
            previewFrame_.store(0, std::memory_order_release);
            normalizedPlayhead_.store(0.0f, std::memory_order_release);
            previewPlaying_.store(true, std::memory_order_release);
            return true;
        }
        if (std::holds_alternative<ui::StopPreviewAction>(action))
        {
            previewPlaying_.store(false, std::memory_order_release);
            return true;
        }
        if (const auto* seek = std::get_if<ui::SeekPreviewAction>(&action))
        {
            if (!std::isfinite(seek->normalizedTime) || seek->normalizedTime < 0.0f ||
                seek->normalizedTime > 1.0f)
            {
                error = { "invalid_seek", "preview seek must be normalized" };
                return true;
            }
            normalizedPlayhead_.store(seek->normalizedTime, std::memory_order_release);
            const auto length = previewLengthFrames_.load(std::memory_order_acquire);
            const auto target = length == 0
                ? 0
                : static_cast<std::uint64_t>(seek->normalizedTime * static_cast<float>(length - 1));
            previewFrame_.store(target, std::memory_order_release);
            return true;
        }
        if (const auto* smear = std::get_if<ui::SetPreviewSmearAction>(&action))
        {
            liveSmearEnabled_.store(smear->enabled, std::memory_order_release);
            return true;
        }
        if (const auto* variation = std::get_if<ui::MorphVariationAction>(&action))
        {
            const auto before = captureState();
            if (variation->slot < 0 || variation->slot >= static_cast<int>(variations_.size()))
            {
                error = { "invalid_variation", "variation slot is invalid" };
                return true;
            }
            const auto index = static_cast<std::size_t>(variation->slot);
            if (variation->store)
            {
                variations_[index] = parameters_.snapshot();
                variationStored_[index] = true;
            }
            else
            {
                if (!variationStored_[index] || variations_[index].size() != parameters_.size())
                {
                    error = { "missing_variation", "save this variation before recalling it" };
                    return true;
                }
                for (std::size_t parameter = 0; parameter < variations_[index].size(); ++parameter)
                    (void) parameters_.write(parameter, variations_[index][parameter]);
            }
            recordMutation("Morph variation", before);
            return true;
        }
        if (const auto* timeline = std::get_if<ui::SetMorphTimelineAction>(&action))
        {
            const auto before = captureState();
            if (timeline->points.empty() && timeline->normalizedPoints.empty())
            {
                error = { "invalid_timeline", "timeline requires at least one point" };
                return true;
            }
            typedTimeline_.clear();
            if (!timeline->points.empty())
            {
                typedTimeline_.reserve(timeline->points.size());
                for (const auto& point : timeline->points)
                    typedTimeline_.push_back({ point.timeNorm, point.amount,
                        static_cast<engine::TimelinePointKind>(point.kind),
                        static_cast<MorphCurveShape>(point.curve) });
                typedTimeline_ = engine::MorphTimelineService::sanitize(std::move(typedTimeline_));
                timeline_.clear();
                for (const auto& point : typedTimeline_)
                    if (point.kind == engine::TimelinePointKind::morph)
                        timeline_.push_back(static_cast<float>(point.amount));
            }
            else timeline_ = timeline->normalizedPoints;
            ++timelineRevision_;
            recordMutation("Morph timeline", before);
            return true;
        }
        if (const auto* onsets = std::get_if<ui::EditSmearnovOnsetsAction>(&action))
        {
            if (onsets->normalizedOnsets.size() > 4096 ||
                !strictlyOrderedNormalized(onsets->normalizedOnsets))
            {
                error = { "invalid_onsets", "Smearnov onsets must be ordered normalized values" };
                return true;
            }
            const auto before = captureState();
            onsetOverlay_ = onsets->normalizedOnsets;
            onsetHandleOverlay_.clear();
            for (const auto onset : onsetOverlay_)
                onsetHandleOverlay_.push_back({ onset, onset, onset, 0.0f });
            ++onsetRevision_;
            recordMutation("Smearnov onsets", before);
            return true;
        }
        if (const auto* handles = std::get_if<ui::EditSmearnovOnsetHandlesAction>(&action))
        {
            const auto before = captureState();
            onsetHandleOverlay_ = handles->onsets;
            onsetOverlay_.clear();
            onsetOverlay_.reserve(onsetHandleOverlay_.size());
            for (const auto& onset : onsetHandleOverlay_)
                onsetOverlay_.push_back(onset.norm);
            ++onsetRevision_;
            recordMutation("Smearnov onset handles", before);
            return true;
        }
        if (std::holds_alternative<ui::DetectSmearnovAction>(action))
            return queueSmearnov(false, error);
        if (std::holds_alternative<ui::ApplySmearnovAction>(action))
            return queueSmearnov(true, error);
        if (const auto* timbre = std::get_if<ui::SetTimbreMotionPlanAction>(&action))
        {
            if (timbre->normalizedPlan.size() > 4096 ||
                !std::all_of(timbre->normalizedPlan.begin(), timbre->normalizedPlan.end(),
                             [](float value) { return std::isfinite(value) && value >= 0.0f && value <= 1.0f; }))
            {
                error = { "invalid_timbre_plan", "timbre curve values must be normalized" };
                return true;
            }
            const auto before = captureState();
            timbrePlan_ = timbre->normalizedPlan;
            timbrePlanJson_ = timbre->serializedPlan;
            ++timbreRevision_;
            recordMutation("Timbre motion", before);
            return true;
        }
        return false;
    }

    void setPresetService(state::PresetService* service) noexcept { presetService_ = service; }
    [[nodiscard]] UndoHistory& history() noexcept { return history_; }
    [[nodiscard]] int spectralViewSource() const noexcept { return spectralViewSource_; }
    [[nodiscard]] int spectralViewMode() const noexcept { return spectralViewMode_; }
    [[nodiscard]] double lastResidualReadDb() const noexcept { return lastResidualReadDb_; }

    // Unified dispatch entry point for all EditorAction types. Delegates to
    // applySessionAction / applyAnalysisAction, then handles the remaining
    // host-domain actions inline. Returns false only for wrapper-specific
    // actions (import, export) that have no shared host boundary contract.
    bool applyEditorAction(const ui::EditorAction& action, Error& error)
    {
        if (applySessionAction(action, error))
            return true;
        if (applyAnalysisAction(action, error))
            return true;
        error = {};

        if (const auto* param = std::get_if<ui::SetParameterAction>(&action))
        {
            const auto* descriptor = ParameterRegistry::find(param->id);
            if (descriptor == nullptr || !descriptor->automatable || descriptor->readOnly)
            {
                error = { "invalid_parameter", "parameter id is unknown or not automatable" };
                return true;
            }
            if (param->begin)
                parameterGestureBefore_ = captureState();
            const auto singleActionBefore = (!param->begin && !param->end && !parameterGestureBefore_)
                ? std::optional<HostStateChunk>(captureState()) : std::nullopt;
            (void) parameters_.write(param->id, param->value);
            if (param->end && parameterGestureBefore_)
            {
                recordMutation("Parameter gesture", *parameterGestureBefore_);
                parameterGestureBefore_.reset();
            }
            else if (singleActionBefore)
                recordMutation("Parameter change", *singleActionBefore);
            return true;
        }
        if (const auto* factory = std::get_if<ui::ApplyFactoryPresetAction>(&action))
        {
            const auto before = captureState();
            if (!state::applyFactoryPreset(factory->index, parameters_))
                error = { "invalid_factory_preset", "factory preset index must be 0..11" };
            else
                recordMutation("Factory preset", before);
            return true;
        }
        if (const auto* note = std::get_if<ui::NoteAction>(&action))
        {
            MidiEvent event;
            event.type = note->on ? MidiEvent::Type::noteOn : MidiEvent::Type::noteOff;
            event.note = note->note;
            event.velocity = note->velocity;
            midi_.consume(event);
            return true;
        }
        if (const auto* spectral = std::get_if<ui::SetSpectralViewAction>(&action))
        {
            if (spectralViewMode_ != spectral->mode)
                resetSpectrumDisplayReferenceForSourceId(spectral->source);
            spectralViewSource_ = spectral->source;
            spectralViewMode_ = spectral->mode;
            return true;
        }
        if (const auto* hist = std::get_if<ui::HistoryAction>(&action))
        {
            const auto current = captureState();
            auto snapshot = hist->redo ? history_.redo(current) : history_.undo(current);
            if (!snapshot)
            {
                error = { hist->redo ? "nothing_to_redo" : "nothing_to_undo",
                          hist->redo ? "redo history is empty" : "undo history is empty" };
                return true;
            }
            applyStateChunk(std::move(*snapshot));
            return true;
        }
        if (const auto* save = std::get_if<ui::SavePresetAction>(&action))
        {
            const auto payload = captureState();
            if (payload.empty())
            {
                error = { "preset_encode_failed", "failed to encode current state as preset" };
                return true;
            }
            if (presetService_ != nullptr)
            {
                const auto stem = std::filesystem::u8path(save->path.utf8).stem().u8string();
                const auto result = presetService_->save(stem, payload, true, nullptr);
                if (!result.succeeded()) error = result.error;
            }
            else
            {
                const auto result = platform::FileService {}.writeAtomic(save->path, payload, true);
                if (!result.succeeded()) error = result.error;
            }
            return true;
        }
        if (const auto* load = std::get_if<ui::LoadPresetAction>(&action))
        {
            const auto result = presetService_ != nullptr
                ? presetService_->load(load->path, nullptr)
                : platform::FileService {}.read(load->path);
            if (!result.succeeded())
            {
                error = result.error;
                return true;
            }
            // Try full-state format (new saves); fall back to parameter-only for
            // backward compatibility with presets saved by older plugin versions.
            auto legacy = LegacyStateCodec::decode(result.bytes.data(), result.bytes.size());
            if (legacy)
            {
                if (!applyStateChunk(result.bytes))
                    error = { "preset_decode_failed", "full-state preset contains an invalid ATS payload" };
                else
                    history_.clear();
            }
            else
            {
                std::string decodeError;
                if (!state::ParameterPresetCodec::decode(result.bytes, parameters_, decodeError))
                    error = { "preset_decode_failed", decodeError };
                else
                    history_.clear();
            }
            return true;
        }
        if (const auto* partial = std::get_if<ui::PartialEditAction>(&action))
        {
            if (partial->partial < 0)
            {
                error = { "invalid_partial", "partial index must be non-negative" };
                return true;
            }
            const auto slot = previewSource_.load(std::memory_order_acquire);
            auto* target = editableSource(slot);
            if (target == nullptr)
            {
                error = { "no_model", "analyse a source before editing partials" };
                return true;
            }
            if (!partial->apply || partial->frame < 0)
                return true; // focus/select only — no mutation
            const auto before = captureState();
            selectionFirstFrame_ = partial->frame;
            selectionLastFrame_ = partial->frame;
            auto* sound = target->sound.get();
            if (partial->partial >= sound->partials || partial->frame >= sound->frames)
            {
                error = { "partial_out_of_range", "partial or frame index is out of range" };
                return true;
            }
            if (!std::isfinite(partial->amplitude) || partial->amplitude < 0.0 ||
                !std::isfinite(partial->frequencyHz) || partial->frequencyHz <= 0.0)
            {
                error = { "invalid_partial_values", "amplitude and frequency must be finite and positive" };
                return true;
            }
            sound->amp[partial->partial][partial->frame] = partial->amplitude;
            sound->frq[partial->partial][partial->frame] = partial->frequencyHz;
            noteSourceMutation(slot);
            if (state_ == HostState::prepared)
                (void) renderSourceForPreview(slot, error);
            recordMutation("Partial edit", before);
            return true;
        }
        if (const auto* residual = std::get_if<ui::ResidualEditAction>(&action))
        {
            if (residual->band < 0 || residual->frame < 0 || !std::isfinite(residual->value))
            {
                error = { "invalid_residual_edit", "residual band, frame, and value are required" };
                return true;
            }
            const auto before = captureState();
            const auto slot = previewSource_.load(std::memory_order_acquire);
            auto* target = editableSource(slot);
            if (target == nullptr)
            {
                error = { "no_model", "analyse a source before editing residual" };
                return true;
            }
            auto* sound = target->sound.get();
            if (sound->band_energy == nullptr || residual->band >= ATSA_CRITICAL_BANDS ||
                residual->frame >= sound->frames)
            {
                error = { "residual_out_of_range", "residual band or frame index is out of range" };
                return true;
            }
            sound->band_energy[residual->band][residual->frame] =
                static_cast<double>(std::max(0.0f, residual->value));
            selectionFirstFrame_ = residual->frame;
            selectionLastFrame_ = residual->frame;
            noteSourceMutation(slot);
            if (state_ == HostState::prepared)
                (void) renderSourceForPreview(slot, error);
            recordMutation("Residual edit", before);
            return true;
        }
        if (const auto* math = std::get_if<ui::ApplyMathAction>(&action))
        {
            if (math->script.empty())
            {
                error = { "empty_script", "math script must not be empty" };
                return true;
            }
            const auto before = captureState();
            const auto slot = previewSource_.load(std::memory_order_acquire);
            auto* target = editableSource(slot);
            if (target == nullptr)
            {
                error = { "no_model", "analyse a source before applying math" };
                return true;
            }
            AtsSoundPtr working { copy_sound(target->sound.get()) };
            if (!working)
            {
                error = { "copy_failed", "could not copy the ATS model for math processing" };
                return true;
            }
            std::string mathError;
            if (!applyMathExprScript(working.get(), math->script, mathError))
            {
                error = { "math_failed", mathError };
                return true;
            }
            target->sound = std::move(working);
            noteSourceMutation(slot);
            if (state_ == HostState::prepared)
                (void) renderSourceForPreview(slot, error);
            recordMutation("Math expression", before);
            return true;
        }
        if (const auto* spectrum = std::get_if<ui::SpectrumEditAction>(&action))
        {
            using Cmd = ui::SpectrumEditCommand;
            const auto partialCommand = spectrum->command == Cmd::mutePartials
                || spectrum->command == Cmd::scalePartials
                || spectrum->command == Cmd::drawPartialFrequency
                || spectrum->command == Cmd::gainPartialEnvelope
                || spectrum->command == Cmd::smoothPartialEnvelope
                || spectrum->command == Cmd::tiltPartialEnvelope
                || spectrum->command == Cmd::soloPartials
                || spectrum->command == Cmd::freezePartial
                || spectrum->command == Cmd::deletePartials
                || spectrum->command == Cmd::prunePartials;
            if (partialCommand)
            {
                const auto slot = previewSource_.load(std::memory_order_acquire);
                auto* target = editableSource(slot);
                if (target == nullptr || !target->sound || target->sound.get()->amp == nullptr
                    || target->sound.get()->partials <= 0 || target->sound.get()->frames <= 0)
                { error = { "no_model", "analyse a source before editing partials" }; return true; }
                const auto* sound = target->sound.get();
                PartialMatrix matrix(sound->partials, sound->frames);
                for (int p = 0; p < sound->partials; ++p)
                    for (int f = 0; f < sound->frames; ++f)
                    {
                        auto& cell = matrix.at(p, f);
                        cell.amplitude = sound->amp[p] ? sound->amp[p][f] : 0.0;
                        cell.frequencyHz = sound->frq && sound->frq[p] ? sound->frq[p][f] : 1.0;
                    }
                PartialEditService service;
                PartialSelection selection;
                selection.partials = spectrum->bands;
                const auto first = std::clamp<int>(spectrum->firstFrame, 0, sound->frames - 1);
                const auto last = std::clamp<int>(spectrum->lastFrame, first, sound->frames - 1);
                for (int f = first; f <= last; ++f) selection.frames.push_back(f);
                service.setSelectionForHost(std::move(selection));
                PartialEditCommand edit;
                edit.value = spectrum->value;
                edit.firstFrame = first; edit.lastFrame = last; edit.frame = first;
                switch (spectrum->command)
                {
                case Cmd::mutePartials:
                case Cmd::deletePartials: edit.kind = PartialEditKind::mute; break;
                case Cmd::scalePartials: edit.kind = PartialEditKind::scaleSelection; break;
                case Cmd::drawPartialFrequency:
                    edit.kind = PartialEditKind::drawFrequency;
                    edit.points = { { static_cast<double>(first), spectrum->value },
                                    { static_cast<double>(last), spectrum->value } };
                    if (!spectrum->points.empty())
                    {
                        edit.points.clear();
                        for (const auto& point : spectrum->points)
                            edit.points.push_back({ static_cast<double>(point.first),
                                                    static_cast<double>(point.second) });
                    }
                    break;
                case Cmd::gainPartialEnvelope: edit.kind = PartialEditKind::gainBox; break;
                case Cmd::smoothPartialEnvelope: edit.kind = PartialEditKind::smoothEnvelope; break;
                case Cmd::tiltPartialEnvelope: edit.kind = PartialEditKind::tiltEnvelope; break;
                case Cmd::soloPartials: edit.kind = PartialEditKind::solo; break;
                case Cmd::freezePartial: edit.kind = PartialEditKind::freeze; break;
                case Cmd::prunePartials:
                    error = { "invalid_spectrum_edit", "prune is a selection-only UI operation" };
                    return true;
                default: break;
                }
                if (spectrum->command == Cmd::gainPartialEnvelope)
                {
                    if (!spectrum->bands.empty())
                    {
                        edit.firstPartial = *std::min_element(spectrum->bands.begin(), spectrum->bands.end());
                        edit.lastPartial = *std::max_element(spectrum->bands.begin(), spectrum->bands.end());
                    }
                }
                const auto before = captureState();
                const auto result = service.apply(matrix, edit);
                if (!result.accepted) { error = { "spectrum_edit_failed", result.error }; return true; }
                if (result.changed)
                {
                    for (int p = 0; p < sound->partials; ++p)
                        for (int f = 0; f < sound->frames; ++f)
                        {
                            if (sound->amp[p]) sound->amp[p][f] = matrix.at(p, f).amplitude;
                            if (sound->frq && sound->frq[p]) sound->frq[p][f] = matrix.at(p, f).frequencyHz;
                        }
                    if (spectrum->command == Cmd::mutePartials
                        || spectrum->command == Cmd::deletePartials)
                        resetSpectrumDisplayReference(slot);
                    noteSourceMutation(slot);
                    recordMutation("Partial spectrum edit", before);
                }
                return true;
            }
            if (spectrum->bands.empty() || spectrum->firstFrame < 0 ||
                spectrum->lastFrame < spectrum->firstFrame || !std::isfinite(spectrum->value))
            {
                error = { "invalid_spectrum_edit", "spectrum edit selection is invalid" };
                return true;
            }
            const auto slot = previewSource_.load(std::memory_order_acquire);
            auto* target = editableSource(slot);
            if (target == nullptr)
            {
                error = { "no_model", "analyse a source before editing the spectrum" };
                return true;
            }
            const auto before = captureState();
            auto* sound = target->sound.get();
            if (sound->band_energy == nullptr || sound->frames <= 0)
            {
                error = { "no_residual", "source has no residual data to edit" };
                return true;
            }

            engine::ResidualMatrix matrix;
            matrix.bands.reserve(ATSA_CRITICAL_BANDS);
            for (int b = 0; b < ATSA_CRITICAL_BANDS; ++b)
            {
                engine::ResidualBandData band;
                band.bandIndex = b;
                band.energy.resize(static_cast<std::size_t>(sound->frames));
                for (int f = 0; f < sound->frames; ++f)
                    band.energy[static_cast<std::size_t>(f)] = sound->band_energy[b][f];
                matrix.bands.push_back(std::move(band));
            }

            using Op  = engine::ResidualEditOperation;
            Op operation = Op::smooth;
            switch (spectrum->command)
            {
            case Cmd::copyResidual:   operation = Op::copy;         break;
            case Cmd::pasteResidual:  operation = Op::paste;        break;
            case Cmd::swapResidual:   operation = Op::swap;         break;
            case Cmd::smoothResidual: operation = Op::smooth;       break;
            case Cmd::tiltResidual:   operation = Op::tilt;         break;
            case Cmd::redistResidual: operation = Op::redistribute; break;
            case Cmd::muteResidual:   operation = Op::mute;        break;
            case Cmd::scaleResidual:  operation = Op::scale;       break;
            case Cmd::drawResidual:   operation = Op::drawDb;      break;
            case Cmd::readResidual:
                if (spectrum->bands.size() != 1)
                {
                    error = { "invalid_spectrum_edit", "read requires exactly one residual band" };
                    return true;
                }
                lastResidualReadDb_ = engine::ResidualEditService::readBandEnergyDb(
                    matrix, spectrum->bands.front(), spectrum->firstFrame);
                return true;
            default: break;
            }

            engine::ResidualSelection selection { spectrum->bands, spectrum->firstFrame, spectrum->lastFrame };
            selectionFirstFrame_ = spectrum->firstFrame;
            selectionLastFrame_ = spectrum->lastFrame;
            engine::ResidualEditParameters params;
            params.scale = std::isfinite(spectrum->value) ? std::max(0.0f, spectrum->value) : 1.0;
            params.smoothRadiusFrames = std::max(1, static_cast<int>(std::round(spectrum->value)));
            params.tiltDbPerBandStep  = static_cast<double>(spectrum->value);
            if (operation == Op::drawDb)
            {
                if (!spectrum->points.empty())
                    for (const auto& point : spectrum->points)
                        params.drawDb.emplace_back(point.first, static_cast<double>(point.second));
                else
                    for (int frame = spectrum->firstFrame; frame <= spectrum->lastFrame; ++frame)
                        params.drawDb.emplace_back(frame, static_cast<double>(spectrum->value));
            }

            const auto editResult = engine::ResidualEditService::apply(
                matrix, operation, selection, params, &residualClipboard_);
            if (!editResult.success)
            {
                error = { "spectrum_edit_failed", editResult.error };
                return true;
            }
            if (editResult.changed)
            {
                for (const auto& band : matrix.bands)
                {
                    if (band.bandIndex < 0 || band.bandIndex >= ATSA_CRITICAL_BANDS) continue;
                    for (std::size_t f = 0; f < band.energy.size() &&
                         f < static_cast<std::size_t>(sound->frames); ++f)
                        sound->band_energy[band.bandIndex][static_cast<int>(f)] = band.energy[f];
                }
                if (spectrum->command == Cmd::muteResidual)
                    resetSpectrumDisplayReference(slot);
                noteSourceMutation(slot);
                if (state_ == HostState::prepared)
                    (void) renderSourceForPreview(slot, error);
                recordMutation("Spectrum edit", before);
            }
            return true;
        }
        if (const auto* bounce = std::get_if<ui::BounceMorphTimelineAction>(&action))
        {
            if (bounce->path.utf8.empty())
            {
                error = { "empty_bounce_path", "bounce export path must not be empty" };
                return true;
            }
            if (bounce->layer < 0 || bounce->layer > 3)
            {
                error = { "invalid_bounce_layer", "bounce layer must be 0..3" };
                return true;
            }
            if (state_ != HostState::prepared || !sources_[0] || !sources_[1] ||
                !sources_[0]->sound || !sources_[1]->sound)
            {
                error = { "not_ready", "host must be prepared and both morph sources analysed before bouncing" };
                return true;
            }
            if (!renderMorphForPreview(error))
            {
                return true;
            }
            auto* pool = bounce->layer == 1 ? &playbackPartials_
                       : bounce->layer == 2 ? &playbackResidual_ : &playback_;
            const auto readSlot = pool->beginAudioRead();
            const auto* rendered = pool->readBuffer(readSlot);
            if (rendered == nullptr || rendered->frameCount() == 0)
            {
                pool->endAudioRead(readSlot);
                error = { "render_failed", "morph timeline bounce produced no audio" };
                return true;
            }
            std::vector<float> leftData(rendered->channel(0),
                                        rendered->channel(0) + rendered->frameCount());
            const auto* right = rendered->channelCount() > 1 ? rendered->channel(1) : rendered->channel(0);
            std::vector<float> rightData(right, right + rendered->frameCount());
            pool->endAudioRead(readSlot);
            const auto sr = spec_.sampleRate > 0.0 ? spec_.sampleRate : 44100.0;
            const auto wavBytes = buildBounceWav(leftData, rightData, static_cast<std::uint32_t>(sr));
            const auto writeResult = platform::FileService {}.writeAtomic(bounce->path, wavBytes, true);
            if (!writeResult.succeeded())
                error = writeResult.error;
            return true;
        }
        if (const auto* wav = std::get_if<ui::ImportWavAction>(&action))
        {
            (void) importSource(wav->path, wav->slot, engine::SourceImportKind::audio, error);
            return true;
        }
        if (const auto* ats = std::get_if<ui::ImportAtsAction>(&action))
        {
            (void) importSource(ats->path, ats->slot, engine::SourceImportKind::ats, error);
            return true;
        }
        if (const auto* exportAction = std::get_if<ui::ExportAtsAction>(&action))
        {
            (void) exportSource(exportAction->path, exportAction->source, error);
            return true;
        }
        return false;
    }

    bool exportSource(const Path& path, int slot, Error& error)
    {
        error = {};
        if (path.empty())
        {
            error = { "missing_path", "an ATS export path is required" };
            return false;
        }
        if (slot < 0 || slot >= static_cast<int>(sources_.size()) ||
            !sources_[static_cast<std::size_t>(slot)] ||
            !sources_[static_cast<std::size_t>(slot)]->sound)
        {
            error = { "missing_model", "analyse a source before exporting ATS" };
            return false;
        }
        AtsSoundPtr snapshot { copy_sound(sources_[static_cast<std::size_t>(slot)]->sound.get()) };
        if (!snapshot)
        {
            error = { "copy_failed", "could not snapshot the ATS model for export" };
            return false;
        }
        const auto type = sources_[static_cast<std::size_t>(slot)]->atsType >= 1 &&
                                  sources_[static_cast<std::size_t>(slot)]->atsType <= 4
                              ? sources_[static_cast<std::size_t>(slot)]->atsType
                              : std::clamp(static_cast<int>(parameters_.read("analysisType")) + 1, 1, 4);
        if (ats_save(snapshot.get(), path.utf8.c_str(), 0.0, type) < 0)
        {
            error = { "ats_save_failed", "could not write the ATS model" };
            return false;
        }
        return true;
    }

    [[nodiscard]] const std::vector<ParameterDescriptor>& parameterDescriptors() const noexcept
    { return ParameterRegistry::descriptors(); }

    [[nodiscard]] std::vector<HostParameterInfo> parameterInfo() const
    {
        std::vector<HostParameterInfo> result;
        const auto& descriptors = ParameterRegistry::descriptors();
        result.reserve(descriptors.size());
        for (const auto& descriptor : descriptors)
            result.push_back({ descriptor.id, descriptor.name, descriptor.group, descriptor.type,
                               descriptor.minimum, descriptor.maximum, descriptor.defaultValue,
                               descriptor.interval, descriptor.skew, descriptor.hostNumericId,
                               descriptor.automatable, descriptor.readOnly, descriptor.choices,
                               descriptor.choiceCount });
        return result;
    }

    bool applyParameterChange(const ParameterChange& change) noexcept
    {
        if (change.index >= parameters_.size())
            return false;
        const auto& descriptor = ParameterRegistry::descriptors()[change.index];
        return descriptor.automatable && !descriptor.readOnly
            && parameters_.write(change.index, change.value);
    }

    bool setParameterNormalized(std::uint32_t hostNumericId, float normalized) noexcept
    {
        if (!std::isfinite(normalized))
            return false;
        const auto& descriptors = ParameterRegistry::descriptors();
        const auto it = std::find_if(descriptors.begin(), descriptors.end(),
                                     [hostNumericId](const auto& descriptor) {
                                         return descriptor.hostNumericId == hostNumericId;
                                     });
        if (it == descriptors.end() || !it->automatable || it->readOnly)
            return false;
        return parameters_.write(it->denseIndex, it->denormalize(normalized));
    }

    [[nodiscard]] std::string parameterText(std::string_view id) const
    {
        const auto* descriptor = ParameterRegistry::find(id);
        return descriptor == nullptr ? std::string {} : descriptor->text(parameters_.read(id));
    }

    bool setParameterText(std::string_view id, std::string_view text) noexcept
    {
        const auto* descriptor = ParameterRegistry::find(id);
        if (descriptor == nullptr || !descriptor->automatable || descriptor->readOnly)
            return false;
        float value = 0.0f;
        return descriptor->fromText(text, value) && parameters_.write(id, value);
    }

    // Build the current full plugin state from live parameters and engine state.
    // Called by native wrappers on getState so the DAW always receives a
    // snapshot of what the plugin is currently doing, not a stale saved chunk.
    [[nodiscard]] HostStateChunk captureState() const
    {
        LegacyState ls;
        const auto sr = spec_.sampleRate > 0.0 ? spec_.sampleRate : 44100.0;

        ls.synth.startTime        = static_cast<double>(parameters_.read("synStart"));
        ls.synth.duration         = static_cast<double>(parameters_.read("synDur"));
        ls.synth.useAllPartials   = parameters_.read("useAllPartials") >= 0.5f;
        ls.synth.usePartialsPhase = parameters_.read("usePhase") >= 0.5f;
        ls.synth.partialsGain     = static_cast<double>(parameters_.read("partGain"));
        ls.synth.residualGain     = static_cast<double>(parameters_.read("resGain"));
        ls.synth.frequencyFactor  = static_cast<double>(parameters_.read("freq"));
        ls.synth.timeFactor       = static_cast<double>(parameters_.read("timeStretch"));
        ls.synth.sampleRate       = sr;
        ls.synth.useIfftSynth     = parameters_.read("useIfftSynth") >= 0.5f;
        ls.synth.frequencyShape.mode    = static_cast<FrequencyShapeMode>(
            static_cast<int>(std::round(parameters_.read("freqShapeMode"))));
        ls.synth.frequencyShape.amount  = static_cast<double>(parameters_.read("freqShapeAmount"));
        ls.synth.frequencyShape.pivotHz = static_cast<double>(parameters_.read("freqShapePivotHz"));
        ls.frequencyShape  = ls.synth.frequencyShape;
        ls.outputTrim      = static_cast<double>(parameters_.read("outputTrim"));
        ls.residualOnly    = parameters_.read("residualOnly") >= 0.5f;
        ls.classicPreviewMode = static_cast<std::int32_t>(std::round(parameters_.read("classicPreviewMode")));

        // AnalysisParams: param choice 0-3 maps to ATSA type 1-4
        ls.analysis.type        = static_cast<int>(std::round(parameters_.read("analysisType"))) + 1;
        ls.analysis.start       = static_cast<double>(parameters_.read("analysisStart"));
        ls.analysis.duration    = static_cast<double>(parameters_.read("analysisDuration"));
        ls.analysis.lowest_freq = static_cast<double>(parameters_.read("analysisLowestFreq"));
        ls.analysis.highest_freq = static_cast<double>(parameters_.read("analysisHighestFreq"));
        ls.analysis.freq_dev    = static_cast<double>(parameters_.read("analysisFreqDev"));
        ls.analysis.win_cycles  = static_cast<int>(std::round(parameters_.read("analysisWinCycles")));
        ls.analysis.win_type    = static_cast<int>(std::round(parameters_.read("analysisWinType")));
        ls.analysis.hop_size    = static_cast<double>(parameters_.read("analysisHopSize"));
        ls.analysis.lowest_mag  = static_cast<double>(parameters_.read("analysisLowestMag"));
        ls.analysis.track_len   = static_cast<int>(std::round(parameters_.read("analysisTrackLen")));
        ls.analysis.min_seg_len = static_cast<int>(std::round(parameters_.read("analysisMinSegLen")));
        ls.analysis.min_gap_len = static_cast<int>(std::round(parameters_.read("analysisMinGapLen")));
        ls.analysis.SMR_thres   = static_cast<double>(parameters_.read("analysisSmrThreshold"));
        ls.analysis.min_seg_SMR = static_cast<double>(parameters_.read("analysisMinSegSmr"));
        ls.analysis.last_peak_cont = static_cast<double>(parameters_.read("analysisLastPeakContribution"));
        ls.analysis.SMR_cont    = static_cast<double>(parameters_.read("analysisSmrContribution"));
        ls.analysis.multiRes    = parameters_.read("analysisMultiRes") >= 0.5f;

        ls.morphAmount            = static_cast<double>(parameters_.read("morphAmount"));
        ls.morphPerformanceMacro  = static_cast<double>(parameters_.read("morphPerformanceMacro"));
        ls.morphSmoothingMs       = static_cast<double>(parameters_.read("morphSmoothingMs"));
        ls.morphGridType          = static_cast<std::int32_t>(std::round(parameters_.read("morphGridType")));
        ls.morphCorrespondenceStrategy = static_cast<std::int32_t>(std::round(parameters_.read("morphCorrespondenceStrategy")));
        ls.morphQuality           = static_cast<std::int32_t>(std::round(parameters_.read("morphQuality")));
        ls.morphUnmatchedSourcePolicy = static_cast<std::int32_t>(std::round(parameters_.read("morphUnmatchedSourcePolicy")));
        ls.morphUnmatchedTargetPolicy = static_cast<std::int32_t>(std::round(parameters_.read("morphUnmatchedTargetPolicy")));
        ls.morphDiscardOptIn      = parameters_.read("morphDiscardOptIn") >= 0.5f;
        ls.morphFrequencyDomain   = static_cast<std::int32_t>(std::round(parameters_.read("morphFrequencyInterpolationDomain")));
        ls.morphAmplitudeDomain   = static_cast<std::int32_t>(std::round(parameters_.read("morphAmplitudeInterpolationDomain")));
        ls.morphPhaseStrategy     = static_cast<std::int32_t>(std::round(parameters_.read("morphPhaseStrategy")));
        ls.morphTransientPolicy   = static_cast<std::int32_t>(std::round(parameters_.read("morphTransientPolicy")));

        constexpr std::array<std::string_view, 6> kLaneAmounts {
            "morphLaneFrequencyAmount", "morphLaneAmplitudeAmount", "morphLaneResidualAmount",
            "morphLaneTimeAmount",      "morphLaneTransientAmount", "morphLanePhaseAmount"
        };
        constexpr std::array<std::string_view, 6> kLaneLinks {
            "morphLaneFrequencyLinked", "morphLaneAmplitudeLinked", "morphLaneResidualLinked",
            "morphLaneTimeLinked",      "morphLaneTransientLinked", "morphLanePhaseLinked"
        };
        for (std::size_t n = 0; n < ls.morphLaneAmounts.size(); ++n)
        {
            ls.morphLaneAmounts[n] = static_cast<double>(parameters_.read(kLaneAmounts[n]));
            ls.morphLaneLinked[n]  = parameters_.read(kLaneLinks[n]) >= 0.5f;
        }

        ls.smearnov.blurMs              = static_cast<double>(parameters_.read("smearnovBlurMs"));
        ls.smearnov.sharpen             = static_cast<double>(parameters_.read("smearnovSharpenAmount"));
        ls.smearnov.exaggerateDb        = static_cast<double>(parameters_.read("smearnovExaggerateDb"));
        ls.smearnov.mix                 = static_cast<double>(parameters_.read("smearnovMix"));
        ls.smearnov.maxDb               = static_cast<double>(parameters_.read("smearnovMaxDb"));
        ls.smearnov.targets             = static_cast<std::int32_t>(std::round(parameters_.read("smearnovTargets")));
        ls.smearnov.scope               = static_cast<std::int32_t>(std::round(parameters_.read("smearnovScope")));
        ls.smearnov.weightDetFlux       = static_cast<double>(parameters_.read("smearnovWeightDetFlux"));
        ls.smearnov.weightResFlux       = static_cast<double>(parameters_.read("smearnovWeightResFlux"));
        ls.smearnov.weightHfcFlux       = static_cast<double>(parameters_.read("smearnovWeightHfcFlux"));
        ls.smearnov.weightBirth         = static_cast<double>(parameters_.read("smearnovWeightBirth"));
        ls.smearnov.weightPhaseDev      = static_cast<double>(parameters_.read("smearnovWeightPhaseDev"));
        ls.smearnov.thresholdLambda     = static_cast<double>(parameters_.read("smearnovThresholdLambda"));
        ls.smearnov.thresholdAutoScale  = static_cast<double>(parameters_.read("smearnovThresholdAutoScale"));
        ls.smearnov.peakWindowFrames    = static_cast<std::int32_t>(std::round(parameters_.read("smearnovPeakWindowFrames")));
        ls.smearnov.minGapFrames        = static_cast<std::int32_t>(std::round(parameters_.read("smearnovMinGapFrames")));
        ls.smearnov.preMarginMs         = static_cast<double>(parameters_.read("smearnovPreMarginMs"));
        ls.smearnov.maxAttackFrames     = static_cast<std::int32_t>(std::round(parameters_.read("smearnovMaxAttackFrames")));
        ls.smearnov.usePhaseDeviation   = parameters_.read("smearnovUsePhaseDeviation") >= 0.5f;
        ls.smearnov.useRpcaFallback     = parameters_.read("smearnovUseRpcaFallback") >= 0.5f;

        ls.morphTimeline   = timeline_;
        ls.smearnovOnsets  = onsetOverlay_;
        ls.timbreJson      = timbrePlanJson_.empty()
            ? serializeTimbreMotionPlan(makeTimbreMotionPlan()) : timbrePlanJson_;

        // Keep the three legacy model roles in their corresponding v17
        // fields. The ATS codec intentionally stores opaque ATS blobs so the
        // host state remains independent of the filesystem.
        if (currentSource_ && currentSource_->sound)
            ls.atsModel = serializeAtsSoundForState(currentSource_->sound.get());
        for (std::size_t slot = 0; slot < sources_.size(); ++slot)
        {
            if (!sources_[slot])
                continue;
            ls.morphSourcePaths[slot] = sources_[slot]->path.utf8;
            if (sources_[slot]->sound)
            {
                ls.morphSoundData[slot] = serializeAtsSoundForState(sources_[slot]->sound.get());
                ls.residualStates[slot] = residualStateForSound(sources_[slot]->sound.get());
            }
        }
        for (std::size_t index = 0; index < variations_.size(); ++index)
        {
            auto& variation = ls.variations[index];
            variation.hasSnapshot = variationStored_[index];
            variation.name = "Variation " + std::to_string(index + 1);
            if (variationStored_[index] && variations_[index].size() == parameters_.size())
            {
                variation.morphAmount = variations_[index][ParameterRegistry::indexOf("morphAmount")];
                variation.performanceMacro = variations_[index][ParameterRegistry::indexOf("morphPerformanceMacro")];
                variation.smoothingMs = variations_[index][ParameterRegistry::indexOf("morphSmoothingMs")];
                constexpr std::array<std::string_view, 6> amounts {
                    "morphLaneFrequencyAmount", "morphLaneAmplitudeAmount", "morphLaneResidualAmount",
                    "morphLaneTimeAmount", "morphLaneTransientAmount", "morphLanePhaseAmount" };
                constexpr std::array<std::string_view, 6> links {
                    "morphLaneFrequencyLinked", "morphLaneAmplitudeLinked", "morphLaneResidualLinked",
                    "morphLaneTimeLinked", "morphLaneTransientLinked", "morphLanePhaseLinked" };
                for (std::size_t lane = 0; lane < amounts.size(); ++lane)
                {
                    variation.laneAmounts[lane] = variations_[index][ParameterRegistry::indexOf(amounts[lane])];
                    variation.laneLinked[lane] = variations_[index][ParameterRegistry::indexOf(links[lane])] >= 0.5f;
                }
            }
        }

        if (!morphRecipeJson_.empty())
            ls.morphRecipe = morphRecipeJson_;
        else if (sources_[0] && sources_[1] && sources_[0]->sound && sources_[1]->sound)
            ls.morphRecipe = serializeMorphRecipe(buildMorphRecipe());

        return LegacyStateCodec::encode(ls);
    }

    // The native wrapper owns the format-specific state callback. It passes
    // the complete ATSP payload here; validation happens before replacing the
    // previously published chunk so a rejected host restore cannot partially
    // mutate state.
    bool restoreState(const void* data, std::size_t size)
    {
        if (data == nullptr || size > kMaximumStateChunkBytes ||
            !ats::AtsStateCodec::decodeHeader(data, size))
            return false;

        HostStateChunk next(static_cast<const std::byte*>(data),
                            static_cast<const std::byte*>(data) + size);
        return applyStateChunk(std::move(next));
    }

    bool restoreState(const HostStateChunk& chunk)
    { return restoreState(chunk.data(), chunk.size()); }

    void clearState() noexcept { stateChunk_.clear(); }

    bool initialize(HostFormat format) noexcept
    {
        // Reinitialization is a valid wrapper lifecycle transition. Detach
        // before resetting flags so no hidden editor pointer survives with a
        // logically cold adapter.
        detachEditor();
        cancelAnalysis();
        cancelSmearnov();
        sources_ = {};
        spectrumAmpDisplayReferences_.fill(0.0);
        spectrumResidualDisplayReferences_.fill(0.0);
        currentSource_.reset();
        currentSourceRevision_ = 0;
        editorSnapshotRevision_.store(0, std::memory_order_relaxed);
        previewSource_.store(0, std::memory_order_release);
        previewLayer_.store(0, std::memory_order_release);
        previewPlaying_.store(false, std::memory_order_release);
        normalizedPlayhead_.store(0.0f, std::memory_order_release);
        previewFrame_.store(0, std::memory_order_release);
        previewLengthFrames_.store(0, std::memory_order_release);
        liveSmearEnabled_.store(false, std::memory_order_release);
        smearedSourceAvailable_.store(false, std::memory_order_release);
        timeline_ = { 0.0f, 1.0f };
        typedTimeline_.clear();
        timbrePlan_.clear();
        timbrePlanJson_.clear();
        onsetOverlay_.clear();
        onsetHandleOverlay_.clear();
        smearnovDetectionCurve_.clear();
        smearnovThresholdCurve_.clear();
        variations_ = {};
        morphRecipeJson_.clear();
        variationStored_ = {};
        timelineRevision_ = 0;
        timbreRevision_ = 0;
        onsetRevision_ = 0;
        spectralViewSource_ = 0;
        spectralViewMode_ = 0;
        history_.clear();
        parameterGestureBefore_.reset();
        format_ = format;
        state_ = HostState::cold;
        telemetry_.publish({});
        return true;
    }

    bool prepare(const HostPrepareSpec& spec)
    {
        // Preserve the direct neutral-test entry point while making the
        // wrapper lifecycle explicit for native adapters.
        if (spec.sampleRate <= 0.0 || spec.maximumBlockSize == 0 || spec.outputChannels != 2 ||
            spec.inputChannels != 0)
            return false;
        spec_ = spec;
        resolveParameterIndices();
        playback_.prepare(spec.outputChannels, spec.maximumBlockSize);
        playbackPartials_.prepare(spec.outputChannels, spec.maximumBlockSize);
        playbackResidual_.prepare(spec.outputChannels, spec.maximumBlockSize);
        playbackCapacityFrames_ = spec.maximumBlockSize;
        outputTrim_.prepare(spec.sampleRate, 20.0);
        partGainSmoother_.prepare(spec.sampleRate, 20.0);
        resGainSmoother_.prepare(spec.sampleRate, 20.0);
        outputTrim_.reset(parameters_.read(outputTrimIndex_));
        partGainSmoother_.reset(parameters_.read(partGainIndex_));
        resGainSmoother_.reset(parameters_.read(resGainIndex_));
        midi_.reset();
        state_ = HostState::prepared;
        return true;
    }

    void release() noexcept
    {
        cancelAnalysis();
        cancelSmearnov();
        previewPlaying_.store(false, std::memory_order_release);
        previewLengthFrames_.store(0, std::memory_order_release);
        detachEditor();
        state_ = HostState::released;
        midi_.reset();
    }

    // Native lifecycle entry point. The host owns the editor object; this
    // adapter owns only the attachment/visibility state and forwards every
    // operation through the backend-neutral seam.
    bool attachEditor(ui::IEditor& editor, void* nativeParent, float scale,
                      float logicalWidth, float logicalHeight) noexcept
    {
        if (nativeParent == nullptr || scale <= 0.0f || logicalWidth <= 0.0f || logicalHeight <= 0.0f)
            return false;

        // A wrapper may recreate or reparent its editor during a host view
        // transition. Never leave the previous editor visible or retained by
        // the neutral adapter while installing the replacement.
        if (editor_ != nullptr)
            detachEditor();

        if (!editor.setSize(static_cast<std::uint32_t>(logicalWidth),
                            static_cast<std::uint32_t>(logicalHeight)))
            return false;
        editor.setScale(scale);
        if (!editor.setParent(nativeParent))
            return false;

        editor_ = &editor;
        editorAttached_ = true;
        editorVisible_ = editor.isShowing();
        editorScale_ = scale;
        editorWidth_ = logicalWidth;
        editorHeight_ = logicalHeight;
        return true;
    }

    bool attachEditor(float scale, float logicalWidth, float logicalHeight) noexcept
    {
        if (scale <= 0.0f || logicalWidth <= 0.0f || logicalHeight <= 0.0f)
            return false;
        editorAttached_ = true;
        editorVisible_ = false;
        editorScale_ = scale;
        editorWidth_ = logicalWidth;
        editorHeight_ = logicalHeight;
        return true;
    }

    bool attachStandaloneEditor(ui::IEditor& editor, float scale,
                                float logicalWidth, float logicalHeight) noexcept
    {
        if (format_ != HostFormat::standalone || scale <= 0.0f ||
            logicalWidth <= 0.0f || logicalHeight <= 0.0f)
            return false;
        if (editor_ != nullptr)
            detachEditor();
        if (!editor.setSize(static_cast<std::uint32_t>(logicalWidth),
                            static_cast<std::uint32_t>(logicalHeight)))
            return false;
        editor.setScale(scale);
        if (!editor.showStandalone())
            return false;

        editor_ = &editor;
        editorAttached_ = true;
        editorVisible_ = editor.isShowing();
        editorScale_ = scale;
        editorWidth_ = logicalWidth;
        editorHeight_ = logicalHeight;
        return editorVisible_;
    }

    void detachEditor() noexcept
    {
        if (editor_ != nullptr)
            editor_->hide();
        editor_ = nullptr;
        editorVisible_ = false;
        editorAttached_ = false;
    }

    bool setEditorVisible(bool visible) noexcept
    {
        if (visible && !editorAttached_) return false;
        if (editor_ != nullptr)
        {
            if (visible)
                editor_->show();
            else
                editor_->hide();
        }
        editorVisible_ = visible;
        return true;
    }

    bool setEditorLogicalSize(float width, float height) noexcept
    {
        if (!editorAttached_ || width <= 0.0f || height <= 0.0f) return false;
        if (editor_ != nullptr &&
            !editor_->setSize(static_cast<std::uint32_t>(width),
                              static_cast<std::uint32_t>(height)))
            return false;
        editorWidth_ = width;
        editorHeight_ = height;
        return true;
    }

    bool publishPlayback(std::int32_t slot, JobGeneration generation) noexcept
    {
        return state_ == HostState::prepared && playback_.publish(slot, generation);
    }

    [[nodiscard]] std::int32_t acquirePlaybackWriteSlot() const noexcept
    { return state_ == HostState::prepared ? playback_.tryAcquireWriteSlot() : PlaybackBufferPool::kNoBuffer; }

    [[nodiscard]] AudioBufferView playbackWriteView(std::int32_t slot) noexcept
    { return state_ == HostState::prepared ? playback_.writeView(slot) : AudioBufferView {}; }

    bool process(AudioBufferView output, MidiEventList midiEvents) noexcept
    {
        if (state_ != HostState::prepared || !output.valid() || output.channelCount == 0)
        {
            output.clear();
            return false;
        }
        for (const auto& event : midiEvents)
            midi_.consume(event);

        if (!previewPlaying_.load(std::memory_order_acquire))
        {
            output.clear();
            return true;
        }

        // Acquire read slots for all three layers atomically before mixing.
        const auto combinedSlot  = playback_.beginAudioRead();
        const auto partialsSlot  = playbackPartials_.beginAudioRead();
        const auto residualSlot  = playbackResidual_.beginAudioRead();
        const auto* combinedBuf  = playback_.readBuffer(combinedSlot);
        const auto* partialsBuf  = playbackPartials_.readBuffer(partialsSlot);
        const auto* residualBuf  = playbackResidual_.readBuffer(residualSlot);

        // Update per-sample smoother targets. All three are ticked every callback
        // so their internal state stays consistent regardless of which layer is active.
        outputTrim_.setTarget(parameters_.read(outputTrimIndex_));
        partGainSmoother_.setTarget(parameters_.read(partGainIndex_));
        resGainSmoother_.setTarget(parameters_.read(resGainIndex_));

        const auto layer        = previewLayer_.load(std::memory_order_acquire);
        const auto classicMode  = parameters_.read(classicPreviewModeIndex_) < 0.5f;
        const auto residualOnly = parameters_.read(residualOnlyIndex_) >= 0.5f;

        // Select the buffer(s) for the current layer.  residualOnly overrides
        // all layer settings to residual-only regardless of classicPreviewMode.
        const OwnedAudioBuffer* primary = nullptr;
        const OwnedAudioBuffer* second  = nullptr;
        bool layerMix = false;

        if (residualOnly || layer == 2)
        {
            primary = residualBuf;
        }
        else if (layer == 1)
        {
            primary = partialsBuf;
        }
        else // layer == 0 (both)
        {
            if (classicMode)
                primary = combinedBuf;
            else
            {
                primary  = partialsBuf;
                second   = residualBuf;
                layerMix = true;
            }
        }

        const auto* lengthRef = primary ? primary : second;
        if (lengthRef != nullptr)
        {
            const auto sourceFrames   = lengthRef->frameCount();
            const auto timelineFrames = previewLengthFrames_.load(std::memory_order_relaxed);
            const auto timelineStart  = timelineFrames > 0
                ? previewFrame_.load(std::memory_order_relaxed) % timelineFrames : 0u;
            const auto frames   = std::min(output.frameCount, sourceFrames);
            const auto channels = std::min(output.channelCount,
                primary ? primary->channelCount() : output.channelCount);

            for (std::uint32_t frame = 0; frame < frames; ++frame)
            {
                const auto trim   = outputTrim_.process();
                const auto partGn = partGainSmoother_.process();
                const auto resGn  = resGainSmoother_.process();
                const auto rf     = sourceFrames == 0 ? 0u : (timelineStart + frame) % sourceFrames;

                for (std::uint32_t channel = 0; channel < channels; ++channel)
                {
                    if (output.channels[channel] == nullptr) continue;
                    float sample = 0.0f;
                    if (layerMix)
                    {
                        if (primary && primary->channel(channel))
                            sample += primary->channel(channel)[rf] * partGn;
                        if (second  && second->channel(channel))
                            sample += second->channel(channel)[rf]  * resGn;
                    }
                    else if (primary && primary->channel(channel))
                    {
                        const float raw = primary->channel(channel)[rf];
                        if (residualOnly || layer == 2)      sample = raw * resGn;
                        else if (layer == 1)                  sample = raw * partGn;
                        else /* classicMode both */           sample = raw;
                    }
                    output.channels[channel][frame] = sample * trim;
                }
            }

            if (sourceFrames > 0)
            {
                const auto nextFrame = timelineFrames == 0
                    ? 0u : (timelineStart + frames) % timelineFrames;
                previewFrame_.store(nextFrame, std::memory_order_release);
                normalizedPlayhead_.store(timelineFrames == 0
                                               ? 0.0f
                                               : static_cast<float>(nextFrame) /
                                                     static_cast<float>(timelineFrames),
                                           std::memory_order_release);
            }
        }

        // Zero any frames beyond what the buffer covers.
        const auto filled = lengthRef ? std::min(output.frameCount, lengthRef->frameCount()) : 0u;
        for (std::uint32_t channel = 0; channel < output.channelCount; ++channel)
            if (output.channels[channel] != nullptr)
                for (std::uint32_t frame = filled; frame < output.frameCount; ++frame)
                    output.channels[channel][frame] = 0.0f;

        playback_.endAudioRead(combinedSlot);
        playbackPartials_.endAudioRead(partialsSlot);
        playbackResidual_.endAudioRead(residualSlot);
        return true;
    }

    [[nodiscard]] int semitoneOffset() const noexcept { return midi_.semitoneOffset(); }

    struct AnalysisProfileWrite
    {
        std::string_view id;
        float value = 0.0f;
    };

    // The profile recipes are exposed as data so a test can check each value
    // against its descriptor range. Applying them through ParameterStore::write
    // clamps silently, which would otherwise hide a value that names the wrong
    // choice ordinal.
    [[nodiscard]] static std::vector<AnalysisProfileWrite> analysisProfileWrites(int profile)
    {
        // analysisType ordinals 0-3 map to ATSA types 1-4, so the full
        // analysis is ordinal 3.
        constexpr float kAnalysisTypeFull = 3.0f;
        switch (profile)
        {
        case 1:
            return { { "analysisType", kAnalysisTypeFull },
                     { "analysisLowestFreq", 70.0f },
                     { "analysisHighestFreq", 9000.0f },
                     { "analysisSmrThreshold", 20.0f },
                     { "analysisTrackLen", 5.0f } };
        case 2:
            return { { "analysisType", kAnalysisTypeFull },
                     { "analysisLowestFreq", 30.0f },
                     { "analysisHighestFreq", 18000.0f },
                     { "analysisHopSize", 0.125f },
                     { "analysisTrackLen", 2.0f },
                     { "analysisMinSegLen", 1.0f } };
        default:
            // 0 restores descriptor defaults; 3 (custom) leaves values alone.
            return {};
        }
    }

private:
    struct SmearnovWorkResult
    {
        bool apply = false;
        int slot = -1;
        std::uint64_t sourceRevision = 0;
        std::vector<AtsOnset> onsets;
        std::vector<double> detectionCurve;
        std::vector<double> thresholdCurve;
        AtsSoundPtr transformed;
        Error error;
    };

    void recordMutation(std::string label, const HostStateChunk& before)
    {
        if (before.empty())
            return;
        history_.begin(std::move(label), before);
        (void) history_.commit(captureState());
    }

    void resolveParameterIndices() noexcept
    {
        outputTrimIndex_       = ParameterRegistry::indexOf("outputTrim");
        partGainIndex_         = ParameterRegistry::indexOf("partGain");
        resGainIndex_          = ParameterRegistry::indexOf("resGain");
        freqIndex_             = ParameterRegistry::indexOf("freq");
        freqShapeModeIndex_    = ParameterRegistry::indexOf("freqShapeMode");
        freqShapeAmountIndex_  = ParameterRegistry::indexOf("freqShapeAmount");
        freqShapePivotHzIndex_ = ParameterRegistry::indexOf("freqShapePivotHz");
        timeStretchIndex_      = ParameterRegistry::indexOf("timeStretch");
        synStartIndex_         = ParameterRegistry::indexOf("synStart");
        synDurIndex_           = ParameterRegistry::indexOf("synDur");
        usePhaseIndex_         = ParameterRegistry::indexOf("usePhase");
        useAllPartialsIndex_   = ParameterRegistry::indexOf("useAllPartials");
        residualOnlyIndex_     = ParameterRegistry::indexOf("residualOnly");
        useIfftSynthIndex_     = ParameterRegistry::indexOf("useIfftSynth");
        classicPreviewModeIndex_ = ParameterRegistry::indexOf("classicPreviewMode");
    }

    [[nodiscard]] std::uint64_t frequencyShapeRevision() const noexcept
    {
        // Keep this revision stable across editorSnapshot() publications. It
        // changes only when a parameter that affects displayed frequencies
        // changes, so mode switches and repaint cadence cannot invalidate the
        // snapshot cache.
        std::uint64_t hash = 1469598103934665603ull;
        const auto mix = [&hash](float value) {
            std::uint32_t bits = 0;
            static_assert(sizeof(bits) == sizeof(value));
            std::memcpy(&bits, &value, sizeof(bits));
            for (int shift = 0; shift < 32; shift += 8)
            {
                hash ^= static_cast<std::uint8_t>((bits >> shift) & 0xffu);
                hash *= 1099511628211ull;
            }
        };
        mix(parameters_.read("freqShapeMode"));
        mix(parameters_.read("freqShapeAmount"));
        mix(parameters_.read("freqShapePivotHz"));
        return hash;
    }

    // Decode the complete chunk before replacing either the live parameters or
    // the accepted byte payload. A header-valid but truncated chunk is not a
    // state and must leave the previous state untouched.
    bool applyStateChunk(HostStateChunk chunk)
    {
        const auto headerOnly = ats::AtsStateCodec::encodeHeader();
        if (chunk == headerOnly)
        {
            // AU class-info and a few neutral hosts legitimately round-trip
            // an empty project as the header-only ATSP payload.
            stateChunk_ = std::move(chunk);
            return true;
        }
        if (chunk.size() <= 8)
            return false;
        auto legacy = LegacyStateCodec::decode(chunk.data(), chunk.size());
        // A small header-extension chunk was emitted by the early neutral
        // host probe. Keep that narrowly scoped compatibility form readable;
        // every real ATSP payload still has to pass the complete decoder.
        const bool legacyProbeChunk = chunk.size() == 9 &&
            AtsStateCodec::decodeHeader(chunk.data(), chunk.size());
        if (!legacy && !legacyProbeChunk)
            return false;
        if (legacy)
        {
            if (!legacy->morphRecipe.empty())
            {
                MorphRecipe recipe;
                std::string recipeError;
                if (!deserializeMorphRecipe(legacy->morphRecipe, recipe, &recipeError))
                    return false;
            }
            if (!legacy->atsModel.empty() && !deserializeAtsSoundForState(legacy->atsModel, 3))
                return false;
            for (std::size_t slot = 0; slot < legacy->morphSoundData.size(); ++slot)
                if (!legacy->morphSoundData[slot].empty() &&
                    !deserializeAtsSoundForState(legacy->morphSoundData[slot], static_cast<int>(slot),
                                                  legacy->morphSourcePaths[slot]))
                    return false;
            applyLegacyStateToParameters(*legacy);
        }
        stateChunk_ = std::move(chunk);
        return true;
    }

    void applyLegacyStateToParameters(const LegacyState& ls)
    {
        applyLegacyStateToParameterStore(ls, parameters_);

        if (!ls.morphTimeline.empty()) timeline_ = ls.morphTimeline;
        typedTimeline_.clear();
        timbrePlanJson_ = ls.timbreJson;
        onsetOverlay_ = ls.smearnovOnsets;
        onsetHandleOverlay_.clear();
        for (const auto onset : onsetOverlay_)
            onsetHandleOverlay_.push_back({ onset, onset, onset, 0.0f });

        if (!ls.atsModel.empty())
            currentSource_ = deserializeAtsSoundForState(ls.atsModel, 3);
        else
            currentSource_.reset();
        ++currentSourceRevision_;
        spectrumAmpDisplayReferences_[0] = 0.0;
        spectrumResidualDisplayReferences_[0] = 0.0;
        for (std::size_t slot = 0; slot < sources_.size(); ++slot)
        {
            if (ls.morphSoundData[slot].empty())
                sources_[slot].reset();
            else
                sources_[slot] = deserializeAtsSoundForState(
                    ls.morphSoundData[slot], static_cast<int>(slot), ls.morphSourcePaths[slot]);
            ++sourceRevisions_[slot];
            spectrumAmpDisplayReferences_[slot + 1] = 0.0;
            spectrumResidualDisplayReferences_[slot + 1] = 0.0;
        }
        morphRecipeJson_ = ls.morphRecipe;
        if (!morphRecipeJson_.empty())
        {
            MorphRecipe restoredRecipe;
            std::string recipeError;
            if (!deserializeMorphRecipe(morphRecipeJson_, restoredRecipe, &recipeError))
                morphRecipeJson_.clear();
        }
        for (std::size_t index = 0; index < ls.variations.size(); ++index)
        {
            variations_[index].clear();
            variationStored_[index] = ls.variations[index].hasSnapshot;
            if (!variationStored_[index])
                continue;
            variations_[index] = parameters_.snapshot();
            const auto set = [this, &index](std::string_view id, float value) {
                const auto dense = ParameterRegistry::indexOf(id);
                if (dense < variations_[index].size())
                    variations_[index][dense] = value;
            };
            set("morphAmount", static_cast<float>(ls.variations[index].morphAmount));
            set("morphPerformanceMacro", static_cast<float>(ls.variations[index].performanceMacro));
            set("morphSmoothingMs", static_cast<float>(ls.variations[index].smoothingMs));
            constexpr std::array<std::string_view, 6> amounts {
                "morphLaneFrequencyAmount", "morphLaneAmplitudeAmount", "morphLaneResidualAmount",
                "morphLaneTimeAmount", "morphLaneTransientAmount", "morphLanePhaseAmount" };
            constexpr std::array<std::string_view, 6> links {
                "morphLaneFrequencyLinked", "morphLaneAmplitudeLinked", "morphLaneResidualLinked",
                "morphLaneTimeLinked", "morphLaneTransientLinked", "morphLanePhaseLinked" };
            for (std::size_t lane = 0; lane < amounts.size(); ++lane)
            {
                set(amounts[lane], static_cast<float>(ls.variations[index].laneAmounts[lane]));
                set(links[lane], ls.variations[index].laneLinked[lane] ? 1.0f : 0.0f);
            }
        }
    }

    static const char* analysisProfileName(int profile) noexcept
    {
        switch (profile)
        {
        case 1: return "speech";
        case 2: return "percussive";
        case 3: return "custom";
        default: return "default";
        }
    }

    void applyAnalysisProfile(int profile)
    {
        if (profile == 0)
        {
            for (const auto& descriptor : ParameterRegistry::descriptors())
                if (descriptor.id.rfind("analysis", 0) == 0)
                    (void) parameters_.write(descriptor.denseIndex, descriptor.defaultValue);
            return;
        }
        for (const auto& write : analysisProfileWrites(profile))
            (void) parameters_.write(write.id, write.value);
    }

    static bool strictlyOrderedNormalized(const std::vector<float>& values) noexcept
    {
        float previous = -1.0f;
        for (const auto value : values)
        {
            if (!std::isfinite(value) || value < 0.0f || value > 1.0f || value < previous)
                return false;
            previous = value;
        }
        return true;
    }

    [[nodiscard]] SmearnovDetectParams smearnovDetectParams() const noexcept
    {
        SmearnovDetectParams params;
        params.weightDetFlux = parameters_.read("smearnovWeightDetFlux");
        params.weightResFlux = parameters_.read("smearnovWeightResFlux");
        params.weightHfcFlux = parameters_.read("smearnovWeightHfcFlux");
        params.weightBirth = parameters_.read("smearnovWeightBirth");
        params.weightPhaseDev = parameters_.read("smearnovWeightPhaseDev");
        params.usePhaseDeviation = parameters_.read("smearnovUsePhaseDeviation") >= 0.5f;
        params.thresholdLambda = parameters_.read("smearnovThresholdLambda");
        params.thresholdAutoScale = parameters_.read("smearnovThresholdAutoScale");
        params.peakWindowRadiusFrames = static_cast<int>(parameters_.read("smearnovPeakWindowFrames"));
        params.minGapFramesOverride = static_cast<int>(parameters_.read("smearnovMinGapFrames"));
        params.preMarginSeconds = parameters_.read("smearnovPreMarginMs") / 1000.0;
        params.maxAttackFrames = static_cast<int>(parameters_.read("smearnovMaxAttackFrames"));
        return params;
    }

    [[nodiscard]] AnalysisParams analysisParams() const noexcept
    {
        AnalysisParams params;
        params.type = static_cast<int>(std::lround(parameters_.read("analysisType"))) + 1;
        params.start = parameters_.read("analysisStart");
        params.duration = parameters_.read("analysisDuration");
        params.lowest_freq = parameters_.read("analysisLowestFreq");
        params.highest_freq = parameters_.read("analysisHighestFreq");
        params.freq_dev = parameters_.read("analysisFreqDev");
        params.win_cycles = static_cast<int>(std::lround(parameters_.read("analysisWinCycles")));
        params.win_type = static_cast<int>(std::lround(parameters_.read("analysisWinType")));
        params.hop_size = parameters_.read("analysisHopSize");
        params.lowest_mag = parameters_.read("analysisLowestMag");
        params.track_len = static_cast<int>(std::lround(parameters_.read("analysisTrackLen")));
        params.min_seg_len = static_cast<int>(std::lround(parameters_.read("analysisMinSegLen")));
        params.min_gap_len = static_cast<int>(std::lround(parameters_.read("analysisMinGapLen")));
        params.SMR_thres = parameters_.read("analysisSmrThreshold");
        params.min_seg_SMR = parameters_.read("analysisMinSegSmr");
        params.last_peak_cont = parameters_.read("analysisLastPeakContribution");
        params.SMR_cont = parameters_.read("analysisSmrContribution");
        params.multiRes = parameters_.read("analysisMultiRes") >= 0.5f;
        return params;
    }

    [[nodiscard]] AnalysisParams analysisParamsForSource(const engine::ImportedSource& source) const noexcept
    {
        auto params = analysisParams();
        const auto sourceDuration = source.sampleRate > 0
            ? static_cast<double>(source.monoSamples.size()) / static_cast<double>(source.sampleRate) : 0.0;
        if (sourceDuration > 0.0)
        {
            // ATSA's legacy tracker does not safely handle a host-automated start at/past EOF.
            params.start = params.start >= sourceDuration ? 0.0 : std::max(0.0, params.start);
            if (params.duration > 0.0)
                params.duration = std::min(params.duration, sourceDuration - params.start);
        }
        params.highest_freq = std::min(params.highest_freq, source.sampleRate * 0.5);
        params.lowest_freq = std::min(params.lowest_freq, std::max(1.0, params.highest_freq - 1.0));
        return params;
    }

    [[nodiscard]] SmearnovParams smearnovParams() const noexcept
    {
        SmearnovParams params;
        params.blurMs = parameters_.read("smearnovBlurMs");
        params.sharpenAmount = parameters_.read("smearnovSharpenAmount");
        params.exaggerateDb = parameters_.read("smearnovExaggerateDb");
        params.mix = parameters_.read("smearnovMix") / 100.0;
        params.maxDb = parameters_.read("smearnovMaxDb");
        switch (static_cast<int>(parameters_.read("smearnovTargets")))
        {
        case 0: params.targets = SmearnovTarget::Partials; break;
        case 1: params.targets = SmearnovTarget::Residual; break;
        default: params.targets = SmearnovTarget::Both; break;
        }
        return params;
    }

    bool queueSmearnov(bool apply, Error& error)
    {
        if (smearnovRunning_.load(std::memory_order_acquire))
        {
            error = { "smearnov_busy", "Smearnov work is already in progress" };
            return true;
        }
        const auto slot = previewSource_.load(std::memory_order_acquire);
        if (slot < 0 || slot >= static_cast<int>(sources_.size()) || !sources_[static_cast<std::size_t>(slot)] ||
            !sources_[static_cast<std::size_t>(slot)]->sound)
        {
            error = { "missing_model", "analyse a source before running Smearnov" };
            return true;
        }
        auto* copied = copy_sound(sources_[static_cast<std::size_t>(slot)]->sound.get());
        if (copied == nullptr)
        {
            error = { "copy_failed", "could not snapshot the source model for Smearnov" };
            return true;
        }
        const auto sourceRevision = sourceRevisions_[static_cast<std::size_t>(slot)];
        const auto detect = smearnovDetectParams();
        const auto transform = smearnovParams();
        const auto requested = onsetOverlay_;
        const auto scope = static_cast<int>(std::lround(parameters_.read("smearnovScope")));
        const auto selectionFirst = selectionFirstFrame_;
        const auto selectionLast = selectionLastFrame_;
        smearnovCancellation_.cancel();
        smearnovCancellation_ = CancellationSource {};
        const auto generation = sourceGenerations_.next();
        const auto token = smearnovCancellation_.token();
        {
            auto promise = std::make_shared<std::promise<SmearnovWorkResult>>();
            smearnovFuture_ = promise->get_future();
            smearnovScheduler_.submitVoid(
                [apply, slot, sourceRevision, snapshot = AtsSoundPtr { copied }, detect, transform,
                 requested, scope, selectionFirst, selectionLast, token, promise]() mutable {
                    SmearnovWorkResult result;
                    result.apply = apply;
                    result.slot = slot;
                    result.sourceRevision = sourceRevision;
                    if (token.isCancelled())
                    {
                        result.error = { "cancelled", "Smearnov work was cancelled" };
                        promise->set_value(std::move(result));
                        return;
                    }
                    SmearnovDetectionFunctions functions;
                    auto onsets = detectOnsets(snapshot.get(), detect, &functions);
                    result.detectionCurve = std::move(functions.combined);
                    result.thresholdCurve = std::move(functions.threshold);
                    if (!requested.empty())
                    {
                        const auto detected = onsets;
                        onsets.clear();
                        const auto frameCount = std::max(1, snapshot.get()->frames - 1);
                        for (const auto normalized : requested)
                        {
                            const auto frame = static_cast<int>(std::lround(normalized * frameCount));
                            auto nearest = detected.end();
                            if (!detected.empty())
                                nearest = std::min_element(detected.begin(), detected.end(),
                                    [frame](const AtsOnset& a, const AtsOnset& b) {
                                        return std::abs(a.frame - frame) < std::abs(b.frame - frame);
                                    });
                            if (nearest != detected.end())
                            {
                                auto edited = *nearest;
                                const auto delta = frame - edited.frame;
                                edited.frame = frame;
                                edited.peakFrame = std::clamp(edited.peakFrame + delta, 0, frameCount);
                                edited.preFrame = std::clamp(edited.preFrame + delta, 0, edited.peakFrame);
                                onsets.push_back(edited);
                            }
                            else
                            {
                                onsets.push_back({ frame, frame, frame, 0.0 });
                            }
                        }
                    }
                    std::string scopeError;
                    onsets = applySmearnovScope(scope, std::move(onsets), selectionFirst,
                                                selectionLast, scopeError);
                    if (!scopeError.empty())
                    {
                        result.error = { "smearnov_scope", std::move(scopeError) };
                        promise->set_value(std::move(result));
                        return;
                    }
                    if (token.isCancelled())
                    {
                        result.error = { "cancelled", "Smearnov work was cancelled" };
                        promise->set_value(std::move(result));
                        return;
                    }
                    result.onsets = std::move(onsets);
                    if (apply)
                    {
                        if (!smearAttacks(snapshot.get(), result.onsets, transform))
                        {
                            result.error = { "smearnov_no_change", "Smearnov did not modify the selected source" };
                            promise->set_value(std::move(result));
                            return;
                        }
                        result.transformed = std::move(snapshot);
                    }
                    promise->set_value(std::move(result));
                });
        }
        smearnovRunning_.store(true, std::memory_order_release);
        return true;
    }

    bool pollSmearnov(Error& error)
    {
        error = {};
        if (!smearnovRunning_.load(std::memory_order_acquire) || !smearnovFuture_.valid() ||
            smearnovFuture_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return false;
        auto result = smearnovFuture_.get();
        smearnovRunning_.store(false, std::memory_order_release);
        if (result.error)
        {
            error = std::move(result.error);
            return true;
        }
        if (result.slot < 0 || result.slot >= static_cast<int>(sources_.size()) ||
            !sources_[static_cast<std::size_t>(result.slot)] ||
            sourceRevisions_[static_cast<std::size_t>(result.slot)] != result.sourceRevision)
        {
            error = { "stale_smearnov", "source changed while Smearnov work was running" };
            return true;
        }
        const auto frameCount = std::max(1, sources_[static_cast<std::size_t>(result.slot)]->sound.get()->frames - 1);
        onsetOverlay_.clear();
        onsetOverlay_.reserve(result.onsets.size());
        onsetHandleOverlay_.clear();
        onsetHandleOverlay_.reserve(result.onsets.size());
        for (const auto& onset : result.onsets)
        {
            onsetOverlay_.push_back(std::clamp(static_cast<float>(onset.frame) / frameCount, 0.0f, 1.0f));
            onsetHandleOverlay_.push_back({
                std::clamp(static_cast<float>(onset.frame) / frameCount, 0.0f, 1.0f),
                std::clamp(static_cast<float>(onset.preFrame) / frameCount, 0.0f, 1.0f),
                std::clamp(static_cast<float>(onset.peakFrame) / frameCount, 0.0f, 1.0f),
                std::max(0.0f, static_cast<float>(onset.strength)) });
        }
        smearnovDetectionCurve_ = std::move(result.detectionCurve);
        smearnovThresholdCurve_ = std::move(result.thresholdCurve);
        ++onsetRevision_;
        if (result.apply)
        {
            sources_[static_cast<std::size_t>(result.slot)]->sound = std::move(result.transformed);
            ++sourceRevisions_[static_cast<std::size_t>(result.slot)];
            smearedSourceAvailable_.store(true, std::memory_order_release);
            if (state_ == HostState::prepared && result.slot == previewSource_.load(std::memory_order_acquire))
                (void) renderSourceForPreview(result.slot, error);
        }
        return true;
    }

    void cancelSmearnov() noexcept { smearnovCancellation_.cancel(); }

    [[nodiscard]] static std::vector<AtsOnset> applySmearnovScope(
        int scope, std::vector<AtsOnset> onsets, int firstFrame, int lastFrame,
        std::string& error)
    {
        error.clear();
        if (scope == 1)
        {
            if (firstFrame < 0 || lastFrame < 0)
            {
                error = "select a spectrum frame range, or set Smearnov scope to All";
                return {};
            }
            const auto lo = std::min(firstFrame, lastFrame);
            const auto hi = std::max(firstFrame, lastFrame);
            onsets.erase(std::remove_if(onsets.begin(), onsets.end(), [lo, hi](const AtsOnset& onset) {
                return onset.peakFrame < lo || onset.preFrame > hi;
            }), onsets.end());
        }
        else if (scope == 2 && onsets.size() > 1)
        {
            double strengthSum = 0.0;
            for (const auto& onset : onsets) strengthSum += onset.strength;
            const auto mean = strengthSum / static_cast<double>(onsets.size());
            onsets.erase(std::remove_if(onsets.begin(), onsets.end(), [mean](const AtsOnset& onset) {
                return onset.strength < mean;
            }), onsets.end());
        }
        return onsets;
    }

    [[nodiscard]] static MorphSourceMetadata makeMorphSourceMetadata(
        const engine::ImportedSource& source, int slot, std::uint64_t revision)
    {
        MorphSourceMetadata metadata;
        const auto* sound = source.sound.get();
        if (sound == nullptr)
            return metadata;
        metadata.id = slot == 0 ? "source_a" : "source_b";
        metadata.name = source.name.empty() ? (slot == 0 ? "Source A" : "Source B") : source.name;
        metadata.hash = "rev:" + std::to_string(revision) + ";frames:" +
                        std::to_string(sound->frames) + ";partials:" +
                        std::to_string(sound->partials);
        metadata.sampleRate = sound->srate;
        metadata.durationSeconds = sound->dur;
        metadata.frameCount = sound->frames;
        metadata.partialCount = sound->partials;
        metadata.frameHopSeconds = sound->frames > 1 && sound->dur > 0.0
            ? sound->dur / static_cast<double>(sound->frames - 1) : 0.0;
        metadata.channelCount = 1;
        metadata.hasPhase = sound->pha != nullptr;
        metadata.hasResidual = sound->band_energy != nullptr;
        auto low = std::numeric_limits<double>::max();
        auto high = 0.0;
        if (sound->frq != nullptr)
            for (int partial = 0; partial < sound->partials; ++partial)
                if (sound->frq[partial] != nullptr)
                    for (int frame = 0; frame < sound->frames; ++frame)
                    {
                        const auto frequency = sound->frq[partial][frame];
                        const auto active = sound->amp == nullptr || sound->amp[partial] == nullptr ||
                                            sound->amp[partial][frame] > 0.0;
                        if (active && std::isfinite(frequency) && frequency > 0.0)
                        {
                            low = std::min(low, frequency);
                            high = std::max(high, frequency);
                        }
                    }
        metadata.lowestFrequencyHz = low == std::numeric_limits<double>::max() ? 0.0 : low;
        metadata.highestFrequencyHz = high > 0.0 ? high : std::max(0.0, sound->srate * 0.5);
        return metadata;
    }

    [[nodiscard]] static std::vector<MorphBandAssignment> makeMorphAssignments(
        const ATS_SOUND* sound, const MorphFrequencyGrid& grid)
    {
        std::vector<MorphPartialFrame> frames;
        if (sound == nullptr || sound->partials <= 0 || sound->frames <= 0 || sound->frq == nullptr)
            return {};
        frames.reserve(static_cast<std::size_t>(sound->partials));
        for (int partial = 0; partial < sound->partials; ++partial)
        {
            if (sound->frq[partial] == nullptr) continue;
            double weightedFrequency = 0.0;
            double totalAmplitude = 0.0;
            double peakAmplitude = 0.0;
            int peakFrame = -1;
            int activeFrames = 0;
            for (int frame = 0; frame < sound->frames; ++frame)
            {
                const auto frequency = sound->frq[partial][frame];
                const auto amplitude = sound->amp != nullptr && sound->amp[partial] != nullptr
                    ? sound->amp[partial][frame] : 1.0;
                if (!std::isfinite(frequency) || frequency <= 0.0 ||
                    !std::isfinite(amplitude) || amplitude <= 0.0)
                    continue;
                weightedFrequency += frequency * amplitude;
                totalAmplitude += amplitude;
                if (amplitude > peakAmplitude)
                {
                    peakAmplitude = amplitude;
                    peakFrame = frame;
                }
                ++activeFrames;
            }
            if (activeFrames == 0 || totalAmplitude <= 0.0) continue;
            frames.push_back({ partial, peakFrame, weightedFrequency / totalAmplitude,
                               totalAmplitude / static_cast<double>(activeFrames), true });
        }
        return assignPartialFramesToFrequencyBands(grid, frames);
    }

    [[nodiscard]] static std::vector<MorphResidualNoiseBand> makeMorphResidualBands(
        const ATS_SOUND* sound)
    {
        std::vector<MorphResidualNoiseBand> bands;
        if (sound == nullptr || sound->band_energy == nullptr || sound->frames <= 0)
            return bands;
        for (int band = 0; band < ATSA_CRITICAL_BANDS; ++band)
        {
            if (sound->band_energy[band] == nullptr) continue;
            double energy = 0.0;
            int count = 0;
            for (int frame = 0; frame < sound->frames; ++frame)
            {
                const auto value = sound->band_energy[band][frame];
                if (std::isfinite(value) && value > 0.0) { energy += value; ++count; }
            }
            if (count > 0 && energy > 0.0)
            {
                MorphResidualNoiseBand value;
                value.bandIndex = band;
                value.orphanResidualPower = energy / static_cast<double>(count) /
                    (static_cast<double>(std::max(1, sound->window_size)) * ATSA_NOISE_VARIANCE);
                bands.push_back(value);
            }
        }
        return bands;
    }

    [[nodiscard]] static std::vector<MorphTransientMarker> detectMorphTransients(
        const ATS_SOUND* sound, double hopSeconds)
    {
        std::vector<MorphTransientMarker> markers;
        if (sound == nullptr || sound->frames <= 1 || sound->amp == nullptr || sound->partials <= 0)
            return markers;
        std::vector<double> energy(static_cast<std::size_t>(sound->frames), 0.0);
        for (int frame = 0; frame < sound->frames; ++frame)
            for (int partial = 0; partial < sound->partials; ++partial)
                if (sound->amp[partial] != nullptr)
                {
                    const auto amplitude = sound->amp[partial][frame];
                    if (std::isfinite(amplitude) && amplitude > 0.0)
                        energy[static_cast<std::size_t>(frame)] += amplitude;
                }
        std::vector<double> flux(static_cast<std::size_t>(sound->frames), 0.0);
        double maximumFlux = 0.0;
        for (int frame = 1; frame < sound->frames; ++frame)
        {
            flux[static_cast<std::size_t>(frame)] = std::max(
                0.0, energy[static_cast<std::size_t>(frame)] - energy[static_cast<std::size_t>(frame - 1)]);
            maximumFlux = std::max(maximumFlux, flux[static_cast<std::size_t>(frame)]);
        }
        if (maximumFlux <= 0.0) return markers;
        const auto threshold = maximumFlux * 0.25;
        for (int frame = 1; frame < sound->frames; ++frame)
        {
            const auto value = flux[static_cast<std::size_t>(frame)];
            if (value < threshold || value < flux[static_cast<std::size_t>(frame - 1)] ||
                (frame + 1 < sound->frames && value < flux[static_cast<std::size_t>(frame + 1)]))
                continue;
            markers.push_back({ std::max(0.0, hopSeconds) * frame, value / maximumFlux, frame });
        }
        return markers;
    }

    [[nodiscard]] MorphRecipe buildMorphRecipe() const
    {
        auto recipe = makeDefaultMorphRecipe();
        if (!sources_[0] || !sources_[1] || !sources_[0]->sound || !sources_[1]->sound)
            return recipe;
        const auto maxFrequency = std::max(20.0, std::min(
            sources_[0]->sound.get()->srate, sources_[1]->sound.get()->srate) * 0.5);
        recipe.frequencyGrid = parameters_.read("morphGridType") >= 0.5f
            ? makeMelFrequencyGrid(25, 0.0, maxFrequency)
            : makeBarkFrequencyGrid(25, 0.0, maxFrequency);
        recipe.sourceA = makeMorphSourceMetadata(*sources_[0], 0, sourceRevisions_[0]);
        recipe.sourceB = makeMorphSourceMetadata(*sources_[1], 1, sourceRevisions_[1]);
        recipe.sourceATransients = detectMorphTransients(sources_[0]->sound.get(), recipe.sourceA.frameHopSeconds);
        recipe.sourceBTransients = detectMorphTransients(sources_[1]->sound.get(), recipe.sourceB.frameHopSeconds);
        const auto a = makeMorphAssignments(sources_[0]->sound.get(), recipe.frequencyGrid);
        const auto b = makeMorphAssignments(sources_[1]->sound.get(), recipe.frequencyGrid);
        const auto strategy = static_cast<int>(std::lround(parameters_.read("morphCorrespondenceStrategy")));
        if (strategy == 1)
            recipe.correspondenceRecords = buildGreedyNearestCorrespondence(a, b);
        else if (strategy == 2)
        {
            MorphAssignmentMatrixOptions options;
            options.advancedStrategyEnabled = true;
            options.amplitudeRankWeight = 0.25;
            recipe.correspondenceRecords = buildAssignmentMatrixCorrespondence(a, b, options).records;
        }
        else if (strategy == 3)
        {
            const auto quality = static_cast<int>(std::lround(parameters_.read("morphQuality")));
            const auto minimumMass = quality == 0 ? 1.0e-5 : quality == 2 ? 1.0e-2 : 1.0e-3;
            recipe.correspondenceRecords = buildOptimalTransportRecords(a, b, { minimumMass });
        }
        else
            recipe.correspondenceRecords = buildFrequencyRankCorrespondence(a, b, 0.25);
        recipe.defaultMorphAmount = parameters_.read("morphAmount");
        constexpr std::array<std::string_view, 6> amounts {
            "morphLaneFrequencyAmount", "morphLaneAmplitudeAmount", "morphLaneResidualAmount",
            "morphLaneTimeAmount", "morphLaneTransientAmount", "morphLanePhaseAmount" };
        constexpr std::array<std::string_view, 6> links {
            "morphLaneFrequencyLinked", "morphLaneAmplitudeLinked", "morphLaneResidualLinked",
            "morphLaneTimeLinked", "morphLaneTransientLinked", "morphLanePhaseLinked" };
        for (std::size_t lane = 0; lane < recipe.lanes.size(); ++lane)
        {
            recipe.lanes[lane].amount = parameters_.read(amounts[lane]);
            recipe.lanes[lane].linkedToMainMorph = parameters_.read(links[lane]) >= 0.5f;
        }
        const auto policy = [](int choice) {
            switch (choice) {
            case 1: return MorphUnmatchedPartialPolicy::FadeIn;
            case 2: return MorphUnmatchedPartialPolicy::Hold;
            case 3: return MorphUnmatchedPartialPolicy::MergeIntoResidual;
            case 4: return MorphUnmatchedPartialPolicy::Discard;
            default: return MorphUnmatchedPartialPolicy::FadeOut;
            }
        };
        recipe.unmatchedPartialPolicies.sourcePolicy = policy(
            static_cast<int>(std::lround(parameters_.read("morphUnmatchedSourcePolicy"))));
        recipe.unmatchedPartialPolicies.targetPolicy = policy(
            static_cast<int>(std::lround(parameters_.read("morphUnmatchedTargetPolicy"))));
        recipe.unmatchedPartialPolicies.discardOptIn = parameters_.read("morphDiscardOptIn") >= 0.5f;
        if (!recipe.unmatchedPartialPolicies.discardOptIn)
        {
            if (recipe.unmatchedPartialPolicies.sourcePolicy == MorphUnmatchedPartialPolicy::Discard)
                recipe.unmatchedPartialPolicies.sourcePolicy = MorphUnmatchedPartialPolicy::FadeOut;
            if (recipe.unmatchedPartialPolicies.targetPolicy == MorphUnmatchedPartialPolicy::Discard)
                recipe.unmatchedPartialPolicies.targetPolicy = MorphUnmatchedPartialPolicy::FadeIn;
        }
        recipe.phase.strategy = static_cast<MorphPhaseStrategy>(std::clamp(
            static_cast<int>(std::lround(parameters_.read("morphPhaseStrategy"))), 0, 4));
        recipe.transient.policy = static_cast<MorphTransientPolicy>(std::clamp(
            static_cast<int>(std::lround(parameters_.read("morphTransientPolicy"))), 0, 3));
        return recipe;
    }

    [[nodiscard]] std::vector<engine::TimelinePoint> morphTimelinePoints() const
    {
        if (!typedTimeline_.empty()) return typedTimeline_;
        std::vector<engine::TimelinePoint> points;
        points.reserve(timeline_.size());
        for (std::size_t index = 0; index < timeline_.size(); ++index)
        {
            const auto time = timeline_.size() > 1
                ? static_cast<double>(index) / static_cast<double>(timeline_.size() - 1) : 0.0;
            points.push_back({ time, static_cast<double>(timeline_[index]),
                               engine::TimelinePointKind::morph, MorphCurveShape::Linear });
        }
        return engine::MorphTimelineService::sanitize(std::move(points));
    }

    [[nodiscard]] TimbreMotionPlan makeTimbreMotionPlan() const
    {
        if (!timbrePlanJson_.empty()) return deserializeTimbreMotionPlan(timbrePlanJson_);
        TimbreMotionPlan plan;
        if (timbrePlan_.empty()) return plan;
        TimbreTargetCurve curve;
        curve.descriptorId = "combined.centroid_axis";
        curve.mode = TimbreTargetMode::AbsoluteTarget;
        curve.smoothingMs = parameters_.read("morphSmoothingMs");
        curve.points.reserve(timbrePlan_.size());
        for (std::size_t index = 0; index < timbrePlan_.size(); ++index)
        {
            const auto time = timbrePlan_.size() > 1
                ? static_cast<double>(index) / static_cast<double>(timbrePlan_.size() - 1) : 0.0;
            curve.points.push_back({ time, static_cast<double>(timbrePlan_[index]), 0 });
        }
        plan.targetCurves.push_back(std::move(curve));
        return plan;
    }

    bool renderMorphForPreview(Error& error)
    {
        error = {};
        if (state_ != HostState::prepared)
        {
            error = { "host_not_prepared", "prepare the audio host before starting morph preview" };
            return false;
        }
        if (!sources_[0] || !sources_[1] || !sources_[0]->sound || !sources_[1]->sound)
        {
            error = { "missing_morph_sources", "analyse Source A and Source B before playing the morph" };
            return false;
        }

        const auto recipe = buildMorphRecipe();
        if (recipe.correspondenceRecords.empty())
        {
            error = { "missing_correspondence", "no morph correspondences could be built" };
            return false;
        }
        const FrequencyShapeSettings frequencyShape {
            static_cast<FrequencyShapeMode>(static_cast<int>(std::lround(parameters_.read("freqShapeMode")))),
            parameters_.read("freqShapeAmount"), parameters_.read("freqShapePivotHz"), 0.0 };
        AtsSoundPtr shapedA;
        AtsSoundPtr shapedB;
        const ATS_SOUND* renderA = sources_[0]->sound.get();
        const ATS_SOUND* renderB = sources_[1]->sound.get();
        const auto prepareSource = [&](const ATS_SOUND* original, AtsSoundPtr& holder,
                                       const ATS_SOUND*& prepared) -> bool {
            const auto shape = isFrequencyShapeActive(
                resolveFrequencyShapeSettingsForSound(original, frequencyShape));
            const auto smear = liveSmearEnabled_.load(std::memory_order_acquire);
            if (!shape && !smear) return true;
            holder.reset(copy_sound(const_cast<ATS_SOUND*>(original)));
            if (!holder)
            {
                error = { "copy_failed", "could not copy a morph source for rendering" };
                return false;
            }
            if (smear)
            {
                auto onsets = detectOnsets(holder.get(), smearnovDetectParams());
                std::string scopeError;
                onsets = applySmearnovScope(
                    static_cast<int>(std::lround(parameters_.read("smearnovScope"))),
                    std::move(onsets), selectionFirstFrame_, selectionLastFrame_, scopeError);
                if (!scopeError.empty())
                {
                    error = { "smearnov_scope", std::move(scopeError) };
                    return false;
                }
                if (!onsets.empty()) (void) smearAttacks(holder.get(), onsets, smearnovParams());
            }
            if (shape) applyFrequencyShapeToSound(holder.get(), frequencyShape);
            prepared = holder.get();
            return true;
        };
        if (!prepareSource(renderA, shapedA, renderA) || !prepareSource(renderB, shapedB, renderB))
            return false;

        const auto timelinePoints = morphTimelinePoints();
        const auto timbreMotion = makeTimbreMotionPlan();
        dsp::MorphTrajectorySettings settings;
        settings.timeline = timelinePoints.empty() ? nullptr : &timelinePoints;
        settings.fixedMorphAmount = parameters_.read("morphAmount");
        settings.sampleRate = spec_.sampleRate > 0.0 ? spec_.sampleRate : 44100.0;
        settings.durationSeconds = std::max(0.25, std::max(
            recipe.sourceA.durationSeconds, recipe.sourceB.durationSeconds));
        settings.envelopeCache = &morphEnvelopeCache_;
        settings.sourcesRevision = sourceRevisions_[0] ^ (sourceRevisions_[1] << 32u);
        settings.timbrePlan = timbreMotion.empty() ? nullptr : &timbreMotion;
        settings.brightnessDb = parameters_.read("timbreBrightnessDb");
        settings.tiltDbPerOctave = parameters_.read("timbreTiltDbPerOct");
        settings.noiseBalance = parameters_.read("timbreNoiseBalance");
        settings.interpolationDomains = {
            parameters_.read("morphFrequencyInterpolationDomain") >= 0.5f,
            parameters_.read("morphAmplitudeInterpolationDomain") >= 0.5f };
        settings.smoothingMs = parameters_.read("morphSmoothingMs");
        settings.performanceMacro = parameters_.read("morphPerformanceMacro");
        const auto rendered = dsp::renderMorphTrajectory(renderA, renderB, recipe, settings);
        const auto frameCount = static_cast<std::uint32_t>(rendered.frameCount());
        if (frameCount == 0)
        {
            error = { "morph_render_failed", "morph renderer produced no samples" };
            return false;
        }
        ensurePlaybackCapacity(frameCount);
        const auto generation = sourceGenerations_.next();
        struct LayerOutput
        {
            PlaybackBufferPool& pool;
            int layer;
        };
        std::array<LayerOutput, 3> outputs {{
            { playback_, 0 }, { playbackPartials_, 1 }, { playbackResidual_, 2 }
        }};
        for (auto& output : outputs)
        {
            const auto slot = output.pool.tryAcquireWriteSlot();
            if (slot == PlaybackBufferPool::kNoBuffer)
            {
                error = { "playback_busy", "no buffer is available for morph preview" };
                return false;
            }
            auto view = output.pool.writeView(slot);
            view.clear();
            for (std::uint32_t frame = 0; frame < frameCount; ++frame)
            {
                const auto partial = rendered.partialSamples[frame];
                const auto residual = rendered.residualSamples[frame];
                const auto sample = output.layer == 1 ? partial
                                  : output.layer == 2 ? residual : partial + residual;
                for (std::uint32_t channel = 0; channel < view.channelCount; ++channel)
                    if (view.channels[channel] != nullptr)
                        view.channels[channel][frame] = sample;
            }
            if (!output.pool.publish(slot, generation))
            {
                error = { "playback_publish_failed", "could not publish morph preview" };
                return false;
            }
        }
        previewLengthFrames_.store(frameCount, std::memory_order_release);
        return true;
    }

    bool renderCurrentForPreview(Error& error)
    {
        if (!currentSource_ || !currentSource_->sound)
        {
            error = { "missing_current_model", "analyse the current source before starting preview" };
            return false;
        }
        // Reuse the established single-source renderer without exposing the
        // current role as Source A to callers. The temporary slot is restored
        // before returning, including on render failure.
        auto saved = std::move(sources_[0]);
        engine::ImportedSource temporary;
        temporary.path = currentSource_->path;
        temporary.name = currentSource_->name;
        temporary.kind = engine::SourceImportKind::ats;
        temporary.atsType = currentSource_->atsType;
        temporary.sound.reset(copy_sound(currentSource_->sound.get()));
        sources_[0] = std::move(temporary);
        auto rendered = renderSourceForPreview(0, error);
        sources_[0] = std::move(saved);
        return rendered;
    }

    // Model edits and preview renders both follow the preview selector, whose
    // slot 3 is the current analysed model rather than an import slot. Indexing
    // sources_ directly made a Current selection resolve to nothing, so the
    // edits silently reported "no model" instead of mutating the model on screen.
    [[nodiscard]] engine::ImportedSource* editableSource(SourceIdentity identity) noexcept
    {
        engine::ImportedSource* resolved = nullptr;
        switch (identity)
        {
        case SourceIdentity::current: resolved = currentSource_ ? &*currentSource_ : nullptr; break;
        case SourceIdentity::sourceA: resolved = sources_[0] ? &*sources_[0] : nullptr; break;
        case SourceIdentity::sourceB: resolved = sources_[1] ? &*sources_[1] : nullptr; break;
        case SourceIdentity::morphTimeline: return nullptr;
        }
        return resolved && resolved->sound ? resolved : nullptr;
    }

    [[nodiscard]] engine::ImportedSource* editableSource(int slot) noexcept
    {
        const auto identity = sourceIdentityForSlot(slot);
        return identity ? editableSource(*identity) : nullptr;
    }

    void noteSourceMutation(int slot) noexcept
    {
        if (slot == 3)
            ++currentSourceRevision_;
        else if (slot == 0 || slot == 1)
            ++sourceRevisions_[static_cast<std::size_t>(slot)];
    }

    void resetSpectrumDisplayReference(int slot) noexcept
    {
        const auto index = slot == 3 ? 0 : slot + 1;
        if (index >= 0 && index < static_cast<int>(spectrumAmpDisplayReferences_.size()))
        {
            spectrumAmpDisplayReferences_[static_cast<std::size_t>(index)] = 0.0;
            spectrumResidualDisplayReferences_[static_cast<std::size_t>(index)] = 0.0;
        }
    }

    void resetSpectrumDisplayReferenceForSourceId(int sourceId) noexcept
    {
        const auto index = std::clamp(sourceId, 0, 2);
        spectrumAmpDisplayReferences_[static_cast<std::size_t>(index)] = 0.0;
        spectrumResidualDisplayReferences_[static_cast<std::size_t>(index)] = 0.0;
    }

    bool renderSourceForPreview(int slot, Error& error)
    {
        error = {};
        if (state_ != HostState::prepared)
        {
            error = { "host_not_prepared", "prepare the audio host before starting preview" };
            return false;
        }
        const auto* selected = editableSource(slot);
        if (selected == nullptr)
        {
            error = { "missing_model", "analyse the selected source before starting preview" };
            return false;
        }
        const auto* sound = selected->sound.get();
        AtsSoundPtr liveSmearSound;
        if (liveSmearEnabled_.load(std::memory_order_acquire))
        {
            if (auto* copy = copy_sound(const_cast<ATS_SOUND*>(sound)))
            {
                liveSmearSound.reset(copy);
                auto onsets = detectOnsets(liveSmearSound.get(), smearnovDetectParams());
                std::string scopeError;
                onsets = applySmearnovScope(
                    static_cast<int>(std::lround(parameters_.read("smearnovScope"))),
                    std::move(onsets), selectionFirstFrame_, selectionLastFrame_, scopeError);
                if (!scopeError.empty())
                {
                    error = { "smearnov_scope", std::move(scopeError) };
                    return false;
                }
                if (!onsets.empty())
                    (void) smearAttacks(liveSmearSound.get(), onsets, smearnovParams());
                sound = liveSmearSound.get();
            }
        }
        const auto generation = sourceGenerations_.next();
        const auto token = sourceCancellation_.token();
        const auto durationSeconds = buildRenderInputs(0).duration > 0.0
            ? buildRenderInputs(0).duration
            : std::max(0.0, sound->dur - buildRenderInputs(0).startTime);
        const auto renderFrames = static_cast<std::uint32_t>(std::max<std::uint64_t>(
            1, static_cast<std::uint64_t>(std::llround(
                   durationSeconds * buildRenderInputs(0).timeFactor *
                   (spec_.sampleRate > 0.0 ? spec_.sampleRate : 44100.0)))));
        ensurePlaybackCapacity(renderFrames);

        // Render all three layers: combined, partials-only, residual-only.
        // Per-sample gain mixing in process() applies partGain/resGain via smoothers.
        struct LayerRender { PlaybackBufferPool& pool; int layer; const char* label; };
        std::array<LayerRender, 3> layers {{
            { playback_,         0, "combined" },
            { playbackPartials_, 1, "partials" },
            { playbackResidual_, 2, "residual" }
        }};
        for (auto& lr : layers)
        {
            const auto writeSlot = lr.pool.tryAcquireWriteSlot();
            if (writeSlot == PlaybackBufferPool::kNoBuffer)
            {
                error = { "playback_busy", std::string("no buffer for ") + lr.label + " layer" };
                return false;
            }
            const auto result = AtsEngine::render(generation, token, sound,
                                                   buildRenderInputs(lr.layer),
                                                   lr.pool.writeView(writeSlot));
            if (result.status != JobResult<RenderStats>::Status::success ||
                !lr.pool.publish(writeSlot, generation))
            {
                error = result.error ? result.error
                                     : Error { "playback_publish_failed",
                                               std::string("could not publish ") + lr.label };
                return false;
            }
        }

        const auto durationFrames = std::max<std::uint64_t>(
            1, static_cast<std::uint64_t>(std::llround(sound->dur * spec_.sampleRate)));
        previewLengthFrames_.store(static_cast<std::uint32_t>(std::min<std::uint64_t>(
                                             durationFrames, std::numeric_limits<std::uint32_t>::max())),
                                   std::memory_order_release);
        return true;
    }

    static std::vector<std::byte> buildBounceWav(const std::vector<float>& left,
                                                  const std::vector<float>& right,
                                                  std::uint32_t sampleRate)
    {
        const auto frames = static_cast<std::uint32_t>(std::min(left.size(), right.size()));
        const std::uint32_t dataBytes = frames * 4u; // stereo 16-bit
        ByteWriter w;
        w.reserve(44 + dataBytes);
        w.writeU8('R'); w.writeU8('I'); w.writeU8('F'); w.writeU8('F');
        w.writeU32(36 + dataBytes);
        w.writeU8('W'); w.writeU8('A'); w.writeU8('V'); w.writeU8('E');
        w.writeU8('f'); w.writeU8('m'); w.writeU8('t'); w.writeU8(' ');
        w.writeU32(16); w.writeU16(1); w.writeU16(2);
        w.writeU32(sampleRate); w.writeU32(sampleRate * 4u);
        w.writeU16(4); w.writeU16(16);
        w.writeU8('d'); w.writeU8('a'); w.writeU8('t'); w.writeU8('a');
        w.writeU32(dataBytes);
        for (std::uint32_t i = 0; i < frames; ++i)
        {
            w.writeU16(static_cast<std::uint16_t>(static_cast<std::int16_t>(
                std::lround(std::clamp(left[i],  -1.0f, 1.0f) * 32767.0f))));
            w.writeU16(static_cast<std::uint16_t>(static_cast<std::int16_t>(
                std::lround(std::clamp(right[i], -1.0f, 1.0f) * 32767.0f))));
        }
        return std::move(w).take();
    }

    void ensurePlaybackCapacity(std::uint32_t frames)
    {
        if (frames <= playbackCapacityFrames_)
            return;
        previewPlaying_.store(false, std::memory_order_release);
        playback_.prepare(spec_.outputChannels, frames);
        playbackPartials_.prepare(spec_.outputChannels, frames);
        playbackResidual_.prepare(spec_.outputChannels, frames);
        playbackCapacityFrames_ = frames;
    }

    // Build SynthParams for a specific render layer. Mixing gains (partGain,
    // resGain) are always 1.0 at render time — they are applied per-sample
    // via OnePoleSmoother in process() to satisfy the 20 ms ramp contract.
    // layer: 0=combined, 1=partials-only, 2=residual-only.
    [[nodiscard]] SynthParams buildRenderInputs(int layer) const noexcept
    {
        SynthParams params;
        params.frequencyFactor  = static_cast<double>(parameters_.read(freqIndex_));
        params.timeFactor       = static_cast<double>(parameters_.read(timeStretchIndex_));
        params.startTime        = static_cast<double>(parameters_.read(synStartIndex_));
        params.duration         = static_cast<double>(parameters_.read(synDurIndex_));
        params.useAllPartials   = parameters_.read(useAllPartialsIndex_) >= 0.5f;
        params.usePartialsPhase = parameters_.read(usePhaseIndex_) >= 0.5f;
        params.useIfftSynth     = parameters_.read(useIfftSynthIndex_) >= 0.5f;
        params.sampleRate       = spec_.sampleRate > 0.0 ? spec_.sampleRate : 44100.0;
        params.frequencyShape.mode    = static_cast<FrequencyShapeMode>(
            static_cast<int>(std::round(parameters_.read(freqShapeModeIndex_))));
        params.frequencyShape.amount  = static_cast<double>(parameters_.read(freqShapeAmountIndex_));
        params.frequencyShape.pivotHz = static_cast<double>(parameters_.read(freqShapePivotHzIndex_));

        const auto semitones = midi_.semitoneOffset();
        if (semitones != 0)
            params.frequencyFactor *= std::pow(2.0, static_cast<double>(semitones) / 12.0);

        params.partialsGain = (layer == 2) ? 0.0 : 1.0;
        params.residualGain = (layer == 1) ? 0.0 : 1.0;

        return params;
    }

    HostIdentity identity_ = HostIdentity::current();
    HostFormat format_ = HostFormat::standalone;
    HostPrepareSpec spec_{};
    HostState state_ = HostState::cold;
    ParameterStore parameters_;
    state::PresetService* presetService_ = nullptr;
    UndoHistory history_;
    std::optional<HostStateChunk> parameterGestureBefore_;
    int spectralViewSource_ = 0;
    int spectralViewMode_ = 0;
    std::array<std::optional<engine::ImportedSource>, 2> sources_;
    // Display references are intentionally separate from the mutable ATS
    // values. Scaling/gain edits therefore keep the same visual ruler until a
    // destructive mute/delete operation explicitly re-baselines it.
    mutable std::array<double, 3> spectrumAmpDisplayReferences_ {};
    mutable std::array<double, 3> spectrumResidualDisplayReferences_ {};
    // Current analysis is a distinct role from morph Source A/B. The legacy
    // integer slots remain at the file/action boundary for compatibility, but
    // this storage prevents Analyze Current from overwriting Source A.
    std::optional<engine::ImportedSource> currentSource_;
    std::uint64_t currentSourceRevision_ = 0;
    mutable std::atomic<std::uint64_t> editorSnapshotRevision_ { 0 };
    GenerationSource sourceGenerations_;
    CancellationSource sourceCancellation_;
    CancellationSource analysisCancellation_;
    JobScheduler analysisScheduler_ { 1 };
    std::future<JobResult<AnalysisOutput>> analysisFuture_;
    int analysisSlot_ = -1;
    std::uint64_t analysisSourceRevision_ = 0;
    std::atomic<bool> analysisRunning_ { false };
    std::atomic<float> analysisProgress_ { 0.0f };
    ui::TelemetryPublisher telemetry_;
    CancellationSource smearnovCancellation_;
    JobScheduler smearnovScheduler_ { 1 };
    std::future<SmearnovWorkResult> smearnovFuture_;
    std::atomic<bool> smearnovRunning_ { false };
    std::array<std::uint64_t, 2> sourceRevisions_ {};
    HostStateChunk stateChunk_;
    engine::ResidualClipboard residualClipboard_;
    double lastResidualReadDb_ = engine::ResidualEditService::kDbFloor;
    PlaybackBufferPool playback_;
    PlaybackBufferPool playbackPartials_;
    PlaybackBufferPool playbackResidual_;
    std::uint32_t playbackCapacityFrames_ = 0;
    MidiNoteRouter midi_;
    OnePoleSmoother outputTrim_;
    OnePoleSmoother partGainSmoother_;
    OnePoleSmoother resGainSmoother_;
    ui::IEditor* editor_ = nullptr;
    bool editorAttached_ = false;
    bool editorVisible_ = false;
    float editorScale_ = 1.0f;
    float editorWidth_ = 0.0f;
    float editorHeight_ = 0.0f;
    std::atomic<int> previewSource_ { 0 };
    std::atomic<int> previewLayer_ { 0 };
    std::atomic<bool> previewPlaying_ { false };
    std::atomic<float> normalizedPlayhead_ { 0.0f };
    std::atomic<bool> liveSmearEnabled_ { false };
    std::atomic<std::uint64_t> previewFrame_ { 0 };
    std::atomic<std::uint32_t> previewLengthFrames_ { 0 };
    std::atomic<bool> smearedSourceAvailable_ { false };
    std::vector<float> timeline_ { 0.0f, 1.0f };
    std::vector<engine::TimelinePoint> typedTimeline_;
    std::vector<float> timbrePlan_;
    std::string timbrePlanJson_;
    dsp::MorphEnvelopeCache morphEnvelopeCache_;
    std::vector<float> onsetOverlay_;
    std::vector<ui::SpectralOnset> onsetHandleOverlay_;
    std::vector<double> smearnovDetectionCurve_;
    std::vector<double> smearnovThresholdCurve_;
    int selectionFirstFrame_ = -1;
    int selectionLastFrame_ = -1;
    std::array<std::vector<float>, 3> variations_;
    std::string morphRecipeJson_;
    std::array<bool, 3> variationStored_ {};
    std::uint64_t timelineRevision_ = 0;
    std::uint64_t timbreRevision_ = 0;
    std::uint64_t onsetRevision_ = 0;

    // Dense indices resolved in prepare() for RT-safe access in process().
    std::size_t outputTrimIndex_       = 0;
    std::size_t partGainIndex_         = 0;
    std::size_t resGainIndex_          = 0;
    std::size_t freqIndex_             = 0;
    std::size_t freqShapeModeIndex_    = 0;
    std::size_t freqShapeAmountIndex_  = 0;
    std::size_t freqShapePivotHzIndex_ = 0;
    std::size_t timeStretchIndex_      = 0;
    std::size_t synStartIndex_         = 0;
    std::size_t synDurIndex_           = 0;
    std::size_t usePhaseIndex_         = 0;
    std::size_t useAllPartialsIndex_   = 0;
    std::size_t residualOnlyIndex_     = 0;
    std::size_t useIfftSynthIndex_     = 0;
    std::size_t classicPreviewModeIndex_ = 0;
};

} // namespace ats::host
