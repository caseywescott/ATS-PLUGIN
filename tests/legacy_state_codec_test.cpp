#include "LegacyStateCodec.h"

#include <cassert>
#include <iostream>

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
    std::cout << "legacy_state_codec_test: ATSP v2-v17 payload round-trip ok\n";
}
