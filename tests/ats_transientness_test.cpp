#include "AtsTransientness.h"
#include "AtsTransientSmear.h"
#include "AtsModel.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace
{
constexpr int kRate = 44100;
constexpr int kHop = 441;
constexpr int kBlock = 2048;
constexpr double kPi = 3.141592653589793;

// Deterministic uniform noise in [-1, 1) (splitmix64).
double noiseAt(std::uint64_t i)
{
    std::uint64_t z = (i + 0x9E3779B97F4A7C15ULL) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 30)) * 0x94D049BB133111EBULL;
    z = (z ^ (z >> 27)) * 0x2545F4914F6CDD1DULL;
    z ^= z >> 31;
    return (double) (z >> 11) / 9007199254740992.0 * 2.0 - 1.0;
}

// 1 s of 440 Hz with a 50 ms raised-cosine fade in/out (a hard-truncated sine
// would be a genuine step transient at the signal edges).
std::vector<double> sine(double amp)
{
    std::vector<double> x(kRate);
    const int fade = kRate / 20;
    for (int n = 0; n < kRate; ++n)
    {
        double env = 1.0;
        if (n < fade)
            env = 0.5 - 0.5 * std::cos(kPi * n / fade);
        else if (n >= kRate - fade)
            env = 0.5 - 0.5 * std::cos(kPi * (kRate - 1 - n) / fade);
        x[(size_t) n] = amp * env * std::sin(2.0 * kPi * 440.0 * n / kRate);
    }
    return x;
}

// Click train: impulse every 0.25 s with a 2 ms exponential decay.
void addClicks(std::vector<double>& x, std::vector<int>& clickSamples)
{
    for (int c = 0; c < 4; ++c)
    {
        const int at = (int) std::lround(c * 0.25 * kRate) + 20;
        clickSamples.push_back(at);
        for (int n = 0; n < 400 && at + n < (int) x.size(); ++n)
            x[(size_t) (at + n)] += std::exp(-(double) n / 88.0) * (n % 2 == 0 ? 1.0 : -0.6);
    }
}

double meanOf(const std::vector<double>& v, const std::vector<int>& frames)
{
    double s = 0.0;
    for (const int f : frames)
        s += v[(size_t) f];
    return frames.empty() ? 0.0 : s / (double) frames.size();
}

bool inUnit(const std::vector<double>& v)
{
    for (const double d : v)
        if (!(d >= 0.0 && d <= 1.0))
            return false;
    return true;
}

// Synthetic model: 2 partials, 200 frames, attacks at frames 50 and 120.
ats::AtsSoundPtr makeModel()
{
    const int frames = 200;
    auto* s = static_cast<ATS_SOUND*>(std::calloc(1, sizeof(ATS_SOUND)));
    init_sound(s, kRate, kHop, 4 * kHop, frames, frames * (double) kHop / kRate, 2, 1);
    for (int f = 0; f < frames; ++f)
    {
        s->time[0][f] = (double) f * kHop / kRate;
        double env = 0.01;
        for (const int onset : {50, 120})
            if (f >= onset)
                env += 0.6 * std::exp(-(double) (f - onset) / 25.0);
        for (int p = 0; p < 2; ++p)
        {
            s->amp[p][f] = env / (p + 1);
            s->frq[p][f] = 440.0 * (p + 1);
        }
        for (int b = 0; b < ATSA_CRITICAL_BANDS; ++b)
            s->band_energy[b][f] = 1.0e-4 * env;
    }
    return ats::AtsSoundPtr(s);
}

bool nearFrame(int f, const std::vector<ats::AtsOnset>& onsets)
{
    for (const auto& o : onsets)
        if (std::abs(o.frame - f) <= 1)
            return true;
    return false;
}
} // namespace

int main()
{
    // --- index on a model-free grid -------------------------------------------
    std::vector<int> clickSamples;
    auto mixed = sine(0.2);
    addClicks(mixed, clickSamples);
    const auto resMixed = ats::transientnessIndex(mixed.data(), mixed.size(), kHop, kBlock);
    assert((int) resMixed.index.size() == (kRate + kHop - 1) / kHop);
    assert(inUnit(resMixed.index));

    std::vector<int> clickFrames;
    std::vector<int> steadyFrames;
    for (int f = 0; f < (int) resMixed.index.size(); ++f)
    {
        const int centre = f * kHop;
        bool nearClick = false;
        bool farFromClick = true;
        for (const int c : clickSamples)
        {
            if (centre >= c - 200 && centre <= c + 200) nearClick = true;
            if (std::abs(centre - c) < kBlock) farFromClick = false;
        }
        if (nearClick) clickFrames.push_back(f);
        else if (farFromClick && centre > kBlock && centre < kRate - kBlock) steadyFrames.push_back(f);
    }
    assert(!clickFrames.empty() && !steadyFrames.empty());
    const double clickMean = meanOf(resMixed.index, clickFrames);
    const double steadyMean = meanOf(resMixed.index, steadyFrames);
    std::cout << "click mean " << clickMean << "  steady mean " << steadyMean << '\n';
    assert(clickMean > steadyMean + 0.2);

    // Pure sine: tonal everywhere.
    const auto pure = sine(0.5);
    const auto resSine = ats::transientnessIndex(pure.data(), pure.size(), kHop, kBlock);
    assert(inUnit(resSine.index));
    double sineMax = 0.0;
    for (const double v : resSine.index)
        sineMax = std::max(sineMax, v);
    std::cout << "sine max " << sineMax << "  mean " << resSine.mean << '\n';
    assert(sineMax < 0.4);

    // White noise: dense in both bases -> intermediate.
    std::vector<double> noise(kRate);
    for (int n = 0; n < kRate; ++n)
        noise[(size_t) n] = 0.3 * noiseAt((std::uint64_t) n);
    const auto resNoise = ats::transientnessIndex(noise.data(), noise.size(), kHop, kBlock);
    assert(inUnit(resNoise.index));
    std::cout << "noise mean " << resNoise.mean << '\n';
    assert(resNoise.mean > resSine.mean + 0.1 && resNoise.mean < clickMean);
    assert(resNoise.mean > 0.15 && resNoise.mean < 0.85);

    // Determinism.
    const auto again = ats::transientnessIndex(mixed.data(), mixed.size(), kHop, kBlock);
    assert(again.index == resMixed.index && again.mean == resMixed.mean);

    // Model-driven overload: same grid -> same values (block = window rounded to pow2).
    {
        const int frames = (int) resMixed.index.size();
        auto* s = static_cast<ATS_SOUND*>(std::calloc(1, sizeof(ATS_SOUND)));
        init_sound(s, kRate, kHop, kBlock, frames, 1.0, 1, 0);
        for (int f = 0; f < frames; ++f)
            s->time[0][f] = (double) f * kHop / kRate;
        ats::AtsSoundPtr owner(s);
        const auto resModel = ats::transientnessIndex(mixed.data(), mixed.size(), kRate, s);
        assert(resModel.index.size() == resMixed.index.size());
        for (size_t f = 0; f < resModel.index.size(); ++f)
            assert(std::abs(resModel.index[f] - resMixed.index[f]) < 1.0e-12);
    }

    // --- Smearnov integration ---------------------------------------------------
    const auto model = makeModel();
    ATS_SOUND* s = model.get();
    std::vector<double> index((size_t) s->frames, 0.05);
    for (const int onset : {50, 120})
        for (int f = onset; f < onset + 3; ++f)
            index[(size_t) f] = 1.0;

    ats::SmearnovDetectParams detect;
    detect.useSmrWeighting = false;
    const auto base = ats::detectOnsets(s, detect);
    assert(!base.empty());
    for (const auto& o : base)
        assert(o.transientness == -1.0);

    // Weight 0 with an index supplied: bit-identical onsets, index attached.
    const auto tagged = ats::detectOnsets(s, detect, nullptr, &index);
    assert(tagged.size() == base.size());
    for (size_t i = 0; i < base.size(); ++i)
    {
        assert(tagged[i].frame == base[i].frame && tagged[i].strength == base[i].strength);
        assert(tagged[i].transientness == index[(size_t) base[i].frame]);
    }

    // Weight 1: every baseline onset is still found (within one frame), possibly more.
    detect.weightTransientness = 1.0;
    const auto fused = ats::detectOnsets(s, detect, nullptr, &index);
    assert(fused.size() >= base.size());
    for (const auto& o : base)
        assert(nearFrame(o.frame, fused));
    assert(nearFrame(50, fused) && nearFrame(120, fused));

    // scaleByTransientness with index 0 leaves the model untouched.
    {
        auto onsets = base;
        for (auto& o : onsets)
            o.transientness = 0.0;
        ats::AtsSoundPtr copy(copy_sound(s));
        ats::SmearnovParams smear;
        smear.blurMs = 30.0;
        smear.exaggerateDb = 6.0;
        smear.scaleByTransientness = true;
        assert(!ats::smearAttacks(copy.get(), onsets, smear));
        for (int p = 0; p < s->partials; ++p)
            for (int f = 0; f < s->frames; ++f)
                assert(copy.get()->amp[p][f] == s->amp[p][f]);
        for (int b = 0; b < ATSA_CRITICAL_BANDS; ++b)
            for (int f = 0; f < s->frames; ++f)
                assert(copy.get()->band_energy[b][f] == s->band_energy[b][f]);

        // ...and with index 1 it behaves exactly like the unscaled transform.
        for (auto& o : onsets)
            o.transientness = 1.0;
        ats::AtsSoundPtr scaled(copy_sound(s));
        ats::AtsSoundPtr plain(copy_sound(s));
        assert(ats::smearAttacks(scaled.get(), onsets, smear));
        smear.scaleByTransientness = false;
        assert(ats::smearAttacks(plain.get(), onsets, smear));
        for (int p = 0; p < s->partials; ++p)
            for (int f = 0; f < s->frames; ++f)
                assert(scaled.get()->amp[p][f] == plain.get()->amp[p][f]);
    }

    std::cout << "ats_transientness_test: ok\n";
    return 0;
}
