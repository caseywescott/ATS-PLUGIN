#include "AtsTransientSmear.h"

#include <algorithm>
#include <cmath>

namespace ats
{
namespace
{
constexpr double kPi = 3.141592653589793238462643383279;
constexpr double kTwoPi = 6.283185307179586476925286766559;
constexpr double kEpsilon = 1.0e-12;

bool finitePositive(double v)
{
    return std::isfinite(v) && v > 0.0;
}

double activeAmp(const ATS_SOUND* s, int p, int f)
{
    if (s->amp == nullptr || s->amp[p] == nullptr)
        return 0.0;
    const double a = s->amp[p][f];
    return finitePositive(a) ? a : 0.0;
}

double bandEnergy(const ATS_SOUND* s, int b, int f)
{
    if (s->band_energy == nullptr || s->band_energy[b] == nullptr)
        return 0.0;
    const double e = s->band_energy[b][f];
    return std::isfinite(e) && e > 0.0 ? e : 0.0;
}

double hopSeconds(const ATS_SOUND* s)
{
    if (s->time != nullptr && s->time[0] != nullptr && s->frames > 1)
    {
        const double dt = s->time[0][1] - s->time[0][0];
        if (finitePositive(dt))
            return dt;
    }
    if (s->srate > 0 && s->frame_size > 0)
        return (double) s->frame_size / (double) s->srate;
    return 0.0;
}

int secondsToFrames(double seconds, double hop)
{
    if (!finitePositive(hop) || seconds <= 0.0)
        return 0;
    return (int) std::lround(seconds / hop);
}

double clampPositiveFinite(double v)
{
    return std::isfinite(v) && v > 0.0 ? v : 0.0;
}

// Wrap to [-pi, pi].
double wrapPhase(double x)
{
    x = std::fmod(x + kPi, kTwoPi);
    if (x < 0.0)
        x += kTwoPi;
    return x - kPi;
}

double medianOf(std::vector<double>& scratch)
{
    if (scratch.empty())
        return 0.0;
    const size_t mid = scratch.size() / 2;
    std::nth_element(scratch.begin(), scratch.begin() + (long) mid, scratch.end());
    const double hi = scratch[mid];
    if (scratch.size() % 2 == 1)
        return hi;
    std::nth_element(scratch.begin(), scratch.begin() + (long) (mid - 1), scratch.end());
    return 0.5 * (hi + scratch[mid - 1]);
}

// Robust standardization: subtract running median, divide by running MAD, half-wave rectify.
std::vector<double> normalizeStream(const std::vector<double>& raw, int radius)
{
    const int n = (int) raw.size();
    std::vector<double> out(raw.size(), 0.0);
    if (n == 0)
        return out;
    radius = std::max(radius, 1);
    std::vector<double> win;
    std::vector<double> dev;
    win.reserve((size_t) (2 * radius + 1));
    dev.reserve((size_t) (2 * radius + 1));
    for (int f = 0; f < n; ++f)
    {
        const int lo = std::max(0, f - radius);
        const int hi = std::min(n - 1, f + radius);
        win.clear();
        for (int k = lo; k <= hi; ++k)
            win.push_back(raw[(size_t) k]);
        const double med = medianOf(win);
        dev.clear();
        for (int k = lo; k <= hi; ++k)
            dev.push_back(std::abs(raw[(size_t) k] - med));
        const double mad = medianOf(dev);
        const double denom = 1.4826 * mad + 1.0e-12;
        const double z = (raw[(size_t) f] - med) / denom;
        out[(size_t) f] = std::max(z, 0.0);
    }
    return out;
}

double globalMad(const std::vector<double>& d)
{
    if (d.empty())
        return 0.0;
    std::vector<double> tmp(d);
    const double med = medianOf(tmp);
    for (auto& v : tmp)
        v = std::abs(v - med);
    return medianOf(tmp);
}

bool smearTargetsPartials(SmearnovTarget target)
{
    return target == SmearnovTarget::Partials || target == SmearnovTarget::Both;
}

bool smearTargetsResidual(SmearnovTarget target)
{
    return target == SmearnovTarget::Residual || target == SmearnovTarget::Both;
}

bool smearParamsActive(const SmearnovParams& params)
{
    return params.blurMs > 0.0
           || params.sharpenAmount > 0.0
           || std::abs(params.exaggerateDb) > 1.0e-9;
}

// Reusable scratch for the per-envelope smear pipeline; avoids re-allocating a
// handful of small vectors on every partial/band the smear touches.
struct EnvelopeScratch
{
    std::vector<double> original;
    std::vector<double> values;
    std::vector<double> low;     // blurred copy used by sharpen
    std::vector<double> blurred; // blur-stage output
    std::vector<double> kernel;
};

// Gaussian-blur `in` into `out` (resized to match), reusing `kernel` as scratch.
void gaussianBlurInto(const std::vector<double>& in,
                      double sigmaFrames,
                      std::vector<double>& out,
                      std::vector<double>& kernel)
{
    if (in.size() <= 1 || !(sigmaFrames > 0.0) || !std::isfinite(sigmaFrames))
    {
        out = in;
        return;
    }

    const int radius = std::max(1, (int) std::ceil(3.0 * sigmaFrames));
    kernel.assign((size_t) (2 * radius + 1), 0.0);
    double kernelSum = 0.0;
    const double denom = 2.0 * sigmaFrames * sigmaFrames;
    for (int k = -radius; k <= radius; ++k)
    {
        const double w = std::exp(-(double) (k * k) / denom);
        kernel[(size_t) (k + radius)] = w;
        kernelSum += w;
    }
    if (kernelSum <= 0.0)
    {
        out = in;
        return;
    }
    for (auto& w : kernel)
        w /= kernelSum;

    const int n = (int) in.size();
    out.assign(in.size(), 0.0);
    for (int i = 0; i < n; ++i)
    {
        double acc = 0.0;
        double wsum = 0.0;
        for (int k = -radius; k <= radius; ++k)
        {
            const int j = i + k;
            if (j < 0 || j >= n)
                continue;
            const double w = kernel[(size_t) (k + radius)];
            acc += w * in[(size_t) j];
            wsum += w;
        }
        out[(size_t) i] = wsum > 0.0 ? acc / wsum : in[(size_t) i];
    }
}

double sumOf(const std::vector<double>& values)
{
    double sum = 0.0;
    for (const auto v : values)
        sum += v;
    return sum;
}

double peakOf(const std::vector<double>& values)
{
    double peak = 0.0;
    for (const auto v : values)
        peak = std::max(peak, v);
    return peak;
}

double maxSlopeOf(const std::vector<double>& values)
{
    double slope = 0.0;
    for (size_t i = 1; i < values.size(); ++i)
        slope = std::max(slope, std::abs(values[i] - values[i - 1]));
    return slope;
}

void rescaleToSum(std::vector<double>& values, double targetSum)
{
    const double current = sumOf(values);
    if (current > kEpsilon && std::isfinite(current) && std::isfinite(targetSum))
    {
        const double scale = targetSum / current;
        for (auto& v : values)
            v = clampPositiveFinite(v * scale);
    }
}

void clampToPeakPreservingSum(std::vector<double>& values, double peakBound, double targetSum)
{
    if (!(peakBound > 0.0) || !std::isfinite(peakBound) || !std::isfinite(targetSum))
        return;

    for (int pass = 0; pass < 8; ++pass)
    {
        bool clippedAny = false;
        for (auto& v : values)
        {
            v = clampPositiveFinite(v);
            if (v > peakBound)
            {
                v = peakBound;
                clippedAny = true;
            }
        }

        const double deficit = targetSum - sumOf(values);
        if (std::abs(deficit) <= 1.0e-12)
            return;
        if (deficit < 0.0)
        {
            rescaleToSum(values, targetSum);
            continue;
        }

        double headroom = 0.0;
        for (const auto v : values)
            headroom += std::max(0.0, peakBound - v);
        if (headroom <= kEpsilon)
            return;

        for (auto& v : values)
            v += deficit * (std::max(0.0, peakBound - v) / headroom);

        if (!clippedAny)
            break;
    }
}

void enforceBlurSlopeLimit(std::vector<double>& blurred, const std::vector<double>& original)
{
    const double originalSlope = maxSlopeOf(original);
    if (maxSlopeOf(blurred) <= originalSlope + 1.0e-12)
        return;

    std::vector<double> candidate(blurred.size(), 0.0);
    double lo = 0.0;
    double hi = 1.0;
    for (int iter = 0; iter < 32; ++iter)
    {
        const double alpha = 0.5 * (lo + hi);
        for (size_t i = 0; i < blurred.size(); ++i)
            candidate[i] = original[i] + alpha * (blurred[i] - original[i]);
        if (maxSlopeOf(candidate) <= originalSlope + 1.0e-12)
            lo = alpha;
        else
            hi = alpha;
    }

    for (size_t i = 0; i < blurred.size(); ++i)
        blurred[i] = original[i] + lo * (blurred[i] - original[i]);
}

double dbToLinearGain(double db, bool energyTarget)
{
    return std::pow(10.0, db / (energyTarget ? 10.0 : 20.0));
}

double smoothStep(double x)
{
    x = std::clamp(x, 0.0, 1.0);
    return x * x * (3.0 - 2.0 * x);
}

// `peakOffset` is the index of the attack peak within [startFrame, endFrame]; the
// Exaggerate ramp reaches full gain there and holds across any extended decay tail,
// so widening the window for Blur never shifts where Exaggerate peaks.
bool transformEnvelope(double* row,
                       int startFrame,
                       int endFrame,
                       int peakOffset,
                       double hop,
                       const SmearnovParams& params,
                       bool energyTarget,
                       EnvelopeScratch& scratch)
{
    if (row == nullptr || endFrame < startFrame)
        return false;

    const int count = endFrame - startFrame + 1;
    if (count <= 0)
        return false;
    peakOffset = std::clamp(peakOffset, 0, count - 1);

    auto& original = scratch.original;
    auto& values = scratch.values;
    original.assign((size_t) count, 0.0);
    for (int i = 0; i < count; ++i)
        original[(size_t) i] = clampPositiveFinite(row[startFrame + i]);

    values = original;
    const double originalSum = sumOf(original);
    const double originalPeak = peakOf(original);

    bool stageRan = false;
    const double blurSeconds = params.blurMs * 0.001;
    if (blurSeconds > 0.0 && finitePositive(hop))
    {
        const double sigmaFrames = blurSeconds / hop;
        if (sigmaFrames > 0.0)
        {
            gaussianBlurInto(values, sigmaFrames, scratch.blurred, scratch.kernel);
            rescaleToSum(scratch.blurred, originalSum);
            clampToPeakPreservingSum(scratch.blurred, originalPeak, originalSum);
            enforceBlurSlopeLimit(scratch.blurred, original);
            values = scratch.blurred;
            stageRan = true;
        }
    }

    if (params.sharpenAmount > 0.0 && std::isfinite(params.sharpenAmount))
    {
        const double sigmaFrames = std::max(0.5, blurSeconds > 0.0 && finitePositive(hop)
                                                     ? blurSeconds / hop
                                                     : 1.0);
        gaussianBlurInto(values, sigmaFrames, scratch.low, scratch.kernel);
        const auto& low = scratch.low;
        for (size_t i = 0; i < values.size(); ++i)
        {
            const double sharpened = values[i] + params.sharpenAmount * (values[i] - low[i]);
            values[i] = std::clamp(clampPositiveFinite(sharpened), 0.0, originalPeak);
        }
        stageRan = true;
    }

    const double maxDb = std::isfinite(params.maxDb) && params.maxDb > 0.0 ? params.maxDb : 12.0;
    const double exDb = std::clamp(std::isfinite(params.exaggerateDb) ? params.exaggerateDb : 0.0,
                                   -maxDb,
                                   maxDb);
    if (std::abs(exDb) > 1.0e-9)
    {
        const double rampDenom = peakOffset > 0 ? (double) peakOffset : 1.0;
        for (int i = 0; i < count; ++i)
        {
            // Ramp up to the attack peak, then hold the boost across the decay tail.
            const double x = count == 1 ? 1.0 : std::min(1.0, (double) i / rampDenom);
            values[(size_t) i] = clampPositiveFinite(values[(size_t) i]
                                                     * dbToLinearGain(exDb * smoothStep(x),
                                                                      energyTarget));
        }
        stageRan = true;
    }

    if (!stageRan)
        return false;

    const double peakBound = originalPeak * dbToLinearGain(std::max(exDb, 0.0), energyTarget);
    for (auto& v : values)
        v = std::clamp(clampPositiveFinite(v), 0.0, std::max(0.0, peakBound));

    // Dry/wet: blend the transformed envelope back toward the original.
    // mix == 1 keeps the full effect; mix == 0 yields the dry envelope (no change).
    const double mix = std::clamp(std::isfinite(params.mix) ? params.mix : 1.0, 0.0, 1.0);
    if (mix < 1.0)
    {
        for (int i = 0; i < count; ++i)
        {
            const double dry = original[(size_t) i];
            values[(size_t) i] = dry + mix * (values[(size_t) i] - dry);
        }
    }

    bool changed = false;
    for (int i = 0; i < count; ++i)
    {
        const double next = values[(size_t) i];
        if (std::abs(row[startFrame + i] - next) > 1.0e-12)
            changed = true;
        row[startFrame + i] = next;
    }
    return changed;
}

void refreshAmplitudeMetadata(ATS_SOUND* sound)
{
    if (sound == nullptr || sound->amp == nullptr || sound->partials <= 0 || sound->frames <= 0)
        return;

    double ampMax = 0.0;
    std::vector<double> partialMean((size_t) sound->partials, 0.0);
    for (int p = 0; p < sound->partials; ++p)
    {
        if (sound->amp[p] == nullptr)
            continue;
        double sum = 0.0;
        int count = 0;
        for (int f = 0; f < sound->frames; ++f)
        {
            const double a = clampPositiveFinite(sound->amp[p][f]);
            ampMax = std::max(ampMax, a);
            if (a > 0.0)
            {
                sum += a;
                ++count;
            }
        }
        partialMean[(size_t) p] = count > 0 ? sum / (double) count : 0.0;
    }

    sound->ampmax = ampMax;
    if (sound->av == nullptr)
        return;

    for (int i = 0; i < sound->partials; ++i)
    {
        const int track = std::clamp(sound->av[i].track, 0, sound->partials - 1);
        sound->av[i].amp = partialMean[(size_t) track];
    }
}
} // namespace

namespace
{
static constexpr double kCriticalBandEdges[ATSA_CRITICAL_BANDS + 1] = {
    0.0, 100.0, 200.0, 300.0, 400.0, 510.0, 630.0, 770.0, 920.0,
    1080.0, 1270.0, 1480.0, 1720.0, 2000.0, 2320.0, 2700.0, 3150.0,
    3700.0, 4400.0, 5300.0, 6400.0, 7700.0, 9500.0, 12000.0, 15500.0,
    20000.0
};
}

double criticalBandEdgeHz(int edge)
{
    return kCriticalBandEdges[std::clamp(edge, 0, ATSA_CRITICAL_BANDS)];
}

double criticalBandCentreHz(int band)
{
    // Zwicker & Fastl (1990) critical-band edges; matches ATSA_CRITICAL_BAND_EDGES.
    if (band < 0)
        band = 0;
    if (band >= ATSA_CRITICAL_BANDS)
        band = ATSA_CRITICAL_BANDS - 1;
    return 0.5 * (criticalBandEdgeHz(band) + criticalBandEdgeHz(band + 1));
}

std::vector<AtsOnset> detectOnsets(const ATS_SOUND* sound,
                                   const SmearnovDetectParams& params,
                                   SmearnovDetectionFunctions* functionsOut,
                                   const std::vector<double>* transientness)
{
    std::vector<AtsOnset> onsets;
    if (sound == nullptr || sound->frames < 2)
        return onsets;

    const int frames = sound->frames;
    const int partials = std::max(sound->partials, 0);
    const double hop = hopSeconds(sound);
    const bool hasResidual = sound->band_energy != nullptr;
    const bool hasPhase = params.usePhaseDeviation && sound->pha != nullptr;

    // D6: external transientness index (AtsTransientness.h), clamped to [0,1].
    const bool hasTransientness = transientness != nullptr && !transientness->empty();
    const auto indexAt = [&](int f) {
        const int last = (int) transientness->size() - 1;
        const double v = (*transientness)[(size_t) std::clamp(f, 0, last)];
        return std::isfinite(v) ? std::clamp(v, 0.0, 1.0) : 0.0;
    };
    const bool useTransientness = hasTransientness && params.weightTransientness > 0.0
                                  && std::isfinite(params.weightTransientness);

    // Perceptual SMR weighting reference.
    double maxSmr = 0.0;
    const bool smrWeighting = params.useSmrWeighting && sound->smr != nullptr;
    if (smrWeighting)
        for (int p = 0; p < partials; ++p)
            if (sound->smr[p] != nullptr)
                for (int f = 0; f < frames; ++f)
                    if (std::isfinite(sound->smr[p][f]) && sound->smr[p][f] > maxSmr)
                        maxSmr = sound->smr[p][f];

    std::vector<double> d1(frames, 0.0); // deterministic energy flux
    std::vector<double> d2(frames, 0.0); // residual energy flux
    std::vector<double> d3(frames, 0.0); // residual HFC flux
    std::vector<double> d4(frames, 0.0); // partial-birth rate
    std::vector<double> d5(frames, 0.0); // phase deviation
    std::vector<double> energy(frames, 0.0); // total energy proxy for attack scan

    std::vector<double> hfc(frames, 0.0);
    if (hasResidual)
        for (int b = 0; b < ATSA_CRITICAL_BANDS; ++b)
        {
            const double c = criticalBandCentreHz(b);
            for (int f = 0; f < frames; ++f)
                hfc[(size_t) f] += c * bandEnergy(sound, b, f);
        }

    for (int f = 0; f < frames; ++f)
    {
        double detEnergy = 0.0;
        for (int p = 0; p < partials; ++p)
        {
            const double a = activeAmp(sound, p, f);
            detEnergy += a * a;

            if (f == 0)
                continue;

            const double aPrev = activeAmp(sound, p, f - 1);
            const double rise = a - aPrev;
            if (rise > 0.0)
            {
                double w = 1.0;
                if (smrWeighting && maxSmr > 0.0 && sound->smr[p] != nullptr
                    && std::isfinite(sound->smr[p][f]))
                {
                    const double r = std::clamp(sound->smr[p][f] / maxSmr, 0.0, 1.0);
                    w = params.smrWeightFloor + (1.0 - params.smrWeightFloor) * r;
                }
                d1[(size_t) f] += w * rise;
            }

            // Partial birth: inactive -> active.
            if (aPrev <= 0.0 && a > 0.0)
                d4[(size_t) f] += 1.0;

            if (hasPhase && aPrev > 0.0 && a > 0.0 && sound->pha[p] != nullptr
                && sound->frq != nullptr && sound->frq[p] != nullptr)
            {
                // Expected phase advance uses the frequency during the elapsed
                // hop [f-1, f], i.e. the previous frame's instantaneous freq.
                const double predicted = sound->pha[p][f - 1] + kTwoPi * sound->frq[p][f - 1] * hop;
                d5[(size_t) f] += std::abs(wrapPhase(sound->pha[p][f] - predicted));
            }
        }

        double resEnergy = 0.0;
        if (hasResidual)
        {
            for (int b = 0; b < ATSA_CRITICAL_BANDS; ++b)
            {
                const double e = bandEnergy(sound, b, f);
                resEnergy += e;
                if (f > 0)
                {
                    const double rise = e - bandEnergy(sound, b, f - 1);
                    if (rise > 0.0)
                        d2[(size_t) f] += rise;
                }
            }
            if (f > 0)
            {
                const double hfcRise = hfc[(size_t) f] - hfc[(size_t) (f - 1)];
                if (hfcRise > 0.0)
                    d3[(size_t) f] = hfcRise;
            }
        }

        energy[(size_t) f] = detEnergy + resEnergy;
    }

    const auto n1 = normalizeStream(d1, params.normWindowRadiusFrames);
    const auto n2 = normalizeStream(d2, params.normWindowRadiusFrames);
    const auto n3 = normalizeStream(d3, params.normWindowRadiusFrames);
    const auto n4 = normalizeStream(d4, params.normWindowRadiusFrames);
    const auto n5 = normalizeStream(d5, params.normWindowRadiusFrames);

    std::vector<double> combined(frames, 0.0);
    for (int f = 0; f < frames; ++f)
        combined[(size_t) f] = params.weightDetFlux * n1[(size_t) f]
                             + params.weightResFlux * n2[(size_t) f]
                             + params.weightHfcFlux * n3[(size_t) f]
                             + params.weightBirth * n4[(size_t) f]
                             + (hasPhase ? params.weightPhaseDev * n5[(size_t) f] : 0.0);

    if (useTransientness)
    {
        // Half-wave-rectified difference of the index: a jump toward "transient".
        std::vector<double> d6(frames, 0.0);
        for (int f = 1; f < frames; ++f)
            d6[(size_t) f] = std::max(0.0, indexAt(f) - indexAt(f - 1));
        const auto n6 = normalizeStream(d6, params.normWindowRadiusFrames);
        for (int f = 0; f < frames; ++f)
            combined[(size_t) f] += params.weightTransientness * n6[(size_t) f];
    }

    // Adaptive threshold: running median over the peak window + lambda.
    const double lambda = params.thresholdLambda > 0.0
                              ? params.thresholdLambda
                              : params.thresholdAutoScale * globalMad(combined);
    const int w = std::max(params.peakWindowRadiusFrames, 1);
    std::vector<double> threshold(frames, 0.0);
    {
        std::vector<double> win;
        win.reserve((size_t) (2 * w + 1));
        for (int f = 0; f < frames; ++f)
        {
            const int lo = std::max(0, f - w);
            const int hi = std::min(frames - 1, f + w);
            win.clear();
            for (int k = lo; k <= hi; ++k)
                win.push_back(combined[(size_t) k]);
            threshold[(size_t) f] = medianOf(win) + lambda;
        }
    }

    if (functionsOut != nullptr)
    {
        functionsOut->combined = combined;
        functionsOut->threshold = threshold;
    }

    int minGap = params.minGapFramesOverride > 0 ? params.minGapFramesOverride
                                                 : secondsToFrames(params.minGapSeconds, hop);
    minGap = std::max(minGap, 1);
    const int preMargin = secondsToFrames(params.preMarginSeconds, hop);

    // Peak picking: local maximum, above adaptive threshold, respecting the gap.
    // Strengths within kTieTolerance (relative) count as equal so a flat-topped
    // peak — e.g. a linear attack whose per-frame rises are mathematically
    // identical — resolves to its earliest frame instead of whichever frame
    // rounding noise favours. normalizeStream can amplify that noise enormously
    // when a window's MAD is zero, which made the chosen frame differ between
    // x86_64 and arm64.
    constexpr double kTieTolerance = 1.0e-9;
    const auto clearlyGreater = [](double a, double b) {
        return a > b + kTieTolerance * std::max(std::abs(a), std::abs(b));
    };
    std::vector<int> peakFrames;
    int lastOnset = -minGap - 1;
    for (int f = 0; f < frames; ++f)
    {
        const double v = combined[(size_t) f];
        if (v <= params.onsetFloor || v < threshold[(size_t) f])
            continue;

        bool isLocalMax = true;
        for (int k = 1; k <= w && isLocalMax; ++k)
        {
            if (f - k >= 0 && clearlyGreater(combined[(size_t) (f - k)], v))
                isLocalMax = false;
            if (f + k < frames && clearlyGreater(combined[(size_t) (f + k)], v))
                isLocalMax = false;
        }
        if (!isLocalMax)
            continue;

        if (f - lastOnset < minGap)
        {
            // Keep the stronger of the two within the refractory window.
            if (!peakFrames.empty() && clearlyGreater(v, combined[(size_t) peakFrames.back()]))
            {
                peakFrames.back() = f;
                lastOnset = f;
            }
            continue;
        }

        peakFrames.push_back(f);
        lastOnset = f;
    }

    // Build attack windows from a lightly smoothed energy curve.
    auto smoothEnergy = [&](int f) {
        const int lo = std::max(0, f - 1);
        const int hi = std::min(frames - 1, f + 1);
        double sum = 0.0;
        for (int k = lo; k <= hi; ++k)
            sum += energy[(size_t) k];
        return sum / (double) (hi - lo + 1);
    };

    for (size_t i = 0; i < peakFrames.size(); ++i)
    {
        const int f0 = peakFrames[i];
        const int nextOnset = (i + 1 < peakFrames.size()) ? peakFrames[i + 1] : frames;
        int limit = nextOnset - 1;
        if (params.maxAttackFrames > 0)
            limit = std::min(limit, f0 + params.maxAttackFrames);
        limit = std::min(limit, frames - 1);

        int fPeak = f0;
        double prev = smoothEnergy(f0);
        for (int f = f0 + 1; f <= limit; ++f)
        {
            const double cur = smoothEnergy(f);
            if (cur <= prev + 1.0e-15)
                break; // stop at the first frame that no longer rises
            fPeak = f;
            prev = cur;
        }

        AtsOnset onset;
        onset.frame = f0;
        onset.preFrame = std::max(0, f0 - preMargin);
        onset.peakFrame = std::max(fPeak, f0);
        onset.strength = combined[(size_t) f0];
        onset.transientness = hasTransientness ? indexAt(f0) : -1.0;
        onsets.push_back(onset);
    }

    return onsets;
}

bool smearAttacks(ATS_SOUND* sound, const std::vector<AtsOnset>& onsets, const SmearnovParams& params)
{
    if (sound == nullptr || sound->frames <= 0 || onsets.empty() || !smearParamsActive(params))
        return false;

    const double hop = hopSeconds(sound);
    const bool editPartials = smearTargetsPartials(params.targets)
                              && sound->amp != nullptr
                              && sound->partials > 0;
    const bool editResidual = smearTargetsResidual(params.targets)
                              && sound->band_energy != nullptr;
    if (!editPartials && !editResidual)
        return false;

    bool changed = false;
    EnvelopeScratch scratch;
    for (size_t i = 0; i < onsets.size(); ++i)
    {
        const auto& onset = onsets[i];

        // Optional per-onset scaling of the effective amount by the transientness
        // index: tonal onsets (index ~0) are barely touched, clicks get the full amount.
        SmearnovParams effective = params;
        if (params.scaleByTransientness && std::isfinite(onset.transientness) && onset.transientness >= 0.0)
        {
            const double scale = std::clamp(onset.transientness, 0.0, 1.0);
            effective.blurMs = params.blurMs * scale;
            effective.sharpenAmount = params.sharpenAmount * scale;
            effective.exaggerateDb = params.exaggerateDb * scale;
            if (!smearParamsActive(effective))
                continue;
        }

        // Blur needs room past the attack peak to spread energy forward into the decay;
        // reach scales with the blur kernel (~3 sigma), bounded by the next onset.
        const double sigmaFrames = (effective.blurMs > 0.0 && finitePositive(hop))
                                       ? (effective.blurMs * 0.001) / hop
                                       : 0.0;
        const int blurExtend = sigmaFrames > 0.0 ? (int) std::ceil(3.0 * sigmaFrames) : 0;

        int start = std::min(onset.preFrame, onset.peakFrame);
        int peak = std::max(onset.preFrame, onset.peakFrame);
        start = std::clamp(start, 0, sound->frames - 1);
        peak = std::clamp(peak, 0, sound->frames - 1);
        if (peak < start)
            continue;

        int end = peak;
        if (blurExtend > 0)
        {
            int bound = sound->frames - 1;
            if (i + 1 < onsets.size())
            {
                const auto& next = onsets[i + 1];
                const int nextStart = std::min(next.preFrame, next.frame);
                bound = std::min(bound, nextStart - 1);
            }
            end = std::clamp(peak + blurExtend, peak, std::max(peak, bound));
        }

        const int peakOffset = peak - start;

        if (editPartials)
        {
            for (int p = 0; p < sound->partials; ++p)
                if (sound->amp[p] != nullptr)
                    changed = transformEnvelope(sound->amp[p], start, end, peakOffset, hop, effective, false, scratch)
                              || changed;
        }

        if (editResidual)
        {
            for (int b = 0; b < ATSA_CRITICAL_BANDS; ++b)
                if (sound->band_energy[b] != nullptr)
                    changed = transformEnvelope(sound->band_energy[b], start, end, peakOffset, hop, effective, true, scratch)
                              || changed;
        }
    }

    if (changed && editPartials)
        refreshAmplitudeMetadata(sound);

    return changed;
}

} // namespace ats
