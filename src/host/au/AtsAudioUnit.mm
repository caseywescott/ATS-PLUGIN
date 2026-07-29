#include "../../core/ParameterRegistry.h"
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

@interface AtsAuHostedEditorView : NSView
@end

@implementation AtsAuHostedEditorView {
    std::unique_ptr<ats::ui::VisageEditor> _editor;
}

- (instancetype)initWithPreferredSize:(NSSize)size
{
    const CGFloat width = size.width > 1.0 ? size.width : 1280.0;
    const CGFloat height = size.height > 1.0 ? size.height : 940.0;
    self = [super initWithFrame:NSMakeRect(0.0, 0.0, width, height)];
    if (self == nil)
        return nil;

    _editor = std::make_unique<ats::ui::VisageEditor>();
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
    (void)audioUnit;
    return [[AtsAuHostedEditorView alloc] initWithPreferredSize:preferredSize];
}

@end

namespace
{
Class cocoaViewFactoryClass()
{
    return [AtsAudioUnitCocoaViewFactory class];
}

class AtsAudioUnit final : public ausdk::AUBase
{
public:
    explicit AtsAudioUnit(AudioComponentInstance instance)
        : ausdk::AUBase(instance, 0, 1)
    {
        host_.initialize(ats::host::HostFormat::au);
    }

    bool StreamFormatWritable(AudioUnitScope, AudioUnitElement) override { return true; }
    bool CanScheduleParameters() const override { return true; }

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
        const auto* raw = reinterpret_cast<const UInt8*>(bytes->data());
        *data = CFDataCreate(kCFAllocatorDefault, raw,
                             static_cast<CFIndex>(bytes->size()));
        return *data == nullptr ? kAudio_ParamError : noErr;
    }

    OSStatus RestoreState(CFPropertyListRef data) override
    {
        if (data == nullptr || CFGetTypeID(data) != CFDataGetTypeID())
            return kAudioUnitErr_InvalidPropertyValue;
        const auto* bytes = CFDataGetBytePtr(static_cast<CFDataRef>(data));
        const auto size = static_cast<std::size_t>(CFDataGetLength(static_cast<CFDataRef>(data)));
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
        const auto& output = *outputs[0];
        const auto frames = inputFrames;
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
                channels[channelCount++] = static_cast<float*>(buffer.mData);
            else
            {
                if (channelCount != 0)
                    return kAudio_ParamError;
                channels[0] = static_cast<float*>(buffer.mData);
                channels[1] = channels[0] + frames;
                channelCount = 2;
            }
        }
        if (channelCount != 2)
            return kAudio_ParamError;
        const ats::AudioBufferView view { channels.data(), 2, frames };
        return host_.process(view, {}) ? noErr : kAudioUnitErr_Uninitialized;
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
        return descriptor != nullptr &&
                       host_.applyParameterChange({ descriptor->denseIndex,
                                                    descriptor->snap(static_cast<float>(value)) })
                   ? noErr
                   : kAudioUnitErr_InvalidParameter;
    }

    Float64 GetLatency() override { return 0.0; }
    Float64 GetTailTime() override { return 0.0; }

private:
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
};
}

AUSDK_COMPONENT_ENTRY(ausdk::AUBaseProcessMultipleFactory, AtsAudioUnit)
