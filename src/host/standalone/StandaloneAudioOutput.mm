#include "StandaloneAudioOutput.h"

#include <algorithm>
#include <cmath>

namespace ats::standalone
{

bool StandaloneAudioOutput::start(host::HostAdapter& host, bool silent, std::string& error)
{
    stop();
    AudioComponentDescription description {};
    description.componentType = kAudioUnitType_Output;
    description.componentSubType = kAudioUnitSubType_DefaultOutput;
    description.componentManufacturer = kAudioUnitManufacturer_Apple;
    AudioComponent component = AudioComponentFindNext(nullptr, &description);
    if (component == nullptr || AudioComponentInstanceNew(component, &unit_) != noErr)
    {
        unit_ = nullptr;
        error = "no default audio output device";
        return false;
    }

    // Device rate and the largest slice CoreAudio will request.
    AudioStreamBasicDescription deviceFormat {};
    UInt32 size = sizeof(deviceFormat);
    AudioUnitGetProperty(unit_, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0,
                         &deviceFormat, &size);
    sampleRate_ = deviceFormat.mSampleRate > 0.0 ? deviceFormat.mSampleRate : 48000.0;
    UInt32 maxFrames = 4096;
    size = sizeof(maxFrames);
    AudioUnitGetProperty(unit_, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0,
                         &maxFrames, &size);

    AudioStreamBasicDescription format {};
    format.mSampleRate = sampleRate_;
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked |
                          kAudioFormatFlagIsNonInterleaved;
    format.mChannelsPerFrame = 2;
    format.mFramesPerPacket = 1;
    format.mBitsPerChannel = 32;
    format.mBytesPerFrame = sizeof(float);
    format.mBytesPerPacket = sizeof(float);
    AURenderCallbackStruct callback { &StandaloneAudioOutput::render, this };
    if (AudioUnitSetProperty(unit_, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0,
                             &format, sizeof(format)) != noErr ||
        AudioUnitSetProperty(unit_, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0,
                             &callback, sizeof(callback)) != noErr)
    {
        error = "audio output format rejected";
        stop();
        return false;
    }
    host_ = &host;
    silent_ = silent;
    if (!host.prepare({ sampleRate_, std::max<UInt32>(maxFrames, 512), 2 }))
    {
        error = "host rejected the device configuration";
        stop();
        return false;
    }
    if (AudioUnitInitialize(unit_) != noErr || AudioOutputUnitStart(unit_) != noErr)
    {
        error = "audio output failed to start";
        stop();
        return false;
    }
    return true;
}

void StandaloneAudioOutput::stop() noexcept
{
    if (unit_ == nullptr)
        return;
    AudioOutputUnitStop(unit_);
    AudioUnitUninitialize(unit_);
    AudioComponentInstanceDispose(unit_);
    unit_ = nullptr;
    host_ = nullptr;
}

OSStatus StandaloneAudioOutput::render(void* context, AudioUnitRenderActionFlags*,
                                       const AudioTimeStamp*, UInt32, UInt32 frames,
                                       AudioBufferList* buffers)
{
    auto* self = static_cast<StandaloneAudioOutput*>(context);
    if (self == nullptr || buffers == nullptr || buffers->mNumberBuffers < 2)
        return noErr;
    float* channels[2] { static_cast<float*>(buffers->mBuffers[0].mData),
                         static_cast<float*>(buffers->mBuffers[1].mData) };
    if (self->host_ == nullptr || !self->host_->process({ channels, 2, frames }, {}))
    {
        std::fill(channels[0], channels[0] + frames, 0.0f);
        std::fill(channels[1], channels[1] + frames, 0.0f);
    }
    float peak = 0.0f;
    for (UInt32 i = 0; i < frames; ++i)
        peak = std::max(peak, std::max(std::abs(channels[0][i]), std::abs(channels[1][i])));
    if (peak > self->peak_.load(std::memory_order_relaxed))
        self->peak_.store(peak, std::memory_order_relaxed);
    self->callbacks_.fetch_add(1, std::memory_order_relaxed);
    if (self->silent_)
    {
        std::fill(channels[0], channels[0] + frames, 0.0f);
        std::fill(channels[1], channels[1] + frames, 0.0f);
    }
    return noErr;
}

} // namespace ats::standalone
