#include "AtsModernModel.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace
{

int failures = 0;

void expect(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << message << '\n';
        ++failures;
    }
}

ats::ModernAtsModel makeSmallModel()
{
    ats::ModernAtsModel model;
    model.header.sampleRate = 48000;
    model.header.hopSizeSamples = 256;
    model.header.windowSizeSamples = 1024;
    model.header.parameterHash = "params:a";
    model.header.sourceHash = "source:b";

    ats::ModernAtsFrame frame;
    frame.timeSeconds = 0.0;
    frame.activePartialIds = { 7 };
    frame.orphanResidualBandEnergy.assign(ats::kModernAtsDefaultResidualBands, 0.0);
    frame.orphanResidualBandEnergy[3] = 0.5;
    model.frames.push_back(frame);

    ats::ModernAtsPartial partial;
    partial.id = 7;
    partial.label = "p7";
    partial.channel = 0;
    partial.frames.push_back(
        { 0.25, 440.0, 0.125, 60.0, 0.125, 0.875, ats::ModernAtsFrameReason::Born });
    model.partials.push_back(partial);

    model.trackLifecycle.push_back({ 7, 0, ats::ModernAtsLifecycleKind::Birth, "first accepted peak" });
    model.transients.push_back({ 0, -1, 0.015625, 0.0, 0.03125, 1.25 });
    model.warnings.push_back("fixture warning");
    return model;
}

ats::ModernAtsModel makeLegacyCompatibleModel()
{
    ats::ModernAtsModel model;
    model.header.sampleRate = 44100;
    model.header.hopSizeSamples = 512;
    model.header.windowSizeSamples = 2048;
    model.header.parameterHash = "classic-params";
    model.header.sourceHash = "classic-source";

    ats::ModernAtsFrame frame;
    frame.timeSeconds = 0.0;
    frame.activePartialIds = { 1 };
    model.frames.push_back(frame);

    ats::ModernAtsPartial partial;
    partial.id = 1;
    partial.label = "classic-1";
    partial.frames.push_back({ 0.125, 220.0, 0.0, 54.0, 0.0, 1.0, ats::ModernAtsFrameReason::Matched });
    model.partials.push_back(partial);
    return model;
}

ats::ModernAtsModel makeRepresentativeModel(int partialCount,
                                            int frameCount,
                                            bool includeOrphanResidual,
                                            int transientPeriod)
{
    ats::ModernAtsModel model;
    model.header.analysisMode = ats::ModernAtsAnalysisMode::ModernAts;
    model.header.trackerMode = ats::ModernAtsTrackerMode::ModernAssignment;
    model.header.sampleRate = 48000;
    model.header.hopSizeSamples = 256;
    model.header.windowSizeSamples = 1024;
    model.header.parameterHash = "representative-params";
    model.header.sourceHash = "representative-source";

    std::vector<int> activeIds;
    activeIds.reserve((std::size_t)partialCount);
    for (int partialId = 0; partialId < partialCount; ++partialId)
        activeIds.push_back(partialId);

    for (int frameIndex = 0; frameIndex < frameCount; ++frameIndex)
    {
        ats::ModernAtsFrame frame;
        frame.timeSeconds = (double)(frameIndex * model.header.hopSizeSamples)
                            / (double)model.header.sampleRate;
        frame.activePartialIds = activeIds;
        if (includeOrphanResidual)
        {
            frame.orphanResidualBandEnergy.assign(ats::kModernAtsDefaultResidualBands, 0.0);
            frame.orphanResidualBandEnergy[(std::size_t)(frameIndex % ats::kModernAtsDefaultResidualBands)]
                = 0.001 * (double)((frameIndex % 7) + 1);
        }
        model.frames.push_back(frame);
    }

    for (int partialId = 0; partialId < partialCount; ++partialId)
    {
        ats::ModernAtsPartial partial;
        partial.id = partialId;
        partial.label = "p" + std::to_string(partialId);
        partial.channel = 0;
        partial.frames.reserve((std::size_t)frameCount);
        for (int frameIndex = 0; frameIndex < frameCount; ++frameIndex)
        {
            const double phase = std::fmod((double)(partialId + frameIndex) * 0.125, 6.25);
            ats::ModernAtsFrameReason reason = ats::ModernAtsFrameReason::Matched;
            if (frameIndex == 0)
                reason = ats::ModernAtsFrameReason::Born;
            else if (frameIndex % 97 == 0)
                reason = ats::ModernAtsFrameReason::Interpolated;

            partial.frames.push_back({ 0.01 + 0.0001 * (double)(partialId % 11),
                                       80.0 + 12.5 * (double)partialId,
                                       phase,
                                       30.0 + (double)(partialId % 24),
                                       0.00001 * (double)((partialId % 5) + 1),
                                       0.5 + 0.5 / (double)((frameIndex % 8) + 1),
                                       reason });
        }
        model.partials.push_back(partial);
        model.trackLifecycle.push_back({ partialId, 0, ats::ModernAtsLifecycleKind::Birth, "representative" });
        model.trackLifecycle.push_back(
            { partialId, frameCount - 1, ats::ModernAtsLifecycleKind::Death, "representative" });
    }

    if (transientPeriod > 0)
    {
        for (int frameIndex = 0; frameIndex < frameCount; frameIndex += transientPeriod)
        {
            const double onset = (double)(frameIndex * model.header.hopSizeSamples)
                                 / (double)model.header.sampleRate;
            model.transients.push_back({ frameIndex, -1, onset, onset, onset + 0.021333333333333333, 1.0 });
        }
    }

    return model;
}

void testSmallDeterministicSerialization()
{
    const auto model = makeSmallModel();
    std::string error;
    expect(ats::validateModernAtsModel(model, error), error.c_str());

    const std::string json1 = ats::serializeModernAtsModelToJson(model);
    const std::string json2 = ats::serializeModernAtsModelToJson(model);
    expect(json1 == json2, "modern ATS serialization is not deterministic");

    const std::string expected =
        "{\"schema_version\":1,\"header\":{\"analysis_mode\":\"classic_ats\",\"tracker_mode\":\"classic_recursive_frequency_smr\",\"sample_rate\":48000,\"hop_size_samples\":256,\"window_size_samples\":1024,\"input_channel_count\":1,\"channel_policy\":\"mono_input\",\"parameter_hash\":\"params:a\",\"source_hash\":\"source:b\"},\"frames\":[{\"time_seconds\":0,\"active_partial_ids\":[7],\"orphan_residual_band_energy\":[0,0,0,0.5,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0]}],\"partials\":[{\"id\":7,\"label\":\"p7\",\"channel\":0,\"frames\":[{\"amplitude\":0.25,\"frequency_hz\":440,\"phase_radians\":0.125,\"smr_db\":60,\"noise_energy\":0.125,\"confidence\":0.875,\"reason\":\"born\"}]}],\"track_lifecycle\":[{\"partial_id\":7,\"frame_index\":0,\"kind\":\"birth\",\"detail\":\"first accepted peak\"}],\"assignment_costs\":[],\"transients\":[{\"frame_index\":0,\"buffer_index\":-1,\"onset_seconds\":0.015625,\"window_start_seconds\":0,\"window_end_seconds\":0.03125,\"gain\":1.25}],\"transient_buffers\":[],\"warnings\":[\"fixture warning\"]}";
    expect(json1 == expected, "modern ATS JSON schema changed unexpectedly");
}

void testChannelPolicySerializationAndValidation()
{
    auto stereoDownmix = makeSmallModel();
    stereoDownmix.header.inputChannelCount = 2;
    stereoDownmix.header.channelPolicy = ats::ModernAtsChannelPolicy::DownmixToMonoAverage;
    std::string error;
    expect(ats::validateModernAtsModel(stereoDownmix, error), error.c_str());

    const std::string json = ats::serializeModernAtsModelToJson(stereoDownmix);
    expect(json.find("\"input_channel_count\":2") != std::string::npos,
           "input channel count missing from modern model header");
    expect(json.find("\"channel_policy\":\"downmix_to_mono_average\"") != std::string::npos,
           "channel policy missing from modern model header");
    expect(ats::defaultModernAtsChannelPolicyForInput(1) == ats::ModernAtsChannelPolicy::MonoInput,
           "mono input should default to mono policy");
    expect(ats::defaultModernAtsChannelPolicyForInput(2) == ats::ModernAtsChannelPolicy::DownmixToMonoAverage,
           "stereo input should default to average downmix policy");

    auto invalidMono = makeSmallModel();
    invalidMono.header.channelPolicy = ats::ModernAtsChannelPolicy::DownmixToMonoAverage;
    expect(!ats::validateModernAtsModel(invalidMono, error),
           "mono input should reject multichannel channel policies");

    auto invalidMultichannel = makeSmallModel();
    invalidMultichannel.header.inputChannelCount = 2;
    invalidMultichannel.header.channelPolicy = ats::ModernAtsChannelPolicy::MonoInput;
    expect(!ats::validateModernAtsModel(invalidMultichannel, error),
           "multichannel input should require explicit multichannel policy");

    auto invalidPartialChannel = stereoDownmix;
    invalidPartialChannel.partials[0].channel = 2;
    expect(!ats::validateModernAtsModel(invalidPartialChannel, error),
           "partial channel should be inside the recorded input channel count");
}

void testOptionalFieldsStayEmpty()
{
    const auto model = makeLegacyCompatibleModel();
    std::string error;
    expect(ats::validateModernAtsModel(model, error), error.c_str());
    const std::string json = ats::serializeModernAtsModelToJson(model);
    expect(json.find("\"orphan_residual_band_energy\":[]") != std::string::npos,
           "empty orphan residual field did not serialize as an optional empty array");
    expect(json.find("\"track_lifecycle\":[]") != std::string::npos,
           "empty lifecycle field did not serialize as an optional empty array");
    expect(json.find("\"assignment_costs\":[]") != std::string::npos,
           "empty assignment cost field did not serialize as an optional empty array");
    expect(json.find("\"transients\":[]") != std::string::npos,
           "empty transient field did not serialize as an optional empty array");
    expect(json.find("\"transient_buffers\":[]") != std::string::npos,
           "empty transient buffer field did not serialize as an optional empty array");
}

void testValidationRejectsDanglingReferences()
{
    std::string error;

    auto duplicatePartial = makeSmallModel();
    duplicatePartial.partials.push_back(duplicatePartial.partials.front());
    expect(!ats::validateModernAtsModel(duplicatePartial, error),
           "duplicate partial ids should be rejected");

    auto missingActivePartial = makeSmallModel();
    missingActivePartial.frames[0].activePartialIds = { 42 };
    expect(!ats::validateModernAtsModel(missingActivePartial, error),
           "active partial ids should reference existing partials");

    auto danglingLifecycle = makeSmallModel();
    danglingLifecycle.trackLifecycle[0].frameIndex = 5;
    expect(!ats::validateModernAtsModel(danglingLifecycle, error),
           "lifecycle events should reference existing frames");

    auto danglingTransientBuffer = makeSmallModel();
    danglingTransientBuffer.transients[0].bufferIndex = 0;
    expect(!ats::validateModernAtsModel(danglingTransientBuffer, error),
           "transient marker buffer indices should reference existing buffers");

    auto danglingBufferMarker = makeSmallModel();
    ats::ModernAtsTransientBuffer buffer;
    buffer.markerIndex = 99;
    buffer.sampleRate = 48000;
    buffer.samples = { 0.0f };
    danglingBufferMarker.transientBuffers.push_back(buffer);
    expect(!ats::validateModernAtsModel(danglingBufferMarker, error),
           "transient buffers should reference existing markers");
}

void testValidationRejectsNonIncreasingFrameTimes()
{
    std::string error;

    auto duplicateTimes = makeSmallModel();
    duplicateTimes.frames.push_back(duplicateTimes.frames.front());
    expect(!ats::validateModernAtsModel(duplicateTimes, error),
           "frame times must be strictly increasing");
}

void testRepresentativeStateSizes()
{
    struct Shape
    {
        const char* name;
        int partials;
        int frames;
        bool orphanResidual;
        int transientPeriod;
        std::size_t maxBytes;
    };

    const Shape shapes[] = {
        { "sustained", 32, 180, true, 0, 1100000 },
        { "vibrato", 40, 360, true, 0, 2600000 },
        { "close_crossing_partials", 48, 300, true, 0, 2600000 },
        { "plucked_percussive", 64, 240, true, 24, 2800000 },
        { "noisy_voice_flute", 48, 300, true, 60, 2600000 },
        { "dense_polyphony", 128, 480, true, 0, 11000000 },
        { "percussive_hit", 96, 180, true, 18, 3200000 },
    };

    for (const auto& shape : shapes)
    {
        const auto model =
            makeRepresentativeModel(shape.partials, shape.frames, shape.orphanResidual, shape.transientPeriod);
        std::string error;
        expect(ats::validateModernAtsModel(model, error), error.c_str());
        const std::size_t size = ats::estimateModernAtsModelStateBytes(model);
        std::cout << "modern_model_state_size " << shape.name << "=" << size << " bytes\n";
        expect(size > 0, "representative model serialized to zero bytes");
        expect(size < shape.maxBytes, "representative model state size exceeded Phase 1 budget");
    }
}

} // namespace

int main()
{
    testSmallDeterministicSerialization();
    testChannelPolicySerializationAndValidation();
    testOptionalFieldsStayEmpty();
    testValidationRejectsDanglingReferences();
    testValidationRejectsNonIncreasingFrameTimes();
    testRepresentativeStateSizes();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
