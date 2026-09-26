#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>

#include "../../src/core/AtsStateCodec.h"

#include <dlfcn.h>

#include <cstdlib>
#include <array>
#include <cstring>
#include <iostream>

namespace
{
using FactoryFunction = AudioComponentFactoryFunction;
using GetPropertyInfoFn = OSStatus (*)(void*, AudioUnitPropertyID, AudioUnitScope,
                                       AudioUnitElement, UInt32*, Boolean*);
using GetPropertyFn = OSStatus (*)(void*, AudioUnitPropertyID, AudioUnitScope,
                                   AudioUnitElement, void*, UInt32*);
using SetPropertyFn = OSStatus (*)(void*, AudioUnitPropertyID, AudioUnitScope,
                                   AudioUnitElement, const void*, UInt32);
using GetParameterFn = OSStatus (*)(void*, AudioUnitParameterID, AudioUnitScope,
                                    AudioUnitElement, AudioUnitParameterValue*);
using SetParameterFn = OSStatus (*)(void*, AudioUnitParameterID, AudioUnitScope,
                                    AudioUnitElement, AudioUnitParameterValue, UInt32);
using ScheduleParametersFn = AudioUnitScheduleParametersProc;
using MidiEventFn = MusicDeviceMIDIEventProc;
using UnitFn = OSStatus (*)(void*);

void require(bool value, const char* expression)
{
    if (!value)
    {
        std::cerr << "AU artifact check failed: " << expression << '\n';
        std::abort();
    }
}
}

int main(int argc, char** argv)
{
    require(argc == 2, "argv[1] contains the AU component path");
    void* module = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    require(module != nullptr, "dlopen(component)");
    auto factory = reinterpret_cast<FactoryFunction>(dlsym(module, "AtsAudioUnitFactory"));
    require(factory != nullptr, "AtsAudioUnitFactory export");

    const AudioComponentDescription description {
        kAudioUnitType_Effect, 'Atr1', 'AtPl', 0, 0
    };
    auto* plugin = factory(&description);
    require(plugin != nullptr, "AudioUnit factory");
    require(plugin->Open(plugin, reinterpret_cast<AudioComponentInstance>(plugin)) == noErr,
            "AudioComponent Open");
    auto initialize = reinterpret_cast<UnitFn>(plugin->Lookup(kAudioUnitInitializeSelect));
    require(initialize != nullptr && initialize(plugin) == noErr, "AudioUnitInitialize");

    UInt32 count = 0;
    UInt32 size = sizeof(count);
    auto getPropertyInfo = reinterpret_cast<GetPropertyInfoFn>(
        plugin->Lookup(kAudioUnitGetPropertyInfoSelect));
    require(getPropertyInfo != nullptr &&
                getPropertyInfo(plugin, kAudioUnitProperty_ParameterList,
                                kAudioUnitScope_Global, 0, &size, nullptr) == noErr,
            "parameter-list property info");
    require(size == 81u * sizeof(AudioUnitParameterID), "81 parameter contract");
    std::array<AudioUnitParameterID, 81> ids {};
    auto getProperty = reinterpret_cast<GetPropertyFn>(
        plugin->Lookup(kAudioUnitGetPropertySelect));
    require(getProperty != nullptr &&
                getProperty(plugin, kAudioUnitProperty_ParameterList,
                            kAudioUnitScope_Global, 0, ids.data(), &size) == noErr,
            "parameter-list property");
    count = size / sizeof(AudioUnitParameterID);
    require(count == 81, "81 parameter list entries");

    UInt32 channelInfoSize = 0;
    require(getPropertyInfo(plugin, kAudioUnitProperty_SupportedNumChannels,
                            kAudioUnitScope_Global, 0, &channelInfoSize, nullptr) == noErr,
            "supported-channel property info");
    require(channelInfoSize == 2u * sizeof(AUChannelInfo),
            "music-effect channel capability size");
    std::array<AUChannelInfo, 2> channelInfo {};
    require(getProperty(plugin, kAudioUnitProperty_SupportedNumChannels,
                        kAudioUnitScope_Global, 0, channelInfo.data(), &channelInfoSize) == noErr,
            "supported-channel property");
    require(channelInfo[0].inChannels == 2 && channelInfo[0].outChannels == 2,
            "stereo music-effect capability");
    require(channelInfo[1].inChannels == 1 && channelInfo[1].outChannels == 2,
            "mono-to-stereo music-effect capability");

    auto setParameter = reinterpret_cast<SetParameterFn>(
        plugin->Lookup(kAudioUnitSetParameterSelect));
    require(setParameter != nullptr, "parameter setter");
    auto getParameter = reinterpret_cast<GetParameterFn>(
        plugin->Lookup(kAudioUnitGetParameterSelect));
    auto getParameterInfo = reinterpret_cast<GetPropertyFn>(
        plugin->Lookup(kAudioUnitGetPropertySelect));
    require(getParameter != nullptr && getParameterInfo != nullptr, "parameter readers");
    for (const auto id : ids)
    {
        AudioUnitParameterInfo info {};
        UInt32 infoSize = sizeof(info);
        require(getParameterInfo(plugin, kAudioUnitProperty_ParameterInfo,
                                  kAudioUnitScope_Global, id, &info, &infoSize) == noErr,
                "parameter info");
        require(info.maxValue >= info.minValue, "parameter range");
        require(setParameter(plugin, id, kAudioUnitScope_Global, 0, info.minValue, 0) == noErr,
                "parameter write");
        AudioUnitParameterValue value = 0.0f;
        require(getParameter(plugin, id, kAudioUnitScope_Global, 0, &value) == noErr,
                "parameter read");
        require(value >= info.minValue && value <= info.maxValue, "parameter value range");
    }

    AudioUnitParameterInfo automationInfo {};
    UInt32 automationInfoSize = sizeof(automationInfo);
    require(getParameterInfo(plugin, kAudioUnitProperty_ParameterInfo,
                              kAudioUnitScope_Global, ids.front(), &automationInfo,
                              &automationInfoSize) == noErr,
            "automation parameter info");
    AudioUnitParameterEvent events[2] {};
    events[0].scope = kAudioUnitScope_Global;
    events[0].parameter = ids.front();
    events[0].eventType = kParameterEvent_Immediate;
    events[0].eventValues.immediate.value = automationInfo.maxValue;
    events[1].scope = kAudioUnitScope_Global;
    events[1].parameter = ids.front();
    events[1].eventType = kParameterEvent_Ramped;
    events[1].eventValues.ramp.startBufferOffset = 8;
    events[1].eventValues.ramp.durationInFrames = 16;
    events[1].eventValues.ramp.startValue = automationInfo.minValue;
    events[1].eventValues.ramp.endValue = automationInfo.maxValue;
    const auto schedule = reinterpret_cast<ScheduleParametersFn>(
        plugin->Lookup(kAudioUnitScheduleParametersSelect));
    require(schedule != nullptr && schedule(plugin, events, 2) == noErr,
            "immediate and ramped parameter schedule");

    const auto midi = reinterpret_cast<MidiEventFn>(
        plugin->Lookup(kMusicDeviceMIDIEventSelect));
    require(midi != nullptr && midi(plugin, 0x90, 60, 100, 4) == noErr,
            "MIDI note-on dispatch");

    AudioStreamBasicDescription format {};
    format.mSampleRate = 48000.0;
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags = kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved;
    format.mBytesPerPacket = sizeof(float);
    format.mFramesPerPacket = 1;
    format.mBytesPerFrame = sizeof(float);
    format.mChannelsPerFrame = 2;
    format.mBitsPerChannel = 32;
    auto setProperty = reinterpret_cast<SetPropertyFn>(
        plugin->Lookup(kAudioUnitSetPropertySelect));
    require(setProperty != nullptr &&
                setProperty(plugin, kAudioUnitProperty_StreamFormat,
                            kAudioUnitScope_Output, 0, &format, sizeof(format)) == noErr,
            "stereo output format");
    AudioStreamBasicDescription confirmed {};
    UInt32 confirmedSize = sizeof(confirmed);
    require(getProperty(plugin, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0,
                         &confirmed, &confirmedSize) == noErr,
            "read stereo output format");
    std::cerr << "AU format channels=" << confirmed.mChannelsPerFrame
              << " flags=" << confirmed.mFormatFlags
              << " bytes=" << confirmed.mBytesPerFrame << '\n';

    CFPropertyListRef state = nullptr;
    UInt32 stateSize = sizeof(state);
    require(getProperty(plugin, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0,
                         &state, &stateSize) == noErr && state != nullptr,
            "class-info state read");
    require(CFGetTypeID(state) == CFDictionaryGetTypeID(), "AU class-info dictionary");
    const auto* stateData = CFDictionaryGetValue(
        static_cast<CFDictionaryRef>(state), CFSTR(kAUPresetDataKey));
    require(stateData != nullptr && CFGetTypeID(stateData) == CFDataGetTypeID(),
            "ATSP class-info data");
    require(ats::AtsStateCodec::decodeHeader(
                CFDataGetBytePtr(static_cast<CFDataRef>(stateData)),
                static_cast<std::size_t>(CFDataGetLength(static_cast<CFDataRef>(stateData))))
                 .has_value(),
            "ATSP class-info header");
    require(setProperty(plugin, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0,
                        &state, sizeof(state)) == noErr,
            "class-info state restore");
    CFRelease(state);

    constexpr UInt32 frames = 64;
    std::array<float, frames> left {};
    std::array<float, frames> right {};
    struct StereoBufferList
    {
        AudioBufferList list {};
        AudioBuffer second {};
    } output;
    output.list.mNumberBuffers = 2;
    output.list.mBuffers[0] = { 1, static_cast<UInt32>(left.size() * sizeof(float)), left.data() };
    output.second = { 1, static_cast<UInt32>(right.size() * sizeof(float)), right.data() };
    auto render = reinterpret_cast<AudioUnitRenderProc>(
        plugin->Lookup(kAudioUnitRenderSelect));
    AudioTimeStamp timestamp {};
    AudioUnitRenderActionFlags renderFlags = 0;
    require(render != nullptr, "stereo render selector lookup");
    const auto renderStatus = render(plugin, &renderFlags, &timestamp, 0, frames, &output.list);
    require(renderStatus == noErr, "stereo render selector status");
    for (const auto sample : left)
        require(sample == 0.0f, "silent left output without playback");
    for (const auto sample : right)
        require(sample == 0.0f, "silent right output without playback");

    for (UInt32 block = 0; block < 4; ++block)
    {
        AudioUnitParameterEvent blockEvents[2] {};
        blockEvents[0].scope = kAudioUnitScope_Global;
        blockEvents[0].parameter = ids[block % ids.size()];
        blockEvents[0].eventType = kParameterEvent_Immediate;
        blockEvents[0].eventValues.immediate.value = automationInfo.minValue;
        blockEvents[1].scope = kAudioUnitScope_Global;
        blockEvents[1].parameter = ids[block % ids.size()];
        blockEvents[1].eventType = kParameterEvent_Ramped;
        blockEvents[1].eventValues.ramp.startBufferOffset = 4;
        blockEvents[1].eventValues.ramp.durationInFrames = 24;
        blockEvents[1].eventValues.ramp.startValue = automationInfo.minValue;
        blockEvents[1].eventValues.ramp.endValue = automationInfo.maxValue;
        require(schedule(plugin, blockEvents, 2) == noErr,
                "repeated automation schedule");
        require(midi(plugin, 0x90, 48 + block, 90, 2) == noErr,
                "repeated MIDI note-on dispatch");
        require(midi(plugin, 0x80, 48 + block, 0, 40) == noErr,
                "repeated MIDI note-off dispatch");
        left.fill(1.0f);
        right.fill(1.0f);
        timestamp.mSampleTime = static_cast<Float64>((block + 1) * frames);
        require(render(plugin, &renderFlags, &timestamp, 0, frames, &output.list) == noErr,
                "repeated stereo render selector status");
        for (const auto sample : left)
            require(sample == 0.0f, "repeated silent left output");
        for (const auto sample : right)
            require(sample == 0.0f, "repeated silent right output");
    }

    auto uninitialize = reinterpret_cast<UnitFn>(plugin->Lookup(kAudioUnitUninitializeSelect));
    require(uninitialize != nullptr && uninitialize(plugin) == noErr, "AudioUnitUninitialize");
    require(plugin->Close(plugin) == noErr, "AudioComponent Close");
    dlclose(module);
    std::cout << "ats_au_artifact_test: component identity, 81 parameters, automation, MIDI, format, and lifecycle ok\n";
}
