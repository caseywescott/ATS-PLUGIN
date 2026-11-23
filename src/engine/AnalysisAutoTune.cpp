#include "AnalysisAutoTune.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ats
{
namespace
{
constexpr double kEps = 1.0e-7;
constexpr double kPi = 3.14159265358979323846;

std::vector<double> rmsNormalised(const double* x, std::size_t n)
{
    double sum = 0.0;
    for (std::size_t i = 0; i < n; ++i)
        sum += x[i] * x[i];
    const auto scale = 1.0 / (std::sqrt(n > 0 ? sum / static_cast<double>(n) : 0.0) + kEps);
    std::vector<double> out(x, x + n);
    for (auto& v : out)
        v *= scale;
    return out;
}

// |FFT| of Hann-windowed frames, hop n/4; frames x (n/2 + 1).
std::vector<std::vector<double>> stftMagnitudes(const std::vector<double>& signal, int n)
{
    std::vector<double> x = signal;
    if (x.size() < static_cast<std::size_t>(n))
        x.resize(static_cast<std::size_t>(n), 0.0);
    std::vector<double> window(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i)
        window[static_cast<std::size_t>(i)] = 0.5 - 0.5 * std::cos(2.0 * kPi * i / (n - 1));
    std::vector<std::vector<double>> frames;
    std::vector<double> re(static_cast<std::size_t>(n)), im(static_cast<std::size_t>(n));
    const auto hop = static_cast<std::size_t>(n / 4);
    for (std::size_t start = 0; start + static_cast<std::size_t>(n) <= x.size(); start += hop)
    {
        for (int i = 0; i < n; ++i)
        {
            re[static_cast<std::size_t>(i)] = x[start + static_cast<std::size_t>(i)]
                * window[static_cast<std::size_t>(i)];
            im[static_cast<std::size_t>(i)] = 0.0;
        }
        fft(re.data(), im.data(), n, 1);
        std::vector<double> mags(static_cast<std::size_t>(n / 2 + 1));
        for (std::size_t k = 0; k < mags.size(); ++k)
            mags[k] = std::hypot(re[k], im[k]);
        frames.push_back(std::move(mags));
    }
    return frames;
}

// Zwicker & Fastl critical-band edges (ATSA_CRITICAL_BAND_EDGES); band b spans
// [edges[b], edges[b+1]).
constexpr double kBandEdges[] = {
    0.0, 100.0, 200.0, 300.0, 400.0, 510.0, 630.0, 770.0, 920.0,
    1080.0, 1270.0, 1480.0, 1720.0, 2000.0, 2320.0, 2700.0, 3150.0,
    3700.0, 4400.0, 5300.0, 6400.0, 7700.0, 9500.0, 12000.0, 15500.0, 20000.0
};

struct Evaluation
{
    /** Combined score; infinity when the settings produce no model. */
    double loss = std::numeric_limits<double>::infinity();
    double spectralLoss = std::numeric_limits<double>::infinity();
    double whiteness = 0.0;
    double tonality = 0.0;
    int partials = 0;
    /** Mean frequency of the lowest partial within kStrongPartialDb of the
        loudest partial's peak amplitude; 0 when the model has none. */
    double lowestStrongPartialHz = 0.0;
};

double lowestStrongPartial(const ATS_SOUND* sound)
{
    double loudest = 0.0;
    std::vector<double> peaks(static_cast<std::size_t>(std::max(0, sound->partials)), 0.0);
    for (int p = 0; p < sound->partials; ++p)
        for (int f = 0; f < sound->frames; ++f)
            peaks[static_cast<std::size_t>(p)] = std::max(peaks[static_cast<std::size_t>(p)],
                                                          sound->amp[p][f]);
    for (const auto peak : peaks)
        loudest = std::max(loudest, peak);
    if (!(loudest > 0.0))
        return 0.0;
    const auto floor = loudest * std::pow(10.0, -AnalysisAutoTune::kStrongPartialDb / 20.0);
    double lowest = 0.0;
    for (int p = 0; p < sound->partials; ++p)
    {
        if (peaks[static_cast<std::size_t>(p)] < floor)
            continue;
        double weighted = 0.0, weight = 0.0;
        for (int f = 0; f < sound->frames; ++f)
            if (sound->amp[p][f] > 0.0 && sound->frq[p][f] > 0.0)
            {
                weighted += sound->frq[p][f] * sound->amp[p][f];
                weight += sound->amp[p][f];
            }
        if (weight > 0.0 && (lowest == 0.0 || weighted / weight < lowest))
            lowest = weighted / weight;
    }
    return lowest;
}

Evaluation evaluate(const AutoTuneRequest& request, AnalysisParams parameters,
                    const CancellationToken& cancellation)
{
    Evaluation result;
    AnalysisRequest analysis;
    analysis.samples = request.samples;
    analysis.sampleCount = request.sampleCount;
    analysis.sampleRate = request.sampleRate;
    analysis.parameters = parameters;
    auto analysed = AtsEngine::analyze(JobGeneration {}, cancellation, analysis);
    if (analysed.status != JobResult<AnalysisOutput>::Status::success || !analysed.value.sound)
        return result;
    const auto* sound = analysed.value.sound.get();
    result.partials = sound->partials;
    result.lowestStrongPartialHz = lowestStrongPartial(sound);

    const auto frames = static_cast<std::size_t>(
        std::ceil(std::max(0.0, sound->dur) * request.sampleRate)) + 1;
    std::vector<float> rendered(frames, 0.0f);
    const auto render = [&](double residualGain, bool usePhase) -> std::size_t {
        SynthParams synth;
        synth.sampleRate = request.sampleRate;
        synth.partialsGain = 1.0;
        synth.residualGain = residualGain;
        synth.usePartialsPhase = usePhase;
        std::fill(rendered.begin(), rendered.end(), 0.0f);
        float* channel = rendered.data();
        AudioBufferView view { &channel, 1, static_cast<std::uint32_t>(frames) };
        const auto stats = AtsEngine::render(JobGeneration {}, cancellation, sound, synth, view);
        if (stats.status != JobResult<RenderStats>::Status::success || !stats.value.finite)
            return 0;
        return stats.value.framesWritten;
    };
    const auto written = render(1.0, false);
    if (written == 0)
        return result;

    // Score against the analysed span: the model starts at `start` seconds.
    const auto first = std::min(request.sampleCount, static_cast<std::size_t>(
        std::max(0.0, parameters.start) * request.sampleRate));
    std::vector<double> candidate(rendered.begin(),
                                  rendered.begin() + static_cast<std::ptrdiff_t>(written));
    result.spectralLoss = AnalysisAutoTune::spectralLoss(request.samples + first,
                                                         request.sampleCount - first,
                                                         candidate.data(), candidate.size());
    result.loss = result.spectralLoss
        + AnalysisAutoTune::kParsimonyWeight * sound->partials
              / AnalysisAutoTune::kParsimonyReference;

    // Residual terms: r = input - phase-locked partials-only resynthesis over
    // the same span (ATSA computes its own residual the same way).
    if (request.whitenessWeight > 0.0 || request.tonalityWeight > 0.0)
    {
        const auto partialsOnly = render(0.0, true);
        if (partialsOnly == 0)
            return result;
        const auto n = std::min(request.sampleCount - first, partialsOnly);
        std::vector<double> residual(n);
        for (std::size_t i = 0; i < n; ++i)
            residual[i] = request.samples[first + i] - static_cast<double>(rendered[i]);
        if (request.whitenessWeight > 0.0)
            result.whiteness = AnalysisAutoTune::residualWhiteness(residual.data(), n);
        if (request.tonalityWeight > 0.0)
            result.tonality = AnalysisAutoTune::residualTonality(residual.data(), n,
                                                                 request.sampleRate);
        result.loss += request.whitenessWeight * result.whiteness
            + request.tonalityWeight * result.tonality;
    }
    return result;
}

AutoTuneStep toStep(const AnalysisParams& parameters, const Evaluation& evaluation)
{
    AutoTuneStep step;
    step.parameters = parameters;
    step.loss = evaluation.loss;
    step.partials = evaluation.partials;
    step.spectralLoss = evaluation.spectralLoss;
    step.whiteness = evaluation.whiteness;
    step.tonality = evaluation.tonality;
    return step;
}
} // namespace

double AnalysisAutoTune::residualWhiteness(const double* residual, std::size_t count)
{
    // Serra (1997) tests the residual's stochasticity with the mean of its
    // normalised autocorrelation over the first L lags.  The lag sum here is
    // of |r_xx(l)|: with signed terms a sinusoid whose period is shorter than
    // ~L samples cancels itself (e.g. a 1 kHz tone at 44.1 kHz would read as
    // white).  Mean removal and the r_xx(0) normalisation make the value
    // independent of the residual's level.
    constexpr auto lags = kWhitenessLags;
    if (residual == nullptr || count < 2 * lags)
        return 0.0;
    double sum = 0.0;
    std::size_t blocks = 0;
    for (std::size_t start = 0; start < count; start += kWhitenessBlock)
    {
        const auto length = std::min(kWhitenessBlock, count - start);
        if (length < 2 * lags)
            break;
        const double* x = residual + start;
        double mean = 0.0;
        for (std::size_t i = 0; i < length; ++i)
            mean += x[i];
        mean /= static_cast<double>(length);
        double r0 = 0.0;
        for (std::size_t i = 0; i < length; ++i)
            r0 += (x[i] - mean) * (x[i] - mean);
        r0 /= static_cast<double>(length);
        if (!(r0 > 1.0e-30)) // digital silence: no evidence either way
            continue;
        double c = 0.0;
        for (std::size_t lag = 1; lag < lags; ++lag)
        {
            double r = 0.0;
            for (std::size_t i = 0; i + lag < length; ++i)
                r += (x[i] - mean) * (x[i + lag] - mean);
            c += std::abs(r / static_cast<double>(length - lag));
        }
        sum += c / (static_cast<double>(lags - 1) * r0);
        ++blocks;
    }
    return blocks > 0 ? sum / static_cast<double>(blocks) : 0.0;
}

double AnalysisAutoTune::residualTonality(const double* residual, std::size_t count, int sampleRate)
{
    // Johnston (1988) maps the spectral flatness measure to a tonality
    // coefficient, alpha = min(SFM_dB / -60, 1); Jehan & Schoner (2001) apply
    // it per critical band.  A Hann main lobe spans 4 bins, so a band of
    // fewer bins is "flat" even when it holds a pure tone: bands narrower
    // than kTonalityMinBandBins are widened symmetrically about their centre
    // (690 Hz for 2048 bins at 44.1 kHz), the Bark spacing is kept.
    constexpr int n = kTonalityFftSize;
    constexpr int bins = n / 2 + 1;
    if (residual == nullptr || count == 0 || sampleRate <= 0)
        return 0.0;
    const double binHz = static_cast<double>(sampleRate) / n;
    struct Band { int lo, hi; };
    std::vector<Band> bands;
    for (std::size_t b = 0; b + 1 < sizeof(kBandEdges) / sizeof(kBandEdges[0]); ++b)
    {
        if (kBandEdges[b] >= 0.5 * sampleRate)
            break;
        int lo = std::max(1, static_cast<int>(std::lround(kBandEdges[b] / binHz)));
        int hi = std::min(bins, static_cast<int>(std::lround(kBandEdges[b + 1] / binHz)));
        if (hi - lo < kTonalityMinBandBins)
        {
            const int centre = (lo + hi) / 2;
            lo = std::max(1, centre - kTonalityMinBandBins / 2);
            hi = std::min(bins, lo + kTonalityMinBandBins);
            lo = std::max(1, hi - kTonalityMinBandBins);
        }
        if (hi - lo >= 2)
            bands.push_back({ lo, hi });
    }
    if (bands.empty())
        return 0.0;

    std::vector<double> x(residual, residual + count);
    if (x.size() < static_cast<std::size_t>(n))
        x.resize(static_cast<std::size_t>(n), 0.0);
    std::vector<double> window(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i)
        window[static_cast<std::size_t>(i)] = 0.5 - 0.5 * std::cos(2.0 * kPi * i / (n - 1));
    std::vector<double> re(static_cast<std::size_t>(n)), im(static_cast<std::size_t>(n));
    std::vector<double> power(static_cast<std::size_t>(bins));
    std::vector<double> arithmetic(bands.size()), geometric(bands.size());
    const auto floorRatio = std::pow(10.0, kTonalityFloorDb / 10.0);
    double sum = 0.0;
    std::size_t counted = 0;
    for (std::size_t start = 0; start + static_cast<std::size_t>(n) <= x.size();
         start += static_cast<std::size_t>(kTonalityHop))
    {
        double peak = 0.0;
        for (int i = 0; i < n; ++i)
        {
            re[static_cast<std::size_t>(i)] = x[start + static_cast<std::size_t>(i)]
                * window[static_cast<std::size_t>(i)];
            im[static_cast<std::size_t>(i)] = 0.0;
        }
        fft(re.data(), im.data(), n, 1);
        for (int k = 0; k < bins; ++k)
        {
            const auto idx = static_cast<std::size_t>(k);
            power[idx] = re[idx] * re[idx] + im[idx] * im[idx];
            peak = std::max(peak, power[idx]);
        }
        if (!(peak > 0.0))
            continue; // silent frame
        // Relative floor (-120 dB) keeps log() finite without depending on level.
        const auto powerFloor = peak * 1.0e-12;
        double loudest = 0.0;
        for (std::size_t b = 0; b < bands.size(); ++b)
        {
            double am = 0.0, logSum = 0.0;
            for (int k = bands[b].lo; k < bands[b].hi; ++k)
            {
                const auto p = power[static_cast<std::size_t>(k)] + powerFloor;
                am += p;
                logSum += std::log(p);
            }
            const auto width = static_cast<double>(bands[b].hi - bands[b].lo);
            arithmetic[b] = am / width;
            geometric[b] = std::exp(logSum / width);
            loudest = std::max(loudest, arithmetic[b]);
        }
        for (std::size_t b = 0; b < bands.size(); ++b)
        {
            if (arithmetic[b] < loudest * floorRatio)
                continue; // negligible energy: no tonality evidence
            const auto sfmDb = 10.0 * std::log10(geometric[b] / arithmetic[b]);
            sum += std::min(1.0, std::max(0.0, sfmDb / kTonalitySfmDb));
            ++counted;
        }
    }
    return counted > 0 ? sum / static_cast<double>(counted) : 0.0;
}

double AnalysisAutoTune::spectralLoss(const double* reference, std::size_t referenceCount,
                                      const double* candidate, std::size_t candidateCount)
{
    if (reference == nullptr || candidate == nullptr || referenceCount == 0 || candidateCount == 0)
        return std::numeric_limits<double>::infinity();
    const auto n = std::min(referenceCount, candidateCount);
    const auto a = rmsNormalised(reference, n);
    const auto b = rmsNormalised(candidate, n);
    double total = 0.0;
    for (const int size : { 512, 1024, 2048 })
    {
        const auto sa = stftMagnitudes(a, size);
        const auto sb = stftMagnitudes(b, size);
        const auto count = std::min(sa.size(), sb.size());
        double diff2 = 0.0, ref2 = 0.0, logL1 = 0.0;
        std::size_t cells = 0;
        for (std::size_t f = 0; f < count; ++f)
            for (std::size_t k = 0; k < sa[f].size(); ++k)
            {
                const auto d = sa[f][k] - sb[f][k];
                diff2 += d * d;
                ref2 += sa[f][k] * sa[f][k];
                logL1 += std::abs(std::log(sa[f][k] + kEps) - std::log(sb[f][k] + kEps));
                ++cells;
            }
        total += std::sqrt(diff2) / (std::sqrt(ref2) + kEps)
            + (cells > 0 ? logL1 / static_cast<double>(cells) : 0.0);
    }
    return total;
}

std::vector<AnalysisParams> AnalysisAutoTune::neighbours(const AnalysisParams& from, int sampleRate,
                                                         double lowestFrequencyCap)
{
    // Ranges follow the plugin's analysis parameter descriptors.
    const auto nyquist = std::min(24000.0, 0.5 * sampleRate);
    std::vector<AnalysisParams> out;
    const auto push = [&](AnalysisParams p) {
        if (!(p.lowest_freq >= 1.0 && p.lowest_freq <= 2000.0 && p.highest_freq >= 20.0
              && p.highest_freq <= nyquist && p.lowest_freq < p.highest_freq
              && p.win_cycles >= 1 && p.win_cycles <= 8 && p.hop_size > 0.0 && p.hop_size <= 1.0))
            return;
        if (lowestFrequencyCap > 0.0 && p.lowest_freq > from.lowest_freq
            && p.lowest_freq > lowestFrequencyCap)
            return;
        const auto same = [&](const AnalysisParams& q) {
            return std::abs(q.lowest_freq - p.lowest_freq) < 1.0e-9
                && std::abs(q.highest_freq - p.highest_freq) < 1.0e-9
                && q.win_cycles == p.win_cycles && std::abs(q.hop_size - p.hop_size) < 1.0e-12;
        };
        if (same(from) || std::any_of(out.begin(), out.end(), same))
            return;
        out.push_back(p);
    };
    for (const auto m : { 0.5, 0.7, 1.5, 2.0 })
    {
        auto p = from;
        p.lowest_freq = from.lowest_freq * m;
        push(p);
    }
    for (const auto m : { 0.5, 0.75, 1.5 })
    {
        auto p = from;
        p.highest_freq = std::min(from.highest_freq * m, nyquist);
        push(p);
    }
    for (const auto d : { -2, -1, 1, 2 })
    {
        auto p = from;
        p.win_cycles = from.win_cycles + d;
        push(p);
    }
    for (const auto hop : { 0.125, 0.25, 0.5 })
    {
        auto p = from;
        p.hop_size = hop;
        push(p);
    }
    return out;
}

JobResult<AutoTuneResult> AnalysisAutoTune::run(JobGeneration generation,
                                                const CancellationToken& cancellation,
                                                const AutoTuneRequest& request,
                                                AtsEngine::Progress progress)
{
    JobResult<AutoTuneResult> result;
    result.generation = generation;
    if (request.samples == nullptr || request.sampleCount == 0 || request.sampleRate <= 0)
    {
        result.error = { "invalid_request", "auto-tune requires non-empty samples and a positive sample rate" };
        return result;
    }
    const auto budget = std::max(1, request.budget);
    const auto report = [&](int done) {
        if (progress) progress(std::min(1.0, static_cast<double>(done) / budget));
    };

    auto& tuned = result.value;
    tuned.parameters = request.start;
    const auto initial = evaluate(request, request.start, cancellation);
    tuned.evaluations = 1;
    report(1);
    if (cancellation.isCancelled())
    {
        result.status = JobResult<AutoTuneResult>::Status::cancelled;
        result.error = { "cancelled", "auto-tune cancelled" };
        return result;
    }
    if (!std::isfinite(initial.loss))
    {
        result.error = { "analysis_failed", "the current analysis settings do not produce a model" };
        return result;
    }
    const auto accept = [&](const AnalysisParams& parameters, const Evaluation& evaluation) {
        tuned.parameters = parameters;
        tuned.loss = evaluation.loss;
        tuned.spectralLoss = evaluation.spectralLoss;
        tuned.whiteness = evaluation.whiteness;
        tuned.tonality = evaluation.tonality;
        tuned.history.push_back(toStep(parameters, evaluation));
    };
    tuned.initialLoss = initial.loss;
    // Guard the starting model's fundamental (see the class comment).
    tuned.lowestFrequencyCap = initial.lowestStrongPartialHz > 0.0
        ? std::max(request.start.lowest_freq, kFundamentalGuard * initial.lowestStrongPartialHz)
        : 0.0;
    accept(request.start, initial);

    // Coordinate descent: sweep every neighbour of the current best, accept
    // each improvement immediately, repeat while a sweep improves.
    bool improved = true;
    while (improved && tuned.evaluations < budget)
    {
        improved = false;
        for (const auto& candidate : neighbours(tuned.parameters, request.sampleRate,
                                                tuned.lowestFrequencyCap))
        {
            if (tuned.evaluations >= budget)
                break;
            const auto trial = evaluate(request, candidate, cancellation);
            ++tuned.evaluations;
            report(tuned.evaluations);
            if (cancellation.isCancelled())
            {
                result.status = JobResult<AutoTuneResult>::Status::cancelled;
                result.error = { "cancelled", "auto-tune cancelled" };
                return result;
            }
            if (trial.loss < tuned.loss - kMinImprovement)
            {
                accept(candidate, trial);
                improved = true;
                break; // re-derive neighbours around the new best
            }
        }
    }
    report(budget);
    result.status = JobResult<AutoTuneResult>::Status::success;
    return result;
}

} // namespace ats
