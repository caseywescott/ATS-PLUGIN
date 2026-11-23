#include "StandaloneMidiInput.h"

#import <Foundation/Foundation.h>

namespace ats::standalone
{
namespace
{
std::string endpointName(MIDIEndpointRef endpoint)
{
    CFStringRef name = nullptr;
    if (MIDIObjectGetStringProperty(endpoint, kMIDIPropertyDisplayName, &name) != noErr || name == nullptr)
        return "MIDI source";
    // Not ARC: the copied string is released here.
    NSString* text = (__bridge NSString*) name;
    std::string result = text.UTF8String != nullptr ? std::string(text.UTF8String) : std::string("MIDI source");
    CFRelease(name);
    return result;
}

// Length in 32-bit words of a Universal MIDI Packet by message type.
UInt32 umpWords(UInt32 type) noexcept
{
    switch (type)
    {
    case 0x3: case 0x4: case 0x8: case 0x9: case 0xA: return 2;
    case 0xB: case 0xC: return 3;
    case 0x5: case 0xD: case 0xE: case 0xF: return 4;
    default: return 1;
    }
}
} // namespace

std::vector<StandaloneMidiInput::Source> StandaloneMidiInput::sources()
{
    std::vector<Source> list;
    const auto count = MIDIGetNumberOfSources();
    for (ItemCount i = 0; i < count; ++i)
    {
        const auto endpoint = MIDIGetSource(i);
        Source source;
        if (endpoint == 0 || MIDIObjectGetIntegerProperty(endpoint, kMIDIPropertyUniqueID, &source.id) != noErr)
            continue;
        source.name = endpointName(endpoint);
        list.push_back(std::move(source));
    }
    return list;
}

bool StandaloneMidiInput::open(std::string& error)
{
    if (port_ != 0)
        return true;
    // Rescan when sources appear or disappear so a newly plugged keyboard
    // joins an "all sources" selection.
    if (MIDIClientCreateWithBlock(CFSTR("ATS Resynth"), &client_, ^(const MIDINotification* note) {
            if (note != nullptr && note->messageID == kMIDIMsgSetupChanged)
                dispatch_async(dispatch_get_main_queue(), ^{ connect(); });
        }) != noErr)
    {
        client_ = 0;
        error = "CoreMIDI client unavailable";
        return false;
    }
    if (MIDIInputPortCreateWithProtocol(client_, CFSTR("ATS Resynth In"), kMIDIProtocol_1_0, &port_,
                                        ^(const MIDIEventList* list, void*) { receive(list); }) != noErr)
    {
        port_ = 0;
        MIDIClientDispose(client_);
        client_ = 0;
        error = "CoreMIDI input port unavailable";
        return false;
    }
    connect();
    return true;
}

void StandaloneMidiInput::close() noexcept
{
    if (port_ != 0)
    {
        for (const auto endpoint : connected_)
            MIDIPortDisconnectSource(port_, endpoint);
        MIDIPortDispose(port_);
        port_ = 0;
    }
    connected_.clear();
    if (client_ != 0)
    {
        MIDIClientDispose(client_);
        client_ = 0;
    }
}

void StandaloneMidiInput::select(Mode mode, MIDIUniqueID id)
{
    mode_ = mode;
    selectedId_ = mode == Mode::one ? id : 0;
    connect();
    // A held note would otherwise keep transposing after its source is gone.
    // Flagged rather than queued: the queue has one producer, CoreMIDI.
    pendingAllNotesOff_.store(true, std::memory_order_release);
}

void StandaloneMidiInput::connect()
{
    if (port_ == 0)
        return;
    for (const auto endpoint : connected_)
        MIDIPortDisconnectSource(port_, endpoint);
    connected_.clear();
    if (mode_ == Mode::none)
        return;
    const auto count = MIDIGetNumberOfSources();
    for (ItemCount i = 0; i < count; ++i)
    {
        const auto endpoint = MIDIGetSource(i);
        MIDIUniqueID id = 0;
        if (endpoint == 0 || MIDIObjectGetIntegerProperty(endpoint, kMIDIPropertyUniqueID, &id) != noErr)
            continue;
        if (mode_ == Mode::one && id != selectedId_)
            continue;
        if (MIDIPortConnectSource(port_, endpoint, nullptr) == noErr)
            connected_.push_back(endpoint);
    }
}

bool StandaloneMidiInput::decode(std::uint8_t status, std::uint8_t data1, std::uint8_t data2,
                                 MidiEvent& event) noexcept
{
    event = {};
    switch (status & 0xF0)
    {
    case 0x90:
        event.type = data2 > 0 ? MidiEvent::Type::noteOn : MidiEvent::Type::noteOff;
        event.note = data1 & 0x7F;
        event.velocity = data2 & 0x7F;
        return true;
    case 0x80:
        event.type = MidiEvent::Type::noteOff;
        event.note = data1 & 0x7F;
        event.velocity = data2 & 0x7F;
        return true;
    case 0xB0:
        // 120 all sound off, 123 all notes off.
        if (data1 == 120 || data1 == 123)
        {
            event.type = MidiEvent::Type::allNotesOff;
            return true;
        }
        return false;
    default:
        return false;
    }
}

void StandaloneMidiInput::push(const MidiEvent& event) noexcept
{
    const auto head = head_.load(std::memory_order_relaxed);
    if (head - tail_.load(std::memory_order_acquire) >= kCapacity)
    {
        dropped_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    queue_[head & (kCapacity - 1)] = event;
    head_.store(head + 1, std::memory_order_release);
}

std::uint32_t StandaloneMidiInput::drain(MidiEvent* out, std::uint32_t capacity) noexcept
{
    auto tail = tail_.load(std::memory_order_relaxed);
    const auto head = head_.load(std::memory_order_acquire);
    std::uint32_t count = 0;
    if (capacity > 0 && pendingAllNotesOff_.exchange(false, std::memory_order_acq_rel))
    {
        out[count] = {};
        out[count++].type = MidiEvent::Type::allNotesOff;
    }
    while (tail != head && count < capacity)
        out[count++] = queue_[tail++ & (kCapacity - 1)];
    tail_.store(tail, std::memory_order_release);
    return count;
}

void StandaloneMidiInput::receive(const MIDIEventList* list) noexcept
{
    if (list == nullptr)
        return;
    const MIDIEventPacket* packet = &list->packet[0];
    for (UInt32 p = 0; p < list->numPackets; ++p)
    {
        for (UInt32 w = 0; w < packet->wordCount;)
        {
            const auto word = packet->words[w];
            const auto type = word >> 28;
            // UMP type 2: MIDI 1.0 channel voice, one 32-bit word.
            MidiEvent event;
            if (type == 0x2 && decode(static_cast<std::uint8_t>(word >> 16), static_cast<std::uint8_t>(word >> 8),
                                      static_cast<std::uint8_t>(word), event))
                push(event);
            w += umpWords(type);
        }
        packet = MIDIEventPacketNext(packet);
    }
}

} // namespace ats::standalone
