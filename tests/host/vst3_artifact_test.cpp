#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "base/source/fobject.h"
#include "public.sdk/source/common/memorystream.h"

#include <dlfcn.h>

#include <array>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
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

class TestAttributes final : public Steinberg::FObject, public Steinberg::Vst::IAttributeList
{
public:
    OBJ_METHODS(TestAttributes, Steinberg::FObject)
    DEFINE_INTERFACES
        DEF_INTERFACE(Steinberg::Vst::IAttributeList)
    END_DEFINE_INTERFACES(Steinberg::FObject)
    REFCOUNT_METHODS(Steinberg::FObject)

    Steinberg::tresult PLUGIN_API setInt(const char* id, Steinberg::int64 value) override
    {
        if (std::strcmp(id, "slot") == 0) slot_ = value;
        else if (std::strcmp(id, "kind") == 0) kind_ = value;
        else if (std::strcmp(id, "layer") == 0) layer_ = value;
        else if (std::strcmp(id, "start") == 0) start_ = value;
        else return Steinberg::kResultFalse;
        return Steinberg::kResultTrue;
    }
    Steinberg::tresult PLUGIN_API getInt(const char* id, Steinberg::int64& value) override
    {
        if (std::strcmp(id, "slot") == 0) value = slot_;
        else if (std::strcmp(id, "kind") == 0) value = kind_;
        else if (std::strcmp(id, "layer") == 0) value = layer_;
        else if (std::strcmp(id, "start") == 0) value = start_;
        else return Steinberg::kResultFalse;
        return Steinberg::kResultTrue;
    }
    Steinberg::tresult PLUGIN_API setFloat(const char*, double) override { return Steinberg::kResultFalse; }
    Steinberg::tresult PLUGIN_API getFloat(const char*, double&) override { return Steinberg::kResultFalse; }
    Steinberg::tresult PLUGIN_API setString(const char*, const Steinberg::Vst::TChar*) override
    { return Steinberg::kResultFalse; }
    Steinberg::tresult PLUGIN_API getString(const char*, Steinberg::Vst::TChar*, Steinberg::uint32) override
    { return Steinberg::kResultFalse; }
    Steinberg::tresult PLUGIN_API setBinary(const char* id, const void* data,
                                            Steinberg::uint32 size) override
    {
        if (data == nullptr || size == 0)
            return Steinberg::kResultFalse;
        if (std::strcmp(id, "path") == 0)
            path_.assign(static_cast<const char*>(data), size);
        else if (std::strcmp(id, "values") == 0)
            values_.assign(static_cast<const std::byte*>(data), static_cast<const std::byte*>(data) + size);
        else
            return Steinberg::kResultFalse;
        return Steinberg::kResultTrue;
    }
    Steinberg::tresult PLUGIN_API getBinary(const char* id, const void*& data,
                                            Steinberg::uint32& size) override
    {
        if (std::strcmp(id, "path") == 0 && !path_.empty())
        {
            data = path_.data();
            size = static_cast<Steinberg::uint32>(path_.size());
        }
        else if (std::strcmp(id, "values") == 0 && !values_.empty())
        {
            data = values_.data();
            size = static_cast<Steinberg::uint32>(values_.size());
        }
        else
            return Steinberg::kResultFalse;
        return Steinberg::kResultTrue;
    }

private:
    Steinberg::int64 slot_ = -1;
    Steinberg::int64 kind_ = -1;
    Steinberg::int64 layer_ = -1;
    Steinberg::int64 start_ = 0;
    std::string path_;
    std::vector<std::byte> values_;
};

class TestMessage final : public Steinberg::FObject, public Steinberg::Vst::IMessage
{
public:
    explicit TestMessage(const char* id) : id_(id) {}

    OBJ_METHODS(TestMessage, Steinberg::FObject)
    DEFINE_INTERFACES
        DEF_INTERFACE(Steinberg::Vst::IMessage)
    END_DEFINE_INTERFACES(Steinberg::FObject)
    REFCOUNT_METHODS(Steinberg::FObject)

    Steinberg::FIDString PLUGIN_API getMessageID() override { return id_.c_str(); }
    void PLUGIN_API setMessageID(Steinberg::FIDString id) override { id_ = id == nullptr ? "" : id; }
    Steinberg::Vst::IAttributeList* PLUGIN_API getAttributes() override { return &attributes_; }

private:
    std::string id_;
    TestAttributes attributes_;
};

void u16(std::ofstream& stream, std::uint16_t value)
{
    stream.put(static_cast<char>(value));
    stream.put(static_cast<char>(value >> 8));
}

void u32(std::ofstream& stream, std::uint32_t value)
{
    u16(stream, static_cast<std::uint16_t>(value));
    u16(stream, static_cast<std::uint16_t>(value >> 16));
}

void writeWave(const std::filesystem::path& path)
{
    constexpr std::uint32_t frames = 32768, sampleRate = 48000, bytes = frames * 2;
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    require(static_cast<bool>(stream), "open VST3 source fixture");
    stream.write("RIFF", 4); u32(stream, 36 + bytes); stream.write("WAVEfmt ", 8); u32(stream, 16);
    u16(stream, 1); u16(stream, 1); u32(stream, sampleRate); u32(stream, sampleRate * 2); u16(stream, 2); u16(stream, 16);
    stream.write("data", 4); u32(stream, bytes);
    for (std::uint32_t index = 0; index < frames; ++index)
        u16(stream, static_cast<std::uint16_t>(std::lround(
            7000.0 * std::sin(2.0 * 3.141592653589793 * 440.0 * index / sampleRate))));
    require(static_cast<bool>(stream), "write VST3 source fixture");
}
}

int main(int argc, char** argv)
{
    require(argc == 2, "argv[1] contains the VST3 module path");
    void* module = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    require(module != nullptr, "dlopen(module)");

#if defined(__APPLE__)
    // The macOS VST3 loader requires these bundle lifecycle exports. Keep the
    // direct artifact test aligned with Steinberg's standalone validator.
    require(dlsym(module, "bundleEntry") != nullptr, "bundleEntry export");
    require(dlsym(module, "bundleExit") != nullptr, "bundleExit export");
#endif

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
    Steinberg::Vst::IComponent* component = nullptr;
    require(factory->createInstance(processorInfo.cid, Steinberg::Vst::IComponent::iid,
                                    reinterpret_cast<void**>(&component)) == Steinberg::kResultTrue,
            "processor instance");
    require(component->initialize(nullptr) == Steinberg::kResultTrue, "processor initialize");

    Steinberg::Vst::IConnectionPoint* controllerConnection = nullptr;
    Steinberg::Vst::IConnectionPoint* processorConnection = nullptr;
    require(controller->queryInterface(Steinberg::Vst::IConnectionPoint::iid,
                                       reinterpret_cast<void**>(&controllerConnection)) == Steinberg::kResultTrue,
            "controller connection point");
    require(component->queryInterface(Steinberg::Vst::IConnectionPoint::iid,
                                      reinterpret_cast<void**>(&processorConnection)) == Steinberg::kResultTrue,
            "processor connection point");
    require(controllerConnection->connect(processorConnection) == Steinberg::kResultTrue,
            "controller connects to processor");
    require(processorConnection->connect(controllerConnection) == Steinberg::kResultTrue,
            "processor connects to controller");

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

    // A controller/editor command reaches the processor connection point.
    // The processor, not the controller, decodes the source, owns the queued
    // analysis, and publishes the audible buffer consumed by process().
    const auto sourcePath = std::filesystem::temp_directory_path() / "ats-vst3-controller-source.wav";
    writeWave(sourcePath);
    TestMessage importMessage("ats.editor.import-source");
    require(importMessage.getAttributes()->setInt("slot", 0) == Steinberg::kResultTrue,
            "import slot attribute");
    require(importMessage.getAttributes()->setInt("kind", 0) == Steinberg::kResultTrue,
            "import kind attribute");
    const auto sourcePathString = sourcePath.u8string();
    require(importMessage.getAttributes()->setBinary("path", sourcePathString.data(),
                                                     static_cast<Steinberg::uint32>(sourcePathString.size())) ==
                Steinberg::kResultTrue,
            "import path attribute");
    require(processorConnection->notify(&importMessage) == Steinberg::kResultTrue,
            "processor accepts editor import");
    TestMessage analyzeMessage("ats.editor.analyze-source");
    require(analyzeMessage.getAttributes()->setInt("slot", 0) == Steinberg::kResultTrue,
            "analysis slot attribute");
    require(processorConnection->notify(&analyzeMessage) == Steinberg::kResultTrue,
            "processor queues editor analysis");

    bool published = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (std::chrono::steady_clock::now() < deadline && !published)
    {
        TestMessage pollMessage("ats.editor.poll-analysis");
        require(processorConnection->notify(&pollMessage) == Steinberg::kResultTrue,
                "processor polls queued analysis");
        left.fill(0.0f);
        right.fill(0.0f);
        data.inputParameterChanges = nullptr;
        data.inputEvents = nullptr;
        require(processor->process(data) == Steinberg::kResultTrue, "process shared editor audio");
        for (const auto sample : left)
            published = published || std::abs(sample) > 1.0e-6f;
        if (!published)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    require(published, "processor publishes controller-imported analysis");

    // Session commands use the same controller-to-processor connection as
    // source import. Verify preview/seek/timeline/stop affect the processor
    // owner rather than a transient editor-side object.
    TestMessage previewMessage("ats.editor.session-preview");
    require(previewMessage.getAttributes()->setInt("slot", 0) == Steinberg::kResultTrue &&
            previewMessage.getAttributes()->setInt("layer", 0) == Steinberg::kResultTrue &&
            previewMessage.getAttributes()->setInt("start", 1) == Steinberg::kResultTrue,
            "session preview attributes");
    require(processorConnection->notify(&previewMessage) == Steinberg::kResultTrue,
            "processor accepts session preview");
    const std::array<float, 1> seekValues { 0.5f };
    TestMessage seekMessage("ats.editor.session-seek-preview");
    require(seekMessage.getAttributes()->setBinary("values", seekValues.data(), sizeof(seekValues)) ==
                Steinberg::kResultTrue &&
            processorConnection->notify(&seekMessage) == Steinberg::kResultTrue,
            "processor accepts session seek");
    const std::array<float, 3> timelineValues { 0.0f, 0.5f, 1.0f };
    TestMessage timelineMessage("ats.editor.session-timeline");
    require(timelineMessage.getAttributes()->setBinary("values", timelineValues.data(), sizeof(timelineValues)) ==
                Steinberg::kResultTrue &&
            processorConnection->notify(&timelineMessage) == Steinberg::kResultTrue,
            "processor accepts session timeline");
    TestMessage stopMessage("ats.editor.session-stop-preview");
    require(processorConnection->notify(&stopMessage) == Steinberg::kResultTrue,
            "processor accepts session stop");
    left.fill(1.0f);
    right.fill(1.0f);
    require(processor->process(data) == Steinberg::kResultTrue, "process stopped session preview");
    for (const auto sample : left) require(sample == 0.0f, "session stop silences left output");
    for (const auto sample : right) require(sample == 0.0f, "session stop silences right output");

    std::error_code ignored;
    std::filesystem::remove(sourcePath, ignored);

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
    require(controllerConnection->disconnect(processorConnection) == Steinberg::kResultTrue,
            "controller disconnects processor");
    require(processorConnection->disconnect(controllerConnection) == Steinberg::kResultTrue,
            "processor disconnects controller");
    controllerConnection->release();
    processorConnection->release();
    component->terminate();
    processor->release();
    component->release();
    controller->terminate();
    controller->release();
    dlclose(module);
    std::cout << "ats_vst3_artifact_test: identity, automation, MIDI, controller/processor ownership, and process ok\n";
}
