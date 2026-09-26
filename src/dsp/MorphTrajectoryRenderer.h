#pragma once

#include "../core/AtsMorphRecipe.h"
#include "../core/AtsSpectralEnvelope.h"
#include "../core/TimbreMotionPlan.h"
#include "../engine/MorphTimelineService.h"

#include <cstdint>
#include <map>
#include <mutex>
#include <tuple>
#include <vector>

extern "C" {
#include "atsa.h"
}

namespace ats::dsp
{

struct MorphTrajectoryBuffer
{
    std::vector<float> partialSamples;
    std::vector<float> residualSamples;

    [[nodiscard]] std::size_t frameCount() const noexcept { return partialSamples.size(); }
};

struct MorphEnvelopeCache
{
    std::mutex lock;
    std::uint64_t revision = ~std::uint64_t { 0 };
    const ATS_SOUND* keyA = nullptr;
    const ATS_SOUND* keyB = nullptr;
    std::map<std::tuple<int, int, std::uint64_t>, ats::BarkSpectralEnvelope> mapA;
    std::map<std::tuple<int, int, std::uint64_t>, ats::BarkSpectralEnvelope> mapB;
    std::map<int, ats::BarkSpectralEnvelope> perFrameA;
    std::map<int, ats::BarkSpectralEnvelope> perFrameB;
};

struct MorphTrajectorySettings
{
    const std::vector<ats::engine::TimelinePoint>* timeline = nullptr;
    double fixedMorphAmount = 0.5;
    double sampleRate = 44100.0;
    double durationSeconds = 0.25;
    MorphEnvelopeCache* envelopeCache = nullptr;
    std::uint64_t sourcesRevision = 0;
    const ats::TimbreMotionPlan* timbrePlan = nullptr;
    double brightnessDb = 0.0;
    double tiltDbPerOctave = 0.0;
    double noiseBalance = 0.5;
    ats::HybridizeDomains interpolationDomains {};
    double smoothingMs = 0.0;
    double performanceMacro = 0.5;
};

[[nodiscard]] MorphTrajectoryBuffer renderMorphTrajectory(
    const ATS_SOUND* soundA,
    const ATS_SOUND* soundB,
    const ats::MorphRecipe& recipe,
    const MorphTrajectorySettings& settings = {});

} // namespace ats::dsp
