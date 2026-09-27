#include "../../core/AtsStateCodec.h"
#include "../../core/ParameterRegistry.h"
#include "../../state/FactoryPresetState.h"
#include "../../state/ParameterPresetCodec.h"
#include "../../platform/FileService.h"
#include "../../host/HostAdapter.h"
#include "../../ui/VisageEditor.h"

#include "base/source/fstreamer.h"
#include "public.sdk/source/vst/vstaudioeffect.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "public.sdk/source/main/pluginfactory_constexpr.h"

#include <visage_ui/events.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

namespace ats::vst3
{

static const Steinberg::FUID kProcessorUid (0x41545352, 0x4553594E, 0x54480001, 0x00000001);
static const Steinberg::FUID kControllerUid (0x41545352, 0x4553594E, 0x54480001, 0x00000002);
static constexpr Steinberg::TUID kProcessorTuid =
    INLINE_UID(0x41545352, 0x4553594E, 0x54480001, 0x00000001);
static constexpr Steinberg::TUID kControllerTuid =
    INLINE_UID(0x41545352, 0x4553594E, 0x54480001, 0x00000002);

namespace
{

constexpr const char* kImportSourceMessage = "ats.editor.import-source";
constexpr const char* kAnalyzeSourceMessage = "ats.editor.analyze-source";
constexpr const char* kCancelAnalysisMessage = "ats.editor.cancel-analysis";
constexpr const char* kPollAnalysisMessage = "ats.editor.poll-analysis";
constexpr const char* kApplyAnalysisProfileMessage = "ats.editor.apply-analysis-profile";
constexpr const char* kAnalysisTelemetryMessage = "ats.editor.analysis-telemetry";
constexpr const char* kSessionPreviewMessage = "ats.editor.session-preview";
constexpr const char* kSessionStopMessage = "ats.editor.session-stop-preview";
constexpr const char* kSessionSeekMessage = "ats.editor.session-seek-preview";
constexpr const char* kSessionSmearMessage = "ats.editor.session-preview-smear";
constexpr const char* kSessionVariationMessage = "ats.editor.session-variation";
constexpr const char* kSessionTimelineMessage = "ats.editor.session-timeline";
constexpr const char* kSessionDetectSmearnovMessage = "ats.editor.session-detect-smearnov";
constexpr const char* kSessionApplySmearnovMessage = "ats.editor.session-apply-smearnov";
constexpr const char* kSessionOnsetsMessage = "ats.editor.session-onsets";
constexpr const char* kSessionTimbreMessage = "ats.editor.session-timbre";
constexpr const char* kExportSourceMessage = "ats.editor.export-source";
constexpr const char* kFactoryPresetMessage = "ats.editor.factory-preset";
constexpr const char* kSavePresetMessage = "ats.editor.save-preset";
constexpr const char* kLoadPresetMessage = "ats.editor.load-preset";
constexpr const char* kHistoryMessage = "ats.editor.history";
constexpr const char* kSpectralViewMessage = "ats.editor.spectral-view";
constexpr const char* kNoteMessage = "ats.editor.note";
constexpr const char* kPartialEditMessage = "ats.editor.partial-edit";
constexpr const char* kResidualEditMessage = "ats.editor.residual-edit";
constexpr const char* kSpectrumEditMessage = "ats.editor.spectrum-edit";
constexpr const char* kMathMessage = "ats.editor.math";
constexpr const char* kBounceTimelineMessage = "ats.editor.bounce-timeline";
constexpr const char* kSourceSlotAttribute = "slot";
constexpr const char* kSourceKindAttribute = "kind";
constexpr const char* kSourcePathAttribute = "path";
constexpr const char* kPreviewLayerAttribute = "layer";
constexpr const char* kPreviewStartAttribute = "start";
constexpr const char* kVariationStoreAttribute = "store";
constexpr const char* kSessionValuesAttribute = "values";
constexpr const char* kTimelinePointsAttribute = "timelinePoints";
constexpr const char* kTimbrePlanAttribute = "timbrePlan";
constexpr const char* kPayloadVersionAttribute = "payloadVersion";
constexpr std::int64_t kPayloadVersion = 1;
constexpr std::size_t kMaxTimelinePoints = 32;
constexpr std::size_t kMaxTimbrePlanBytes = 1024 * 1024;
constexpr const char* kAnalysisProfileAttribute = "profile";
constexpr const char* kAnalysisRunningAttribute = "running";
constexpr const char* kAnalysisProgressAttribute = "progressMilli";
constexpr const char* kRedoAttribute = "redo";
constexpr const char* kNoteOnAttribute = "on";
constexpr const char* kNoteAttribute = "note";
constexpr const char* kVelocityAttribute = "velocity";
constexpr const char* kPartialAttribute = "partial";
constexpr const char* kFrameAttribute = "frame";
constexpr const char* kApplyAttribute = "apply";
constexpr const char* kAmplitudeAttribute = "amplitude";
constexpr const char* kFrequencyAttribute = "frequencyHz";
constexpr const char* kBandAttribute = "band";
constexpr const char* kCommandAttribute = "command";
constexpr const char* kFirstFrameAttribute = "firstFrame";
constexpr const char* kLastFrameAttribute = "lastFrame";
constexpr const char* kValueAttribute = "value";
// Packed SpectrumPointWire records for draw/read spectrum edits. Optional on
// decode so older controllers that never sent points still interoperate.
constexpr const char* kSpectrumPointsAttribute = "spectrumPoints";
// Packed SpectralOnset records (norm, preNorm, peakNorm, strength) sent beside
// the marker list; peers that only read the markers keep working.
constexpr const char* kOnsetHandlesAttribute = "onsetHandles";
constexpr const char* kPathAttribute = "path";
constexpr const char* kSourceAttribute = "source";

std::vector<std::byte> readStream(Steinberg::IBStream* stream)
{
    std::vector<std::byte> bytes;
    std::array<std::byte, 4096> buffer{};
    for (;;)
    {
        Steinberg::int32 bytesRead = 0;
        if (stream->read(buffer.data(), static_cast<Steinberg::int32>(buffer.size()), &bytesRead) !=
                Steinberg::kResultTrue ||
            bytesRead <= 0)
            break;
        bytes.insert(bytes.end(), buffer.begin(), buffer.begin() + bytesRead);
    }
    return bytes;
}

bool writeStream(Steinberg::IBStream* stream, const std::vector<std::byte>& bytes)
{
    if (bytes.empty())
        return false;
    Steinberg::int32 bytesWritten = 0;
    return stream->write(const_cast<std::byte*>(bytes.data()),
                         static_cast<Steinberg::int32>(bytes.size()), &bytesWritten) ==
               Steinberg::kResultTrue &&
           bytesWritten == static_cast<Steinberg::int32>(bytes.size());
}

bool readFloatVector(Steinberg::Vst::IAttributeList* attributes, const char* key,
                     std::vector<float>& values)
{
    const void* bytes = nullptr;
    Steinberg::uint32 size = 0;
    if (attributes == nullptr || attributes->getBinary(key, bytes, size) != Steinberg::kResultTrue ||
        bytes == nullptr || size == 0 || size % sizeof(float) != 0)
        return false;
    const auto count = static_cast<std::size_t>(size / sizeof(float));
    if (count > 4096)
        return false;
    values.resize(count);
    std::memcpy(values.data(), bytes, size);
    return true;
}

// Complex editor messages are versioned independently of the message ID so
// the payload can evolve without silently reinterpreting an older vector or
// edit structure. Missing versions remain accepted for compatibility with
// hosts that predate the versioned contract.
bool validPayloadVersion(Steinberg::Vst::IAttributeList* attributes)
{
    Steinberg::int64 version = kPayloadVersion;
    return attributes != nullptr &&
        (attributes->getInt(kPayloadVersionAttribute, version) != Steinberg::kResultTrue ||
         version == kPayloadVersion);
}

bool readString(Steinberg::Vst::IAttributeList* attributes, const char* key,
                std::string& value)
{
    const void* bytes = nullptr;
    Steinberg::uint32 size = 0;
    if (attributes == nullptr || attributes->getBinary(key, bytes, size) != Steinberg::kResultTrue ||
        bytes == nullptr || size == 0)
        return false;
    value.assign(static_cast<const char*>(bytes), size);
    return true;
}

// Wire layout for a typed timeline point, spelled out so the payload does not
// depend on the padding the compiler picks for MorphTimelineActionPoint.
struct TimelinePointWire
{
    float timeNorm = 0.0f;
    float amount = 0.0f;
    std::uint8_t kind = 0;
    std::uint8_t curve = 0;
    std::uint8_t reserved[2] = { 0, 0 };
};
static_assert(sizeof(TimelinePointWire) == 12, "timeline point wire layout changed");

bool readTimelinePoints(Steinberg::Vst::IAttributeList* attributes,
                        std::vector<ats::ui::MorphTimelineActionPoint>& points)
{
    const void* bytes = nullptr;
    Steinberg::uint32 size = 0;
    if (attributes == nullptr || attributes->getBinary(kTimelinePointsAttribute, bytes, size) !=
                                     Steinberg::kResultTrue ||
        bytes == nullptr || size == 0 || size % sizeof(TimelinePointWire) != 0)
        return false;
    const auto count = static_cast<std::size_t>(size / sizeof(TimelinePointWire));
    if (count > kMaxTimelinePoints)
        return false;
    std::vector<TimelinePointWire> wire(count);
    std::memcpy(wire.data(), bytes, size);
    points.clear();
    points.reserve(count);
    for (const auto& entry : wire)
        points.push_back({ entry.timeNorm, entry.amount, entry.kind, entry.curve });
    return true;
}

struct SpectrumPointWire
{
    std::int32_t frame = 0;
    float value = 0.0f;
};
static_assert(sizeof(SpectrumPointWire) == 8, "spectrum point wire layout changed");
// Same bound as validateAction(SpectrumEditAction).
constexpr std::size_t kMaximumSpectrumPoints = 4096;

// Absent attribute -> empty points (true); malformed or oversize -> false.
bool readSpectrumPoints(Steinberg::Vst::IAttributeList* attributes,
                        std::vector<std::pair<std::int32_t, float>>& points)
{
    points.clear();
    const void* bytes = nullptr;
    Steinberg::uint32 size = 0;
    if (attributes == nullptr ||
        attributes->getBinary(kSpectrumPointsAttribute, bytes, size) != Steinberg::kResultTrue)
        return true;
    if (bytes == nullptr || size % sizeof(SpectrumPointWire) != 0)
        return false;
    const auto count = static_cast<std::size_t>(size / sizeof(SpectrumPointWire));
    if (count > kMaximumSpectrumPoints)
        return false;
    std::vector<SpectrumPointWire> wire(count);
    if (count != 0)
        std::memcpy(wire.data(), bytes, size);
    points.reserve(count);
    for (const auto& point : wire)
        points.emplace_back(point.frame, point.value);
    return true;
}

static_assert(sizeof(ats::ui::SpectralOnset) == 4 * sizeof(float), "onset handle wire layout changed");

// Absent -> false (caller falls back to markers); malformed -> false as well.
bool readOnsetHandles(Steinberg::Vst::IAttributeList* attributes, std::size_t expectedCount,
                      std::vector<ats::ui::SpectralOnset>& handles)
{
    const void* bytes = nullptr;
    Steinberg::uint32 size = 0;
    if (attributes == nullptr ||
        attributes->getBinary(kOnsetHandlesAttribute, bytes, size) != Steinberg::kResultTrue ||
        bytes == nullptr || size % sizeof(ats::ui::SpectralOnset) != 0 ||
        size / sizeof(ats::ui::SpectralOnset) != expectedCount)
        return false;
    handles.resize(expectedCount);
    std::memcpy(handles.data(), bytes, size);
    return true;
}

bool readIntVector(Steinberg::Vst::IAttributeList* attributes, const char* key,
                   std::vector<int>& values)
{
    const void* bytes = nullptr;
    Steinberg::uint32 size = 0;
    if (attributes == nullptr || attributes->getBinary(key, bytes, size) != Steinberg::kResultTrue ||
        bytes == nullptr || size == 0 || size % sizeof(std::int32_t) != 0)
        return false;
    const auto count = static_cast<std::size_t>(size / sizeof(std::int32_t));
    if (count > 220)
        return false;
    values.resize(count);
    std::memcpy(values.data(), bytes, size);
    return true;
}

void copyAscii(Steinberg::Vst::String128 destination, std::string_view source)
{
    std::string value(source);
    Steinberg::UString(destination, 128).fromAscii(value.c_str());
}

} // namespace

class AtsVst3Processor final : public Steinberg::Vst::AudioEffect
{
public:
    AtsVst3Processor()
    {
        setControllerClass(kControllerUid);
        host_.initialize(ats::host::HostFormat::vst3);
    }

    static Steinberg::FUnknown* createInstance(void*)
    { return static_cast<Steinberg::Vst::IAudioProcessor*>(new AtsVst3Processor()); }

#ifdef ATS_VST3_DIFFERENTIAL_TEST
    // tests/host/vst3_differential_test.cpp compares editor-facing state too.
    [[nodiscard]] const ats::host::HostAdapter& hostForTest() const noexcept { return host_; }
#endif

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) SMTG_OVERRIDE
    {
        const auto result = AudioEffect::initialize(context);
        if (result != Steinberg::kResultTrue)
            return result;
        addAudioOutput(STR16("ATS Resynth Output"), Steinberg::Vst::SpeakerArr::kStereo);
        addEventInput(STR16("MIDI In"));
        return Steinberg::kResultTrue;
    }

    Steinberg::tresult PLUGIN_API setBusArrangements(
        Steinberg::Vst::SpeakerArrangement* inputs, Steinberg::int32 numIns,
        Steinberg::Vst::SpeakerArrangement* outputs, Steinberg::int32 numOuts) SMTG_OVERRIDE
    {
        if (numIns != 0 || numOuts != 1 || outputs == nullptr ||
            outputs[0] != Steinberg::Vst::SpeakerArr::kStereo)
            return Steinberg::kResultFalse;
        (void) inputs;
        return AudioEffect::setBusArrangements(nullptr, 0, outputs, 1);
    }

    Steinberg::tresult PLUGIN_API canProcessSampleSize(Steinberg::int32 symbolicSampleSize) SMTG_OVERRIDE
    {
        return symbolicSampleSize == Steinberg::Vst::kSample32 ? Steinberg::kResultTrue
                                                                : Steinberg::kResultFalse;
    }

    Steinberg::tresult PLUGIN_API setupProcessing(Steinberg::Vst::ProcessSetup& setup) SMTG_OVERRIDE
    {
        const auto result = AudioEffect::setupProcessing(setup);
        if (result != Steinberg::kResultTrue)
            return result;

        const ats::host::HostPrepareSpec spec { setup.sampleRate,
                                                static_cast<std::uint32_t>(setup.maxSamplesPerBlock),
                                                2,
                                                0 };
        return host_.prepare(spec) ? Steinberg::kResultTrue : Steinberg::kResultFalse;
    }

    Steinberg::tresult PLUGIN_API setActive(Steinberg::TBool state) SMTG_OVERRIDE
    {
        if (!state)
            host_.release();
        return AudioEffect::setActive(state);
    }

    Steinberg::tresult PLUGIN_API setProcessing(Steinberg::TBool) SMTG_OVERRIDE
    { return Steinberg::kResultTrue; }

    Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData& data) SMTG_OVERRIDE
    {
        if (data.symbolicSampleSize != Steinberg::Vst::kSample32)
            return Steinberg::kResultFalse;

        // VST3 hosts issue parameter-flush calls with no audio buffers. They
        // still carry automation and must succeed without entering the audio
        // adapter or dereferencing an absent output bus.
        applyParameterChanges(data.inputParameterChanges);
        if (data.numOutputs == 0 || data.outputs == nullptr)
            return Steinberg::kResultTrue;
        if (data.numOutputs != 1 || data.outputs[0].numChannels != 2)
            return Steinberg::kResultFalse;
        if (data.outputs[0].channelBuffers32 == nullptr || data.numSamples <= 0)
            return Steinberg::kResultTrue;

        std::array<ats::MidiEvent, 256> midiEvents{};
        std::uint32_t midiCount = 0;
        if (data.inputEvents != nullptr)
        {
            const auto eventCount = data.inputEvents->getEventCount();
            for (Steinberg::int32 index = 0; index < eventCount && midiCount < midiEvents.size(); ++index)
            {
                Steinberg::Vst::Event event {};
                if (data.inputEvents->getEvent(index, event) != Steinberg::kResultTrue)
                    continue;
                if (event.type == Steinberg::Vst::Event::kNoteOnEvent)
                {
                    midiEvents[midiCount++] = { static_cast<std::uint32_t>(event.sampleOffset),
                                                ats::MidiEvent::Type::noteOn,
                                                static_cast<std::uint8_t>(event.noteOn.pitch),
                                                static_cast<std::uint8_t>(event.noteOn.velocity * 127.0f) };
                }
                else if (event.type == Steinberg::Vst::Event::kNoteOffEvent)
                {
                    midiEvents[midiCount++] = { static_cast<std::uint32_t>(event.sampleOffset),
                                                ats::MidiEvent::Type::noteOff,
                                                static_cast<std::uint8_t>(event.noteOff.pitch),
                                                0 };
                }
            }
        }

        float* const channels[] = { data.outputs[0].channelBuffers32[0],
                                    data.outputs[0].channelBuffers32[1] };
        const ats::AudioBufferView output { channels, 2,
                                            static_cast<std::uint32_t>(data.numSamples) };
        host_.process(output, { midiEvents.data(), midiCount });
        return Steinberg::kResultTrue;
    }

    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) SMTG_OVERRIDE
    {
        if (state == nullptr)
            return Steinberg::kResultFalse;
        const auto bytes = readStream(state);
        return host_.restoreState(bytes) ? Steinberg::kResultTrue : Steinberg::kResultFalse;
    }

    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) SMTG_OVERRIDE
    {
        if (state == nullptr)
            return Steinberg::kResultFalse;
        return writeStream(state, host_.captureState()) ? Steinberg::kResultTrue
                                                        : Steinberg::kResultFalse;
    }

    // Editor-originated file and analysis work is deliberately received by
    // the processor.  A VST3 controller can be recreated independently, so
    // keeping decoded sources or playback state there would disconnect the
    // visible editor from the audio instance that process() renders.
    Steinberg::tresult PLUGIN_API notify(Steinberg::Vst::IMessage* message) SMTG_OVERRIDE
    {
        if (message == nullptr)
            return Steinberg::kInvalidArgument;
        const auto* messageId = message->getMessageID();
        auto* attributes = message->getAttributes();
        if (messageId == nullptr || attributes == nullptr)
            return Steinberg::kResultFalse;

        if (Steinberg::FIDStringsEqual(messageId, kImportSourceMessage))
        {
            Steinberg::int64 slot = -1;
            Steinberg::int64 kind = -1;
            const void* pathBytes = nullptr;
            Steinberg::uint32 pathSize = 0;
            if (attributes->getInt(kSourceSlotAttribute, slot) != Steinberg::kResultTrue ||
                attributes->getInt(kSourceKindAttribute, kind) != Steinberg::kResultTrue ||
                attributes->getBinary(kSourcePathAttribute, pathBytes, pathSize) != Steinberg::kResultTrue ||
                pathBytes == nullptr || pathSize == 0 || slot < 0 || slot > 1 || kind < 0 || kind > 1)
                return Steinberg::kResultFalse;
            const auto importKind = kind == 0 ? ats::engine::SourceImportKind::audio
                                              : ats::engine::SourceImportKind::ats;
            ats::Error error;
            const ats::Path path { std::string(static_cast<const char*>(pathBytes), pathSize) };
            return host_.importSource(path, static_cast<int>(slot), importKind, error)
                       ? Steinberg::kResultTrue
                       : Steinberg::kResultFalse;
        }
        if (Steinberg::FIDStringsEqual(messageId, kAnalyzeSourceMessage))
        {
            Steinberg::int64 slot = -1;
            if (attributes->getInt(kSourceSlotAttribute, slot) != Steinberg::kResultTrue ||
                slot < 0 || slot > 1)
                return Steinberg::kResultFalse;
            ats::Error error;
            return host_.queueAnalysisSource(static_cast<int>(slot), error)
                       ? Steinberg::kResultTrue
                       : Steinberg::kResultFalse;
        }
        if (Steinberg::FIDStringsEqual(messageId, kCancelAnalysisMessage))
        {
            host_.cancelAnalysis();
            return Steinberg::kResultTrue;
        }
        if (Steinberg::FIDStringsEqual(messageId, kPollAnalysisMessage))
        {
            // The editor's Visage timer runs this on its UI thread. This is
            // intentionally not done in process(): completion may render and
            // publish a new buffer, neither of which belongs on the audio
            // callback.
            ats::Error error;
            (void) host_.pollAnalysis(error);
            publishAnalysisTelemetry();
            return Steinberg::kResultTrue;
        }
        if (Steinberg::FIDStringsEqual(messageId, kApplyAnalysisProfileMessage))
        {
            Steinberg::int64 profile = -1;
            if (attributes->getInt(kAnalysisProfileAttribute, profile) != Steinberg::kResultTrue)
                return Steinberg::kResultFalse;
            ats::Error error;
            const auto accepted = host_.applyAnalysisAction(
                ats::ui::ApplyAnalysisProfileAction { static_cast<int>(profile) }, error);
            publishAnalysisTelemetry();
            return accepted && !error ? Steinberg::kResultTrue : Steinberg::kResultFalse;
        }
        if (Steinberg::FIDStringsEqual(messageId, kSessionPreviewMessage))
        {
            Steinberg::int64 source = -1, layer = -1, start = 0;
            if (attributes->getInt(kSourceSlotAttribute, source) != Steinberg::kResultTrue ||
                attributes->getInt(kPreviewLayerAttribute, layer) != Steinberg::kResultTrue ||
                attributes->getInt(kPreviewStartAttribute, start) != Steinberg::kResultTrue)
                return Steinberg::kResultFalse;
            ats::Error error;
            return host_.applySessionAction(ats::ui::PreviewAction {
                                                static_cast<int>(source), static_cast<int>(layer), start != 0 }, error) && !error
                       ? Steinberg::kResultTrue : Steinberg::kResultFalse;
        }
        if (Steinberg::FIDStringsEqual(messageId, kSessionStopMessage))
        {
            ats::Error error;
            return host_.applySessionAction(ats::ui::StopPreviewAction {}, error) && !error
                       ? Steinberg::kResultTrue : Steinberg::kResultFalse;
        }
        if (Steinberg::FIDStringsEqual(messageId, kSessionSmearMessage))
        {
            Steinberg::int64 enabled = 0;
            if (attributes->getInt(kPreviewStartAttribute, enabled) != Steinberg::kResultTrue)
                return Steinberg::kResultFalse;
            ats::Error error;
            return host_.applySessionAction(ats::ui::SetPreviewSmearAction { enabled != 0 }, error) && !error
                       ? Steinberg::kResultTrue : Steinberg::kResultFalse;
        }
        if (Steinberg::FIDStringsEqual(messageId, kSessionVariationMessage))
        {
            Steinberg::int64 slot = -1, store = 0;
            if (attributes->getInt(kSourceSlotAttribute, slot) != Steinberg::kResultTrue ||
                attributes->getInt(kVariationStoreAttribute, store) != Steinberg::kResultTrue)
                return Steinberg::kResultFalse;
            ats::Error error;
            return host_.applySessionAction(ats::ui::MorphVariationAction {
                                                static_cast<int>(slot), store != 0 }, error) && !error
                       ? Steinberg::kResultTrue : Steinberg::kResultFalse;
        }
        if (Steinberg::FIDStringsEqual(messageId, kSessionSeekMessage) ||
            Steinberg::FIDStringsEqual(messageId, kSessionOnsetsMessage))
        {
            std::vector<float> values;
            if (!validPayloadVersion(attributes) ||
                !readFloatVector(attributes, kSessionValuesAttribute, values))
                return Steinberg::kResultFalse;
            ats::ui::EditorAction action;
            if (Steinberg::FIDStringsEqual(messageId, kSessionSeekMessage) && values.size() == 1)
                action = ats::ui::SeekPreviewAction { values.front() };
            else if (Steinberg::FIDStringsEqual(messageId, kSessionOnsetsMessage))
            {
                std::vector<ats::ui::SpectralOnset> handles;
                if (readOnsetHandles(attributes, values.size(), handles))
                    action = ats::ui::EditSmearnovOnsetHandlesAction { std::move(handles) };
                else
                    action = ats::ui::EditSmearnovOnsetsAction { std::move(values) };
            }
            else
                return Steinberg::kResultFalse;
            ats::Error error;
            return host_.applySessionAction(action, error) && !error
                       ? Steinberg::kResultTrue : Steinberg::kResultFalse;
        }
        if (Steinberg::FIDStringsEqual(messageId, kSessionTimelineMessage))
        {
            if (!validPayloadVersion(attributes))
                return Steinberg::kResultFalse;
            // Either representation may be absent: the compact editor sends the
            // simple curve, the timeline editor sends typed points. Dropping the
            // typed points here used to make VST3 produce different state than
            // AU and standalone from the same editor action.
            ats::ui::SetMorphTimelineAction timeline;
            const bool haveCurve =
                readFloatVector(attributes, kSessionValuesAttribute, timeline.normalizedPoints);
            const bool havePoints = readTimelinePoints(attributes, timeline.points);
            if (!haveCurve && !havePoints)
                return Steinberg::kResultFalse;
            ats::Error error;
            return host_.applySessionAction(timeline, error) && !error
                       ? Steinberg::kResultTrue : Steinberg::kResultFalse;
        }
        if (Steinberg::FIDStringsEqual(messageId, kSessionTimbreMessage))
        {
            if (!validPayloadVersion(attributes))
                return Steinberg::kResultFalse;
            ats::ui::SetTimbreMotionPlanAction timbre;
            const bool haveCurve =
                readFloatVector(attributes, kSessionValuesAttribute, timbre.normalizedPlan);
            const bool havePlan = readString(attributes, kTimbrePlanAttribute, timbre.serializedPlan);
            if (!haveCurve && !havePlan)
                return Steinberg::kResultFalse;
            if (timbre.serializedPlan.size() > kMaxTimbrePlanBytes)
                return Steinberg::kResultFalse;
            ats::Error error;
            return host_.applySessionAction(timbre, error) && !error
                       ? Steinberg::kResultTrue : Steinberg::kResultFalse;
        }
        if (Steinberg::FIDStringsEqual(messageId, kSessionDetectSmearnovMessage) ||
            Steinberg::FIDStringsEqual(messageId, kSessionApplySmearnovMessage))
        {
            ats::Error error;
            const auto action = Steinberg::FIDStringsEqual(messageId, kSessionDetectSmearnovMessage)
                ? ats::ui::EditorAction { ats::ui::DetectSmearnovAction {} }
                : ats::ui::EditorAction { ats::ui::ApplySmearnovAction {} };
            return host_.applySessionAction(action, error) && !error
                       ? Steinberg::kResultTrue : Steinberg::kResultFalse;
        }
        if (Steinberg::FIDStringsEqual(messageId, kExportSourceMessage))
        {
            Steinberg::int64 source = -1;
            std::string path;
            if (attributes->getInt(kSourceAttribute, source) != Steinberg::kResultTrue ||
                !readString(attributes, kPathAttribute, path))
                return Steinberg::kResultFalse;
            ats::Error error;
            return host_.applyEditorAction(ats::ui::ExportAtsAction { ats::Path { path }, static_cast<int>(source) }, error) && !error
                       ? Steinberg::kResultTrue : Steinberg::kResultFalse;
        }
        if (Steinberg::FIDStringsEqual(messageId, kFactoryPresetMessage))
        {
            Steinberg::int64 index = -1;
            if (attributes->getInt(kSourceAttribute, index) != Steinberg::kResultTrue)
                return Steinberg::kResultFalse;
            ats::Error error;
            return host_.applyEditorAction(ats::ui::ApplyFactoryPresetAction { static_cast<std::int32_t>(index) }, error) && !error
                       ? Steinberg::kResultTrue : Steinberg::kResultFalse;
        }
        if (Steinberg::FIDStringsEqual(messageId, kSavePresetMessage) ||
            Steinberg::FIDStringsEqual(messageId, kLoadPresetMessage))
        {
            std::string path;
            if (!readString(attributes, kPathAttribute, path))
                return Steinberg::kResultFalse;
            ats::Error error;
            const ats::ui::EditorAction action = Steinberg::FIDStringsEqual(messageId, kSavePresetMessage)
                ? ats::ui::EditorAction { ats::ui::SavePresetAction { ats::Path { path } } }
                : ats::ui::EditorAction { ats::ui::LoadPresetAction { ats::Path { path } } };
            return host_.applyEditorAction(action, error) && !error
                       ? Steinberg::kResultTrue : Steinberg::kResultFalse;
        }
        if (Steinberg::FIDStringsEqual(messageId, kHistoryMessage))
        {
            Steinberg::int64 redo = 0;
            if (attributes->getInt(kRedoAttribute, redo) != Steinberg::kResultTrue)
                return Steinberg::kResultFalse;
            ats::Error error;
            return host_.applyEditorAction(ats::ui::HistoryAction { redo != 0 }, error) && !error
                       ? Steinberg::kResultTrue : Steinberg::kResultFalse;
        }
        if (Steinberg::FIDStringsEqual(messageId, kSpectralViewMessage))
        {
            Steinberg::int64 source = -1, mode = -1;
            if (attributes->getInt(kSourceAttribute, source) != Steinberg::kResultTrue ||
                attributes->getInt(kCommandAttribute, mode) != Steinberg::kResultTrue)
                return Steinberg::kResultFalse;
            ats::Error error;
            return host_.applyEditorAction(ats::ui::SetSpectralViewAction {
                                                static_cast<int>(source), static_cast<int>(mode) }, error) && !error
                       ? Steinberg::kResultTrue : Steinberg::kResultFalse;
        }
        if (Steinberg::FIDStringsEqual(messageId, kNoteMessage))
        {
            Steinberg::int64 on = 0, note = 60, velocity = 0;
            if (attributes->getInt(kNoteOnAttribute, on) != Steinberg::kResultTrue ||
                attributes->getInt(kNoteAttribute, note) != Steinberg::kResultTrue ||
                attributes->getInt(kVelocityAttribute, velocity) != Steinberg::kResultTrue)
                return Steinberg::kResultFalse;
            ats::Error error;
            return host_.applyEditorAction(ats::ui::NoteAction {
                                                on != 0, static_cast<std::uint8_t>(note),
                                                static_cast<std::uint8_t>(velocity) }, error) && !error
                       ? Steinberg::kResultTrue : Steinberg::kResultFalse;
        }
        if (Steinberg::FIDStringsEqual(messageId, kPartialEditMessage))
        {
            Steinberg::int64 partial = -1, frame = -1, apply = 0;
            double amplitude = 0.0, frequency = 1.0;
            if (!validPayloadVersion(attributes) ||
                attributes->getInt(kPartialAttribute, partial) != Steinberg::kResultTrue ||
                attributes->getInt(kFrameAttribute, frame) != Steinberg::kResultTrue ||
                attributes->getInt(kApplyAttribute, apply) != Steinberg::kResultTrue ||
                attributes->getFloat(kAmplitudeAttribute, amplitude) != Steinberg::kResultTrue ||
                attributes->getFloat(kFrequencyAttribute, frequency) != Steinberg::kResultTrue)
                return Steinberg::kResultFalse;
            ats::Error error;
            return host_.applyEditorAction(ats::ui::PartialEditAction {
                                                static_cast<std::int32_t>(partial),
                                                static_cast<std::int32_t>(frame), apply != 0,
                                                amplitude, frequency }, error) && !error
                       ? Steinberg::kResultTrue : Steinberg::kResultFalse;
        }
        if (Steinberg::FIDStringsEqual(messageId, kResidualEditMessage))
        {
            Steinberg::int64 band = -1, frame = -1;
            double value = 0.0;
            if (!validPayloadVersion(attributes) ||
                attributes->getInt(kBandAttribute, band) != Steinberg::kResultTrue ||
                attributes->getInt(kFrameAttribute, frame) != Steinberg::kResultTrue ||
                attributes->getFloat(kValueAttribute, value) != Steinberg::kResultTrue)
                return Steinberg::kResultFalse;
            ats::Error error;
            return host_.applyEditorAction(ats::ui::ResidualEditAction {
                                                static_cast<std::int32_t>(band),
                                                static_cast<std::int32_t>(frame),
                                                static_cast<float>(value) }, error) && !error
                       ? Steinberg::kResultTrue : Steinberg::kResultFalse;
        }
        if (Steinberg::FIDStringsEqual(messageId, kSpectrumEditMessage))
        {
            Steinberg::int64 command = 0, first = 0, last = 0;
            double value = 0.0;
            std::vector<int> bands;
            std::vector<std::pair<std::int32_t, float>> points;
            if (!validPayloadVersion(attributes) ||
                attributes->getInt(kCommandAttribute, command) != Steinberg::kResultTrue ||
                !readIntVector(attributes, kSessionValuesAttribute, bands) ||
                attributes->getInt(kFirstFrameAttribute, first) != Steinberg::kResultTrue ||
                attributes->getInt(kLastFrameAttribute, last) != Steinberg::kResultTrue ||
                attributes->getFloat(kValueAttribute, value) != Steinberg::kResultTrue ||
                !readSpectrumPoints(attributes, points))
                return Steinberg::kResultFalse;
            ats::Error error;
            return host_.applyEditorAction(ats::ui::SpectrumEditAction {
                                                static_cast<ats::ui::SpectrumEditCommand>(command),
                                                std::move(bands), static_cast<std::int32_t>(first),
                                                static_cast<std::int32_t>(last), static_cast<float>(value),
                                                std::move(points) }, error) && !error
                       ? Steinberg::kResultTrue : Steinberg::kResultFalse;
        }
        if (Steinberg::FIDStringsEqual(messageId, kMathMessage))
        {
            std::string script;
            if (!readString(attributes, kSessionValuesAttribute, script))
                return Steinberg::kResultFalse;
            ats::Error error;
            return host_.applyEditorAction(ats::ui::ApplyMathAction { std::move(script) }, error) && !error
                       ? Steinberg::kResultTrue : Steinberg::kResultFalse;
        }
        if (Steinberg::FIDStringsEqual(messageId, kBounceTimelineMessage))
        {
            Steinberg::int64 layer = -1;
            std::string path;
            if (attributes->getInt(kCommandAttribute, layer) != Steinberg::kResultTrue ||
                !readString(attributes, kPathAttribute, path))
                return Steinberg::kResultFalse;
            ats::Error error;
            return host_.applyEditorAction(ats::ui::BounceMorphTimelineAction {
                                                ats::Path { path }, static_cast<int>(layer) }, error) && !error
                       ? Steinberg::kResultTrue : Steinberg::kResultFalse;
        }
        return AudioEffect::notify(message);
    }

private:
    void publishAnalysisTelemetry()
    {
        auto* message = allocateMessage();
        if (message == nullptr)
            return;
        message->setMessageID(kAnalysisTelemetryMessage);
        auto* attributes = message->getAttributes();
        const auto telemetry = host_.telemetry();
        if (attributes != nullptr)
        {
            (void) attributes->setInt(kAnalysisRunningAttribute, telemetry.analysisRunning ? 1 : 0);
            (void) attributes->setInt(kAnalysisProgressAttribute,
                                      static_cast<Steinberg::int64>(std::lround(telemetry.progress * 1000.0f)));
            (void) sendMessage(message);
        }
        message->release();
    }

    void applyParameterChanges(Steinberg::Vst::IParameterChanges* changes) noexcept
    {
        if (changes == nullptr)
            return;
        for (Steinberg::int32 index = 0; index < changes->getParameterCount(); ++index)
        {
            auto* queue = changes->getParameterData(index);
            if (queue == nullptr || queue->getPointCount() == 0)
                continue;
            Steinberg::int32 sampleOffset = 0;
            Steinberg::Vst::ParamValue normalized = 0.0;
            if (queue->getPoint(queue->getPointCount() - 1, sampleOffset, normalized) ==
                Steinberg::kResultTrue)
                host_.setParameterNormalized(static_cast<std::uint32_t>(queue->getParameterId()),
                                              static_cast<float>(normalized));
        }
    }

    ats::host::HostAdapter host_;
};

class Vst3EditorActionTarget
{
public:
    virtual ~Vst3EditorActionTarget() = default;
    virtual void handleEditorAction(const ats::ui::EditorAction& action) = 0;
    virtual void pumpEditorWork() = 0;
    [[nodiscard]] virtual ats::ui::TelemetrySnapshot editorTelemetry() const = 0;
    [[nodiscard]] virtual ats::ui::EditorSnapshot editorSnapshot() const = 0;
};

class Vst3EditorWorkPump final : public visage::EventTimer
{
public:
    explicit Vst3EditorWorkPump(Vst3EditorActionTarget* target) : target_(target) {}

    void setEditor(ats::ui::VisageEditor* editor) noexcept { editor_ = editor; }

    void timerCallback() override
    {
        if (target_ != nullptr)
        {
            target_->pumpEditorWork();
            if (editor_ != nullptr)
                editor_->setEditorSnapshot(target_->editorSnapshot());
        }
    }

private:
    Vst3EditorActionTarget* target_ = nullptr;
    ats::ui::VisageEditor* editor_ = nullptr;
};

class AtsVst3PlugView final : public Steinberg::FObject, public Steinberg::IPlugView
{
public:
    explicit AtsVst3PlugView(Vst3EditorActionTarget& actionTarget)
        : actionTarget_(&actionTarget), workPump_(actionTarget_)
    {
        ats::ui::EditorEventSink sink;
        sink.context = actionTarget_;
        sink.action = &AtsVst3PlugView::editorAction;
        editor_ = std::make_unique<ats::ui::VisageEditor>(sink);
        workPump_.setEditor(editor_.get());
    }
    ~AtsVst3PlugView() override
    {
        workPump_.stopTimer();
        editor_.reset();
    }

    OBJ_METHODS(AtsVst3PlugView, Steinberg::FObject)
    DEFINE_INTERFACES
        DEF_INTERFACE(Steinberg::IPlugView)
    END_DEFINE_INTERFACES(Steinberg::FObject)
    REFCOUNT_METHODS(Steinberg::FObject)

    Steinberg::tresult PLUGIN_API isPlatformTypeSupported(Steinberg::FIDString type) override
    {
#if defined(__APPLE__)
        return Steinberg::FIDStringsEqual(type, Steinberg::kPlatformTypeNSView)
                   ? Steinberg::kResultTrue
                   : Steinberg::kResultFalse;
#elif defined(_WIN32)
        return Steinberg::FIDStringsEqual(type, Steinberg::kPlatformTypeHWND)
                   ? Steinberg::kResultTrue
                   : Steinberg::kResultFalse;
#else
        (void) type;
        return Steinberg::kResultFalse;
#endif
    }

    Steinberg::tresult PLUGIN_API attached(void* parent, Steinberg::FIDString type) override
    {
        if (isPlatformTypeSupported(type) != Steinberg::kResultTrue || parent == nullptr)
            return Steinberg::kResultFalse;
        if (!editor_)
            editor_ = std::make_unique<ats::ui::VisageEditor>();
        if (!editor_->setParent(parent))
            return Steinberg::kResultFalse;
        editor_->enableUserPreferences();
        editor_->show();
        workPump_.startTimer(20);
        return Steinberg::kResultTrue;
    }

    Steinberg::tresult PLUGIN_API removed() override
    {
        workPump_.stopTimer();
        editor_.reset();
        return Steinberg::kResultTrue;
    }

    Steinberg::tresult PLUGIN_API onWheel(float) override { return Steinberg::kResultFalse; }
    Steinberg::tresult PLUGIN_API onKeyDown(Steinberg::char16, Steinberg::int16,
                                            Steinberg::int16) override
    { return Steinberg::kResultFalse; }
    Steinberg::tresult PLUGIN_API onKeyUp(Steinberg::char16, Steinberg::int16,
                                          Steinberg::int16) override
    { return Steinberg::kResultFalse; }

    Steinberg::tresult PLUGIN_API getSize(Steinberg::ViewRect* size) override
    {
        if (size == nullptr)
            return Steinberg::kResultFalse;
        std::uint32_t width = 0, height = 0;
        editor_->getSize(width, height);
        size->left = 0;
        size->top = 0;
        size->right = static_cast<Steinberg::int32>(width);
        size->bottom = static_cast<Steinberg::int32>(height);
        return Steinberg::kResultTrue;
    }

    Steinberg::tresult PLUGIN_API onSize(Steinberg::ViewRect* newSize) override
    {
        if (newSize == nullptr || newSize->getWidth() <= 0 || newSize->getHeight() <= 0)
            return Steinberg::kResultFalse;
        return editor_->setSize(static_cast<std::uint32_t>(newSize->getWidth()),
                                static_cast<std::uint32_t>(newSize->getHeight()))
                   ? Steinberg::kResultTrue
                   : Steinberg::kResultFalse;
    }

    Steinberg::tresult PLUGIN_API onFocus(Steinberg::TBool) override
    { return Steinberg::kResultTrue; }

    Steinberg::tresult PLUGIN_API setFrame(Steinberg::IPlugFrame* frame) override
    {
        frame_ = frame;
        return Steinberg::kResultTrue;
    }

    Steinberg::tresult PLUGIN_API canResize() override
    { return editor_->canResize() ? Steinberg::kResultTrue : Steinberg::kResultFalse; }

    Steinberg::tresult PLUGIN_API checkSizeConstraint(Steinberg::ViewRect* rect) override
    {
        if (rect == nullptr)
            return Steinberg::kResultFalse;
        const auto constrained = editor_->constrainSize({
            static_cast<std::uint32_t>(std::max(0, rect->getWidth())),
            static_cast<std::uint32_t>(std::max(0, rect->getHeight()))
        });
        rect->right  = rect->left + static_cast<Steinberg::int32>(constrained.width);
        rect->bottom = rect->top  + static_cast<Steinberg::int32>(constrained.height);
        return Steinberg::kResultTrue;
    }

private:
    static void editorAction(void* context, const ats::ui::EditorAction* action)
    {
        auto* target = static_cast<Vst3EditorActionTarget*>(context);
        if (target != nullptr && action != nullptr)
            target->handleEditorAction(*action);
    }

    Vst3EditorActionTarget* actionTarget_ = nullptr;
    std::unique_ptr<ats::ui::VisageEditor> editor_;
    Vst3EditorWorkPump workPump_;
    Steinberg::IPlugFrame* frame_ = nullptr;
};

class AtsVst3Controller final : public Steinberg::Vst::EditController, public Vst3EditorActionTarget
{
public:
    static Steinberg::FUnknown* createInstance(void*)
    { return static_cast<Steinberg::Vst::IEditController*>(new AtsVst3Controller()); }

    Steinberg::IPlugView* PLUGIN_API createView(Steinberg::FIDString name) SMTG_OVERRIDE;

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) SMTG_OVERRIDE
    {
        const auto result = EditController::initialize(context);
        if (result != Steinberg::kResultTrue)
            return result;

        for (const auto& descriptor : ats::ParameterRegistry::descriptors())
        {
            Steinberg::Vst::String128 title {};
            copyAscii(title, descriptor.name);
            const auto steps = descriptor.type == ats::ParameterType::floatValue
                                   ? 0
                                   : static_cast<Steinberg::int32>(descriptor.maximum - descriptor.minimum);
            const auto flags = descriptor.readOnly
                                   ? Steinberg::Vst::ParameterInfo::kIsReadOnly
                                   : (descriptor.automatable ? Steinberg::Vst::ParameterInfo::kCanAutomate : 0);
            parameters.addParameter(title, nullptr, steps,
                                     descriptor.normalize(descriptor.defaultValue), flags,
                                     static_cast<Steinberg::int32>(descriptor.hostNumericId));
        }
        return Steinberg::kResultTrue;
    }

    // The processor owns the authoritative state, but the controller keeps its
    // own parameter table that the host reads for automation and generic UI.
    // Consuming the same ATSP chunk here is what makes a restored project show
    // its saved values instead of the defaults from initialize().
    Steinberg::tresult PLUGIN_API setComponentState(Steinberg::IBStream* state) SMTG_OVERRIDE
    {
        if (state == nullptr)
            return Steinberg::kResultFalse;
        const auto bytes = readStream(state);
        if (bytes == ats::AtsStateCodec::encodeHeader())
            return Steinberg::kResultTrue; // empty project round-trip
        const auto legacy = ats::LegacyStateCodec::decode(bytes.data(), bytes.size());
        if (!legacy)
            return Steinberg::kResultFalse;

        ats::ParameterStore restored;
        ats::applyLegacyStateToParameterStore(*legacy, restored);
        for (const auto& descriptor : ats::ParameterRegistry::descriptors())
        {
            const auto tag = static_cast<Steinberg::Vst::ParamID>(descriptor.hostNumericId);
            setParamNormalized(tag, descriptor.normalize(restored.read(descriptor.denseIndex)));
        }
        return Steinberg::kResultTrue;
    }

    Steinberg::tresult PLUGIN_API notify(Steinberg::Vst::IMessage* message) SMTG_OVERRIDE
    {
        if (message == nullptr || !Steinberg::FIDStringsEqual(message->getMessageID(), kAnalysisTelemetryMessage))
            return EditController::notify(message);
        auto* attributes = message->getAttributes();
        Steinberg::int64 running = 0, progressMilli = 0;
        if (attributes == nullptr ||
            attributes->getInt(kAnalysisRunningAttribute, running) != Steinberg::kResultTrue ||
            attributes->getInt(kAnalysisProgressAttribute, progressMilli) != Steinberg::kResultTrue)
            return Steinberg::kResultFalse;
        editorTelemetry_.analysisRunning = running != 0;
        editorTelemetry_.progress = std::clamp(static_cast<float>(progressMilli) / 1000.0f, 0.0f, 1.0f);
        editorTelemetry_.state = editorTelemetry_.analysisRunning ? ats::ui::UiState::analyzing
                                                                    : ats::ui::UiState::analyzed;
        editorTelemetry_.status = editorTelemetry_.analysisRunning ? "analysing source" : "analysis complete";
        return Steinberg::kResultTrue;
    }

    void handleEditorAction(const ats::ui::EditorAction& action) override
    {
        if (const auto* profile = std::get_if<ats::ui::ApplyAnalysisProfileAction>(&action))
        {
            (void) sendAnalysisProfile(profile->profile);
            return;
        }
        if (const auto* preview = std::get_if<ats::ui::PreviewAction>(&action))
        {
            (void) sendPreview(*preview);
            return;
        }
        if (std::holds_alternative<ats::ui::StopPreviewAction>(action))
        {
            (void) sendMessageId(kSessionStopMessage);
            return;
        }
        if (const auto* seek = std::get_if<ats::ui::SeekPreviewAction>(&action))
        {
            (void) sendFloatValues(kSessionSeekMessage, { seek->normalizedTime });
            return;
        }
        if (const auto* smear = std::get_if<ats::ui::SetPreviewSmearAction>(&action))
        {
            (void) sendToggle(kSessionSmearMessage, kPreviewStartAttribute, smear->enabled);
            return;
        }
        if (const auto* variation = std::get_if<ats::ui::MorphVariationAction>(&action))
        {
            (void) sendVariation(*variation);
            return;
        }
        if (const auto* timeline = std::get_if<ats::ui::SetMorphTimelineAction>(&action))
        {
            (void) sendMorphTimeline(*timeline);
            return;
        }
        if (std::holds_alternative<ats::ui::DetectSmearnovAction>(action))
        {
            (void) sendMessageId(kSessionDetectSmearnovMessage);
            return;
        }
        if (std::holds_alternative<ats::ui::ApplySmearnovAction>(action))
        {
            (void) sendMessageId(kSessionApplySmearnovMessage);
            return;
        }
        if (const auto* onsets = std::get_if<ats::ui::EditSmearnovOnsetsAction>(&action))
        {
            (void) sendFloatValues(kSessionOnsetsMessage, onsets->normalizedOnsets);
            return;
        }
        if (const auto* handles = std::get_if<ats::ui::EditSmearnovOnsetHandlesAction>(&action))
        {
            (void) sendOnsetHandles(handles->onsets);
            return;
        }
        if (const auto* timbre = std::get_if<ats::ui::SetTimbreMotionPlanAction>(&action))
        {
            (void) sendTimbreMotionPlan(*timbre);
            return;
        }
        if (const auto* exportAction = std::get_if<ats::ui::ExportAtsAction>(&action))
        {
            (void) sendPathAction(kExportSourceMessage, exportAction->path,
                                  kSourceAttribute, exportAction->source);
            return;
        }
        if (const auto* factory = std::get_if<ats::ui::ApplyFactoryPresetAction>(&action))
        {
            // Seed from the controller's current values so the recipe only
            // changes the parameters it names (legacy no-reset semantics).
            ats::ParameterStore preset;
            for (const auto& descriptor : ats::ParameterRegistry::descriptors())
                (void) preset.write(descriptor.denseIndex,
                                    descriptor.denormalize(static_cast<float>(getParamNormalized(
                                        static_cast<Steinberg::Vst::ParamID>(descriptor.hostNumericId)))));
            if (!ats::state::applyFactoryPreset(factory->index, preset))
                return;
            (void) sendIntAction(kFactoryPresetMessage, kSourceAttribute, factory->index);
            applyPreset(preset);
            return;
        }
        if (const auto* save = std::get_if<ats::ui::SavePresetAction>(&action))
        {
            (void) sendPathAction(kSavePresetMessage, save->path);
            return;
        }
        if (const auto* load = std::get_if<ats::ui::LoadPresetAction>(&action))
        {
            (void) sendPathAction(kLoadPresetMessage, load->path);
            ats::platform::FileService files;
            const auto payload = files.read(load->path);
            ats::ParameterStore preset;
            std::string error;
            if (payload.succeeded() &&
                ats::state::ParameterPresetCodec::decode(payload.bytes, preset, error))
                applyPreset(preset);
            return;
        }
        if (const auto* history = std::get_if<ats::ui::HistoryAction>(&action))
        {
            (void) sendIntAction(kHistoryMessage, kRedoAttribute, history->redo ? 1 : 0);
            return;
        }
        if (const auto* view = std::get_if<ats::ui::SetSpectralViewAction>(&action))
        {
            (void) sendTwoInts(kSpectralViewMessage, kSourceAttribute, view->source,
                               kCommandAttribute, view->mode);
            return;
        }
        if (const auto* note = std::get_if<ats::ui::NoteAction>(&action))
        {
            (void) sendThreeInts(kNoteMessage, kNoteOnAttribute, note->on ? 1 : 0,
                                 kNoteAttribute, note->note, kVelocityAttribute, note->velocity);
            return;
        }
        if (const auto* partial = std::get_if<ats::ui::PartialEditAction>(&action))
        {
            (void) sendPartialEdit(*partial);
            return;
        }
        if (const auto* residual = std::get_if<ats::ui::ResidualEditAction>(&action))
        {
            (void) sendResidualEdit(*residual);
            return;
        }
        if (const auto* spectrum = std::get_if<ats::ui::SpectrumEditAction>(&action))
        {
            (void) sendSpectrumEdit(*spectrum);
            return;
        }
        if (const auto* math = std::get_if<ats::ui::ApplyMathAction>(&action))
        {
            (void) sendStringAction(kMathMessage, kSessionValuesAttribute, math->script);
            return;
        }
        if (const auto* bounce = std::get_if<ats::ui::BounceMorphTimelineAction>(&action))
        {
            (void) sendPathAction(kBounceTimelineMessage, bounce->path,
                                  kCommandAttribute, bounce->layer);
            return;
        }
        if (const auto* wav = std::get_if<ats::ui::ImportWavAction>(&action))
        {
            (void) sendImport(wav->path, wav->slot, ats::engine::SourceImportKind::audio);
            return;
        }
        if (const auto* ats = std::get_if<ats::ui::ImportAtsAction>(&action))
        {
            (void) sendImport(ats->path, ats->slot, ats::engine::SourceImportKind::ats);
            return;
        }
        if (const auto* analyse = std::get_if<ats::ui::AnalyzeMorphSourceAction>(&action))
        {
            (void) sendSlotCommand(kAnalyzeSourceMessage, analyse->slot);
            return;
        }
        if (std::holds_alternative<ats::ui::AnalyzeCurrentAction>(action))
        {
            (void) sendSlotCommand(kAnalyzeSourceMessage, 0);
            return;
        }
        if (std::holds_alternative<ats::ui::CancelJobsAction>(action))
        {
            (void) sendMessageId(kCancelAnalysisMessage);
            return;
        }
        const auto* parameter = std::get_if<ats::ui::SetParameterAction>(&action);
        if (parameter == nullptr)
            return;
        const auto* descriptor = ats::ParameterRegistry::find(parameter->id);
        if (descriptor == nullptr || !descriptor->automatable || descriptor->readOnly)
            return;

        const auto tag = static_cast<Steinberg::Vst::ParamID>(descriptor->hostNumericId);
        const auto normalized = descriptor->normalize(parameter->value);
        if (parameter->begin)
            beginEdit(tag);
        setParamNormalized(tag, normalized);
        performEdit(tag, normalized);
        if (parameter->end)
            endEdit(tag);
    }

    void pumpEditorWork() override
    { (void) sendMessageId(kPollAnalysisMessage); }

    [[nodiscard]] ats::ui::TelemetrySnapshot editorTelemetry() const override
    { return editorTelemetry_; }

    [[nodiscard]] ats::ui::EditorSnapshot editorSnapshot() const override
    {
        ats::ui::EditorSnapshot snapshot;
        snapshot.telemetry = editorTelemetry_;
        for (const auto& descriptor : ats::ParameterRegistry::descriptors())
            if (descriptor.denseIndex < snapshot.parameters.size())
                snapshot.parameters[descriptor.denseIndex] = static_cast<float>(
                    descriptor.denormalize(const_cast<AtsVst3Controller*>(this)->
                                          getParamNormalized(descriptor.hostNumericId)));
        return snapshot;
    }

private:
    bool sendIntAction(const char* messageId, const char* attribute, std::int64_t value)
    {
        auto* message = allocateMessage();
        if (message == nullptr)
            return false;
        message->setMessageID(messageId);
        auto* attributes = message->getAttributes();
        const auto result = attributes != nullptr &&
            attributes->setInt(attribute, value) == Steinberg::kResultTrue &&
            sendMessage(message) == Steinberg::kResultTrue;
        message->release();
        return result;
    }

    bool sendTwoInts(const char* messageId, const char* firstKey, std::int64_t first,
                     const char* secondKey, std::int64_t second)
    {
        auto* message = allocateMessage();
        if (message == nullptr)
            return false;
        message->setMessageID(messageId);
        auto* attributes = message->getAttributes();
        const auto result = attributes != nullptr &&
            attributes->setInt(firstKey, first) == Steinberg::kResultTrue &&
            attributes->setInt(secondKey, second) == Steinberg::kResultTrue &&
            sendMessage(message) == Steinberg::kResultTrue;
        message->release();
        return result;
    }

    bool sendThreeInts(const char* messageId, const char* firstKey, std::int64_t first,
                       const char* secondKey, std::int64_t second,
                       const char* thirdKey, std::int64_t third)
    {
        auto* message = allocateMessage();
        if (message == nullptr)
            return false;
        message->setMessageID(messageId);
        auto* attributes = message->getAttributes();
        const auto result = attributes != nullptr &&
            attributes->setInt(firstKey, first) == Steinberg::kResultTrue &&
            attributes->setInt(secondKey, second) == Steinberg::kResultTrue &&
            attributes->setInt(thirdKey, third) == Steinberg::kResultTrue &&
            sendMessage(message) == Steinberg::kResultTrue;
        message->release();
        return result;
    }

    bool sendStringAction(const char* messageId, const char* attribute, std::string_view value)
    {
        if (value.empty() || value.size() > UINT32_MAX)
            return false;
        auto* message = allocateMessage();
        if (message == nullptr)
            return false;
        message->setMessageID(messageId);
        auto* attributes = message->getAttributes();
        const auto result = attributes != nullptr &&
            attributes->setBinary(attribute, value.data(), static_cast<Steinberg::uint32>(value.size())) == Steinberg::kResultTrue &&
            sendMessage(message) == Steinberg::kResultTrue;
        message->release();
        return result;
    }

    bool sendPathAction(const char* messageId, const ats::Path& path,
                        const char* numericKey = nullptr, std::int64_t numericValue = 0)
    {
        if (path.empty() || path.string().size() > UINT32_MAX)
            return false;
        auto* message = allocateMessage();
        if (message == nullptr)
            return false;
        message->setMessageID(messageId);
        auto* attributes = message->getAttributes();
        const auto result = attributes != nullptr &&
            attributes->setBinary(kPathAttribute, path.string().data(),
                                  static_cast<Steinberg::uint32>(path.string().size())) == Steinberg::kResultTrue &&
            (numericKey == nullptr || attributes->setInt(numericKey, numericValue) == Steinberg::kResultTrue) &&
            sendMessage(message) == Steinberg::kResultTrue;
        message->release();
        return result;
    }

    bool sendPartialEdit(const ats::ui::PartialEditAction& edit)
    {
        auto* message = allocateMessage();
        if (message == nullptr)
            return false;
        message->setMessageID(kPartialEditMessage);
        auto* attributes = message->getAttributes();
        const auto result = attributes != nullptr &&
            attributes->setInt(kPayloadVersionAttribute, kPayloadVersion) == Steinberg::kResultTrue &&
            attributes->setInt(kPartialAttribute, edit.partial) == Steinberg::kResultTrue &&
            attributes->setInt(kFrameAttribute, edit.frame) == Steinberg::kResultTrue &&
            attributes->setInt(kApplyAttribute, edit.apply ? 1 : 0) == Steinberg::kResultTrue &&
            attributes->setFloat(kAmplitudeAttribute, edit.amplitude) == Steinberg::kResultTrue &&
            attributes->setFloat(kFrequencyAttribute, edit.frequencyHz) == Steinberg::kResultTrue &&
            sendMessage(message) == Steinberg::kResultTrue;
        message->release();
        return result;
    }

    bool sendResidualEdit(const ats::ui::ResidualEditAction& edit)
    {
        auto* message = allocateMessage();
        if (message == nullptr)
            return false;
        message->setMessageID(kResidualEditMessage);
        auto* attributes = message->getAttributes();
        const auto result = attributes != nullptr &&
            attributes->setInt(kPayloadVersionAttribute, kPayloadVersion) == Steinberg::kResultTrue &&
            attributes->setInt(kBandAttribute, edit.band) == Steinberg::kResultTrue &&
            attributes->setInt(kFrameAttribute, edit.frame) == Steinberg::kResultTrue &&
            attributes->setFloat(kValueAttribute, edit.value) == Steinberg::kResultTrue &&
            sendMessage(message) == Steinberg::kResultTrue;
        message->release();
        return result;
    }

    bool sendSpectrumEdit(const ats::ui::SpectrumEditAction& edit)
    {
        if (edit.bands.empty() || edit.bands.size() > 220 ||
            edit.points.size() > kMaximumSpectrumPoints)
            return false;
        std::vector<SpectrumPointWire> wirePoints;
        wirePoints.reserve(edit.points.size());
        for (const auto& [frame, value] : edit.points)
            wirePoints.push_back({ frame, value });
        auto* message = allocateMessage();
        if (message == nullptr)
            return false;
        message->setMessageID(kSpectrumEditMessage);
        auto* attributes = message->getAttributes();
        const auto bytes = static_cast<Steinberg::uint32>(edit.bands.size() * sizeof(std::int32_t));
        const auto result = attributes != nullptr &&
            attributes->setInt(kPayloadVersionAttribute, kPayloadVersion) == Steinberg::kResultTrue &&
            attributes->setInt(kCommandAttribute, static_cast<std::int64_t>(edit.command)) == Steinberg::kResultTrue &&
            attributes->setBinary(kSessionValuesAttribute, edit.bands.data(), bytes) == Steinberg::kResultTrue &&
            attributes->setInt(kFirstFrameAttribute, edit.firstFrame) == Steinberg::kResultTrue &&
            attributes->setInt(kLastFrameAttribute, edit.lastFrame) == Steinberg::kResultTrue &&
            attributes->setFloat(kValueAttribute, edit.value) == Steinberg::kResultTrue &&
            (wirePoints.empty() ||
             attributes->setBinary(kSpectrumPointsAttribute, wirePoints.data(),
                                   static_cast<Steinberg::uint32>(wirePoints.size() *
                                                                  sizeof(SpectrumPointWire))) ==
                 Steinberg::kResultTrue) &&
            sendMessage(message) == Steinberg::kResultTrue;
        message->release();
        return result;
    }

    bool sendAnalysisProfile(int profile)
    {
        if (profile < 0 || profile > 3)
            return false;
        auto* message = allocateMessage();
        if (message == nullptr)
            return false;
        message->setMessageID(kApplyAnalysisProfileMessage);
        auto* attributes = message->getAttributes();
        const auto result = attributes != nullptr &&
            attributes->setInt(kAnalysisProfileAttribute, profile) == Steinberg::kResultTrue &&
            sendMessage(message) == Steinberg::kResultTrue;
        message->release();
        return result;
    }

    bool sendPreview(const ats::ui::PreviewAction& preview)
    {
        auto* message = allocateMessage();
        if (message == nullptr)
            return false;
        message->setMessageID(kSessionPreviewMessage);
        auto* attributes = message->getAttributes();
        const auto result = attributes != nullptr &&
            attributes->setInt(kSourceSlotAttribute, preview.source) == Steinberg::kResultTrue &&
            attributes->setInt(kPreviewLayerAttribute, preview.layer) == Steinberg::kResultTrue &&
            attributes->setInt(kPreviewStartAttribute, preview.start ? 1 : 0) == Steinberg::kResultTrue &&
            sendMessage(message) == Steinberg::kResultTrue;
        message->release();
        return result;
    }

    bool sendToggle(const char* messageId, const char* attribute, bool enabled)
    {
        auto* message = allocateMessage();
        if (message == nullptr)
            return false;
        message->setMessageID(messageId);
        auto* attributes = message->getAttributes();
        const auto result = attributes != nullptr &&
            attributes->setInt(attribute, enabled ? 1 : 0) == Steinberg::kResultTrue &&
            sendMessage(message) == Steinberg::kResultTrue;
        message->release();
        return result;
    }

    bool sendVariation(const ats::ui::MorphVariationAction& variation)
    {
        auto* message = allocateMessage();
        if (message == nullptr)
            return false;
        message->setMessageID(kSessionVariationMessage);
        auto* attributes = message->getAttributes();
        const auto result = attributes != nullptr &&
            attributes->setInt(kSourceSlotAttribute, variation.slot) == Steinberg::kResultTrue &&
            attributes->setInt(kVariationStoreAttribute, variation.store ? 1 : 0) == Steinberg::kResultTrue &&
            sendMessage(message) == Steinberg::kResultTrue;
        message->release();
        return result;
    }

    bool sendFloatValues(const char* messageId, const std::vector<float>& values)
    {
        if (values.empty() || values.size() > 4096 || values.size() > UINT32_MAX / sizeof(float))
            return false;
        auto* message = allocateMessage();
        if (message == nullptr)
            return false;
        message->setMessageID(messageId);
        auto* attributes = message->getAttributes();
        const auto byteCount = static_cast<Steinberg::uint32>(values.size() * sizeof(float));
        const auto result = attributes != nullptr &&
            attributes->setInt(kPayloadVersionAttribute, kPayloadVersion) == Steinberg::kResultTrue &&
            attributes->setBinary(kSessionValuesAttribute, values.data(), byteCount) == Steinberg::kResultTrue &&
            sendMessage(message) == Steinberg::kResultTrue;
        message->release();
        return result;
    }

    // Markers plus the full handles: the processor applies the handles, older
    // processors that only read the markers still get the compatible projection.
    bool sendOnsetHandles(const std::vector<ats::ui::SpectralOnset>& handles)
    {
        if (handles.empty() || handles.size() > 4096)
            return false;
        std::vector<float> markers;
        markers.reserve(handles.size());
        for (const auto& onset : handles)
            markers.push_back(onset.norm);
        auto* message = allocateMessage();
        if (message == nullptr)
            return false;
        message->setMessageID(kSessionOnsetsMessage);
        auto* attributes = message->getAttributes();
        const auto result = attributes != nullptr &&
            attributes->setInt(kPayloadVersionAttribute, kPayloadVersion) == Steinberg::kResultTrue &&
            attributes->setBinary(kSessionValuesAttribute, markers.data(),
                                  static_cast<Steinberg::uint32>(markers.size() * sizeof(float))) ==
                Steinberg::kResultTrue &&
            attributes->setBinary(kOnsetHandlesAttribute, handles.data(),
                                  static_cast<Steinberg::uint32>(handles.size() *
                                                                 sizeof(ats::ui::SpectralOnset))) ==
                Steinberg::kResultTrue &&
            sendMessage(message) == Steinberg::kResultTrue;
        message->release();
        return result;
    }

    bool sendMorphTimeline(const ats::ui::SetMorphTimelineAction& timeline)
    {
        if (timeline.normalizedPoints.size() > kMaxTimelinePoints ||
            timeline.points.size() > kMaxTimelinePoints ||
            (timeline.normalizedPoints.empty() && timeline.points.empty()))
            return false;
        auto* message = allocateMessage();
        if (message == nullptr)
            return false;
        message->setMessageID(kSessionTimelineMessage);
        auto* attributes = message->getAttributes();
        bool result = attributes != nullptr &&
            attributes->setInt(kPayloadVersionAttribute, kPayloadVersion) == Steinberg::kResultTrue;
        if (result && !timeline.normalizedPoints.empty())
            result = attributes->setBinary(
                         kSessionValuesAttribute, timeline.normalizedPoints.data(),
                         static_cast<Steinberg::uint32>(timeline.normalizedPoints.size() *
                                                        sizeof(float))) == Steinberg::kResultTrue;
        if (result && !timeline.points.empty())
        {
            std::vector<TimelinePointWire> wire;
            wire.reserve(timeline.points.size());
            for (const auto& point : timeline.points)
                wire.push_back({ point.timeNorm, point.amount, point.kind, point.curve, { 0, 0 } });
            result = attributes->setBinary(
                         kTimelinePointsAttribute, wire.data(),
                         static_cast<Steinberg::uint32>(wire.size() * sizeof(TimelinePointWire))) ==
                     Steinberg::kResultTrue;
        }
        result = result && sendMessage(message) == Steinberg::kResultTrue;
        message->release();
        return result;
    }

    bool sendTimbreMotionPlan(const ats::ui::SetTimbreMotionPlanAction& timbre)
    {
        if (timbre.normalizedPlan.size() > 4096 ||
            timbre.serializedPlan.size() > kMaxTimbrePlanBytes ||
            (timbre.normalizedPlan.empty() && timbre.serializedPlan.empty()))
            return false;
        auto* message = allocateMessage();
        if (message == nullptr)
            return false;
        message->setMessageID(kSessionTimbreMessage);
        auto* attributes = message->getAttributes();
        bool result = attributes != nullptr &&
            attributes->setInt(kPayloadVersionAttribute, kPayloadVersion) == Steinberg::kResultTrue;
        if (result && !timbre.normalizedPlan.empty())
            result = attributes->setBinary(
                         kSessionValuesAttribute, timbre.normalizedPlan.data(),
                         static_cast<Steinberg::uint32>(timbre.normalizedPlan.size() *
                                                        sizeof(float))) == Steinberg::kResultTrue;
        if (result && !timbre.serializedPlan.empty())
            result = attributes->setBinary(
                         kTimbrePlanAttribute, timbre.serializedPlan.data(),
                         static_cast<Steinberg::uint32>(timbre.serializedPlan.size())) ==
                     Steinberg::kResultTrue;
        result = result && sendMessage(message) == Steinberg::kResultTrue;
        message->release();
        return result;
    }

    bool sendImport(const ats::Path& path, int slot, ats::engine::SourceImportKind kind)
    {
        if (path.empty() || slot < 0 || slot > 1 || path.string().size() > UINT32_MAX)
            return false;
        auto* message = allocateMessage();
        if (message == nullptr)
            return false;
        message->setMessageID(kImportSourceMessage);
        auto* attributes = message->getAttributes();
        const auto result = attributes != nullptr &&
            attributes->setInt(kSourceSlotAttribute, slot) == Steinberg::kResultTrue &&
            attributes->setInt(kSourceKindAttribute,
                               kind == ats::engine::SourceImportKind::audio ? 0 : 1) == Steinberg::kResultTrue &&
            attributes->setBinary(kSourcePathAttribute, path.string().data(),
                                  static_cast<Steinberg::uint32>(path.string().size())) == Steinberg::kResultTrue &&
            sendMessage(message) == Steinberg::kResultTrue;
        message->release();
        return result;
    }

    bool sendSlotCommand(const char* messageId, int slot)
    {
        if (slot < 0 || slot > 1)
            return false;
        auto* message = allocateMessage();
        if (message == nullptr)
            return false;
        message->setMessageID(messageId);
        auto* attributes = message->getAttributes();
        const auto result = attributes != nullptr &&
            attributes->setInt(kSourceSlotAttribute, slot) == Steinberg::kResultTrue &&
            sendMessage(message) == Steinberg::kResultTrue;
        message->release();
        return result;
    }

    bool sendMessageId(const char* messageId)
    {
        auto* message = allocateMessage();
        if (message == nullptr)
            return false;
        message->setMessageID(messageId);
        const auto result = sendMessage(message) == Steinberg::kResultTrue;
        message->release();
        return result;
    }

    void applyPreset(const ats::ParameterStore& preset)
    {
        for (const auto& descriptor : ats::ParameterRegistry::descriptors())
        {
            const auto tag = static_cast<Steinberg::Vst::ParamID>(descriptor.hostNumericId);
            const auto normalized = descriptor.normalize(preset.read(descriptor.denseIndex));
            if (std::abs(normalized - getParamNormalized(tag)) < 1.0e-7)
                continue; // untouched: do not write an automation edit
            beginEdit(tag);
            setParamNormalized(tag, normalized);
            performEdit(tag, normalized);
            endEdit(tag);
        }
    }

    ats::ui::TelemetrySnapshot editorTelemetry_ {};
};

Steinberg::IPlugView* PLUGIN_API AtsVst3Controller::createView(Steinberg::FIDString name)
{
    return Steinberg::FIDStringsEqual(name, Steinberg::Vst::ViewType::kEditor)
               ? new AtsVst3PlugView(*this)
               : nullptr;
}

} // namespace ats::vst3

#define stringCompanyName "ATSPlugin"
#define stringCompanyWeb "https://atsplugin.example"
#define stringCompanyEmail ""
#define stringPluginName "ATS Resynth"
#define stringPluginVersion "0.1.0"

BEGIN_FACTORY_DEF(stringCompanyName, stringCompanyWeb, stringCompanyEmail, 2)

DEF_CLASS(ats::vst3::kProcessorTuid,
          Steinberg::PClassInfo::kManyInstances,
          kVstAudioEffectClass,
          stringPluginName,
          Steinberg::Vst::kDistributable,
          "Fx|Instrument",
          stringPluginVersion,
          kVstVersionString,
          ats::vst3::AtsVst3Processor::createInstance,
          nullptr)

DEF_CLASS(ats::vst3::kControllerTuid,
          Steinberg::PClassInfo::kManyInstances,
          kVstComponentControllerClass,
          stringPluginName " Controller",
          0,
          "",
          stringPluginVersion,
          kVstVersionString,
          ats::vst3::AtsVst3Controller::createInstance,
          nullptr)

END_FACTORY
