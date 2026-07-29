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

#include <array>
#include <cstddef>
#include <cstdint>
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
        const auto& chunk = host_.stateChunk();
        const auto defaultChunk = ats::AtsStateCodec::encodeHeader();
        return writeStream(state, chunk.empty() ? defaultChunk : chunk) ? Steinberg::kResultTrue
                                                                         : Steinberg::kResultFalse;
    }

private:
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
};

class AtsVst3PlugView final : public Steinberg::FObject, public Steinberg::IPlugView
{
public:
    explicit AtsVst3PlugView(Vst3EditorActionTarget& actionTarget) : actionTarget_(&actionTarget)
    {
        ats::ui::EditorEventSink sink;
        sink.context = actionTarget_;
        sink.action = &AtsVst3PlugView::editorAction;
        editor_ = std::make_unique<ats::ui::VisageEditor>(sink);
    }
    ~AtsVst3PlugView() override { editor_.reset(); }

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
        editor_->show();
        return Steinberg::kResultTrue;
    }

    Steinberg::tresult PLUGIN_API removed() override
    {
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
        return rect != nullptr && rect->getWidth() > 0 && rect->getHeight() > 0
                   ? Steinberg::kResultTrue
                   : Steinberg::kResultFalse;
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
    Steinberg::IPlugFrame* frame_ = nullptr;
};

class AtsVst3Controller final : public Steinberg::Vst::EditController, public Vst3EditorActionTarget
{
public:
    AtsVst3Controller()
    { sourceHost_.initialize(ats::host::HostFormat::vst3); }

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

    Steinberg::tresult PLUGIN_API setComponentState(Steinberg::IBStream* state) SMTG_OVERRIDE
    {
        return state == nullptr ? Steinberg::kResultFalse : Steinberg::kResultTrue;
    }

    void handleEditorAction(const ats::ui::EditorAction& action) override
    {
        if (const auto* factory = std::get_if<ats::ui::ApplyFactoryPresetAction>(&action))
        {
            ats::ParameterStore preset;
            if (!ats::state::applyFactoryPreset(factory->index, preset))
                return;
            applyPreset(preset);
            return;
        }
        ats::platform::FileService files;
        if (const auto* save = std::get_if<ats::ui::SavePresetAction>(&action))
        {
            ats::ParameterStore current;
            for (const auto& descriptor : ats::ParameterRegistry::descriptors())
                (void) current.write(descriptor.denseIndex, descriptor.denormalize(
                    static_cast<float>(getParamNormalized(
                        static_cast<Steinberg::Vst::ParamID>(descriptor.hostNumericId)))));
            (void) files.writeAtomic(save->path, ats::state::ParameterPresetCodec::encode(current), true);
            return;
        }
        if (const auto* load = std::get_if<ats::ui::LoadPresetAction>(&action))
        {
            const auto payload = files.read(load->path);
            ats::ParameterStore preset;
            std::string error;
            if (payload.succeeded() &&
                ats::state::ParameterPresetCodec::decode(payload.bytes, preset, error))
                applyPreset(preset);
            return;
        }
        ats::Error importError;
        if (const auto* wav = std::get_if<ats::ui::ImportWavAction>(&action))
        {
            (void) sourceHost_.importSource(wav->path, wav->slot,
                                            ats::engine::SourceImportKind::audio, importError);
            return;
        }
        if (const auto* ats = std::get_if<ats::ui::ImportAtsAction>(&action))
        {
            (void) sourceHost_.importSource(ats->path, ats->slot,
                                            ats::engine::SourceImportKind::ats, importError);
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

private:
    void applyPreset(const ats::ParameterStore& preset)
    {
        for (const auto& descriptor : ats::ParameterRegistry::descriptors())
        {
            const auto tag = static_cast<Steinberg::Vst::ParamID>(descriptor.hostNumericId);
            const auto normalized = descriptor.normalize(preset.read(descriptor.denseIndex));
            beginEdit(tag);
            setParamNormalized(tag, normalized);
            performEdit(tag, normalized);
            endEdit(tag);
        }
    }

    ats::host::HostAdapter sourceHost_;
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
