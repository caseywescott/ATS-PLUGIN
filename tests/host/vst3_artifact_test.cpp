#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "public.sdk/source/common/memorystream.h"

#include <dlfcn.h>

#include <array>
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>

namespace
{
using GetFactoryFunction = Steinberg::IPluginFactory* (*)();

void require(bool value, const char* expression)
{
    if (!value)
    {
        std::cerr << "VST3 artifact check failed: " << expression << '\n';
        std::abort();
    }
}
}

int main(int argc, char** argv)
{
    require(argc == 2, "argv[1] contains the VST3 module path");
    void* module = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    require(module != nullptr, "dlopen(module)");

    const auto getFactory = reinterpret_cast<GetFactoryFunction>(dlsym(module, "GetPluginFactory"));
    require(getFactory != nullptr, "GetPluginFactory export");
    auto* factory = getFactory();
    require(factory != nullptr, "factory");

    Steinberg::PFactoryInfo factoryInfo {};
    require(factory->getFactoryInfo(&factoryInfo) == Steinberg::kResultTrue,
            "factory metadata");
    require(std::strcmp(factoryInfo.vendor, "ATSPlugin") == 0, "vendor");
    require(factory->countClasses() == 2, "processor/controller class count");

    Steinberg::IPluginFactory2* factory2 = nullptr;
    require(factory->queryInterface(Steinberg::IPluginFactory2::iid,
                                    reinterpret_cast<void**>(&factory2)) == Steinberg::kResultTrue,
            "factory version 2 interface");

    Steinberg::PClassInfo2 processorInfo {};
    Steinberg::PClassInfo2 controllerInfo {};
    require(factory2->getClassInfo2(0, &processorInfo) == Steinberg::kResultTrue,
            "processor class metadata");
    require(factory2->getClassInfo2(1, &controllerInfo) == Steinberg::kResultTrue,
            "controller class metadata");
    if (std::strcmp(processorInfo.category, "Audio Module Class") != 0)
        std::swap(processorInfo, controllerInfo);
    require(std::strcmp(processorInfo.name, "ATS Resynth") == 0, "processor name");
    require(std::strstr(processorInfo.subCategories, "Fx") != nullptr, "effect category");
    require(std::strcmp(controllerInfo.name, "ATS Resynth Controller") == 0, "controller name");
    factory2->release();

    Steinberg::Vst::IEditController* controller = nullptr;
    require(factory->createInstance(controllerInfo.cid, Steinberg::Vst::IEditController::iid,
                                    reinterpret_cast<void**>(&controller)) == Steinberg::kResultTrue,
            "controller instance");
    require(controller->initialize(nullptr) == Steinberg::kResultTrue, "controller initialize");
    require(controller->getParameterCount() == 81, "81 parameter contract");
    for (Steinberg::Vst::ParamID id = 1; id <= 81; ++id)
    {
        require(controller->setParamNormalized(id, 0.5) == Steinberg::kResultTrue,
                "controller parameter write");
        require(std::abs(controller->getParamNormalized(id) - 0.5) < 1.0e-6,
                "controller parameter read");
    }
    controller->terminate();
    controller->release();

    Steinberg::Vst::IComponent* component = nullptr;
    require(factory->createInstance(processorInfo.cid, Steinberg::Vst::IComponent::iid,
                                    reinterpret_cast<void**>(&component)) == Steinberg::kResultTrue,
            "processor instance");
    require(component->initialize(nullptr) == Steinberg::kResultTrue, "processor initialize");

    Steinberg::Vst::IAudioProcessor* processor = nullptr;
    require(component->queryInterface(Steinberg::Vst::IAudioProcessor::iid,
                                      reinterpret_cast<void**>(&processor)) == Steinberg::kResultTrue,
            "audio processor interface");
    Steinberg::Vst::SpeakerArrangement outputArrangement[] = { Steinberg::Vst::SpeakerArr::kStereo };
    require(processor->setBusArrangements(nullptr, 0, outputArrangement, 1) == Steinberg::kResultTrue,
            "no-input stereo arrangement");
    Steinberg::Vst::ProcessSetup setup { Steinberg::Vst::kRealtime,
                                        Steinberg::Vst::kSample32,
                                        64,
                                        48000.0 };
    require(processor->setupProcessing(setup) == Steinberg::kResultTrue, "setup processing");
    require(component->setActive(true) == Steinberg::kResultTrue, "activate");
    require(processor->setProcessing(true) == Steinberg::kResultTrue, "start processing");

    std::array<float, 64> left;
    std::array<float, 64> right;
    left.fill(1.0f);
    right.fill(1.0f);
    float* channels[] = { left.data(), right.data() };
    Steinberg::Vst::AudioBusBuffers output;
    output.numChannels = 2;
    output.channelBuffers32 = channels;
    Steinberg::Vst::ProcessData data {};
    data.numSamples = 64;
    data.numOutputs = 1;
    data.outputs = &output;
    data.symbolicSampleSize = Steinberg::Vst::kSample32;
    require(processor->process(data) == Steinberg::kResultTrue, "process");
    for (float sample : left) require(sample == 0.0f, "left output silence");
    for (float sample : right) require(sample == 0.0f, "right output silence");

    Steinberg::MemoryStream state;
    require(component->getState(&state) == Steinberg::kResultTrue && state.getSize() > 0,
            "processor state write");
    Steinberg::int64 rewoundPosition = -1;
    require(state.seek(0, Steinberg::IBStream::kIBSeekSet, &rewoundPosition)
                == Steinberg::kResultTrue
                && rewoundPosition == 0,
            "processor state rewind");
    require(component->setState(&state) == Steinberg::kResultTrue,
            "processor state reopen");

    processor->setProcessing(false);
    component->setActive(false);
    component->terminate();
    processor->release();
    component->release();
    dlclose(module);
    std::cout << "ats_vst3_artifact_test: identity, 81 parameters, bus, lifecycle, and process ok\n";
}
