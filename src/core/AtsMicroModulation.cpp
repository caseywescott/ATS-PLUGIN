#include "AtsMicroModulation.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

namespace ats
{

namespace
{
constexpr double kTwoPi = 6.283185307179586;
constexpr double kLn10Over20 = 0.11512925464970229;
constexpr double kLn2Over1200 = 0.0005776226504666211;
constexpr std::uint64_t kSharedPartial = 0xFFFFFFFFFFFFull; // "partial" id of the common component
constexpr std::uint64_t kAmpChannel = 1;
constexpr std::uint64_t kFrqChannel = 2;
constexpr int kEmphasisedPartials = 3;

std::uint64_t mix64(std::uint64_t z) noexcept
{
    z += 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

std::uint64_t hash4(std::uint64_t seed, std::uint64_t a, std::uint64_t b, std::uint64_t c) noexcept
{
    return mix64(seed ^ mix64(a ^ mix64(b ^ mix64(c + 0x2545F4914F6CDD1Dull))));
}

double uniform01(std::uint64_t h) noexcept { return static_cast<double>(h >> 11) * 0x1.0p-53; }

/** Standard normal from a hash of (seed, partial, channel, frame) via Box-Muller. */
double gaussian(std::uint64_t seed, std::uint64_t partial, std::uint64_t channel, std::uint64_t frame) noexcept
{
    const double u1 = std::max(uniform01(hash4(seed, partial, channel, frame)), 1.0e-300);
    const double u2 = uniform01(hash4(seed, partial, channel | 0x100, frame));
    return std::sqrt(-2.0 * std::log(u1)) * std::cos(kTwoPi * u2);
}

double hopSeconds(const ATS_SOUND* s) noexcept
{
    if (s->frame_size > 0 && s->srate > 0)
        return static_cast<double>(s->frame_size) / s->srate;
    if (s->dur > 0.0 && s->frames > 0)
        return s->dur / s->frames;
    return 0.01;
}

/** Unit-variance low-pass noise for one partial along frames (burn-in before frame 0). */
void filteredNoise(const MicroModulationParams& params, std::uint64_t channel, int partial, int frames,
                   double a, const std::vector<unsigned char>& active, std::vector<double>& out)
{
    const double c = std::clamp(params.correlation, 0.0, 1.0);
    const double wShared = std::sqrt(c);
    const double wOwn = std::sqrt(1.0 - c);
    const int burn = static_cast<int>(std::ceil(6.0 / a));
    const std::uint64_t frameBias = static_cast<std::uint64_t>(burn) + 16;
    double y = 0.0;
    out.assign(static_cast<std::size_t>(frames), 0.0);
    for (int f = -burn; f < frames; ++f)
    {
        const auto fid = static_cast<std::uint64_t>(f) + frameBias;
        const double x = wShared * gaussian(params.seed, kSharedPartial, channel, fid)
                       + wOwn * gaussian(params.seed, static_cast<std::uint64_t>(partial), channel, fid);
        y += a * (x - y);
        if (f >= 0)
            out[static_cast<std::size_t>(f)] = y;
    }
    // Analytic variance restoration for the one-pole: var(y) = var(x) * a / (2 - a).
    const double analytic = std::sqrt((2.0 - a) / a);
    for (auto& v : out)
        v *= analytic;
    // Empirical normalisation over the active frames so the realised rms equals the depth.
    double sum = 0.0;
    int count = 0;
    for (int f = 0; f < frames; ++f)
        if (active[static_cast<std::size_t>(f)])
        {
            sum += out[static_cast<std::size_t>(f)];
            ++count;
        }
    if (count < 4)
        return;
    const double mean = sum / count;
    double sq = 0.0;
    for (int f = 0; f < frames; ++f)
        if (active[static_cast<std::size_t>(f)])
            sq += (out[static_cast<std::size_t>(f)] - mean) * (out[static_cast<std::size_t>(f)] - mean);
    const double rms = std::sqrt(sq / count);
    if (rms <= 1.0e-12)
        return;
    for (auto& v : out)
        v = (v - mean) / rms;
}
} // namespace

AtsSoundPtr applyMicroModulation(const ATS_SOUND* sound, const MicroModulationParams& params)
{
    if (sound == nullptr || sound->frames <= 0 || sound->partials <= 0 || !params.active())
        return {};
    auto* out = copy_sound(const_cast<ATS_SOUND*>(sound));
    if (out == nullptr)
        return {};
    AtsSoundPtr result(out);

    const int frames = sound->frames;
    const int partials = sound->partials;
    const double hop = hopSeconds(sound);
    const double rate = std::clamp(params.rateHz, 0.05, 1000.0);
    const double a = std::clamp(1.0 - std::exp(-kTwoPi * rate * hop), 1.0e-4, 0.999);
    const double shimmer = std::max(0.0, params.shimmerDb);
    const double jitter = std::max(0.0, params.jitterCents);
    const double emphasis = std::clamp(params.lowPartialEmphasis, 0.0, 1.0);

    // Rank partials by mean active frequency; inactive partials rank last.
    std::vector<double> meanFrq(static_cast<std::size_t>(partials), 1.0e300);
    for (int p = 0; p < partials; ++p)
    {
        double sum = 0.0;
        int n = 0;
        for (int f = 0; f < frames; ++f)
            if (sound->amp[p][f] > 0.0 && sound->frq[p][f] > 0.0)
            {
                sum += sound->frq[p][f];
                ++n;
            }
        if (n > 0)
            meanFrq[static_cast<std::size_t>(p)] = sum / n;
    }
    std::vector<int> order(static_cast<std::size_t>(partials));
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](int x, int y) {
        return meanFrq[static_cast<std::size_t>(x)] < meanFrq[static_cast<std::size_t>(y)];
    });
    std::vector<double> weight(static_cast<std::size_t>(partials), 1.0);
    for (int r = 0; r < partials; ++r)
        weight[static_cast<std::size_t>(order[static_cast<std::size_t>(r)])] = r < kEmphasisedPartials ? 1.0 : 1.0 - emphasis;

    std::vector<unsigned char> active(static_cast<std::size_t>(frames));
    std::vector<double> noise;
    double frqmax = 0.0;
    for (int p = 0; p < partials; ++p)
    {
        for (int f = 0; f < frames; ++f)
            active[static_cast<std::size_t>(f)] = sound->amp[p][f] > 0.0 && sound->frq[p][f] > 0.0 ? 1 : 0;
        const double w = weight[static_cast<std::size_t>(p)];

        if (shimmer > 0.0 && w > 0.0)
        {
            filteredNoise(params, kAmpChannel, p, frames, a, active, noise);
            const double sigma = shimmer * w * kLn10Over20; // natural-log sigma
            const double meanFix = std::exp(-0.5 * sigma * sigma);
            for (int f = 0; f < frames; ++f)
                if (active[static_cast<std::size_t>(f)])
                    out->amp[p][f] = sound->amp[p][f] * meanFix * std::exp(sigma * noise[static_cast<std::size_t>(f)]);
        }
        if (jitter > 0.0 && w > 0.0)
        {
            filteredNoise(params, kFrqChannel, p, frames, a, active, noise);
            const double sigma = jitter * w * kLn2Over1200;
            const double meanFix = std::exp(-0.5 * sigma * sigma);
            const double top = sound->srate > 0 ? 0.49 * sound->srate : 20000.0;
            for (int f = 0; f < frames; ++f)
                if (active[static_cast<std::size_t>(f)])
                    out->frq[p][f] = std::min(top, sound->frq[p][f] * meanFix * std::exp(sigma * noise[static_cast<std::size_t>(f)]));
        }
        for (int f = 0; f < frames; ++f)
            frqmax = std::max(frqmax, out->frq[p][f]);
    }
    out->frqmax = frqmax;
    return result;
}

} // namespace ats
