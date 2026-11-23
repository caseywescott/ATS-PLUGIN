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
    source.morphTimeline = { 0.0f, 0.25f, 0.75f, 1.0f };
    source.timbreJson = "{\"version\":1,\"plan\":[]}";
    source.smearnovOnsets = { 0.1f, 0.5f, 0.9f };
    source.analysis.multiRes = true;
    source.synth.useIfftSynth = true;
    source.timbreBrightnessDb = -7.5;
    source.timbreTiltDbPerOct = 3.25;
    source.timbreNoiseBalance = 0.8;
    source.soundCurves[0] = { 0.0f, 0.0f, 0.5f, 0.25f, 1.0f, 1.0f }; // time warp
    source.soundCurves[1] = { 0.0f, -3.0f, 1.0f, 12.0f };             // pitch
    source.morphTimelinePoints = { 0.0f, 0.0f, 0.0f, 1.0f, 0.4f, 0.8f, 0.0f, 3.0f, 1.0f, 1.0f, 0.0f, 0.0f };
    // v22 research-derived options.
    source.analysis.reverseTracking = true;
    source.analysis.clampResidual = true;
    source.synth.residualDensity = 12;
    source.synth.animationShimmerDb = 1.5;
    source.synth.animationJitterCents = 7.0;
    source.synth.animationRateHz = 6.0;
    source.synth.vibratoDepth = 0.25;
    source.synth.vibratoRateScale = 1.75;
    source.synth.transposeCharacter = true;
    source.synth.transposeTiltDbPerOct = -2.5;
    source.smearnov.weightTransientness = 0.6;
    source.morphOnsetAlignedWarp = true;

    for (std::uint32_t version = 2; version <= ats::LegacyStateCodec::kCurrentVersion; ++version)
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
        if (version >= 18)
        {
            assert(decoded->morphTimeline == source.morphTimeline);
            assert(decoded->timbreJson == source.timbreJson);
            assert(decoded->smearnovOnsets == source.smearnovOnsets);
        }
        else
        {
            // Older versions default to {0.0, 1.0} and empty
            assert(decoded->morphTimeline == std::vector<float>({ 0.0f, 1.0f }));
            assert(decoded->timbreJson.empty());
            assert(decoded->smearnovOnsets.empty());
        }
        if (version >= 19)
        {
            assert(decoded->analysis.multiRes && decoded->synth.useIfftSynth);
            assert(decoded->timbreBrightnessDb == -7.5 && decoded->timbreTiltDbPerOct == 3.25 &&
                   decoded->timbreNoiseBalance == 0.8);
        }
        else
        {
            // Not carried before v19: registry defaults.
            assert(!decoded->analysis.multiRes && !decoded->synth.useIfftSynth);
            assert(decoded->timbreBrightnessDb == 0.0 && decoded->timbreTiltDbPerOct == 0.0 &&
                   decoded->timbreNoiseBalance == 0.5);
        }
        if (version >= 21)
            assert(decoded->morphTimelinePoints == source.morphTimelinePoints);
        else
            assert(decoded->morphTimelinePoints.empty());
        if (version >= 22)
        {
            assert(decoded->analysis.reverseTracking && decoded->analysis.clampResidual);
            assert(decoded->synth.residualDensity == 12);
            assert(decoded->synth.animationShimmerDb == 1.5 && decoded->synth.animationJitterCents == 7.0
                   && decoded->synth.animationRateHz == 6.0);
            assert(decoded->synth.vibratoDepth == 0.25 && decoded->synth.vibratoRateScale == 1.75);
            assert(decoded->synth.transposeCharacter && decoded->synth.transposeTiltDbPerOct == -2.5);
            assert(decoded->smearnov.weightTransientness == 0.6);
            assert(decoded->morphOnsetAlignedWarp);
        }
        else
        {
            // Not carried before v22: registry defaults.
            assert(decoded->smearnov.weightTransientness == 0.0);
            assert(!decoded->morphOnsetAlignedWarp);
            assert(!decoded->analysis.reverseTracking && !decoded->analysis.clampResidual);
            assert(decoded->synth.residualDensity == 1 && decoded->synth.animationShimmerDb == 0.0
                   && decoded->synth.vibratoDepth == 1.0 && !decoded->synth.transposeCharacter);
        }
        if (version >= 20)
            assert(decoded->soundCurves == source.soundCurves);
        else
            for (const auto& curve : decoded->soundCurves)
                assert(curve.empty()); // not carried before v20
        if (version >= 13)
            assert(decoded->frequencyShape.mode == ats::FrequencyShapeMode::Harmonic);
        auto truncated = bytes;
        truncated.pop_back();
        assert(!ats::LegacyStateCodec::decode(truncated.data(), truncated.size()));
    }

    auto malformed = source;
    malformed.outputTrim = std::numeric_limits<double>::quiet_NaN();
    assert(ats::LegacyStateCodec::encode(malformed).empty());

    // v18 field validation: non-finite timeline value → encode fails
    auto badTimeline = source;
    badTimeline.morphTimeline = { 0.0f, std::numeric_limits<float>::infinity(), 1.0f };
    assert(ats::LegacyStateCodec::encode(badTimeline).empty());

    // v18 field validation: too many timeline points → encode fails
    auto tooManyPoints = source;
    tooManyPoints.morphTimeline.resize(33, 0.5f);
    assert(ats::LegacyStateCodec::encode(tooManyPoints).empty());

    // v20 field validation: odd-length or non-finite curves fail to encode.
    auto oddCurve = source;
    oddCurve.soundCurves[2] = { 0.0f, 1.0f, 0.5f };
    assert(ats::LegacyStateCodec::encode(oddCurve).empty());
    auto nanCurve = source;
    nanCurve.soundCurves[1] = { 0.0f, std::numeric_limits<float>::quiet_NaN() };
    assert(ats::LegacyStateCodec::encode(nanCurve).empty());
    // v22 fields are validated like the older ones.
    auto nanVibrato = source;
    nanVibrato.synth.vibratoDepth = std::numeric_limits<double>::quiet_NaN();
    assert(ats::LegacyStateCodec::encode(nanVibrato).empty());
    auto nanWeight = source;
    nanWeight.smearnov.weightTransientness = std::numeric_limits<double>::infinity();
    assert(ats::LegacyStateCodec::encode(nanWeight).empty());
    auto badDensity = source;
    badDensity.synth.residualDensity = 0;
    assert(ats::LegacyStateCodec::encode(badDensity).empty());

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
    std::cout << "legacy_state_codec_test: ATSP v2-v22 payload round-trip ok\n";
}
