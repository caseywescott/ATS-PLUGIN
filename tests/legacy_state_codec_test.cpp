#include "LegacyStateCodec.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>

int main()
{
    ats::LegacyState source;
    source.analysis.type = 4;
    source.synth.partialsGain = 1.75;
    source.frequencyShape.mode = ats::FrequencyShapeMode::Harmonic;
    source.outputTrim = 0.8;
    source.atsModel = { static_cast<std::byte>(0x10), static_cast<std::byte>(0x20) };
    source.morphRecipe = "{\"version\":1}";
    source.morphAmount = 0.5;
    source.morphLaneAmounts[2] = 0.75;
    source.morphLaneLinked[2] = false;
    source.variations[1].hasSnapshot = true;
    source.variations[1].name = "saved";
    source.morphSourcePaths[0] = "/tmp/source-a.wav";
    source.residualStates[0].hasResidual = true;
    source.residualStates[0].bands.push_back({ 2, 3.0, 4.0 });
    source.smearnov.mix = 42.0;
    source.morphSoundData[1] = { static_cast<std::byte>(0x7f) };

    for (std::uint32_t version = 2; version <= 17; ++version)
    {
        const auto bytes = ats::LegacyStateCodec::encode(source, version);
        assert(!bytes.empty());
        const auto decoded = ats::LegacyStateCodec::decode(bytes.data(), bytes.size());
        assert(decoded);
        assert(decoded->synth.partialsGain == source.synth.partialsGain);
        assert(decoded->atsModel == source.atsModel);
        if (version >= 7) assert(decoded->morphLaneLinked[2] == false);
        if (version >= 9) assert(decoded->variations[1].name == "saved");
        if (version >= 17) assert(decoded->morphSoundData[1] == source.morphSoundData[1]);
        auto truncated = bytes;
        truncated.pop_back();
        assert(!ats::LegacyStateCodec::decode(truncated.data(), truncated.size()));
    }

    auto malformed = source;
    malformed.outputTrim = std::numeric_limits<double>::quiet_NaN();
    assert(ats::LegacyStateCodec::encode(malformed).empty());

    auto corrupt = ats::LegacyStateCodec::encode(source);
    // ATSP v17 stores outputTrim after analysis, synthesis, and
    // frequency-shape. Corrupting it must reject the whole state.
    constexpr std::size_t outputTrimOffset = 8 + (5 * 8 + 3 * 4 + 2 * 8 + 3 * 4 + 4 * 8 + 4) +
                                             (7 * 8 + 2) + (4 + 2 * 8);
    assert(outputTrimOffset + sizeof(double) <= corrupt.size());
    constexpr std::uint64_t nanBits = 0x7ff8000000000000ULL;
    for (unsigned byte = 0; byte < sizeof(nanBits); ++byte)
        corrupt[outputTrimOffset + byte] = static_cast<std::byte>(nanBits >> (byte * 8));
    assert(!ats::LegacyStateCodec::decode(corrupt.data(), corrupt.size()));
    std::cout << "legacy_state_codec_test: ATSP v2-v17 payload round-trip ok\n";
}
