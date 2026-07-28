#pragma once

#include "../core/MidiNoteRouter.h"
#include "../core/ParameterRegistry.h"
#include "../core/PlaybackBuffers.h"

#include <array>
#include <atomic>
#include <cstdint>

namespace ats::engine
{

enum class PlaybackLayer : std::uint8_t { partials, residual, combined, smear };
enum class PreviewMode : std::uint8_t { both, partialsOnly, residualOnly, smearOnly };

struct PlaybackConfig
{
    double sampleRate = 0.0;
    std::uint32_t maximumBlockSize = 0;
    std::uint32_t outputChannels = 0;
};

class PlaybackEngine
{
public:
    static constexpr std::size_t kLayerCount = 4;

    bool prepare(const PlaybackConfig& config);
    void release() noexcept;

    [[nodiscard]] bool prepared() const noexcept { return prepared_.load(std::memory_order_acquire); }
    [[nodiscard]] const PlaybackConfig& config() const noexcept { return config_; }

    [[nodiscard]] std::int32_t tryAcquireWriteSlot(PlaybackLayer layer) const noexcept;
    [[nodiscard]] AudioBufferView writeView(PlaybackLayer layer, std::int32_t slot) noexcept;
    bool publish(PlaybackLayer layer, std::int32_t slot, JobGeneration generation) noexcept;

    void setPreviewMode(PreviewMode mode) noexcept
    { previewMode_.store(mode, std::memory_order_release); }
    [[nodiscard]] PreviewMode previewMode() const noexcept
    { return previewMode_.load(std::memory_order_acquire); }

    void setLayerMix(bool enabled) noexcept
    { layerMix_.store(enabled, std::memory_order_release); }
    [[nodiscard]] bool layerMix() const noexcept
    { return layerMix_.load(std::memory_order_acquire); }

    // This is the only audio-thread entry point. It reads immutable published
    // layers and atomic/plain parameter values; it does not allocate, lock,
    // access files, dispatch UI work, or join workers.
    bool process(AudioBufferView output,
                 MidiEventList midiEvents,
                 const ParameterStore& parameters) noexcept;

    [[nodiscard]] int semitoneOffset() const noexcept { return midi_.semitoneOffset(); }
    [[nodiscard]] JobGeneration activeGeneration(PlaybackLayer layer) const noexcept;

private:
    [[nodiscard]] static constexpr std::size_t index(PlaybackLayer layer) noexcept
    { return static_cast<std::size_t>(layer); }

    PlaybackConfig config_{};
    std::array<PlaybackBufferPool, kLayerCount> layers_;
    MidiNoteRouter midi_;
    OnePoleSmoother partialGain_;
    OnePoleSmoother residualGain_;
    OnePoleSmoother outputTrim_;
    std::atomic<PreviewMode> previewMode_ { PreviewMode::both };
    std::atomic<bool> layerMix_ { false };
    std::atomic<bool> prepared_ { false };
};

} // namespace ats::engine
