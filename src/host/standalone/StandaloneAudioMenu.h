#pragma once

#include "../../ui/VisageEditor.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ats::standalone
{

// The standalone AUDIO menu without CoreAudio/CoreMIDI types, so the row
// layout and id decoding are testable: output devices then MIDI inputs, the
// current choice of each marked.
struct AudioMenuDevice
{
    std::string uid;
    std::string name;
};

struct AudioMenuMidiSource
{
    std::int32_t id = 0;
    std::string name;
};

enum class MidiMenuMode : std::uint8_t { all, none, one };

struct AudioMenuChoice
{
    enum class Kind : std::uint8_t { outputDevice, midiInput };
    Kind kind = Kind::outputDevice;
    std::string deviceUid;            // outputDevice: "" = system default
    MidiMenuMode midiMode = MidiMenuMode::all;
    std::int32_t midiSourceId = 0;    // midiInput with MidiMenuMode::one
};

namespace audio_menu
{
inline constexpr int kSystemDefault = 1;
inline constexpr int kFirstDevice = 100;  // + device index
inline constexpr int kMidiAll = 1000;
inline constexpr int kMidiNone = 1001;
inline constexpr int kFirstMidiSource = 2000; // + source index
inline constexpr int kMaxEntries = 800;
} // namespace audio_menu

[[nodiscard]] inline std::vector<ui::AudioMenuOption> buildAudioMenu(
    const std::vector<AudioMenuDevice>& devices, const std::string& currentUid, bool deviceRunning,
    const std::vector<AudioMenuMidiSource>& sources, MidiMenuMode midiMode, std::int32_t midiSourceId)
{
    using namespace audio_menu;
    std::vector<ui::AudioMenuOption> rows;
    rows.push_back({ 0, deviceRunning ? "OUTPUT DEVICE" : "OUTPUT DEVICE (not running)", false, false, false });
    rows.push_back({ kSystemDefault, "System default", currentUid.empty(), true, false });
    for (std::size_t i = 0; i < devices.size() && i < kMaxEntries; ++i)
        rows.push_back({ kFirstDevice + static_cast<int>(i), devices[i].name,
                         !currentUid.empty() && devices[i].uid == currentUid, true, false });
    rows.push_back({ 0, "MIDI INPUT", false, false, true });
    rows.push_back({ kMidiAll, "All sources", midiMode == MidiMenuMode::all, true, false });
    rows.push_back({ kMidiNone, "None", midiMode == MidiMenuMode::none, true, false });
    for (std::size_t i = 0; i < sources.size() && i < kMaxEntries; ++i)
        rows.push_back({ kFirstMidiSource + static_cast<int>(i), sources[i].name,
                         midiMode == MidiMenuMode::one && sources[i].id == midiSourceId, true, false });
    if (sources.empty())
        rows.push_back({ 0, "(no MIDI sources)", false, false, false });
    return rows;
}

// Decodes a picked id against the lists the menu was built from.
[[nodiscard]] inline std::optional<AudioMenuChoice> resolveAudioMenuChoice(
    int id, const std::vector<AudioMenuDevice>& devices, const std::vector<AudioMenuMidiSource>& sources)
{
    using namespace audio_menu;
    AudioMenuChoice choice;
    if (id == kSystemDefault)
        return choice;
    if (id >= kFirstDevice && id < kFirstDevice + kMaxEntries)
    {
        const auto index = static_cast<std::size_t>(id - kFirstDevice);
        if (index >= devices.size())
            return std::nullopt;
        choice.deviceUid = devices[index].uid;
        return choice;
    }
    choice.kind = AudioMenuChoice::Kind::midiInput;
    if (id == kMidiAll)
        return choice;
    if (id == kMidiNone)
    {
        choice.midiMode = MidiMenuMode::none;
        return choice;
    }
    if (id >= kFirstMidiSource && id < kFirstMidiSource + kMaxEntries)
    {
        const auto index = static_cast<std::size_t>(id - kFirstMidiSource);
        if (index >= sources.size())
            return std::nullopt;
        choice.midiMode = MidiMenuMode::one;
        choice.midiSourceId = sources[index].id;
        return choice;
    }
    return std::nullopt;
}

} // namespace ats::standalone
