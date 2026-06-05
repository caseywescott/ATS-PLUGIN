#include "AtsSpectralEnvelope.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ats
{
namespace
{

double clampUnit(double value)
{
    if (!std::isfinite(value))
        return 0.0;
    return std::clamp(value, 0.0, 1.0);
}

double safeFloor(double value)
{
    if (!std::isfinite(value) || value <= 0.0)
        return BarkSpectralEnvelope::kDefaultAmplitudeFloor;
    return std::max(value, std::numeric_limits<double>::min());
}

double amplitudeToDb(double amplitude, double floor)
{
    return 20.0 * std::log10(std::max(amplitude, floor));
}

constexpr double kPi = 3.14159265358979323846;

// Low-quefrency (cepstral) low-pass of a log-grid via a DCT-II / IDCT-II pair, keeping only
// coefficients 0..order. This is the smoothing kernel of the True Envelope iteration. The
// cosine table cos(pi*(b+0.5)*k/N) is precomputed by the caller and shared across iterations.
void cepstralLowpass(const std::vector<double>& in,
                     std::vector<double>& out,
                     const std::vector<double>& cosTab,
                     int order)
{
    const int n = static_cast<int>(in.size());
    out.assign(static_cast<size_t>(n), 0.0);
    if (n <= 0)
        return;

    std::vector<double> cep(static_cast<size_t>(order + 1), 0.0);
    for (int k = 0; k <= order; ++k)
    {
        double s = 0.0;
        const double* row = &cosTab[static_cast<size_t>(k) * static_cast<size_t>(n)];
        for (int b = 0; b < n; ++b)
            s += in[static_cast<size_t>(b)] * row[b];
        cep[static_cast<size_t>(k)] = s;
    }

    const double invN = 1.0 / static_cast<double>(n);
    for (int b = 0; b < n; ++b)
    {
        double acc = cep[0];
        for (int k = 1; k <= order; ++k)
            acc += 2.0 * cep[static_cast<size_t>(k)]
                       * cosTab[static_cast<size_t>(k) * static_cast<size_t>(n) + static_cast<size_t>(b)];
        out[static_cast<size_t>(b)] = acc * invN;
    }
}

// Geometric (log-frequency) interpolation: the perceptual midpoint of two pitches is their
// geometric mean, not their arithmetic mean. Falls back to linear if an endpoint is not a
// usable positive frequency.
double logInterpHz(double f0, double f1, double t)
{
    if (!std::isfinite(f0) || !std::isfinite(f1) || f0 <= 0.0 || f1 <= 0.0)
        return f0 + (f1 - f0) * t;
    return std::exp(std::log(f0) + (std::log(f1) - std::log(f0)) * t);
}

// Equal-power (constant-energy) interpolation: interpolate power linearly, then take the
// root. Avoids the perceived loudness sag a linear-amplitude blend produces at the midpoint.
double equalPowerInterpAmp(double a0, double a1, double t)
{
    const double power = (1.0 - t) * a0 * a0 + t * a1 * a1;
    return power > 0.0 ? std::sqrt(power) : 0.0;
}

} // namespace

double BarkSpectralEnvelope::hzToBark(double frequencyHz)
{
    if (!std::isfinite(frequencyHz) || frequencyHz <= 0.0)
        return 0.0;

    return 13.0 * std::atan(0.00076 * frequencyHz)
           + 3.5 * std::atan((frequencyHz / 7500.0) * (frequencyHz / 7500.0));
}

void BarkSpectralEnvelope::build(const std::vector<SpectralEnvelopePoint>& points,
                                 int binCount,
                                 int lifterOrder,
                                 double newAmplitudeFloor)
{
    valid = false;
    maxBark = kDefaultMaxBark;
    amplitudeFloor = safeFloor(newAmplitudeFloor);
    peakAmplitude = amplitudeFloor;
    binCount = std::max(2, binCount);

    ampBins.assign(static_cast<size_t>(binCount), amplitudeFloor);
    logBins.assign(static_cast<size_t>(binCount), std::log(amplitudeFloor));

    const double logFloor = std::log(amplitudeFloor);

    // Seed the log-amplitude grid from the peaks. Each peak lands in its nearest Bark bin and,
    // where several share a bin, the louder one wins — the envelope is an upper bound on the
    // spectral peaks, not an average across them.
    std::vector<double> seed(static_cast<size_t>(binCount),
                             std::numeric_limits<double>::quiet_NaN());
    std::vector<double> barkPositions;
    barkPositions.reserve(points.size());
    double maxLogAmp = logFloor;
    int validPointCount = 0;
    for (const auto& point : points)
    {
        if (!std::isfinite(point.frequencyHz) || !std::isfinite(point.amplitude)
            || point.frequencyHz <= 0.0 || point.amplitude <= 0.0)
        {
            continue;
        }

        ++validPointCount;
        const double bark = hzToBark(point.frequencyHz);
        const double rawBin = bark / maxBark * static_cast<double>(binCount - 1);
        const int bin = std::clamp(static_cast<int>(std::llround(rawBin)), 0, binCount - 1);
        const double logAmp = std::log(std::max(amplitudeFloor, point.amplitude));

        double& cell = seed[static_cast<size_t>(bin)];
        cell = std::isnan(cell) ? logAmp : std::max(cell, logAmp);
        barkPositions.push_back(bark);
        maxLogAmp = std::max(maxLogAmp, logAmp);
    }

    if (validPointCount == 0)
        return;

    // Fill the gaps between seeded bins with log-linear interpolation (constant hold beyond the
    // outermost peaks). This gives the True Envelope iteration a peak-connecting starting curve
    // rather than deep -inf valleys that would induce inter-harmonic ripple.
    std::vector<int> anchors;
    anchors.reserve(static_cast<size_t>(binCount));
    for (int b = 0; b < binCount; ++b)
        if (!std::isnan(seed[static_cast<size_t>(b)]))
            anchors.push_back(b);

    std::vector<double> logGrid(static_cast<size_t>(binCount), logFloor);
    {
        const int first = anchors.front();
        for (int b = 0; b <= first; ++b)
            logGrid[static_cast<size_t>(b)] = seed[static_cast<size_t>(first)];
        for (size_t ai = 1; ai < anchors.size(); ++ai)
        {
            const int left = anchors[ai - 1];
            const int right = anchors[ai];
            const double lv = seed[static_cast<size_t>(left)];
            const double rv = seed[static_cast<size_t>(right)];
            logGrid[static_cast<size_t>(right)] = rv;
            for (int b = left + 1; b < right; ++b)
            {
                const double t = static_cast<double>(b - left) / static_cast<double>(right - left);
                logGrid[static_cast<size_t>(b)] = lv + t * (rv - lv);
            }
        }
        const int last = anchors.back();
        for (int b = last; b < binCount; ++b)
            logGrid[static_cast<size_t>(b)] = seed[static_cast<size_t>(last)];
    }

    // Auto-select the cepstral order from the median Bark spacing of the partials. A coarser
    // harmonic comb (sparse partials) needs a lower order to avoid resolving — and rippling
    // between — the harmonics, while keeping the broad formant shape.
    int order = lifterOrder;
    if (order <= 0)
    {
        double medianSpacing = maxBark;
        if (barkPositions.size() >= 2)
        {
            std::sort(barkPositions.begin(), barkPositions.end());
            std::vector<double> gaps;
            gaps.reserve(barkPositions.size());
            for (size_t i = 1; i < barkPositions.size(); ++i)
            {
                const double g = barkPositions[i] - barkPositions[i - 1];
                if (g > 1.0e-6)
                    gaps.push_back(g);
            }
            if (!gaps.empty())
            {
                std::sort(gaps.begin(), gaps.end());
                medianSpacing = gaps[gaps.size() / 2];
            }
        }
        // Highest retained cosine has a half-period of maxBark/order Bark; cap it at the
        // harmonic spacing so the comb is smoothed out.
        order = static_cast<int>(std::floor(maxBark / std::max(medianSpacing, 1.0e-3)));
    }
    order = std::clamp(order, 3, std::min(binCount - 1, kMaxLifterOrder));

    // Precompute cos(pi*(b+0.5)*k/N) for the DCT pair, shared across iterations.
    std::vector<double> cosTab(static_cast<size_t>(order + 1) * static_cast<size_t>(binCount));
    for (int k = 0; k <= order; ++k)
        for (int b = 0; b < binCount; ++b)
            cosTab[static_cast<size_t>(k) * static_cast<size_t>(binCount) + static_cast<size_t>(b)] =
                std::cos(kPi * (static_cast<double>(b) + 0.5) * static_cast<double>(k)
                         / static_cast<double>(binCount));

    // True Envelope iteration: smooth, then lift the working spectrum up to the smoothed
    // envelope where it dipped below. Converges to a smooth curve riding the spectral peaks.
    std::vector<double> work = logGrid;
    std::vector<double> env = logGrid;
    std::vector<double> smoothed;
    constexpr int kMaxIterations = 32;
    constexpr double kConvergenceTol = 0.02; // ~0.17 dB in log-amplitude
    for (int iter = 0; iter < kMaxIterations; ++iter)
    {
        cepstralLowpass(work, smoothed, cosTab, order);
        double maxDelta = 0.0;
        for (int b = 0; b < binCount; ++b)
        {
            maxDelta = std::max(maxDelta,
                                std::abs(smoothed[static_cast<size_t>(b)] - env[static_cast<size_t>(b)]));
            env[static_cast<size_t>(b)] = smoothed[static_cast<size_t>(b)];
            work[static_cast<size_t>(b)] = std::max(work[static_cast<size_t>(b)],
                                                    smoothed[static_cast<size_t>(b)]);
        }
        if (maxDelta < kConvergenceTol)
            break;
    }

    // Convert to linear amplitude, bounded by the source's own dynamic range (the loudest
    // partial) so cepstral overshoot near tall isolated peaks cannot synthesise gain.
    peakAmplitude = amplitudeFloor;
    for (int b = 0; b < binCount; ++b)
    {
        const double logA = std::clamp(env[static_cast<size_t>(b)], logFloor, maxLogAmp);
        const double a = std::max(amplitudeFloor, std::exp(logA));
        ampBins[static_cast<size_t>(b)] = a;
        logBins[static_cast<size_t>(b)] = std::log(a);
        peakAmplitude = std::max(peakAmplitude, a);
    }

    valid = true;
}

double BarkSpectralEnvelope::sampleAtHz(double frequencyHz) const
{
    if (!std::isfinite(frequencyHz) || frequencyHz <= 0.0)
        return 0.0;
    return sampleAtBark(hzToBark(frequencyHz));
}

double BarkSpectralEnvelope::sampleAtBark(double bark) const
{
    if (!valid || ampBins.size() < 2 || !std::isfinite(bark))
        return 0.0;

    const double rawBin = bark / maxBark * static_cast<double>(ampBins.size() - 1);
    const double binF = std::clamp(rawBin, 0.0, static_cast<double>(ampBins.size() - 1));
    const int b0 = static_cast<int>(std::floor(binF));
    if (b0 >= static_cast<int>(ampBins.size()) - 1)
        return std::max(amplitudeFloor, ampBins.back());

    const double t = binF - static_cast<double>(b0);
    // Interpolate in the log-amplitude domain to match how the envelope was estimated and
    // morphed — a linear-amplitude blend here would bias the curve between bins. logBins are
    // precomputed in build() (== log(ampBins[i])) to avoid two std::log per call on this path.
    const double l0 = logBins[static_cast<size_t>(b0)];
    const double l1 = logBins[static_cast<size_t>(b0 + 1)];
    return std::max(amplitudeFloor, std::exp(l0 * (1.0 - t) + l1 * t));
}

std::vector<double> BarkSpectralEnvelope::findPeakBarks(double minProminenceDb) const
{
    std::vector<double> peaks;
    const int n = static_cast<int>(ampBins.size());
    if (!valid || n < 3)
        return peaks;

    // Work in the log domain so prominence is expressed in dB. ln-amplitude * 20/ln(10) = dB.
    std::vector<double> logv(static_cast<size_t>(n));
    for (int b = 0; b < n; ++b)
        logv[static_cast<size_t>(b)] = std::log(std::max(amplitudeFloor, ampBins[static_cast<size_t>(b)]));

    const double dbPerLn = 20.0 / std::log(10.0);
    const double binToBark = maxBark / static_cast<double>(n - 1);

    for (int b = 1; b < n - 1; ++b)
    {
        const double v = logv[static_cast<size_t>(b)];
        if (!(v > logv[static_cast<size_t>(b - 1)] && v >= logv[static_cast<size_t>(b + 1)]))
            continue;

        // Topographic prominence: scan outward to the lowest valley on each side before the
        // curve rises above this peak again. The smaller of the two valley drops is the
        // prominence — this rejects shoulders on the flank of a taller formant.
        double leftValley = v;
        for (int i = b - 1; i >= 0; --i)
        {
            if (logv[static_cast<size_t>(i)] > v)
                break;
            leftValley = std::min(leftValley, logv[static_cast<size_t>(i)]);
        }
        double rightValley = v;
        for (int i = b + 1; i < n; ++i)
        {
            if (logv[static_cast<size_t>(i)] > v)
                break;
            rightValley = std::min(rightValley, logv[static_cast<size_t>(i)]);
        }

        const double prominenceDb = (v - std::max(leftValley, rightValley)) * dbPerLn;
        if (prominenceDb < minProminenceDb)
            continue;

        // Sub-bin localization: fit a parabola to the three log-amplitude samples around the
        // peak and take its vertex. Refines the formant position below the bin grid (~0.375
        // Bark at the default resolution), which the warp anchors are built from.
        const double y0 = logv[static_cast<size_t>(b - 1)];
        const double y1 = v;
        const double y2 = logv[static_cast<size_t>(b + 1)];
        const double denom = y0 - 2.0 * y1 + y2;
        double delta = 0.0;
        if (std::abs(denom) > 1.0e-18)
            delta = std::clamp(0.5 * (y0 - y2) / denom, -0.5, 0.5);
        peaks.push_back((static_cast<double>(b) + delta) * binToBark);
    }

    return peaks;
}

namespace
{

// Monotonic (order-preserving) match of two ascending peak lists via a Needleman-Wunsch DP.
// Matching cost is the Bark distance between paired peaks; an unmatched peak on either side
// pays gapPenalty. Returns index pairs (ia, ib) of matched peaks, ascending in both.
std::vector<std::pair<int, int>> matchPeaksMonotonic(const std::vector<double>& a,
                                                     const std::vector<double>& b,
                                                     double gapPenalty)
{
    const int na = static_cast<int>(a.size());
    const int nb = static_cast<int>(b.size());
    std::vector<std::pair<int, int>> matched;
    if (na == 0 || nb == 0)
        return matched;

    const int cols = nb + 1;
    std::vector<double> cost(static_cast<size_t>((na + 1) * cols), 0.0);
    std::vector<char> back(static_cast<size_t>((na + 1) * cols), 0); // 0=diag,1=up,2=left
    auto at = [cols](int i, int j) { return static_cast<size_t>(i * cols + j); };

    for (int i = 1; i <= na; ++i)
    {
        cost[at(i, 0)] = i * gapPenalty;
        back[at(i, 0)] = 1;
    }
    for (int j = 1; j <= nb; ++j)
    {
        cost[at(0, j)] = j * gapPenalty;
        back[at(0, j)] = 2;
    }

    for (int i = 1; i <= na; ++i)
    {
        for (int j = 1; j <= nb; ++j)
        {
            const double diag = cost[at(i - 1, j - 1)]
                                + std::abs(a[static_cast<size_t>(i - 1)] - b[static_cast<size_t>(j - 1)]);
            const double up = cost[at(i - 1, j)] + gapPenalty;
            const double left = cost[at(i, j - 1)] + gapPenalty;
            double best = diag;
            char dir = 0;
            if (up < best) { best = up; dir = 1; }
            if (left < best) { best = left; dir = 2; }
            cost[at(i, j)] = best;
            back[at(i, j)] = dir;
        }
    }

    int i = na;
    int j = nb;
    while (i > 0 || j > 0)
    {
        const char dir = back[at(i, j)];
        if (dir == 0 && i > 0 && j > 0)
        {
            matched.emplace_back(i - 1, j - 1);
            --i;
            --j;
        }
        else if (dir == 1 && i > 0)
        {
            --i;
        }
        else
        {
            --j;
        }
    }
    std::reverse(matched.begin(), matched.end());
    return matched;
}

} // namespace

void FormantAlignedEnvelopeMorph::build(const BarkSpectralEnvelope& a,
                                        const BarkSpectralEnvelope& b,
                                        double relativeFloor,
                                        double minProminenceDb)
{
    valid = false;
    envA = nullptr;
    envB = nullptr;
    anchorA.clear();
    anchorB.clear();
    matchedFormants = 0;

    if (!a.isValid() || !b.isValid())
        return;

    envA = &a;
    envB = &b;
    maxBark = std::min(a.getMaxBark(), b.getMaxBark());

    const double relFloor = (std::isfinite(relativeFloor) && relativeFloor > 0.0)
                                ? relativeFloor
                                : kDefaultRelativeFloor;
    floorA = std::max(a.getAmplitudeFloor(), a.getPeakAmplitude() * relFloor);
    floorB = std::max(b.getAmplitudeFloor(), b.getPeakAmplitude() * relFloor);

    // Match formant peaks across the two envelopes, then build a strictly-ascending list of
    // warp anchors on each axis: the matched peaks bracketed by the 0 and maxBark endpoints.
    // anchorA[k] in A's Bark axis corresponds to anchorB[k] in B's. Unmatched peaks are simply
    // not anchored — they ride whatever warp the surrounding matched peaks impose.
    const std::vector<double> peaksA = a.findPeakBarks(minProminenceDb);
    const std::vector<double> peaksB = b.findPeakBarks(minProminenceDb);

    // Two unmatched peaks cost 2*gapPenalty; matching them costs their Bark distance. So a
    // gap penalty of G makes matching preferred whenever the peaks are within 2G Bark. A
    // formant can migrate several Bark between two sounds (e.g. vowel F2 spans ~6 Bark), so the
    // penalty is set high enough to keep tracking such a formant as one entity, while monotonic
    // ordering still prevents pairing F1 of one sound with F2 of the other.
    constexpr double kGapPenaltyBark = 6.0;
    constexpr double kMinSeparation = 1.0e-3;
    const auto pairs = matchPeaksMonotonic(peaksA, peaksB, kGapPenaltyBark);

    // Keep only matched peaks that are interior and strictly ascending on both axes, so the
    // piecewise-linear warp stays monotonic (and invertible) for every morph amount.
    std::vector<std::pair<double, double>> accepted;
    accepted.reserve(pairs.size());
    for (const auto& pr : pairs)
    {
        const double pa = peaksA[static_cast<size_t>(pr.first)];
        const double pb = peaksB[static_cast<size_t>(pr.second)];
        const double lastA = accepted.empty() ? 0.0 : accepted.back().first;
        const double lastB = accepted.empty() ? 0.0 : accepted.back().second;
        if (pa <= lastA + kMinSeparation || pb <= lastB + kMinSeparation)
            continue;
        if (pa >= maxBark - kMinSeparation || pb >= maxBark - kMinSeparation)
            continue;
        accepted.emplace_back(pa, pb);
    }
    matchedFormants = static_cast<int>(accepted.size());

    // Bark position of the lowest envelope point strictly inside (loBark, hiBark), or -1 if the
    // interval holds no interior bin. Used to anchor the inter-formant trough.
    auto findValleyBark = [&](const BarkSpectralEnvelope& env, double loBark, double hiBark) {
        const int bins = env.getBinCount();
        const double mb = env.getMaxBark();
        if (bins < 2 || mb <= 0.0)
            return -1.0;
        const double step = mb / static_cast<double>(bins - 1);
        double bestBark = -1.0;
        double bestAmp = std::numeric_limits<double>::infinity();
        for (int bi = 0; bi < bins; ++bi)
        {
            const double bk = static_cast<double>(bi) * step;
            if (bk <= loBark + kMinSeparation || bk >= hiBark - kMinSeparation)
                continue;
            const double amp = env.sampleAtBark(bk);
            if (amp < bestAmp)
            {
                bestAmp = amp;
                bestBark = bk;
            }
        }
        return bestBark;
    };

    // Build the warp-anchor lists: endpoint 0, then for each matched formant the peak followed
    // by the matched inter-formant trough leading to the next formant, then endpoint maxBark.
    // Matching the troughs (Caetano/Rodet) keeps the spectral valley between two formants
    // aligned across the morph instead of smearing into a shallow dip at the midpoint.
    anchorA.push_back(0.0);
    anchorB.push_back(0.0);
    for (size_t i = 0; i < accepted.size(); ++i)
    {
        anchorA.push_back(accepted[i].first);
        anchorB.push_back(accepted[i].second);

        if (i + 1 < accepted.size())
        {
            const double vA = findValleyBark(a, accepted[i].first, accepted[i + 1].first);
            const double vB = findValleyBark(b, accepted[i].second, accepted[i + 1].second);
            if (vA > anchorA.back() + kMinSeparation && vB > anchorB.back() + kMinSeparation
                && vA < accepted[i + 1].first - kMinSeparation
                && vB < accepted[i + 1].second - kMinSeparation)
            {
                anchorA.push_back(vA);
                anchorB.push_back(vB);
            }
        }
    }
    anchorA.push_back(maxBark);
    anchorB.push_back(maxBark);

    valid = true;
}

double FormantAlignedEnvelopeMorph::sampleAtHz(double frequencyHz, double morphAmount) const
{
    if (!std::isfinite(frequencyHz) || frequencyHz <= 0.0)
        return 0.0;
    return sampleAtBark(BarkSpectralEnvelope::hzToBark(frequencyHz), morphAmount);
}

double FormantAlignedEnvelopeMorph::sampleAtBark(double bark, double morphAmount) const
{
    if (!valid || envA == nullptr || envB == nullptr || !std::isfinite(bark))
        return 0.0;

    const double m = clampUnit(morphAmount);
    const double x = std::clamp(bark, 0.0, maxBark);

    // The morphed Bark axis interpolates each anchor between its A and B position:
    // P_k(m) = (1-m)*anchorA[k] + m*anchorB[k]. Find the segment [P_k, P_{k+1}] containing x,
    // then map x back onto A's and B's axes by the same fractional position so corresponding
    // formants line up before the amplitude blend.
    const size_t segments = anchorA.size();
    double barkA = x;
    double barkB = x;
    bool placed = false;
    for (size_t k = 0; k + 1 < segments; ++k)
    {
        const double pk = (1.0 - m) * anchorA[k] + m * anchorB[k];
        const double pk1 = (1.0 - m) * anchorA[k + 1] + m * anchorB[k + 1];
        if (x >= pk - 1.0e-9 && x <= pk1 + 1.0e-9)
        {
            const double span = pk1 - pk;
            const double t = span > 1.0e-12 ? std::clamp((x - pk) / span, 0.0, 1.0) : 0.0;
            barkA = anchorA[k] + t * (anchorA[k + 1] - anchorA[k]);
            barkB = anchorB[k] + t * (anchorB[k + 1] - anchorB[k]);
            placed = true;
            break;
        }
    }
    if (!placed)
    {
        // x beyond the last anchor (numerical edge) — clamp to endpoints.
        barkA = anchorA.back();
        barkB = anchorB.back();
    }

    const double eA = std::max(floorA, envA->sampleAtBark(barkA));
    const double eB = std::max(floorB, envB->sampleAtBark(barkB));
    return std::exp((1.0 - m) * std::log(eA) + m * std::log(eB));
}

SpectralEnvelopeTransferResult applySpectralEnvelopeTransfer(double sourceAmplitude,
                                                             double sourceEnvelopeAmplitude,
                                                             double targetEnvelopeAmplitude,
                                                             double amount,
                                                             double amplitudeFloor)
{
    SpectralEnvelopeTransferResult result;
    result.outputAmplitude = std::max(0.0, sourceAmplitude);

    if (!std::isfinite(sourceAmplitude) || sourceAmplitude < 0.0
        || !std::isfinite(sourceEnvelopeAmplitude) || !std::isfinite(targetEnvelopeAmplitude))
    {
        return result;
    }

    result.ok = true;
    if (sourceAmplitude == 0.0)
        return result;

    const double floor = safeFloor(amplitudeFloor);
    const double envA = std::max(floor, sourceEnvelopeAmplitude);
    const double envB = std::max(floor, targetEnvelopeAmplitude);
    const double transferAmount = clampUnit(amount);
    const double logGain = (std::log(envB) - std::log(envA)) * transferAmount;
    const double logOut = std::log(sourceAmplitude) + logGain;

    result.outputAmplitude = std::exp(logOut);
    result.transferGainDb = amplitudeToDb(result.outputAmplitude, floor)
                            - amplitudeToDb(sourceAmplitude, floor);
    result.ok = std::isfinite(result.outputAmplitude) && result.outputAmplitude >= 0.0
                && std::isfinite(result.transferGainDb);
    if (!result.ok)
        result.outputAmplitude = 0.0;
    return result;
}

double softCapEnvelopeBoost(double outputAmplitude,
                            double sourceAmplitude,
                            double maxBoostRatio,
                            double kneeDb)
{
    if (!std::isfinite(outputAmplitude) || outputAmplitude <= 0.0)
        return 0.0;
    if (!std::isfinite(sourceAmplitude) || sourceAmplitude <= 0.0
        || !std::isfinite(maxBoostRatio) || maxBoostRatio <= 1.0)
        return std::max(0.0, outputAmplitude);

    const double ratio = outputAmplitude / sourceAmplitude;
    if (ratio <= 0.0)
        return 0.0;

    const double ceilingDb = 20.0 * std::log10(maxBoostRatio);
    const double xDb = 20.0 * std::log10(ratio);
    const double w = (std::isfinite(kneeDb) && kneeDb > 0.0) ? kneeDb : 0.0;

    double yDb;
    if (w <= 0.0)
    {
        yDb = std::min(xDb, ceilingDb); // degenerate to a hard limit if no knee width
    }
    else if (xDb <= ceilingDb - 0.5 * w)
    {
        yDb = xDb; // below the knee: unity gain
    }
    else if (xDb >= ceilingDb + 0.5 * w)
    {
        yDb = ceilingDb; // above the knee: clamped at the ceiling
    }
    else
    {
        // Quadratic soft knee (limiter form): value and slope are continuous at both knee edges
        // and the curve asymptotes flat at the ceiling.
        const double over = xDb - ceilingDb + 0.5 * w;
        yDb = xDb - (over * over) / (2.0 * w);
    }

    const double cappedRatio = std::pow(10.0, yDb / 20.0);
    return sourceAmplitude * cappedRatio;
}

HybridizedPartialRenderResult resolveHybridizedPartialRender(HybridizedPartialKind kind,
                                                             double sourceFrequencyHz,
                                                             double sourceAmplitude,
                                                             double targetFrequencyHz,
                                                             double targetAmplitude,
                                                             double morphAmount,
                                                             double nearestTargetHz,
                                                             double nearestSourceHz)
{
    HybridizedPartialRenderResult result;
    const double amount = clampUnit(morphAmount);

    switch (kind)
    {
        case HybridizedPartialKind::Matched:
            // Geometric frequency blend + equal-power amplitude blend so pitch glides track
            // perceived pitch and the morph midpoint holds its loudness.
            result.frequencyHz = logInterpHz(sourceFrequencyHz, targetFrequencyHz, amount);
            result.amplitude = equalPowerInterpAmp(sourceAmplitude, targetAmplitude, amount);
            break;

        case HybridizedPartialKind::UnmatchedSource:
            // Cross-fade OUT in place, at the partial's own frequency. An earlier version glided
            // each unmatched source partial toward its nearest target partial; with dissimilar
            // sources (where most partials are unmatched) that turned the blend into a mass of
            // partials sliding to arbitrary lower targets — audible downward sweeps and tones
            // present in neither source. Fading in place keeps the morph midpoint a clean union
            // of the two spectra. Equal-power (sqrt) fade pairs with the UnmatchedTarget fade-in
            // so a replacing pair holds constant power through the crossfade; time-domain
            // smoothing is handled by per-sample amplitude ramping in the renderer, not here.
            (void) nearestTargetHz;
            result.frequencyHz = sourceFrequencyHz;
            result.amplitude = sourceAmplitude * std::sqrt(1.0 - amount);
            break;

        case HybridizedPartialKind::UnmatchedTarget:
            // Glide in from nearest source partial (when one is supplied) while fading in
            // with the equal-power (sqrt) law.
            result.frequencyHz = (nearestSourceHz > 0.0)
                                     ? logInterpHz(nearestSourceHz, targetFrequencyHz, amount)
                                     : targetFrequencyHz;
            result.amplitude = targetAmplitude * std::sqrt(amount);
            break;
    }

    result.active = std::isfinite(result.frequencyHz) && std::isfinite(result.amplitude)
                    && result.frequencyHz > 0.0 && result.amplitude > 0.0;
    if (!result.active)
    {
        result.frequencyHz = 0.0;
        result.amplitude = 0.0;
    }
    return result;
}

namespace
{
// Bark-scale normalization to [0,1] over [minHz, maxHz]. Mirrors the RDC display fit.
double rdcBarkNorm(double hz, double minHz, double maxHz)
{
    auto bark = [](double f) { return 6.0 * std::asinh(std::max(1.0, f) / 600.0); };
    const double bMin = bark(std::max(1.0, minHz));
    const double bMax = bark(std::max(1.0, maxHz));
    return std::clamp((bark(std::max(1.0, hz)) - bMin) / std::max(1.0e-9, bMax - bMin), 0.0, 1.0);
}

// Cosine basis row: [1, 2cos(pi*pos), 2cos(2*pi*pos), ..., 2cos(order*pi*pos)].
void rdcBasisRow(double pos, int order, double* row)
{
    row[0] = 1.0;
    const double theta = M_PI * std::clamp(pos, 0.0, 1.0);
    for (int k = 1; k <= order; ++k)
        row[k] = 2.0 * std::cos((double) k * theta);
}

// Gaussian elimination with partial pivoting for a small n*n system (row-major A, rhs b).
bool rdcSolve(int n, double* A, double* b, double* x)
{
    for (int col = 0; col < n; ++col)
    {
        int pivotRow = col;
        double pivotAbs = std::abs(A[col * n + col]);
        for (int row = col + 1; row < n; ++row)
        {
            const double v = std::abs(A[row * n + col]);
            if (v > pivotAbs) { pivotAbs = v; pivotRow = row; }
        }
        if (pivotAbs < 1.0e-12)
            return false;
        if (pivotRow != col)
        {
            for (int k = 0; k < n; ++k) std::swap(A[col * n + k], A[pivotRow * n + k]);
            std::swap(b[col], b[pivotRow]);
        }
        const double diag = A[col * n + col];
        for (int row = col + 1; row < n; ++row)
        {
            const double f = A[row * n + col] / diag;
            for (int k = col; k < n; ++k) A[row * n + k] -= f * A[col * n + k];
            b[row] -= f * b[col];
        }
    }
    for (int row = n - 1; row >= 0; --row)
    {
        x[row] = b[row];
        for (int k = row + 1; k < n; ++k) x[row] -= A[row * n + k] * x[k];
        x[row] /= A[row * n + row];
    }
    return true;
}
} // namespace

bool reimposeRdcEnvelopeFrame(const std::vector<double>& freqsHz,
                              std::vector<double>& amps,
                              double smoothness,
                              double minHz,
                              double maxHz,
                              double amplitudeFloor)
{
    if (freqsHz.size() != amps.size())
        return false;

    const double floor = safeFloor(amplitudeFloor);

    // Gather active partials (amp above floor) as (barkPos, dB) with amplitude weighting.
    std::vector<int> activeIdx;
    std::vector<double> bark;
    std::vector<double> dB;
    std::vector<double> weight;
    activeIdx.reserve(amps.size());
    double peak = floor;
    for (std::size_t i = 0; i < amps.size(); ++i)
        if (std::isfinite(amps[i]) && amps[i] > floor)
            peak = std::max(peak, amps[i]);

    for (std::size_t i = 0; i < amps.size(); ++i)
    {
        if (!std::isfinite(amps[i]) || amps[i] <= floor || !std::isfinite(freqsHz[i]))
            continue;
        activeIdx.push_back((int) i);
        bark.push_back(rdcBarkNorm(freqsHz[i], minHz, maxHz));
        dB.push_back(amplitudeToDb(amps[i], floor));
        weight.push_back(std::max(1.0e-4, amps[i] / peak));
    }

    if (activeIdx.size() < 3)
        return false;

    // smoothness -> cosine-series order and Tikhonov lambda. Higher smoothness = lower order
    // (fewer ripples) and stronger second-derivative penalty.
    const double s = std::clamp(smoothness, 0.0, 1.0);
    constexpr int kMaxOrder = 14;
    int order = (int) std::lround(12.0 - 10.0 * s); // 12 (detailed) .. 2 (smooth)
    order = std::max(1, std::min(order, (int) activeIdx.size() - 1));
    order = std::min(order, kMaxOrder);
    const double reg = 2.0e-3 + 4.8e-2 * s; // 0.002 .. 0.05

    const int n = order + 1;
    constexpr int kMaxN = kMaxOrder + 1;
    double A[kMaxN * kMaxN] = {};
    double rhs[kMaxN] = {};
    double coeffs[kMaxN] = {};
    double row[kMaxN];

    for (std::size_t i = 0; i < activeIdx.size(); ++i)
    {
        rdcBasisRow(bark[i], order, row);
        const double w = weight[i];
        const double y = dB[i];
        for (int r = 0; r < n; ++r)
        {
            rhs[r] += w * row[r] * y;
            for (int c = 0; c < n; ++c)
                A[r * n + c] += w * row[r] * row[c];
        }
    }
    // Second-derivative Tikhonov penalty on cosine coefficients: diag [0, 1^4, 2^4, ...].
    for (int k = 1; k < n; ++k)
        A[k * n + k] += reg * (double) (k * k * k * k);

    if (!rdcSolve(n, A, rhs, coeffs))
        return false;

    // Re-impose: set each active partial's amplitude to the fitted envelope at its frequency.
    for (std::size_t i = 0; i < activeIdx.size(); ++i)
    {
        rdcBasisRow(bark[i], order, row);
        double envDb = 0.0;
        for (int k = 0; k < n; ++k)
            envDb += coeffs[k] * row[k];
        double envAmp = std::pow(10.0, envDb / 20.0);
        if (!std::isfinite(envAmp) || envAmp < 0.0)
            envAmp = 0.0;
        amps[(std::size_t) activeIdx[i]] = envAmp;
    }
    return true;
}

} // namespace ats
