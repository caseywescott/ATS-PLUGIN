// Standalone AUDIO menu and MIDI input logic, without devices: menu rows and
// id decoding, MIDI 1.0 decoding, and the SPSC queue the audio callback drains.
#include "../../src/host/standalone/StandaloneAudioMenu.h"
#include "../../src/host/standalone/StandaloneMidiInput.h"

#include <cstdio>
#include <cstdlib>

namespace
{
int failures = 0;
void check(bool condition, const char* what)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}
} // namespace

int main()
{
    using namespace ats;
    using namespace ats::standalone;

    // Menu: headings disabled, current device and MIDI mode marked.
    const std::vector<AudioMenuDevice> devices { { "uid-a", "Built-in" }, { "uid-b", "Interface" } };
    const std::vector<AudioMenuMidiSource> sources { { 11, "Keys" }, { 22, "Pads" } };
    auto rows = buildAudioMenu(devices, "uid-b", true, sources, MidiMenuMode::one, 22);
    check(rows.size() == 9, "row count");
    check(rows[0].id == 0 && !rows[0].enabled, "output heading");
    check(rows[1].id == audio_menu::kSystemDefault && !rows[1].checked, "default unchecked");
    check(rows[3].label == "Interface" && rows[3].checked, "selected device checked");
    check(rows[4].id == 0 && rows[4].breakBefore, "midi heading after a break");
    check(!rows[5].checked && !rows[6].checked, "all/none unchecked");
    check(rows[8].label == "Pads" && rows[8].checked, "selected source checked");
    rows = buildAudioMenu(devices, "", false, {}, MidiMenuMode::all, 0);
    check(rows[0].label == "OUTPUT DEVICE (not running)", "not-running heading");
    check(rows[1].checked, "system default checked");
    check(rows.back().label == "(no MIDI sources)" && !rows.back().enabled, "no sources row");

    // Decoding picks against the lists the menu was built from.
    auto choice = resolveAudioMenuChoice(audio_menu::kFirstDevice + 1, devices, sources);
    check(choice && choice->kind == AudioMenuChoice::Kind::outputDevice && choice->deviceUid == "uid-b",
          "device pick");
    choice = resolveAudioMenuChoice(audio_menu::kSystemDefault, devices, sources);
    check(choice && choice->deviceUid.empty(), "default pick");
    choice = resolveAudioMenuChoice(audio_menu::kFirstMidiSource, devices, sources);
    check(choice && choice->kind == AudioMenuChoice::Kind::midiInput && choice->midiMode == MidiMenuMode::one
              && choice->midiSourceId == 11, "source pick");
    choice = resolveAudioMenuChoice(audio_menu::kMidiNone, devices, sources);
    check(choice && choice->midiMode == MidiMenuMode::none, "none pick");
    check(!resolveAudioMenuChoice(audio_menu::kFirstDevice + 5, devices, sources), "stale device id");
    check(!resolveAudioMenuChoice(-2, devices, sources), "heading id");

    // MIDI 1.0 decoding.
    MidiEvent event;
    check(StandaloneMidiInput::decode(0x93, 64, 100, event) && event.type == MidiEvent::Type::noteOn
              && event.note == 64 && event.velocity == 100, "note on, any channel");
    check(StandaloneMidiInput::decode(0x90, 64, 0, event) && event.type == MidiEvent::Type::noteOff,
          "zero-velocity note on is note off");
    check(StandaloneMidiInput::decode(0x80, 64, 40, event) && event.type == MidiEvent::Type::noteOff, "note off");
    check(StandaloneMidiInput::decode(0xB0, 123, 0, event) && event.type == MidiEvent::Type::allNotesOff,
          "all notes off");
    check(StandaloneMidiInput::decode(0xB5, 120, 0, event) && event.type == MidiEvent::Type::allNotesOff,
          "all sound off");
    check(!StandaloneMidiInput::decode(0xB0, 7, 100, event), "volume CC ignored");
    check(!StandaloneMidiInput::decode(0xE0, 0, 64, event), "pitch bend ignored");

    // Queue: FIFO order, bounded, drops counted; select() queues all-notes-off.
    StandaloneMidiInput midi;
    for (int i = 0; i < 300; ++i)
    {
        MidiEvent e;
        e.type = MidiEvent::Type::noteOn;
        e.note = static_cast<std::uint8_t>(i & 0x7F);
        e.velocity = 1;
        midi.push(e);
    }
    check(midi.dropped() == 44, "overflow dropped");
    std::array<MidiEvent, 64> out {};
    auto n = midi.drain(out.data(), out.size());
    check(n == 64 && out[0].note == 0 && out[63].note == 63, "drain order and capacity");
    std::uint32_t total = n;
    while ((n = midi.drain(out.data(), out.size())) > 0)
        total += n;
    check(total == 256, "drained everything queued");
    midi.select(StandaloneMidiInput::Mode::none);
    n = midi.drain(out.data(), out.size());
    check(n == 1 && out[0].type == MidiEvent::Type::allNotesOff, "selection change releases notes");
    check(midi.drain(out.data(), out.size()) == 0, "all-notes-off sent once");

    if (failures != 0)
        return EXIT_FAILURE;
    std::printf("standalone_audio_midi_test: ok\n");
    return EXIT_SUCCESS;
}
