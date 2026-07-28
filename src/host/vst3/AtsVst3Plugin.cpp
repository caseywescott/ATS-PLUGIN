#include "../../core/AtsStateCodec.h"
#include "../../core/ParameterRegistry.h"
#include "../../host/HostAdapter.h"

#include "base/source/fstreamer.h"
#include "public.sdk/source/vst/vstaudioeffect.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "public.sdk/source/main/pluginfactory_constexpr.h"

#include <array>
#include <cstddef>
#include <cstdint>
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
        if (data.symbolicSampleSize != Steinberg::Vst::kSample32 || data.numOutputs != 1 ||
            data.outputs == nullptr || data.outputs[0].numChannels != 2 ||
            data.outputs[0].channelBuffers32 == nullptr)
            return Steinberg::kResultFalse;

        applyParameterChanges(data.inputParameterChanges);

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

class AtsVst3Controller final : public Steinberg::Vst::EditController
{
public:
    static Steinberg::FUnknown* createInstance(void*)
    { return static_cast<Steinberg::Vst::IEditController*>(new AtsVst3Controller()); }

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
};

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
