#pragma once

#include "../../host/HostAdapter.h"

#include <AudioToolbox/AudioToolbox.h>

#include <atomic>
#include <cstdint>
#include <string>

namespace ats::standalone
{

// Minimal CoreAudio default-output engine for the standalone app (the role
// JUCE's StandalonePluginHolder played): prepares the host at the device rate
// and renders it from the HAL output callback. process() is the RT-safe host
// entry point, so nothing here allocates on the audio thread.
class StandaloneAudioOutput
{
public:
    StandaloneAudioOutput() = default;
    ~StandaloneAudioOutput() { stop(); }
    StandaloneAudioOutput(const StandaloneAudioOutput&) = delete;
    StandaloneAudioOutput& operator=(const StandaloneAudioOutput&) = delete;

    // silent: run the real device callback but deliver zeros to the device —
    // used by automated runs so they exercise live audio without sound.
    bool start(host::HostAdapter& host, bool silent, std::string& error);
    void stop() noexcept;

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
    bool silent_ = false;
    double sampleRate_ = 0.0;
    std::atomic<std::uint64_t> callbacks_ { 0 };
    std::atomic<float> peak_ { 0.0f };
};

} // namespace ats::standalone
