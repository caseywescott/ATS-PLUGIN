#include <AudioToolbox/AudioToolbox.h>

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

    auto uninitialize = plugin->Lookup(kAudioUnitUninitializeSelect);
    require(uninitialize != nullptr && uninitialize(plugin) == noErr, "AudioUnitUninitialize");
    require(plugin->Close(plugin) == noErr, "AudioComponent Close");
    dlclose(module);
    std::cout << "ats_au_artifact_test: component identity, 81 parameters, format, and lifecycle ok\n";
}
