#pragma once

#include "../../core/AtsPrimitives.h"

#include <CoreMIDI/CoreMIDI.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace ats::standalone
{

// CoreMIDI input for the standalone app (the MIDI half of JUCE's
// StandalonePluginHolder). CoreMIDI's thread decodes note on/off and
// all-notes/all-sound-off into a single-producer/single-consumer queue that
// the audio callback drains into HostAdapter::process(), the same path the
// VST3 wrapper's event input takes. Nothing on the audio side allocates.
class StandaloneMidiInput
{
public:
    struct Source
    {
        MIDIUniqueID id = 0;
        std::string name;
    };

    // Selection: every source, none, or one source by its unique id.
    enum class Mode : std::uint8_t { all, none, one };

    StandaloneMidiInput() = default;
    ~StandaloneMidiInput() { close(); }
    StandaloneMidiInput(const StandaloneMidiInput&) = delete;
    StandaloneMidiInput& operator=(const StandaloneMidiInput&) = delete;

    [[nodiscard]] static std::vector<Source> sources();

    // Creates the client and input port and connects the current selection.
    bool open(std::string& error);
    void close() noexcept;
    [[nodiscard]] bool isOpen() const noexcept { return port_ != 0; }

    // Main thread. Reconnects the port; also rescans after devices change.
    void select(Mode mode, MIDIUniqueID id = 0);
    [[nodiscard]] Mode mode() const noexcept { return mode_; }
    [[nodiscard]] MIDIUniqueID selectedId() const noexcept { return selectedId_; }

    // Audio thread: moves up to `capacity` queued events into `out`.
    std::uint32_t drain(MidiEvent* out, std::uint32_t capacity) noexcept;

    // Decodes one MIDI 1.0 channel-voice message; false if it is not one the
    // host uses. Public for tests.
    static bool decode(std::uint8_t status, std::uint8_t data1, std::uint8_t data2, MidiEvent& event) noexcept;

    // Producer side (CoreMIDI thread); public for tests.
    void push(const MidiEvent& event) noexcept;
    [[nodiscard]] std::uint64_t dropped() const noexcept { return dropped_.load(std::memory_order_relaxed); }

private:
    void connect();
    void receive(const MIDIEventList* list) noexcept;

    static constexpr std::uint32_t kCapacity = 256; // power of two
    std::array<MidiEvent, kCapacity> queue_ {};
    std::atomic<std::uint32_t> head_ { 0 }; // written by the producer
    std::atomic<std::uint32_t> tail_ { 0 }; // written by the consumer
    std::atomic<std::uint64_t> dropped_ { 0 };
    std::atomic<bool> pendingAllNotesOff_ { false };

    MIDIClientRef client_ = 0;
    MIDIPortRef port_ = 0;
    std::vector<MIDIEndpointRef> connected_;
    Mode mode_ = Mode::all;
    MIDIUniqueID selectedId_ = 0;
};

} // namespace ats::standalone
