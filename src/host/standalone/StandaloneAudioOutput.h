#pragma once

#include "../../host/HostAdapter.h"

#include <AudioToolbox/AudioToolbox.h>

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace ats::standalone
{

class StandaloneMidiInput;

struct AudioOutputDevice
{
    AudioDeviceID id = 0;
    std::string uid;   // stable across launches (saved in preferences)
    std::string name;
};

// Minimal CoreAudio output engine for the standalone app (the role JUCE's
// StandalonePluginHolder played): prepares the host at the device rate and
// renders it from the HAL output callback, passing any queued MIDI input.
// An empty device UID follows the system default output; otherwise the
// named device is opened through AUHAL. process() is the RT-safe host entry
// point, so nothing here allocates on the audio thread.
class StandaloneAudioOutput
{
public:
    StandaloneAudioOutput() = default;
    ~StandaloneAudioOutput() { stop(); }
    StandaloneAudioOutput(const StandaloneAudioOutput&) = delete;
    StandaloneAudioOutput& operator=(const StandaloneAudioOutput&) = delete;

    // silent: run the real device callback but deliver zeros to the device —
    // used by automated runs so they exercise live audio without sound.
    bool start(host::HostAdapter& host, bool silent, std::string& error,
               const std::string& deviceUid = {});
    void stop() noexcept;

    // Output-capable devices, in CoreAudio's order.
    [[nodiscard]] static std::vector<AudioOutputDevice> outputDevices();
    // The UID start() was given ("" = system default).
    [[nodiscard]] const std::string& deviceUid() const noexcept { return deviceUid_; }
    // Set before start(); drained on the audio thread.
    void setMidiInput(StandaloneMidiInput* midi) noexcept { midi_ = midi; }

    [[nodiscard]] bool running() const noexcept { return unit_ != nullptr; }
    [[nodiscard]] double sampleRate() const noexcept { return sampleRate_; }
    [[nodiscard]] std::uint64_t callbacks() const noexcept { return callbacks_.load(std::memory_order_relaxed); }
    [[nodiscard]] float peak() const noexcept { return peak_.load(std::memory_order_relaxed); }

private:
    static OSStatus render(void* context, AudioUnitRenderActionFlags* flags,
                           const AudioTimeStamp* timestamp, UInt32 bus, UInt32 frames,
                           AudioBufferList* buffers);

    AudioUnit unit_ = nullptr;
    host::HostAdapter* host_ = nullptr;
    StandaloneMidiInput* midi_ = nullptr;
    std::string deviceUid_;
    bool silent_ = false;
    double sampleRate_ = 0.0;
    std::atomic<std::uint64_t> callbacks_ { 0 };
    std::atomic<float> peak_ { 0.0f };
};

} // namespace ats::standalone
