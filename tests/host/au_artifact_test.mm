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
    auto initialize = plugin->Lookup(kAudioUnitInitializeSelect);
    require(initialize != nullptr && initialize(plugin) == noErr, "AudioUnitInitialize");

    UInt32 count = 0;
    UInt32 size = sizeof(count);
    auto getPropertyInfo = plugin->Lookup(kAudioUnitGetPropertyInfoSelect);
    require(getPropertyInfo != nullptr &&
                getPropertyInfo(plugin, kAudioUnitProperty_ParameterList,
                                kAudioUnitScope_Global, 0, &size, nullptr) == noErr,
            "parameter-list property info");
    require(size == 81u * sizeof(AudioUnitParameterID), "81 parameter contract");
    std::array<AudioUnitParameterID, 81> ids {};
    auto getProperty = plugin->Lookup(kAudioUnitGetPropertySelect);
    require(getProperty != nullptr &&
                getProperty(plugin, kAudioUnitProperty_ParameterList,
                            kAudioUnitScope_Global, 0, ids.data(), &size) == noErr,
            "parameter-list property");
    count = size / sizeof(AudioUnitParameterID);
    require(count == 81, "81 parameter list entries");
    auto setParameter = plugin->Lookup(kAudioUnitSetParameterSelect);
    require(setParameter != nullptr, "parameter setter");
    require(setParameter(plugin, ids[0], kAudioUnitScope_Global, 0, 0.0f, 0) == noErr,
            "parameter write");

    AudioStreamBasicDescription format {};
    format.mSampleRate = 48000.0;
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags = kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved;
    format.mBytesPerPacket = sizeof(float);
    format.mFramesPerPacket = 1;
    format.mBytesPerFrame = sizeof(float);
    format.mChannelsPerFrame = 2;
    format.mBitsPerChannel = 32;
    auto setProperty = plugin->Lookup(kAudioUnitSetPropertySelect);
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
    require(CFGetTypeID(state) == CFDataGetTypeID(), "ATSP class-info data");
    require(ats::AtsStateCodec::decodeHeader(
                CFDataGetBytePtr(static_cast<CFDataRef>(state)),
                static_cast<std::size_t>(CFDataGetLength(static_cast<CFDataRef>(state))))
                 .has_value(),
            "ATSP class-info header");
    auto setClassInfo = plugin->Lookup(kAudioUnitSetPropertySelect);
    require(setClassInfo != nullptr &&
                setClassInfo(plugin, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0,
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
    auto process = reinterpret_cast<AudioUnitProcessMultipleProc>(
        plugin->Lookup(kAudioUnitProcessMultipleSelect));
    AudioBufferList* outputs[] = { &output.list };
    AudioTimeStamp timestamp {};
    // A direct factory instance has no component-manager bus metadata for
    // AUBase's redundant render-argument check. The selector still enters
    // the real ProcessMultipleBufferLists override below.
    AudioUnitRenderActionFlags processFlags = kAudioUnitRenderAction_DoNotCheckRenderArgs;
    require(process != nullptr, "stereo process selector");
    const auto processStatus = process(plugin, &processFlags, &timestamp, frames, 0, nullptr, 1, outputs);
    require(processStatus == noErr, "stereo process selector");
    for (const auto sample : left)
        require(sample == 0.0f, "silent left output without playback");
    for (const auto sample : right)
        require(sample == 0.0f, "silent right output without playback");

    auto uninitialize = plugin->Lookup(kAudioUnitUninitializeSelect);
    require(uninitialize != nullptr && uninitialize(plugin) == noErr, "AudioUnitUninitialize");
    require(plugin->Close(plugin) == noErr, "AudioComponent Close");
    dlclose(module);
    std::cout << "ats_au_artifact_test: component identity, 81 parameters, format, and lifecycle ok\n";
}
