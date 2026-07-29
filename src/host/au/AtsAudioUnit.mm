#include "../../core/ParameterRegistry.h"
#include "../../state/FactoryPresetState.h"
#include "../../state/ParameterPresetCodec.h"
#include "../../platform/FileService.h"
#include "../../host/HostAdapter.h"
#include "../../ui/VisageEditor.h"

#include <AudioToolbox/AudioToolbox.h>
#include <AudioUnit/AUCocoaUIView.h>
#include <CoreFoundation/CoreFoundation.h>

#import <Cocoa/Cocoa.h>

#include <AudioUnitSDK/AUBase.h>
#include <AudioUnitSDK/AUPlugInDispatch.h>
#include <AudioUnitSDK/ComponentBase.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>

namespace ausdk
{
struct AtsAudioUnitLookup
{
    static AudioComponentMethod Lookup(SInt16 selector)
    {
        const auto process = AUBaseProcessMultipleLookup::Lookup(selector);
        return process != nullptr ? process : AUMIDILookup::Lookup(selector);
    }
};

template <class Implementor>
class AtsAudioUnitFactory : public APFactory<AtsAudioUnitLookup, Implementor>
{
};
}

namespace
{
class AuEditorActionTarget
{
public:
    virtual ~AuEditorActionTarget() = default;
    virtual void handleEditorAction(const ats::ui::EditorAction& action) = 0;
};

void forwardEditorAction(void* context, const ats::ui::EditorAction* action)
{
    auto* target = static_cast<AuEditorActionTarget*>(context);
    if (target != nullptr && action != nullptr)
        target->handleEditorAction(*action);
}

AuEditorActionTarget* editorActionTargetForAudioUnit(AudioUnit audioUnit);
}

@interface AtsAuHostedEditorView : NSView
@end

@implementation AtsAuHostedEditorView {
    std::unique_ptr<ats::ui::VisageEditor> _editor;
}

- (instancetype)initWithPreferredSize:(NSSize)size actionTarget:(AuEditorActionTarget*)actionTarget
{
    const CGFloat width = size.width > 1.0 ? size.width : 1280.0;
    const CGFloat height = size.height > 1.0 ? size.height : 940.0;
    self = [super initWithFrame:NSMakeRect(0.0, 0.0, width, height)];
    if (self == nil)
        return nil;

    ats::ui::EditorEventSink sink;
    sink.context = actionTarget;
    sink.action = &forwardEditorAction;
    _editor = std::make_unique<ats::ui::VisageEditor>(sink);
    if (!_editor->setSize(static_cast<std::uint32_t>(width),
                          static_cast<std::uint32_t>(height)) ||
        !_editor->setParent((__bridge void*)self))
    {
        _editor.reset();
        return nil;
    }
    _editor->show();
    return self;
}

- (void)setFrameSize:(NSSize)newSize
{
    [super setFrameSize:newSize];
    if (_editor != nullptr && newSize.width > 0.0 && newSize.height > 0.0)
        _editor->setSize(static_cast<std::uint32_t>(newSize.width),
                         static_cast<std::uint32_t>(newSize.height));
}

- (BOOL)acceptsFirstResponder
{
    return YES;
}

- (void)dealloc
{
    _editor.reset();
#if !__has_feature(objc_arc)
    [super dealloc];
#endif
}

@end

@interface AtsAudioUnitCocoaViewFactory : NSObject <AUCocoaUIBase>
@end

@implementation AtsAudioUnitCocoaViewFactory

- (unsigned)interfaceVersion
{
    return 0;
}

- (NSView*)uiViewForAudioUnit:(AudioUnit)audioUnit withSize:(NSSize)preferredSize
{
    return [[AtsAuHostedEditorView alloc]
        initWithPreferredSize:preferredSize
                  actionTarget:editorActionTargetForAudioUnit(audioUnit)];
}

@end

namespace
{
Class cocoaViewFactoryClass()
{
    return [AtsAudioUnitCocoaViewFactory class];
}

class AtsAudioUnit final : public ausdk::AUBase, public AuEditorActionTarget
{
public:
    explicit AtsAudioUnit(AudioComponentInstance instance)
        : ausdk::AUBase(instance, 0, 1)
    {
        host_.initialize(ats::host::HostFormat::au);
    }

    bool StreamFormatWritable(AudioUnitScope, AudioUnitElement) override { return true; }
    bool CanScheduleParameters() const override { return true; }

    UInt32 SupportedNumChannels(const AUChannelInfo** outInfo) override
    {
        static constexpr AUChannelInfo supported[] = {
            { 0, 2 }
        };
        if (outInfo != nullptr)
            *outInfo = supported;
        return static_cast<UInt32>(std::size(supported));
    }

    OSStatus Initialize() override
    {
        const auto result = ausdk::AUBase::Initialize();
        if (result != noErr)
            return result;
        const auto format = GetStreamFormat(kAudioUnitScope_Output, 0);
        const auto sampleRate = format.mSampleRate > 0.0 ? format.mSampleRate : 48000.0;
        return host_.prepare({ sampleRate, 512, 2, 0 }) ? noErr : kAudio_ParamError;
    }

    void Cleanup() override { host_.release(); }

    OSStatus SaveState(CFPropertyListRef* data) override
    {
        if (data == nullptr)
            return kAudio_ParamError;
        const auto fallback = ats::AtsStateCodec::encodeHeader();
        const auto& chunk = host_.stateChunk();
        const auto* bytes = chunk.empty() ? &fallback : &chunk;
        const auto payload = CFDataCreate(
            kCFAllocatorDefault, reinterpret_cast<const UInt8*>(bytes->data()),
            static_cast<CFIndex>(bytes->size()));
        if (payload == nullptr)
            return kAudio_ParamError;

        const auto description = GetComponentDescription();
        auto dictionary = CFDictionaryCreateMutable(
            kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks,
            &kCFTypeDictionaryValueCallBacks);
        if (dictionary == nullptr)
        {
            CFRelease(payload);
            return kAudio_ParamError;
        }

        const auto setNumber = [&](CFStringRef key, SInt32 value) {
            const auto number = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &value);
            if (number != nullptr)
            {
                CFDictionarySetValue(dictionary, key, number);
                CFRelease(number);
            }
        };
        setNumber(CFSTR(kAUPresetVersionKey), 0);
        setNumber(CFSTR(kAUPresetTypeKey), static_cast<SInt32>(description.componentType));
        setNumber(CFSTR(kAUPresetSubtypeKey), static_cast<SInt32>(description.componentSubType));
        setNumber(CFSTR(kAUPresetManufacturerKey),
                  static_cast<SInt32>(description.componentManufacturer));
        CFDictionarySetValue(dictionary, CFSTR(kAUPresetNameKey), CFSTR("ATS Resynth"));
        CFDictionarySetValue(dictionary, CFSTR(kAUPresetDataKey), payload);
        CFRelease(payload);
        *data = dictionary;
        return noErr;
    }

    OSStatus RestoreState(CFPropertyListRef data) override
    {
        if (data == nullptr)
            return kAudioUnitErr_InvalidPropertyValue;

        CFDataRef payload = nullptr;
        if (CFGetTypeID(data) == CFDictionaryGetTypeID())
        {
            const auto* value = CFDictionaryGetValue(
                static_cast<CFDictionaryRef>(data), CFSTR(kAUPresetDataKey));
            if (value == nullptr || CFGetTypeID(value) != CFDataGetTypeID())
                return kAudioUnitErr_InvalidPropertyValue;
            payload = static_cast<CFDataRef>(const_cast<void*>(value));
        }
        else if (CFGetTypeID(data) == CFDataGetTypeID())
        {
            // Accept the pre-dictionary ATSP form for projects saved by the
            // focused wrapper tests before AU property-list conformance was added.
            payload = static_cast<CFDataRef>(data);
        }
        else
        {
            return kAudioUnitErr_InvalidPropertyValue;
        }

        const auto* bytes = CFDataGetBytePtr(payload);
        const auto size = static_cast<std::size_t>(CFDataGetLength(payload));
        return host_.restoreState(bytes, size) ? noErr : kAudioUnitErr_InvalidPropertyValue;
    }

    OSStatus GetPropertyInfo(AudioUnitPropertyID inID, AudioUnitScope inScope,
                             AudioUnitElement inElement, UInt32& outDataSize,
                             bool& outWritable) override
    {
        if (inID == kAudioUnitProperty_CocoaUI && inScope == kAudioUnitScope_Global)
        {
            outDataSize = sizeof(AudioUnitCocoaViewInfo);
            outWritable = false;
            return noErr;
        }
        return ausdk::AUBase::GetPropertyInfo(inID, inScope, inElement, outDataSize, outWritable);
    }

    OSStatus GetProperty(AudioUnitPropertyID inID, AudioUnitScope inScope,
                         AudioUnitElement inElement, void* outData) override
    {
        if (inID == kAudioUnitProperty_CocoaUI && inScope == kAudioUnitScope_Global)
        {
            if (outData == nullptr)
                return kAudio_ParamError;
            @autoreleasepool
            {
                Class cls = cocoaViewFactoryClass();
                NSBundle* bundle = [NSBundle bundleForClass:cls];
                if (cls == nil || bundle == nil || bundle.bundlePath == nil)
                    return kAudioUnitErr_InvalidPropertyValue;

                auto* info = static_cast<AudioUnitCocoaViewInfo*>(outData);
                info->mCocoaAUViewClass[0] = CFStringCreateCopy(
                    kCFAllocatorDefault, (__bridge CFStringRef)NSStringFromClass(cls));
                info->mCocoaAUViewBundleLocation = CFURLCreateWithFileSystemPath(
                    kCFAllocatorDefault, (__bridge CFStringRef)bundle.bundlePath,
                    kCFURLPOSIXPathStyle, true);
                if (info->mCocoaAUViewClass[0] == nullptr ||
                    info->mCocoaAUViewBundleLocation == nullptr)
                    return kAudioUnitErr_InvalidPropertyValue;
            }
            return noErr;
        }
        return ausdk::AUBase::GetProperty(inID, inScope, inElement, outData);
    }

    OSStatus ProcessMultipleBufferLists(AudioUnitRenderActionFlags&,
                                        UInt32 inputFrames,
                                        UInt32 inputCount,
                                        const AudioBufferList**,
                                        UInt32 outputCount,
                                        AudioBufferList** outputs) override
    {
        if (inputCount != 0 || outputCount != 1 || outputs == nullptr || outputs[0] == nullptr)
            return kAudio_ParamError;
        ScheduledProcessContext context { this, outputs[0], false };
        auto& events = GetParamEventList();
        if (!events.empty())
        {
            const auto result = ProcessForScheduledParams(events, inputFrames, &context);
            events.clear();
            midiEventCount_ = 0;
            return result;
        }
        const auto result = processOutput(*outputs[0], 0, inputFrames,
                                          { midiEvents_.data(),
                                            static_cast<std::uint32_t>(midiEventCount_) });
        midiEventCount_ = 0;
        return result;
    }

    OSStatus MIDIEvent(UInt32 status, UInt32 data1, UInt32 data2,
                       UInt32 offset) override
    {
        const auto type = static_cast<UInt8>(status & 0xf0u);
        if (type != 0x80u && type != 0x90u)
            return noErr;
        if (midiEventCount_ >= midiEvents_.size())
            return kAudio_ParamError;
        midiEvents_[midiEventCount_++] = {
            offset,
            type == 0x90u && data2 != 0u ? ats::MidiEvent::Type::noteOn
                                         : ats::MidiEvent::Type::noteOff,
            static_cast<std::uint8_t>(data1 & 0x7fu),
            static_cast<std::uint8_t>(data2 & 0x7fu)
        };
        return noErr;
    }

protected:
    OSStatus ProcessScheduledSlice(void* userData, UInt32 startFrame,
                                   UInt32 frameCount, UInt32) override
    {
        auto& context = *static_cast<ScheduledProcessContext*>(userData);
        for (const auto& event : GetParamEventList())
        {
            const auto* descriptor = descriptorFor(event.parameter);
            if (descriptor == nullptr)
                continue;
            const auto value = Globals()->GetParameter(event.parameter);
            host_.applyParameterChange({ descriptor->denseIndex,
                                         descriptor->snap(static_cast<float>(value)) });
        }
        const ats::MidiEventList midi = context.midiDispatched
            ? ats::MidiEventList {}
            : ats::MidiEventList { midiEvents_.data(),
                                   static_cast<std::uint32_t>(midiEventCount_) };
        context.midiDispatched = true;
        return processOutput(*context.output, startFrame, frameCount, midi);
    }

private:
    struct ScheduledProcessContext
    {
        AtsAudioUnit* unit = nullptr;
        AudioBufferList* output = nullptr;
        bool midiDispatched = false;
    };

    OSStatus processOutput(AudioBufferList& output, UInt32 startFrame, UInt32 frames,
                           ats::MidiEventList midi)
    {
        const auto totalFrames = startFrame + frames;
        if (output.mNumberBuffers == 0 || output.mNumberBuffers > 2)
            return kAudio_ParamError;
        std::array<float*, 2> channels{};
        UInt32 channelCount = 0;
        for (UInt32 index = 0; index < output.mNumberBuffers; ++index)
        {
            const auto& buffer = output.mBuffers[index];
            if (buffer.mNumberChannels == 0 || buffer.mNumberChannels > 2 ||
                buffer.mData == nullptr)
                return kAudio_ParamError;
            if (buffer.mNumberChannels == 1)
                channels[channelCount++] = static_cast<float*>(buffer.mData) + startFrame;
            else
            {
                if (channelCount != 0)
                    return kAudio_ParamError;
                channels[0] = static_cast<float*>(buffer.mData) + startFrame * buffer.mNumberChannels;
                channels[1] = channels[0] + totalFrames;
                channelCount = 2;
            }
        }
        if (channelCount != 2)
            return kAudio_ParamError;
        const ats::AudioBufferView view { channels.data(), 2, frames };
        return host_.process(view, midi) ? noErr : kAudioUnitErr_Uninitialized;
    }

    OSStatus GetParameterList(AudioUnitScope scope, AudioUnitParameterID* ids,
                              UInt32& count) override
    {
        if (scope != kAudioUnitScope_Global)
            return kAudioUnitErr_InvalidScope;
        const auto& descriptors = ats::ParameterRegistry::descriptors();
        if (ids == nullptr)
        {
            count = static_cast<UInt32>(descriptors.size());
            return noErr;
        }
        // AUBase's parameter-list dispatch initializes the output count to
        // zero before calling this override, so a non-null list is the
        // already-sized buffer requested by the host.
        const auto limit = descriptors.size();
        for (std::size_t index = 0; index < limit; ++index)
            ids[index] = descriptors[index].hostNumericId;
        count = static_cast<UInt32>(limit);
        return noErr;
    }

    OSStatus GetParameterInfo(AudioUnitScope scope, AudioUnitParameterID id,
                              AudioUnitParameterInfo& info) override
    {
        if (scope != kAudioUnitScope_Global)
            return kAudioUnitErr_InvalidScope;
        const auto* descriptor = descriptorFor(id);
        if (descriptor == nullptr)
            return kAudioUnitErr_InvalidParameter;
        info = {};
        std::strncpy(info.name, descriptor->name.data(), sizeof(info.name) - 1);
        info.unit = kAudioUnitParameterUnit_Generic;
        info.minValue = descriptor->minimum;
        info.maxValue = descriptor->maximum;
        info.defaultValue = descriptor->defaultValue;
        info.flags = descriptor->automatable ? kAudioUnitParameterFlag_IsWritable : 0;
        return noErr;
    }

    OSStatus GetParameter(AudioUnitParameterID id, AudioUnitScope scope,
                          AudioUnitElement, AudioUnitParameterValue& value) override
    {
        if (scope != kAudioUnitScope_Global)
            return kAudioUnitErr_InvalidScope;
        const auto* descriptor = descriptorFor(id);
        if (descriptor == nullptr)
            return kAudioUnitErr_InvalidParameter;
        value = host_.parameters().read(descriptor->denseIndex);
        return noErr;
    }

    OSStatus SetParameter(AudioUnitParameterID id, AudioUnitScope scope,
                          AudioUnitElement, AudioUnitParameterValue value,
                          UInt32) override
    {
        if (scope != kAudioUnitScope_Global)
            return kAudioUnitErr_InvalidScope;
        const auto* descriptor = descriptorFor(id);
        if (descriptor == nullptr)
            return kAudioUnitErr_InvalidParameter;
        const auto snapped = descriptor->snap(static_cast<float>(value));
        Globals()->SetParameter(descriptor->hostNumericId, snapped, true);
        return host_.applyParameterChange({ descriptor->denseIndex, snapped })
            ? noErr
            : kAudioUnitErr_InvalidParameter;
    }

    void handleEditorAction(const ats::ui::EditorAction& action) override
    {
        if (const auto* factory = std::get_if<ats::ui::ApplyFactoryPresetAction>(&action))
        {
            (void) ats::state::applyFactoryPreset(factory->index, host_.parameters());
            return;
        }
        ats::platform::FileService files;
        if (const auto* save = std::get_if<ats::ui::SavePresetAction>(&action))
        {
            (void) files.writeAtomic(save->path, ats::state::ParameterPresetCodec::encode(host_.parameters()), true);
            return;
        }
        if (const auto* load = std::get_if<ats::ui::LoadPresetAction>(&action))
        {
            const auto payload = files.read(load->path);
            std::string error;
            if (payload.succeeded())
                (void) ats::state::ParameterPresetCodec::decode(payload.bytes, host_.parameters(), error);
            return;
        }
        ats::Error importError;
        if (const auto* wav = std::get_if<ats::ui::ImportWavAction>(&action))
        {
            (void) host_.importSource(wav->path, wav->slot,
                                      ats::engine::SourceImportKind::audio, importError);
            return;
        }
        if (const auto* ats = std::get_if<ats::ui::ImportAtsAction>(&action))
        {
            (void) host_.importSource(ats->path, ats->slot,
                                      ats::engine::SourceImportKind::ats, importError);
            return;
        }
        if (const auto* analyse = std::get_if<ats::ui::AnalyzeMorphSourceAction>(&action))
        {
            ats::Error error;
            (void) host_.analyzeSource(analyse->slot, error);
            return;
        }
        if (std::holds_alternative<ats::ui::AnalyzeCurrentAction>(action))
        {
            ats::Error error;
            (void) host_.analyzeSource(0, error);
            return;
        }
        const auto* parameter = std::get_if<ats::ui::SetParameterAction>(&action);
        if (parameter == nullptr)
            return;
        const auto* descriptor = ats::ParameterRegistry::find(parameter->id);
        if (descriptor == nullptr || !descriptor->automatable || descriptor->readOnly)
            return;
        (void) SetParameter(descriptor->hostNumericId, kAudioUnitScope_Global, 0,
                            parameter->value, 0);
    }

    Float64 GetLatency() override { return 0.0; }
    Float64 GetTailTime() override { return 0.0; }

    static const ats::ParameterDescriptor* descriptorFor(AudioUnitParameterID id)
    {
        const auto& descriptors = ats::ParameterRegistry::descriptors();
        const auto it = std::find_if(descriptors.begin(), descriptors.end(),
                                     [id](const auto& descriptor) {
                                         return descriptor.hostNumericId == id;
                                     });
        return it == descriptors.end() ? nullptr : &*it;
    }

    ats::host::HostAdapter host_;
    std::array<ats::MidiEvent, 256> midiEvents_{};
    std::size_t midiEventCount_ = 0;
};

AuEditorActionTarget* editorActionTargetForAudioUnit(AudioUnit audioUnit)
{
    // AudioUnit is the SDK's plug-in-interface pointer. The implementation
    // object is constructed in the documented storage that follows that
    // interface, rather than at the interface address itself.
    auto* instance = reinterpret_cast<ausdk::AudioComponentPlugInInstance*>(audioUnit);
    auto* unit = instance != nullptr
        ? reinterpret_cast<AtsAudioUnit*>(&instance->mInstanceStorage)
        : nullptr;
    return unit != nullptr ? static_cast<AuEditorActionTarget*>(unit) : nullptr;
}
}

AUSDK_COMPONENT_ENTRY(ausdk::AtsAudioUnitFactory, AtsAudioUnit)
