#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "base/source/fobject.h"
#include "public.sdk/source/common/memorystream.h"

#include <dlfcn.h>

#include <array>
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <vector>

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

class TestParameterQueue final : public Steinberg::FObject,
                                 public Steinberg::Vst::IParamValueQueue
{
public:
    explicit TestParameterQueue(Steinberg::Vst::ParamID parameterId)
        : parameterId_(parameterId) {}

    OBJ_METHODS(TestParameterQueue, Steinberg::FObject)
    DEFINE_INTERFACES
        DEF_INTERFACE(Steinberg::Vst::IParamValueQueue)
    END_DEFINE_INTERFACES(Steinberg::FObject)
    REFCOUNT_METHODS(Steinberg::FObject)

    Steinberg::Vst::ParamID PLUGIN_API getParameterId() override { return parameterId_; }
    Steinberg::int32 PLUGIN_API getPointCount() override
    { return static_cast<Steinberg::int32>(points_.size()); }

    Steinberg::tresult PLUGIN_API getPoint(Steinberg::int32 index,
                                           Steinberg::int32& sampleOffset,
                                           Steinberg::Vst::ParamValue& value) override
    {
        if (index < 0 || static_cast<std::size_t>(index) >= points_.size())
            return Steinberg::kResultFalse;
        sampleOffset = points_[static_cast<std::size_t>(index)].first;
        value = points_[static_cast<std::size_t>(index)].second;
        return Steinberg::kResultTrue;
    }

    Steinberg::tresult PLUGIN_API addPoint(Steinberg::int32 sampleOffset,
                                           Steinberg::Vst::ParamValue value,
                                           Steinberg::int32& index) override
    {
        index = static_cast<Steinberg::int32>(points_.size());
        points_.emplace_back(sampleOffset, value);
        return Steinberg::kResultTrue;
    }

private:
    Steinberg::Vst::ParamID parameterId_;
    std::vector<std::pair<Steinberg::int32, Steinberg::Vst::ParamValue>> points_;
};

class TestParameterChanges final : public Steinberg::FObject,
                                   public Steinberg::Vst::IParameterChanges
{
public:
    OBJ_METHODS(TestParameterChanges, Steinberg::FObject)
    DEFINE_INTERFACES
        DEF_INTERFACE(Steinberg::Vst::IParameterChanges)
    END_DEFINE_INTERFACES(Steinberg::FObject)
    REFCOUNT_METHODS(Steinberg::FObject)

    Steinberg::int32 PLUGIN_API getParameterCount() override
    { return static_cast<Steinberg::int32>(queues_.size()); }

    Steinberg::Vst::IParamValueQueue* PLUGIN_API getParameterData(Steinberg::int32 index) override
    {
        if (index < 0 || static_cast<std::size_t>(index) >= queues_.size())
            return nullptr;
        return &queues_[static_cast<std::size_t>(index)];
    }

    Steinberg::Vst::IParamValueQueue* PLUGIN_API addParameterData(
        const Steinberg::Vst::ParamID& id, Steinberg::int32& index) override
    {
        index = static_cast<Steinberg::int32>(queues_.size());
        queues_.emplace_back(id);
        return &queues_.back();
    }

private:
    std::vector<TestParameterQueue> queues_;
};

class TestEventList final : public Steinberg::FObject, public Steinberg::Vst::IEventList
{
public:
    OBJ_METHODS(TestEventList, Steinberg::FObject)
    DEFINE_INTERFACES
        DEF_INTERFACE(Steinberg::Vst::IEventList)
    END_DEFINE_INTERFACES(Steinberg::FObject)
    REFCOUNT_METHODS(Steinberg::FObject)

    Steinberg::int32 PLUGIN_API getEventCount() override
    { return static_cast<Steinberg::int32>(events_.size()); }

    Steinberg::tresult PLUGIN_API getEvent(Steinberg::int32 index,
                                           Steinberg::Vst::Event& event) override
    {
        if (index < 0 || static_cast<std::size_t>(index) >= events_.size())
            return Steinberg::kResultFalse;
        event = events_[static_cast<std::size_t>(index)];
        return Steinberg::kResultTrue;
    }

    Steinberg::tresult PLUGIN_API addEvent(Steinberg::Vst::Event& event) override
    {
        events_.push_back(event);
        return Steinberg::kResultTrue;
    }

private:
    std::vector<Steinberg::Vst::Event> events_;
};
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

    TestParameterChanges parameterChanges;
    for (Steinberg::Vst::ParamID id = 1; id <= 81; ++id)
    {
        Steinberg::int32 parameterQueueIndex = -1;
        auto* parameterQueue = parameterChanges.addParameterData(id, parameterQueueIndex);
        require(parameterQueue != nullptr && parameterQueueIndex == id - 1,
                "automation queue allocation");
        Steinberg::int32 pointIndex = -1;
        require(parameterQueue->addPoint(0, 0.25, pointIndex) == Steinberg::kResultTrue,
                "automation start point");
        require(parameterQueue->addPoint(32, 0.75, pointIndex) == Steinberg::kResultTrue,
                "automation end point");
    }
    require(parameterChanges.getParameterCount() == 81, "81 automation queues");

    TestEventList events;
    Steinberg::Vst::Event noteOn {};
    noteOn.type = Steinberg::Vst::Event::kNoteOnEvent;
    noteOn.sampleOffset = 7;
    noteOn.noteOn.pitch = 60;
    noteOn.noteOn.velocity = 0.8f;
    require(events.addEvent(noteOn) == Steinberg::kResultTrue, "MIDI note-on dispatch");
    Steinberg::Vst::Event noteOff {};
    noteOff.type = Steinberg::Vst::Event::kNoteOffEvent;
    noteOff.sampleOffset = 48;
    noteOff.noteOff.pitch = 60;
    require(events.addEvent(noteOff) == Steinberg::kResultTrue, "MIDI note-off dispatch");
    for (int index = 0; index < 300; ++index)
    {
        Steinberg::Vst::Event extra {};
        extra.type = Steinberg::Vst::Event::kNoteOnEvent;
        extra.sampleOffset = index % 64;
        extra.noteOn.pitch = static_cast<Steinberg::int16>(36 + index % 48);
        extra.noteOn.velocity = 0.5f;
        require(events.addEvent(extra) == Steinberg::kResultTrue,
                "bounded MIDI event list construction");
    }

    Steinberg::Vst::ProcessData data {};
    data.numSamples = 64;
    data.numOutputs = 1;
    data.outputs = &output;
    data.symbolicSampleSize = Steinberg::Vst::kSample32;
    data.inputParameterChanges = &parameterChanges;
    data.inputEvents = &events;
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
    std::cout << "ats_vst3_artifact_test: identity, 81 parameters, automation, MIDI, bus, lifecycle, and process ok\n";
}
