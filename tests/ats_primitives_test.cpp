#include "AtsPrimitives.h"

#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>

namespace
{
void testAudioBufferAndMidi()
{
    float left[3] = { 1.0f, 2.0f, 3.0f };
    float right[3] = { 4.0f, 5.0f, 6.0f };
    float* channels[] = { left, right };
    ats::AudioBufferView view { channels, 2, 3 };
    assert(view.valid());
    view.add(view, 0.5f);
    assert(left[0] == 1.5f && left[2] == 4.5f);
    assert(right[1] == 7.5f);

    const ats::MidiEvent events[] = {
        { 0, ats::MidiEvent::Type::noteOn, 60, 100 },
        { 127, ats::MidiEvent::Type::noteOff, 60, 0 }
    };
    ats::MidiEventList list { events, 2 };
    assert(list.begin()->sampleOffset == 0);
    assert((list.end() - list.begin()) == 2);
}

void testEndianStableBytes()
{
    ats::ByteWriter writer;
    writer.writeU16(0x1234);
    writer.writeU32(0x78563412);
    writer.writeU64(0x1122334455667788ULL);
    writer.writeI32(-7);
    writer.writeF32(1.25f);
    const auto& bytes = writer.bytes();
    const std::uint8_t expected[] = {
        0x34, 0x12, 0x12, 0x34, 0x56, 0x78,
        0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11,
        0xf9, 0xff, 0xff, 0xff, 0x00, 0x00, 0xa0, 0x3f
    };
    assert(bytes.size() == sizeof(expected));
    for (std::size_t i = 0; i < bytes.size(); ++i)
        assert(std::to_integer<std::uint8_t>(bytes[i]) == expected[i]);

    ats::ByteReader reader(bytes.data(), bytes.size());
    std::uint16_t u16 = 0; std::uint32_t u32 = 0; std::uint64_t u64 = 0;
    std::int32_t i32 = 0; float f32 = 0.0f;
    assert(reader.readU16(u16) && reader.readU32(u32) && reader.readU64(u64));
    assert(reader.readI32(i32) && reader.readF32(f32));
    assert(u16 == 0x1234 && u32 == 0x78563412 && u64 == 0x1122334455667788ULL);
    assert(i32 == -7 && f32 == 1.25f && reader.ok() && reader.remaining() == 0);

    std::uint32_t truncated = 0;
    ats::ByteReader bad(bytes.data(), 2);
    assert(!bad.readU32(truncated) && !bad.ok());
}

void testGenerationCancellationAndSmoothing()
{
    ats::GenerationSource source;
    const auto first = source.next();
    const auto second = source.next();
    assert(first.value == 1 && second.value == 2 && source.current() == second);

    ats::CancellationToken token;
    assert(!token.isCancelled());
    token.cancel();
    assert(token.isCancelled());

    ats::OnePoleSmoother smoother;
    smoother.prepare(1000.0, 20.0);
    smoother.reset(0.0f);
    smoother.setTarget(1.0f);
    const float firstSample = smoother.process();
    assert(std::fabs(firstSample - (1.0f - std::exp(-1.0f / 20.0f))) < 1.0e-6f);
    for (int i = 0; i < 200; ++i) (void) smoother.process();
    assert(std::fabs(smoother.current() - 1.0f) < 1.0e-4f);
}
}

int main()
{
    testAudioBufferAndMidi();
    testEndianStableBytes();
    testGenerationCancellationAndSmoothing();
    std::cout << "ats_primitives_test: ok\n";
    return 0;
}
