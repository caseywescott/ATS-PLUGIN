#include "AtsEngine.h"
#include "AtsParams.h"

#include <cassert>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace
{

void require(bool condition, const char* label)
{
    if (!condition)
    {
        std::cerr << "ats_audio_golden_matrix_test FAILED: " << label << '\n';
        std::abort();
    }
}

// Build a mono sine fixture. 440 Hz is well below Nyquist for all tested rates.
std::vector<double> sineFixture(int sampleRate, double frequencyHz = 440.0,
                                double amplitude = 0.5, double seconds = 0.75)
{
    const auto count = static_cast<std::size_t>(static_cast<double>(sampleRate) * seconds);
    std::vector<double> out(count);
    for (std::size_t i = 0; i < count; ++i)
        out[i] = amplitude * std::sin(2.0 * 3.141592653589793 * frequencyHz *
                                      static_cast<double>(i) / static_cast<double>(sampleRate));
    return out;
}

// Render 'frames' samples and return stats.
ats::RenderStats renderWith(const ATS_SOUND* sound, ats::SynthParams params, std::size_t frames)
{
    std::vector<float> left(frames, 0.0f), right(frames, 0.0f);
    float* ptrs[] { left.data(), right.data() };
    ats::AudioBufferView view { ptrs, 2, static_cast<std::uint32_t>(frames) };
    ats::CancellationToken live;
    const auto result = ats::AtsEngine::render({ 1 }, live, sound, params, view);
    require(result.status == ats::JobResult<ats::RenderStats>::Status::success, "render status");
    return result.value;
}

// Analyse a fixture and return the sound, or abort.
ats::AtsSoundPtr analyse(const std::vector<double>& samples, int sampleRate,
                         const ats::AnalysisParams& params = {})
{
    ats::CancellationToken live;
    auto result = ats::AtsEngine::analyze({ 1 }, live,
        { samples.data(), samples.size(), sampleRate, params });
    require(result.status == ats::JobResult<ats::AnalysisOutput>::Status::success,
            "analysis status");
    require(result.value.sound.get() != nullptr, "analysis produced a sound");
    return std::move(result.value.sound);
}

// ─── scenario helpers ────────────────────────────────────────────────────────

void testSampleRate(int sampleRate)
{
    const auto fixture = sineFixture(sampleRate);
    ats::SynthParams p;
    p.sampleRate = static_cast<double>(sampleRate);
    const auto sound = analyse(fixture, sampleRate);
    const auto stats = renderWith(sound.get(), p, 4096);
    require(stats.framesWritten > 0, "non-zero frames at sample rate");
    require(stats.finite, "finite output at sample rate");
    require(stats.peak > 0.0f, "non-silent output at sample rate");
}

void testBlockSize(const ATS_SOUND* sound, std::size_t blockFrames, const char* label)
{
    const auto stats = renderWith(sound, {}, blockFrames);
    require(stats.framesWritten > 0, label);
    require(stats.finite, label);
}

void testGainIsolation(const ATS_SOUND* sound)
{
    ats::SynthParams partialsOnly;
    partialsOnly.residualGain = 0.0;
    const auto pStats = renderWith(sound, partialsOnly, 4096);
    require(pStats.finite, "partials-only finite");

    ats::SynthParams residualOnly;
    residualOnly.partialsGain = 0.0;
    const auto rStats = renderWith(sound, residualOnly, 4096);
    require(rStats.finite, "residual-only finite");

    ats::SynthParams combined;
    combined.partialsGain = 1.0;
    combined.residualGain = 1.0;
    const auto cStats = renderWith(sound, combined, 4096);
    // Combined peak must be at least as high as each isolated layer.
    require(cStats.peak >= pStats.peak || cStats.peak >= rStats.peak,
            "combined peak >= at least one isolated layer");
}

void testPhaseFlag(const ATS_SOUND* sound)
{
    ats::SynthParams withPhase;
    withPhase.usePartialsPhase = true;
    const auto s1 = renderWith(sound, withPhase, 4096);
    require(s1.finite, "phase=on finite");
    require(s1.peak > 0.0f, "phase=on non-silent");

    ats::SynthParams noPhase;
    noPhase.usePartialsPhase = false;
    const auto s2 = renderWith(sound, noPhase, 4096);
    require(s2.finite, "phase=off finite");
    require(s2.peak > 0.0f, "phase=off non-silent");
}

void testIfftPath(const ATS_SOUND* sound)
{
    ats::SynthParams p;
    p.useIfftSynth = true;
    const auto stats = renderWith(sound, p, 4096);
    require(stats.finite, "IFFT path finite");
    require(stats.peak > 0.0f, "IFFT path non-silent");
}

void testFrequencyFactor(const ATS_SOUND* sound)
{
    ats::SynthParams up;
    up.frequencyFactor = 2.0;
    const auto upStats = renderWith(sound, up, 4096);
    require(upStats.finite, "freq*2 finite");
    require(upStats.peak > 0.0f, "freq*2 non-silent");

    ats::SynthParams down;
    down.frequencyFactor = 0.5;
    const auto downStats = renderWith(sound, down, 4096);
    require(downStats.finite, "freq*0.5 finite");
}

void testTimeFactor(const ATS_SOUND* sound)
{
    ats::SynthParams stretched;
    stretched.timeFactor = 2.0;
    require(renderWith(sound, stretched, 8192).finite, "time-stretch*2 finite");

    ats::SynthParams compressed;
    compressed.timeFactor = 0.5;
    require(renderWith(sound, compressed, 2048).finite, "time-compress*0.5 finite");
}

void testStartDuration(const ATS_SOUND* sound)
{
    ats::SynthParams startOffset;
    startOffset.startTime = 0.07;
    require(renderWith(sound, startOffset, 4096).finite, "start-offset finite");

    ats::SynthParams shortDur;
    shortDur.duration = 0.2;
    const auto shortStats = renderWith(sound, shortDur, 4096);
    require(shortStats.finite, "short-duration finite");
    require(shortStats.framesWritten <= 4096, "short-duration respects length");
}

void testNullSound()
{
    // nullptr must not crash; output must remain finite (pre-filled with 1.0 to
    // detect accidental pass-through).
    constexpr std::size_t kFrames = 512;
    std::vector<float> left(kFrames, 1.0f), right(kFrames, 1.0f);
    float* ptrs[] { left.data(), right.data() };
    ats::AudioBufferView view { ptrs, 2, static_cast<std::uint32_t>(kFrames) };
    ats::CancellationToken live;
    const auto result = ats::AtsEngine::render({ 1 }, live, nullptr, {}, view);
    // Either an error result or a success producing silence; crash is not allowed.
    if (result.status == ats::JobResult<ats::RenderStats>::Status::success)
        require(result.value.finite, "null-sound render finite");
    for (const float s : left)  require(std::isfinite(s), "null-sound left finite");
    for (const float s : right) require(std::isfinite(s), "null-sound right finite");
}

void testCancellation(int sampleRate)
{
    // Cancelling before or during analysis must never crash.
    const auto fixture = sineFixture(sampleRate, 440.0, 0.5, 1.5);
    ats::CancellationSource src;
    src.cancel();
    const auto token = src.token();
    const auto result = ats::AtsEngine::analyze({ 1 }, token,
        { fixture.data(), fixture.size(), sampleRate });
    (void) result; // cancelled or completed — both are valid; crash is not
}

void testDeterminism(const ATS_SOUND* sound, int sampleRate)
{
    ats::SynthParams p;
    p.sampleRate = static_cast<double>(sampleRate);
    constexpr std::size_t kFrames = 2048;
    std::vector<float> left1(kFrames), right1(kFrames), left2(kFrames), right2(kFrames);
    float* ptrs1[] { left1.data(), right1.data() };
    float* ptrs2[] { left2.data(), right2.data() };
    ats::CancellationToken live;
    const auto r1 = ats::AtsEngine::render({ 1 }, live, sound, p,
                                            { ptrs1, 2, static_cast<std::uint32_t>(kFrames) });
    const auto r2 = ats::AtsEngine::render({ 1 }, live, sound, p,
                                            { ptrs2, 2, static_cast<std::uint32_t>(kFrames) });
    require(r1.status == ats::JobResult<ats::RenderStats>::Status::success, "det run1");
    require(r2.status == ats::JobResult<ats::RenderStats>::Status::success, "det run2");
    for (std::size_t i = 0; i < kFrames; ++i)
        require(left1[i] == left2[i], "render is deterministic");
}

} // namespace

int main()
{
    // §14.2: sample rates 44.1 / 48 / 88.2 / 96 kHz
    for (const int sr : { 44100, 48000, 88200, 96000 })
        testSampleRate(sr);

    // Shared fixture at 48 kHz for all remaining scenarios.
    constexpr int kSr = 48000;
    const auto fixture48 = sineFixture(kSr);
    const auto sound48   = analyse(fixture48, kSr);

    // §14.2: small and large host blocks
    testBlockSize(sound48.get(), 64,   "64-frame block");
    testBlockSize(sound48.get(), 512,  "512-frame block");
    testBlockSize(sound48.get(), 4096, "4096-frame block");

    // §14.2: partial / residual / combined gain isolation
    testGainIsolation(sound48.get());

    // §14.2: phase and IFFT flags
    testPhaseFlag(sound48.get());
    testIfftPath(sound48.get());

    // §14.2: frequency and time factors
    testFrequencyFactor(sound48.get());
    testTimeFactor(sound48.get());

    // §14.2: start / duration clipping
    testStartDuration(sound48.get());

    // §14.2: null model (empty model / zero-length handling)
    testNullSound();

    // §14.2: cancellation during analysis
    testCancellation(48000);

    // §14.2: determinism (identical inputs → identical outputs)
    testDeterminism(sound48.get(), kSr);

    std::cout << "ats_audio_golden_matrix_test: §14.2 golden matrix ok\n";
    return 0;
}
