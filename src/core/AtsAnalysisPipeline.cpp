#include "AtsAnalysisPipeline.h"

#include "AtsResidualBandNoise.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <numeric>
#include <sstream>
#include <unordered_set>
#include <utility>

extern "C" {
#include "atsa.h"
}

namespace ats
{

namespace
{

constexpr double kTwoPi = 6.28318530717958647692;
constexpr double kHalfPi = 1.57079632679489661923;
// Treat the local FFT main lobe around an accepted peak as explained energy.
// Otherwise clean tracked sinusoids leak window skirts into orphan residual bands.
constexpr int kResidualPeakMaxLobeHalfWidthBins = 16;
constexpr double kResidualStrongNeighbourRelativeDb = 24.0;
constexpr double kAssignmentForbiddenCost = 1.0e9;
constexpr double kAssignmentTieBreakScale = 1.0e-9;

double clamp(double value, double lo, double hi)
{
    return std::max(lo, std::min(hi, value));
}

double lerp(double a, double b, double t) { return a + (b - a) * t; }

bool finite(double value) { return std::isfinite(value); }

double ampFromDb(double db) { return std::pow(10.0, db / 20.0); }

double wrapPhase(double phase)
{
    if (!std::isfinite(phase))
        return 0.0;
    return std::remainder(phase, kTwoPi);
}

double interpolateWrappedPhase(double a, double b, double t)
{
    return wrapPhase(a + wrapPhase(b - a) * t);
}

double oscillatorPhaseAtFrameStart(double spectrumPhase,
                                   double centerFrequencyHz,
                                   double interpolatedFrequencyHz,
                                   double binSpacingHz,
                                   const PeakEstimationConfig& config)
{
    double phase = spectrumPhase + kHalfPi;
    if (config.windowSizeSamples > 0 && config.fftSizeSamples > 0 && binSpacingHz > 0.0
        && std::isfinite(centerFrequencyHz) && std::isfinite(interpolatedFrequencyHz))
    {
        const double offsetBins = (interpolatedFrequencyHz - centerFrequencyHz) / binSpacingHz;
        const double windowCenter = 0.5 * static_cast<double>(config.windowSizeSamples - 1);
        phase -= kTwoPi * offsetBins * windowCenter / static_cast<double>(config.fftSizeSamples);
    }
    return wrapPhase(phase);
}

bool spectrumBinFinite(const SpectrumBin& bin)
{
    return finite(bin.frequencyHz) && finite(bin.magnitudeDb) && finite(bin.phaseRadians);
}

void freeResidual(double*** res)
{
    if (res == nullptr || *res == nullptr)
        return;
    std::free((*res)[0]);
    std::free((*res)[1]);
    std::free(*res);
    *res = nullptr;
}

ModernAtsPartialFrame sleptFrame()
{
    ModernAtsPartialFrame frame;
    frame.confidence = 0.0;
    frame.reason = ModernAtsFrameReason::Slept;
    return frame;
}

ModernAtsPartialFrame frameFromPeak(const TrackingPeak& peak, ModernAtsFrameReason reason, double confidence)
{
    ModernAtsPartialFrame frame;
    frame.amplitude = peak.amplitude;
    frame.frequencyHz = peak.frequencyHz;
    frame.phaseRadians = peak.phaseRadians;
    frame.smrDb = peak.smrDb;
    frame.confidence = clamp(confidence, 0.0, 1.0);
    frame.reason = reason;
    return frame;
}

bool containsFrame(const std::unordered_set<int>& frameSet, int frameIndex)
{
    return frameSet.count(frameIndex) != 0;
}

bool modernFrameActive(const ModernAtsPartialFrame& frame)
{
    return frame.amplitude > 0.0 && frame.frequencyHz > 0.0
           && frame.reason != ModernAtsFrameReason::Slept
           && frame.reason != ModernAtsFrameReason::Pruned
           && frame.reason != ModernAtsFrameReason::Rejected;
}

int lastActivePartialFrameIndex(const ModernAtsPartial& partial, int beforeFrameIndex)
{
    const int start = std::min(beforeFrameIndex, static_cast<int>(partial.frames.size())) - 1;
    for (int i = start; i >= 0; --i)
    {
        if (modernFrameActive(partial.frames[static_cast<std::size_t>(i)]))
            return i;
    }
    return -1;
}

void fillInterpolatedGap(ModernAtsPartial& partial,
                         int currentFrameIndex,
                         std::vector<ModernAtsTrackLifecycleEvent>& events)
{
    if (currentFrameIndex <= 0 || currentFrameIndex >= static_cast<int>(partial.frames.size()))
        return;

    int gapEnd = currentFrameIndex - 1;
    while (gapEnd >= 0
           && partial.frames[static_cast<std::size_t>(gapEnd)].reason == ModernAtsFrameReason::Slept)
    {
        --gapEnd;
    }

    const int previousActiveIndex = gapEnd;
    const int gapStart = gapEnd + 1;
    const int gapLength = currentFrameIndex - gapStart;
    if (gapLength <= 0 || previousActiveIndex < 0
        || !modernFrameActive(partial.frames[static_cast<std::size_t>(previousActiveIndex)]))
    {
        return;
    }

    const auto previous = partial.frames[static_cast<std::size_t>(previousActiveIndex)];
    const auto current = partial.frames[static_cast<std::size_t>(currentFrameIndex)];
    const double denom = static_cast<double>(currentFrameIndex - previousActiveIndex);
    for (int frameIndex = gapStart; frameIndex < currentFrameIndex; ++frameIndex)
    {
        const double t = static_cast<double>(frameIndex - previousActiveIndex) / denom;
        ModernAtsPartialFrame interpolated;
        interpolated.amplitude = lerp(previous.amplitude, current.amplitude, t);
        interpolated.frequencyHz = lerp(previous.frequencyHz, current.frequencyHz, t);
        interpolated.phaseRadians = interpolateWrappedPhase(previous.phaseRadians, current.phaseRadians, t);
        interpolated.smrDb = lerp(previous.smrDb, current.smrDb, t);
        interpolated.noiseEnergy = lerp(previous.noiseEnergy, current.noiseEnergy, t);
        interpolated.confidence = std::min(previous.confidence, current.confidence) * 0.5;
        interpolated.reason = ModernAtsFrameReason::Interpolated;
        partial.frames[static_cast<std::size_t>(frameIndex)] = interpolated;
        events.push_back({ partial.id, frameIndex, ModernAtsLifecycleKind::GapFill, "short_assignment_gap" });
    }
}

double ampToDb(double amp)
{
    return amp > 1.0e-15 ? 20.0 * std::log10(amp) : -300.0;
}

int barkBandIndexForFrequency(double frequencyHz)
{
    const double f = std::max(0.0, frequencyHz);
    const double bark = 13.0 * std::atan(0.00076 * f)
                        + 3.5 * std::atan((f / 7500.0) * (f / 7500.0));
    return std::max(0, std::min(kModernAtsDefaultResidualBands - 1,
                                static_cast<int>(std::floor(bark))));
}

double spectralEnergyFromAmplitude(double amplitude)
{
    const double amp = std::isfinite(amplitude) ? std::max(0.0, amplitude) : 0.0;
    return 0.5 * amp * amp;
}

double spectralEnergyFromBin(const SpectrumBin& bin)
{
    if (!spectrumBinFinite(bin))
        return 0.0;
    return spectralEnergyFromAmplitude(ampFromDb(bin.magnitudeDb));
}

bool localSpectrumMaximum(const std::vector<SpectrumBin>& bins, int index)
{
    if (index <= 0 || index + 1 >= static_cast<int>(bins.size()))
        return false;
    if (!spectrumBinFinite(bins[static_cast<std::size_t>(index - 1)])
        || !spectrumBinFinite(bins[static_cast<std::size_t>(index)])
        || !spectrumBinFinite(bins[static_cast<std::size_t>(index + 1)]))
    {
        return false;
    }

    const double center = bins[static_cast<std::size_t>(index)].magnitudeDb;
    return center >= bins[static_cast<std::size_t>(index - 1)].magnitudeDb
           && center >= bins[static_cast<std::size_t>(index + 1)].magnitudeDb;
}

int residualLobeBoundaryForDirection(const std::vector<SpectrumBin>& bins, int sourceIndex, int direction)
{
    if (sourceIndex < 0 || sourceIndex >= static_cast<int>(bins.size()) || (direction != -1 && direction != 1)
        || !spectrumBinFinite(bins[static_cast<std::size_t>(sourceIndex)]))
    {
        return sourceIndex;
    }

    const double peakMagnitudeDb = bins[static_cast<std::size_t>(sourceIndex)].magnitudeDb;
    const int hardBoundary = sourceIndex + direction * kResidualPeakMaxLobeHalfWidthBins;
    const int clampedHardBoundary =
        std::max(0, std::min(static_cast<int>(bins.size()) - 1, hardBoundary));

    for (int offset = 2; offset <= kResidualPeakMaxLobeHalfWidthBins; ++offset)
    {
        const int candidate = sourceIndex + direction * offset;
        if (candidate <= 0 || candidate + 1 >= static_cast<int>(bins.size()))
            break;

        const auto& bin = bins[static_cast<std::size_t>(candidate)];
        if (!localSpectrumMaximum(bins, candidate)
            || bin.magnitudeDb < peakMagnitudeDb - kResidualStrongNeighbourRelativeDb)
        {
            continue;
        }

        int valley = sourceIndex;
        double valleyMagnitudeDb = peakMagnitudeDb;
        for (int i = sourceIndex + direction; i != candidate; i += direction)
        {
            const auto& valleyBin = bins[static_cast<std::size_t>(i)];
            if (!spectrumBinFinite(valleyBin))
                continue;
            if (valleyBin.magnitudeDb < valleyMagnitudeDb)
            {
                valleyMagnitudeDb = valleyBin.magnitudeDb;
                valley = i;
            }
        }

        return valley == sourceIndex
                   ? sourceIndex
                   : std::max(0, std::min(static_cast<int>(bins.size()) - 1, valley - direction));
    }

    return clampedHardBoundary;
}

std::uint64_t fnv1aUpdate(std::uint64_t hash, std::uint64_t value)
{
    for (int i = 0; i < 8; ++i)
    {
        hash ^= (value >> (i * 8)) & 0xffu;
        hash *= 1099511628211ull;
    }
    return hash;
}

std::string hex64(std::uint64_t value)
{
    const char* digits = "0123456789abcdef";
    std::string out(16, '0');
    for (int i = 15; i >= 0; --i)
    {
        out[static_cast<std::size_t>(i)] = digits[value & 0x0fu];
        value >>= 4;
    }
    return out;
}

std::string sourceHashForAudio(const std::vector<double>& monoSamples, int sampleRate)
{
    std::uint64_t hash = 1469598103934665603ull;
    hash = fnv1aUpdate(hash, static_cast<std::uint64_t>(std::max(0, sampleRate)));
    hash = fnv1aUpdate(hash, static_cast<std::uint64_t>(monoSamples.size()));
    for (const double sample : monoSamples)
    {
        const auto quantized = static_cast<std::int64_t>(
            std::llround(clamp(std::isfinite(sample) ? sample : 0.0, -4.0, 4.0) * 2147483647.0));
        hash = fnv1aUpdate(hash, static_cast<std::uint64_t>(quantized));
    }
    return "direct_audio:" + std::to_string(sampleRate) + ":" + std::to_string(monoSamples.size())
           + ":" + hex64(hash);
}

std::string parameterHashForModernAnalysis(const AnalysisParams& params,
                                           const ModernAtsAnalysisConfig& config,
                                           int windowSize,
                                           int hopSize)
{
    std::ostringstream out;
    out << "modern:type=" << params.type
        << ";mode=" << toString(config.analysisMode)
        << ";tracker=" << toString(config.trackerMode)
        << ";peak=" << (config.useExperimentalPeakEstimator
                            ? "experimental_reassigned_peak_estimator"
                            : "classic_stft_peak_estimator")
        << ";lf=" << params.lowest_freq
        << ";hf=" << params.highest_freq
        << ";lm=" << params.lowest_mag
        << ";win=" << windowSize
        << ";hop=" << hopSize
        << ";phase=oscillator_start"
        << ";phaseCost=0.15"
        << ";maxPeaks=" << config.maxPeaksPerFrame
        << ";maxCand=" << config.maxCandidatesPerTrack
        << ";transientBuffers=" << (config.captureTransientBuffers ? 1 : 0)
        << ";maxTransientBufferSeconds=" << config.maxTransientBufferSeconds;
    return out.str();
}

int analysisWindowSize(const AnalysisParams& params)
{
    return std::max(16, params.win_size > 0 ? params.win_size : ATSA_WSIZE);
}

int analysisHopSize(const AnalysisParams& params, int windowSize)
{
    if (params.hop_size > 1.0)
        return std::max(1, static_cast<int>(std::llround(params.hop_size)));
    return std::max(1, static_cast<int>(std::llround(static_cast<double>(windowSize)
                                                     * std::max(0.001, params.hop_size))));
}

double analysisWindowWeight(int index, int size, int windowType)
{
    if (size <= 1)
        return 1.0;

    const double phase = kTwoPi * static_cast<double>(index) / static_cast<double>(size - 1);
    switch (windowType)
    {
        case BLACKMAN:
            return 0.42 - 0.50 * std::cos(phase) + 0.08 * std::cos(2.0 * phase);
        case BLACKMAN_H:
            return 0.35875 - 0.48829 * std::cos(phase) + 0.14128 * std::cos(2.0 * phase)
                   - 0.01168 * std::cos(3.0 * phase);
        case HANNING:
        case VONHANN:
            return 0.5 - 0.5 * std::cos(phase);
        default:
            return 1.0;
    }
}

std::size_t nextPowerOfTwo(std::size_t value)
{
    std::size_t out = 1;
    while (out < value)
        out <<= 1;
    return out;
}

void fftInPlace(std::vector<std::complex<double>>& data)
{
    const std::size_t n = data.size();
    if (n <= 1)
        return;

    for (std::size_t i = 1, j = 0; i < n; ++i)
    {
        std::size_t bit = n >> 1;
        for (; (j & bit) != 0; bit >>= 1)
            j ^= bit;
        j ^= bit;

        if (i < j)
            std::swap(data[i], data[j]);
    }

    for (std::size_t len = 2; len <= n; len <<= 1)
    {
        const double angle = -kTwoPi / static_cast<double>(len);
        const std::complex<double> step(std::cos(angle), std::sin(angle));
        for (std::size_t i = 0; i < n; i += len)
        {
            std::complex<double> twiddle(1.0, 0.0);
            const std::size_t half = len >> 1;
            for (std::size_t j = 0; j < half; ++j)
            {
                const auto even = data[i + j];
                const auto odd = data[i + j + half] * twiddle;
                data[i + j] = even + odd;
                data[i + j + half] = even - odd;
                twiddle *= step;
            }
        }
    }
}

std::vector<SpectrumBin> estimateSpectrumBinsForFrame(const std::vector<double>& monoSamples,
                                                      int frameStartSample,
                                                      int windowSize,
                                                      int sampleRate,
                                                      const AnalysisParams& params,
                                                      double& frameMeanSquare)
{
    frameMeanSquare = 0.0;
    if (windowSize <= 0 || sampleRate <= 0)
        return {};

    const double nyquist = 0.5 * static_cast<double>(sampleRate);
    const double lowest = std::max(0.0, params.lowest_freq);
    const double highest = std::max(lowest, std::min(params.highest_freq, nyquist));
    const auto fftSize = nextPowerOfTwo(static_cast<std::size_t>(windowSize));
    const int lowestBin = std::max(1,
                                   static_cast<int>(std::floor(lowest * static_cast<double>(fftSize)
                                                               / static_cast<double>(sampleRate))) - 1);
    const int highestBin =
        std::min(static_cast<int>(fftSize / 2),
                 static_cast<int>(std::ceil(highest * static_cast<double>(fftSize)
                                            / static_cast<double>(sampleRate))) + 1);

    if (highestBin <= lowestBin)
        return {};

    std::vector<std::complex<double>> spectrum(fftSize);
    double windowSum = 0.0;
    double windowPower = 0.0;
    double weightedEnergy = 0.0;

    for (int i = 0; i < windowSize; ++i)
    {
        const int sampleIndex = frameStartSample + i;
        const double sample = sampleIndex >= 0 && sampleIndex < static_cast<int>(monoSamples.size())
                                  ? monoSamples[static_cast<std::size_t>(sampleIndex)]
                                  : 0.0;
        const double w = analysisWindowWeight(i, windowSize, params.win_type);
        const double x = (std::isfinite(sample) ? sample : 0.0) * w;
        spectrum[static_cast<std::size_t>(i)] = { x, 0.0 };
        windowSum += w;
        windowPower += w * w;
        weightedEnergy += x * x;
    }

    frameMeanSquare = windowPower > 1.0e-12 ? weightedEnergy / windowPower : 0.0;

    std::vector<SpectrumBin> bins;
    bins.reserve(static_cast<std::size_t>(highestBin - lowestBin + 1));
    const double ampNorm = 2.0 / std::max(1.0e-12, windowSum);

    fftInPlace(spectrum);

    for (int bin = lowestBin; bin <= highestBin; ++bin)
    {
        const auto& value = spectrum[static_cast<std::size_t>(bin)];
        const double amp = ampNorm * std::abs(value);
        SpectrumBin spectrumBin;
        spectrumBin.frequencyHz = static_cast<double>(bin) * static_cast<double>(sampleRate)
                                  / static_cast<double>(fftSize);
        spectrumBin.magnitudeDb = ampToDb(amp);
        spectrumBin.phaseRadians = std::atan2(value.imag(), value.real());
        bins.push_back(spectrumBin);
    }

    return bins;
}

std::vector<SpectralPeak> capAndSortPeaks(std::vector<SpectralPeak> peaks, std::size_t maxPeaks)
{
    std::sort(peaks.begin(), peaks.end(), [](const SpectralPeak& a, const SpectralPeak& b) {
        if (a.amplitude == b.amplitude)
            return a.frequencyHz < b.frequencyHz;
        return a.amplitude > b.amplitude;
    });

    if (maxPeaks > 0 && peaks.size() > maxPeaks)
        peaks.resize(maxPeaks);

    std::sort(peaks.begin(), peaks.end(), [](const SpectralPeak& a, const SpectralPeak& b) {
        return a.frequencyHz < b.frequencyHz;
    });

    return peaks;
}

std::vector<double> estimateOrphanResidualBands(const std::vector<SpectrumBin>& bins,
                                                const TrackingPeakFrame& assignedPeaks)
{
    std::vector<double> total(kModernAtsDefaultResidualBands, 0.0);
    std::vector<double> assigned(kModernAtsDefaultResidualBands, 0.0);
    std::vector<double> assignedBinEnergy(bins.size(), 0.0);

    for (const auto& bin : bins)
    {
        if (!spectrumBinFinite(bin))
            continue;

        const int band = barkBandIndexForFrequency(bin.frequencyHz);
        total[static_cast<std::size_t>(band)] += spectralEnergyFromBin(bin);
    }

    for (const auto& peak : assignedPeaks)
    {
        if (!std::isfinite(peak.frequencyHz))
            continue;

        if (peak.sourceIndex < 0 || peak.sourceIndex >= static_cast<int>(bins.size()))
        {
            const int band = barkBandIndexForFrequency(peak.frequencyHz);
            assigned[static_cast<std::size_t>(band)] += spectralEnergyFromAmplitude(peak.amplitude);
            continue;
        }

        const int leftBoundary = residualLobeBoundaryForDirection(bins, peak.sourceIndex, -1);
        const int rightBoundary = residualLobeBoundaryForDirection(bins, peak.sourceIndex, 1);
        for (int binIndex = leftBoundary; binIndex <= rightBoundary; ++binIndex)
        {
            if (binIndex < 0 || binIndex >= static_cast<int>(bins.size()))
                continue;

            assignedBinEnergy[static_cast<std::size_t>(binIndex)] =
                std::max(assignedBinEnergy[static_cast<std::size_t>(binIndex)],
                         spectralEnergyFromBin(bins[static_cast<std::size_t>(binIndex)]));
        }
    }

    for (std::size_t binIndex = 0; binIndex < assignedBinEnergy.size(); ++binIndex)
    {
        if (assignedBinEnergy[binIndex] <= 0.0 || !spectrumBinFinite(bins[binIndex]))
            continue;

        const int band = barkBandIndexForFrequency(bins[binIndex].frequencyHz);
        assigned[static_cast<std::size_t>(band)] += assignedBinEnergy[binIndex];
    }

    return computeOrphanResidualBandEnergy(total, assigned);
}

TrackingPeakFrame trackingFrameFromPeaks(const std::vector<SpectralPeak>& peaks)
{
    TrackingPeakFrame frame;
    frame.reserve(peaks.size());
    for (const auto& peak : peaks)
    {
        TrackingPeak trackingPeak;
        trackingPeak.sourceIndex = peak.binIndex;
        trackingPeak.frequencyHz = peak.frequencyHz;
        trackingPeak.amplitude = peak.amplitude;
        trackingPeak.smrDb = peak.smrDb;
        trackingPeak.phaseRadians = peak.phaseRadians;
        frame.push_back(trackingPeak);
    }
    return frame;
}

int nearestSpectrumBinIndex(const std::vector<SpectrumBin>& bins, double frequencyHz)
{
    if (bins.empty() || !std::isfinite(frequencyHz))
        return -1;

    int bestIndex = -1;
    double bestDelta = std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < bins.size(); ++i)
    {
        if (!spectrumBinFinite(bins[i]))
            continue;

        const double delta = std::abs(bins[i].frequencyHz - frequencyHz);
        if (delta < bestDelta)
        {
            bestDelta = delta;
            bestIndex = static_cast<int>(i);
        }
    }

    return bestIndex;
}

TrackingPeakFrame trackingFrameFromActivePartials(const std::vector<ModernAtsPartial>& partials,
                                                  std::size_t frameIndex,
                                                  const std::vector<SpectrumBin>& bins)
{
    TrackingPeakFrame frame;
    for (const auto& partial : partials)
    {
        if (frameIndex >= partial.frames.size())
            continue;

        const auto& partialFrame = partial.frames[frameIndex];
        if (!modernFrameActive(partialFrame))
            continue;

        TrackingPeak trackingPeak;
        trackingPeak.sourceIndex = nearestSpectrumBinIndex(bins, partialFrame.frequencyHz);
        trackingPeak.frequencyHz = partialFrame.frequencyHz;
        trackingPeak.amplitude = partialFrame.amplitude;
        trackingPeak.smrDb = partialFrame.smrDb;
        trackingPeak.phaseRadians = partialFrame.phaseRadians;
        frame.push_back(trackingPeak);
    }

    return frame;
}

void pruneShortModernTracks(AssignmentTrackerResult& result, int minSegmentFrames)
{
    const int minFrames = std::max(1, minSegmentFrames);
    for (auto& partial : result.partials)
    {
        int activeCount = 0;
        int firstActiveFrame = -1;
        for (std::size_t frameIndex = 0; frameIndex < partial.frames.size(); ++frameIndex)
        {
            if (modernFrameActive(partial.frames[frameIndex]))
            {
                if (firstActiveFrame < 0)
                    firstActiveFrame = static_cast<int>(frameIndex);
                ++activeCount;
            }
        }

        if (activeCount == 0 || activeCount >= minFrames)
            continue;

        for (auto& frame : partial.frames)
        {
            if (!modernFrameActive(frame))
                continue;
            frame.reason = ModernAtsFrameReason::Pruned;
            frame.confidence = 0.0;
        }
        result.trackLifecycle.push_back(
            { partial.id, firstActiveFrame, ModernAtsLifecycleKind::Pruned, "short_direct_track" });
    }
}

void populateModernFramesFromTrackedPartials(ModernAtsModel& model,
                                             const std::vector<double>& frameTimes,
                                             const std::vector<double>& frameMeanSquareEnergy,
                                             const std::vector<TrackingPeakFrame>& peakFrames,
                                             const std::vector<std::vector<double>>& residualBandsByFrame)
{
    model.frames.clear();
    model.frames.reserve(frameTimes.size());

    for (std::size_t frameIndex = 0; frameIndex < frameTimes.size(); ++frameIndex)
    {
        ModernAtsFrame frame;
        frame.timeSeconds = frameTimes[frameIndex];

        for (const auto& partial : model.partials)
        {
            if (frameIndex < partial.frames.size() && modernFrameActive(partial.frames[frameIndex]))
                frame.activePartialIds.push_back(partial.id);
        }

        double assignedEnergy = 0.0;
        if (frameIndex < peakFrames.size())
        {
            for (const auto& peak : peakFrames[frameIndex])
                assignedEnergy += 0.5 * peak.amplitude * peak.amplitude;
        }

        const double frameEnergy = frameIndex < frameMeanSquareEnergy.size()
                                       ? std::max(0.0, frameMeanSquareEnergy[frameIndex])
                                       : 0.0;
        if (frameIndex < residualBandsByFrame.size()
            && residualBandsByFrame[frameIndex].size() == kModernAtsDefaultResidualBands)
        {
            frame.orphanResidualBandEnergy = residualBandsByFrame[frameIndex];
        }
        else
        {
            const double orphanEnergy = std::max(0.0, frameEnergy - assignedEnergy);
            frame.orphanResidualBandEnergy.assign(kModernAtsDefaultResidualBands,
                                                  orphanEnergy
                                                      / static_cast<double>(kModernAtsDefaultResidualBands));
        }

        model.frames.push_back(std::move(frame));
    }
}

void attachTransientBuffersFromSource(ModernAtsModel& model,
                                      const std::vector<double>& monoSamples,
                                      int sampleRate,
                                      double maxBufferSeconds)
{
    if (monoSamples.empty() || sampleRate <= 0 || model.transients.empty() || maxBufferSeconds <= 0.0)
        return;

    const int maxSamples = std::max(1, static_cast<int>(std::ceil(maxBufferSeconds
                                                                  * static_cast<double>(sampleRate))));

    for (std::size_t markerIndex = 0; markerIndex < model.transients.size(); ++markerIndex)
    {
        auto& marker = model.transients[markerIndex];
        int startSample = std::max(0, static_cast<int>(std::floor(marker.windowStartSeconds
                                                                  * static_cast<double>(sampleRate))));
        int endSample = std::min(static_cast<int>(monoSamples.size()),
                                 static_cast<int>(std::ceil(marker.windowEndSeconds
                                                            * static_cast<double>(sampleRate))));
        if (endSample <= startSample)
            continue;

        const int length = endSample - startSample;
        if (length > maxSamples)
        {
            const int onsetSample = std::max(0, std::min(static_cast<int>(monoSamples.size()) - 1,
                                                         static_cast<int>(std::llround(marker.onsetSeconds
                                                                                      * sampleRate))));
            startSample = std::max(0, onsetSample - maxSamples / 2);
            endSample = std::min(static_cast<int>(monoSamples.size()), startSample + maxSamples);
            startSample = std::max(0, endSample - maxSamples);
        }

        ModernAtsTransientBuffer buffer;
        buffer.markerIndex = static_cast<int>(markerIndex);
        buffer.sampleRate = sampleRate;
        buffer.gain = 1.0;
        buffer.samples.reserve(static_cast<std::size_t>(std::max(0, endSample - startSample)));
        for (int i = startSample; i < endSample; ++i)
        {
            const double sample = monoSamples[static_cast<std::size_t>(i)];
            buffer.samples.push_back(static_cast<float>(std::isfinite(sample) ? sample : 0.0));
        }

        if (buffer.samples.empty())
            continue;

        marker.bufferIndex = static_cast<int>(model.transientBuffers.size());
        model.transientBuffers.push_back(std::move(buffer));
    }
}

struct Candidate
{
    int partialId = 0;
    int peakIndex = 0;
    ModernAtsAssignmentCost cost;
};

std::vector<int> solveMinCostAssignment(const std::vector<std::vector<double>>& costs)
{
    const int n = static_cast<int>(costs.size());
    if (n == 0)
        return {};

    const int m = static_cast<int>(costs.front().size());
    if (m < n)
        return {};

    std::vector<double> u(static_cast<std::size_t>(n + 1), 0.0);
    std::vector<double> v(static_cast<std::size_t>(m + 1), 0.0);
    std::vector<int> p(static_cast<std::size_t>(m + 1), 0);
    std::vector<int> way(static_cast<std::size_t>(m + 1), 0);

    for (int i = 1; i <= n; ++i)
    {
        p[0] = i;
        int j0 = 0;
        std::vector<double> minv(static_cast<std::size_t>(m + 1),
                                 std::numeric_limits<double>::infinity());
        std::vector<char> used(static_cast<std::size_t>(m + 1), false);

        do
        {
            used[static_cast<std::size_t>(j0)] = true;
            const int i0 = p[static_cast<std::size_t>(j0)];
            double delta = std::numeric_limits<double>::infinity();
            int j1 = 0;

            for (int j = 1; j <= m; ++j)
            {
                if (used[static_cast<std::size_t>(j)])
                    continue;

                const double cur = costs[static_cast<std::size_t>(i0 - 1)][static_cast<std::size_t>(j - 1)]
                                   - u[static_cast<std::size_t>(i0)]
                                   - v[static_cast<std::size_t>(j)];
                if (cur < minv[static_cast<std::size_t>(j)])
                {
                    minv[static_cast<std::size_t>(j)] = cur;
                    way[static_cast<std::size_t>(j)] = j0;
                }
                if (minv[static_cast<std::size_t>(j)] < delta)
                {
                    delta = minv[static_cast<std::size_t>(j)];
                    j1 = j;
                }
            }

            for (int j = 0; j <= m; ++j)
            {
                if (used[static_cast<std::size_t>(j)])
                {
                    u[static_cast<std::size_t>(p[static_cast<std::size_t>(j)])] += delta;
                    v[static_cast<std::size_t>(j)] -= delta;
                }
                else
                {
                    minv[static_cast<std::size_t>(j)] -= delta;
                }
            }

            j0 = j1;
        }
        while (p[static_cast<std::size_t>(j0)] != 0);

        do
        {
            const int j1 = way[static_cast<std::size_t>(j0)];
            p[static_cast<std::size_t>(j0)] = p[static_cast<std::size_t>(j1)];
            j0 = j1;
        }
        while (j0 != 0);
    }

    std::vector<int> rowToColumn(static_cast<std::size_t>(n), -1);
    for (int j = 1; j <= m; ++j)
    {
        const int row = p[static_cast<std::size_t>(j)];
        if (row > 0 && row <= n)
            rowToColumn[static_cast<std::size_t>(row - 1)] = j - 1;
    }

    return rowToColumn;
}

ModernAtsAssignmentCost assignmentCostFor(int frameIndex,
                                          int partialId,
                                          int peakIndex,
                                          const ModernAtsPartial& partial,
                                          const TrackingPeak& peak,
                                          const AssignmentTrackerConfig& config)
{
    ModernAtsAssignmentCost cost;
    cost.frameIndex = frameIndex;
    cost.partialId = partialId;
    cost.peakIndex = peak.sourceIndex;

    const auto& last = partial.frames.back();
    const int lastActiveIndex = lastActivePartialFrameIndex(partial, frameIndex);
    const int previousActiveIndex =
        lastActiveIndex > 0 ? lastActivePartialFrameIndex(partial, lastActiveIndex) : -1;
    const auto& lastActive =
        lastActiveIndex >= 0 ? partial.frames[static_cast<std::size_t>(lastActiveIndex)] : last;
    const bool useSlope = config.analysisMode == ModernAtsAnalysisMode::ModernAts
                          && config.useFrequencySlopeContinuity && previousActiveIndex >= 0;

    const auto& previousActive = previousActiveIndex >= 0
                                     ? partial.frames[static_cast<std::size_t>(previousActiveIndex)]
                                     : lastActive;
    const double previousSlope = useSlope
                                     ? (lastActive.frequencyHz - previousActive.frequencyHz)
                                           / static_cast<double>(std::max(1,
                                                                          lastActiveIndex
                                                                              - previousActiveIndex))
                                     : 0.0;
    const double predictedFrequency =
        useSlope ? lastActive.frequencyHz
                       + previousSlope * static_cast<double>(std::max(1, frameIndex - lastActiveIndex))
                 : lastActive.frequencyHz;
    const double frequencyDenom = std::max(20.0, std::abs(predictedFrequency));
    cost.frequencyCost = config.frequencyWeight * std::abs(peak.frequencyHz - predictedFrequency) / frequencyDenom;

    const double ampDenom = std::max(1.0e-6, lastActive.amplitude);
    cost.amplitudeCost = config.amplitudeWeight * std::abs(peak.amplitude - lastActive.amplitude) / ampDenom;
    cost.smrCost = config.smrWeight * std::abs(peak.smrDb - lastActive.smrDb) / 100.0;
    cost.birthDeathCost = config.birthPenalty + config.deathPenalty;
    cost.spuriousPeakCost = config.spuriousPeakPenalty;

    if (config.analysisMode == ModernAtsAnalysisMode::ModernAts && config.phaseWeight > 0.0
        && config.sampleRate > 0 && config.hopSizeSamples > 0
        && std::isfinite(lastActive.phaseRadians) && std::isfinite(peak.phaseRadians))
    {
        const int frameDelta = std::max(1, frameIndex - lastActiveIndex);
        const double expectedPhase =
            wrapPhase(lastActive.phaseRadians
                      + kTwoPi * predictedFrequency
                            * static_cast<double>(config.hopSizeSamples)
                            * static_cast<double>(frameDelta)
                            / static_cast<double>(config.sampleRate));
        const double phaseError = std::abs(wrapPhase(peak.phaseRadians - expectedPhase));
        cost.phaseCost = config.phaseWeight * phaseError / (0.5 * kTwoPi);
    }

    if (useSlope)
    {
        const double observedSlope =
            (peak.frequencyHz - lastActive.frequencyHz)
            / static_cast<double>(std::max(1, frameIndex - lastActiveIndex));
        cost.slopeCost = config.frequencySlopeWeight * std::abs(observedSlope - previousSlope)
                         / std::max(100.0, std::abs(previousSlope));
    }

    cost.totalCost = cost.frequencyCost + cost.amplitudeCost + cost.smrCost + cost.phaseCost + cost.slopeCost;
    return cost;
}

} // namespace

const std::vector<AnalysisModeDescriptor>& analysisModeRegistry()
{
    static const std::vector<AnalysisModeDescriptor> modes = {
        { ModernAtsAnalysisMode::ClassicAts,
          ModernAtsTrackerMode::ClassicRecursiveFrequencySmr,
          "classic_ats",
          "Classic ATS",
          true,
          true },
        { ModernAtsAnalysisMode::ModernAts,
          ModernAtsTrackerMode::ModernAssignment,
          "modern_ats",
          "Modern ATS",
          false,
          true },
        { ModernAtsAnalysisMode::Experimental,
          ModernAtsTrackerMode::Experimental,
          "experimental",
          "Experimental",
          false,
          false },
    };
    return modes;
}

const AnalysisModeDescriptor* findAnalysisModeDescriptor(const std::string& id)
{
    const auto& modes = analysisModeRegistry();
    const auto it = std::find_if(modes.begin(), modes.end(), [&id](const AnalysisModeDescriptor& mode) {
        return id == mode.id;
    });
    return it == modes.end() ? nullptr : &*it;
}

const AnalysisModeDescriptor& defaultAnalysisModeDescriptor()
{
    const auto& modes = analysisModeRegistry();
    const auto it = std::find_if(modes.begin(), modes.end(), [](const AnalysisModeDescriptor& mode) {
        return mode.compatibilityDefault;
    });
    return it == modes.end() ? modes.front() : *it;
}

std::vector<SpectralPeak> ClassicStftPeakEstimator::estimatePeaks(
    const std::vector<SpectrumBin>& bins,
    const PeakEstimationConfig& config,
    PeakEstimationDiagnostics& diagnostics) const
{
    diagnostics = {};
    diagnostics.lowestFrequencyHz = config.lowestFrequencyHz;
    diagnostics.highestFrequencyHz = config.highestFrequencyHz;
    diagnostics.magnitudeThresholdDb = config.magnitudeThresholdDb;

    std::vector<SpectralPeak> peaks;
    if (bins.size() < 3)
        return peaks;

    for (std::size_t i = 1; i + 1 < bins.size(); ++i)
    {
        const auto& left = bins[i - 1];
        const auto& center = bins[i];
        const auto& right = bins[i + 1];

        if (!spectrumBinFinite(left) || !spectrumBinFinite(center) || !spectrumBinFinite(right))
            continue;

        if (center.magnitudeDb < left.magnitudeDb || center.magnitudeDb < right.magnitudeDb)
            continue;

        ++diagnostics.rawPeaks;

        if (center.frequencyHz < config.lowestFrequencyHz || center.frequencyHz > config.highestFrequencyHz)
        {
            ++diagnostics.rejectedPeaks;
            diagnostics.rejectedPeakReasons.push_back("frequency_range");
            continue;
        }
        if (center.magnitudeDb < config.magnitudeThresholdDb)
        {
            ++diagnostics.rejectedPeaks;
            diagnostics.rejectedPeakReasons.push_back("magnitude_threshold");
            continue;
        }

        double interpolatedFrequency = center.frequencyHz;
        double interpolatedMagnitude = center.magnitudeDb;
        if (config.interpolateParabolically)
        {
            const double denom = left.magnitudeDb - 2.0 * center.magnitudeDb + right.magnitudeDb;
            if (std::abs(denom) > 1.0e-12)
            {
                const double offset = clamp(0.5 * (left.magnitudeDb - right.magnitudeDb) / denom, -0.5, 0.5);
                const double binSpacing = 0.5 * (right.frequencyHz - left.frequencyHz);
                interpolatedFrequency = center.frequencyHz + offset * binSpacing;
                interpolatedMagnitude = center.magnitudeDb - 0.25 * (left.magnitudeDb - right.magnitudeDb) * offset;
            }
        }

        SpectralPeak peak;
        peak.binIndex = static_cast<int>(i);
        peak.frequencyHz = interpolatedFrequency;
        peak.amplitude = ampFromDb(interpolatedMagnitude);
        peak.phaseRadians = oscillatorPhaseAtFrameStart(center.phaseRadians,
                                                        center.frequencyHz,
                                                        interpolatedFrequency,
                                                        0.5 * (right.frequencyHz - left.frequencyHz),
                                                        config);
        // smrDb is set to magnitude as a proxy; a real SMR requires a psychoacoustic
        // masking threshold which is not computed here.
        peak.smrDb = interpolatedMagnitude;
        peak.magnitudeDb = interpolatedMagnitude;
        peaks.push_back(peak);
        ++diagnostics.acceptedPeaks;
    }

    return peaks;
}

std::vector<SpectralPeak> ExperimentalReassignedPeakEstimator::estimatePeaks(
    const std::vector<SpectrumBin>& bins,
    const PeakEstimationConfig& config,
    PeakEstimationDiagnostics& diagnostics) const
{
    diagnostics = {};
    diagnostics.lowestFrequencyHz = config.lowestFrequencyHz;
    diagnostics.highestFrequencyHz = config.highestFrequencyHz;
    diagnostics.magnitudeThresholdDb = config.magnitudeThresholdDb;

    std::vector<SpectralPeak> peaks;
    if (bins.size() < 5)
        return peaks;

    for (std::size_t i = 2; i + 2 < bins.size(); ++i)
    {
        const auto& c = bins[i];
        if (!spectrumBinFinite(bins[i - 1]) || !spectrumBinFinite(c) || !spectrumBinFinite(bins[i + 1]))
            continue;

        if (c.magnitudeDb < bins[i - 1].magnitudeDb || c.magnitudeDb < bins[i + 1].magnitudeDb)
            continue;
        ++diagnostics.rawPeaks;

        if (c.frequencyHz < config.lowestFrequencyHz || c.frequencyHz > config.highestFrequencyHz)
        {
            ++diagnostics.rejectedPeaks;
            diagnostics.rejectedPeakReasons.push_back("frequency_range");
            continue;
        }
        if (c.magnitudeDb < config.magnitudeThresholdDb)
        {
            ++diagnostics.rejectedPeaks;
            diagnostics.rejectedPeakReasons.push_back("magnitude_threshold");
            continue;
        }

        double reassignedOffset = 0.0;
        const double denom = bins[i - 1].magnitudeDb - 2.0 * c.magnitudeDb + bins[i + 1].magnitudeDb;
        if (std::abs(denom) > 1.0e-12)
        {
            reassignedOffset =
                clamp(0.5 * (bins[i - 1].magnitudeDb - bins[i + 1].magnitudeDb) / denom, -0.75, 0.75);
        }
        const double localSpacing = 0.5 * (bins[i + 1].frequencyHz - bins[i - 1].frequencyHz);

        SpectralPeak peak;
        peak.binIndex = static_cast<int>(i);
        peak.frequencyHz = c.frequencyHz + reassignedOffset * localSpacing;
        peak.magnitudeDb =
            c.magnitudeDb - 0.25 * (bins[i - 1].magnitudeDb - bins[i + 1].magnitudeDb) * reassignedOffset;
        peak.amplitude = ampFromDb(peak.magnitudeDb);
        peak.phaseRadians = oscillatorPhaseAtFrameStart(c.phaseRadians,
                                                        c.frequencyHz,
                                                        peak.frequencyHz,
                                                        localSpacing,
                                                        config);
        peak.smrDb = peak.magnitudeDb;
        peaks.push_back(peak);
        ++diagnostics.acceptedPeaks;
    }

    return peaks;
}

PeakEstimatorComparison comparePeakEstimators(const IPeakEstimator& baseline,
                                              const IPeakEstimator& candidate,
                                              const std::vector<SpectrumBin>& bins,
                                              const PeakEstimationConfig& config,
                                              double peakMatchToleranceHz)
{
    PeakEstimatorComparison comparison;
    comparison.baselineId = baseline.id();
    comparison.candidateId = candidate.id();

    PeakEstimationDiagnostics baselineDiag;
    PeakEstimationDiagnostics candidateDiag;
    const auto t0 = std::chrono::high_resolution_clock::now();
    const auto baselinePeaks = baseline.estimatePeaks(bins, config, baselineDiag);
    const auto t1 = std::chrono::high_resolution_clock::now();
    const auto candidatePeaks = candidate.estimatePeaks(bins, config, candidateDiag);
    const auto t2 = std::chrono::high_resolution_clock::now();

    comparison.baselineCpuMillis =
        std::chrono::duration<double, std::milli>(t1 - t0).count();
    comparison.candidateCpuMillis =
        std::chrono::duration<double, std::milli>(t2 - t1).count();
    comparison.baselineAcceptedPeaks = static_cast<int>(baselinePeaks.size());
    comparison.candidateAcceptedPeaks = static_cast<int>(candidatePeaks.size());

    int matched = 0;
    double freqDeltaAcc = 0.0;
    std::vector<bool> candidateUsed(candidatePeaks.size(), false);
    for (const auto& basePeak : baselinePeaks)
    {
        double bestDelta = std::numeric_limits<double>::infinity();
        int bestIdx = -1;
        for (std::size_t i = 0; i < candidatePeaks.size(); ++i)
        {
            if (candidateUsed[i])
                continue;
            const double delta = std::abs(candidatePeaks[i].frequencyHz - basePeak.frequencyHz);
            if (delta < bestDelta)
            {
                bestDelta = delta;
                bestIdx = static_cast<int>(i);
            }
        }
        if (bestIdx >= 0 && bestDelta <= peakMatchToleranceHz)
        {
            candidateUsed[static_cast<std::size_t>(bestIdx)] = true;
            ++matched;
            freqDeltaAcc += bestDelta;
        }
    }

    const int denom = std::max(1, static_cast<int>(std::max(baselinePeaks.size(), candidatePeaks.size())));
    comparison.matchedPeakRatio = static_cast<double>(matched) / static_cast<double>(denom);
    comparison.meanAbsFrequencyDeltaHz = matched > 0 ? freqDeltaAcc / static_cast<double>(matched) : 0.0;
    comparison.recommendation = (comparison.matchedPeakRatio >= 0.80
                                 && comparison.candidateCpuMillis <= comparison.baselineCpuMillis * 1.5)
                                    ? "experimental_candidate_viable"
                                    : "keep_experimental_only";
    return comparison;
}

ClassicAtsAnalysisResult runClassicAtsAnalysis(const AnalysisParams& params,
                                               const std::vector<double>& monoSamples,
                                               int sampleRate)
{
    ClassicAtsAnalysisResult result;
    if (monoSamples.empty())
    {
        result.errorMessage = "No samples to analyze.";
        return result;
    }
    if (sampleRate <= 0)
    {
        result.errorMessage = "Invalid sample rate.";
        return result;
    }

    ANARGS an {};
    params.toAnargs(an);

    double** residual = nullptr;
    int residualLength = 0;
    // tracker() takes a non-const pointer but does not modify the sample data.
    auto* samples = const_cast<double*>(monoSamples.data());
    ATS_SOUND* raw = tracker(&an, samples, sampleRate, static_cast<int>(monoSamples.size()), &residual, &residualLength);
    freeResidual(&residual);

    if (raw == nullptr)
    {
        result.errorMessage = "Classic ATS tracker failed.";
        return result;
    }

    for (int partial = 0; partial < raw->partials; ++partial)
    {
        std::vector<bool> activity;
        activity.reserve(static_cast<std::size_t>(raw->frames));
        for (int frame = 0; frame < raw->frames; ++frame)
        {
            const bool active = raw->amp != nullptr && raw->frq != nullptr && raw->amp[partial] != nullptr
                                && raw->frq[partial] != nullptr && raw->amp[partial][frame] > 0.0
                                && raw->frq[partial][frame] > 0.0;
            activity.push_back(active);
        }
        auto lifecycle =
            normalizeTrackLifecycleFromActivity(partial, activity, params.min_gap_len, params.min_seg_len);
        result.trackLifecycle.insert(result.trackLifecycle.end(), lifecycle.begin(), lifecycle.end());
    }

    result.sound.reset(raw);
    return result;
}

ModernAtsAnalysisResult runModernAtsAnalysis(const AnalysisParams& params,
                                             const std::vector<double>& monoSamples,
                                             int sampleRate,
                                             const ModernAtsAnalysisConfig& config)
{
    ModernAtsAnalysisResult result;
    if (monoSamples.empty())
    {
        result.errorMessage = "No samples to analyze.";
        return result;
    }
    if (sampleRate <= 0)
    {
        result.errorMessage = "Invalid sample rate.";
        return result;
    }

    const int windowSize = analysisWindowSize(params);
    const int hopSize = analysisHopSize(params, windowSize);
    const int startSample = std::max(0, static_cast<int>(std::floor(params.start * sampleRate)));
    const int requestedEnd = params.duration > 0.0
                                 ? startSample + std::max(1, static_cast<int>(std::ceil(params.duration * sampleRate)))
                                 : static_cast<int>(monoSamples.size());
    const int endSample = std::min(static_cast<int>(monoSamples.size()), requestedEnd);
    if (startSample >= endSample)
    {
        result.errorMessage = "Analysis range is empty.";
        return result;
    }

    PeakEstimationConfig peakConfig;
    peakConfig.lowestFrequencyHz = params.lowest_freq;
    peakConfig.highestFrequencyHz = std::min(params.highest_freq, 0.5 * static_cast<double>(sampleRate));
    peakConfig.magnitudeThresholdDb = params.lowest_mag;
    peakConfig.interpolateParabolically = true;
    peakConfig.windowSizeSamples = windowSize;
    peakConfig.fftSizeSamples = static_cast<int>(nextPowerOfTwo(static_cast<std::size_t>(windowSize)));

    ClassicStftPeakEstimator classicEstimator;
    ExperimentalReassignedPeakEstimator experimentalEstimator;
    const IPeakEstimator& estimator = config.useExperimentalPeakEstimator
                                          ? static_cast<const IPeakEstimator&>(experimentalEstimator)
                                          : static_cast<const IPeakEstimator&>(classicEstimator);

    std::vector<TrackingPeakFrame> trackingFrames;
    std::vector<double> frameTimes;
    std::vector<double> frameMeanSquareEnergy;
    std::vector<std::vector<SpectrumBin>> spectrumBinsByFrame;
    std::vector<TransientFeatureFrame> transientFeatures;

    double previousPeakEnergy = 0.0;
    int previousPeakCount = 0;
    for (int frameStart = startSample; frameStart < endSample; frameStart += hopSize)
    {
        double frameEnergy = 0.0;
        auto bins = estimateSpectrumBinsForFrame(monoSamples,
                                                 frameStart,
                                                 windowSize,
                                                 sampleRate,
                                                 params,
                                                 frameEnergy);
        PeakEstimationDiagnostics diagnostics;
        auto peaks = estimator.estimatePeaks(bins, peakConfig, diagnostics);
        peaks = capAndSortPeaks(std::move(peaks), config.maxPeaksPerFrame);
        diagnostics.acceptedPeaks = static_cast<int>(peaks.size());
        diagnostics.rejectedPeaks = std::max(0, diagnostics.rawPeaks - diagnostics.acceptedPeaks);
        result.peakDiagnostics.push_back(std::move(diagnostics));

        auto trackingFrame = trackingFrameFromPeaks(peaks);
        double peakEnergy = 0.0;
        for (const auto& peak : trackingFrame)
            peakEnergy += peak.amplitude * peak.amplitude;

        TransientFeatureFrame feature;
        feature.timeSeconds = static_cast<double>(frameStart) / static_cast<double>(sampleRate);
        feature.energy = frameEnergy;
        feature.spectralFlux = std::max(0.0, peakEnergy - previousPeakEnergy);
        const double peakCountDelta =
            std::abs(static_cast<double>(trackingFrame.size()) - static_cast<double>(previousPeakCount));
        feature.modelDiscontinuity = peakCountDelta
                                     / static_cast<double>(std::max<std::size_t>(
                                         1, std::max(trackingFrame.size(),
                                                     static_cast<std::size_t>(std::max(0, previousPeakCount)))));
        transientFeatures.push_back(feature);

        frameTimes.push_back(feature.timeSeconds);
        frameMeanSquareEnergy.push_back(frameEnergy);
        trackingFrames.push_back(std::move(trackingFrame));
        spectrumBinsByFrame.push_back(std::move(bins));
        previousPeakEnergy = peakEnergy;
        previousPeakCount = static_cast<int>(trackingFrames.back().size());
    }

    if (trackingFrames.empty())
    {
        result.errorMessage = "Modern analysis produced no frames.";
        return result;
    }

    TransientDetectionDiagnostics transientDiagnostics;
    auto transients = detectTransientMarkers(transientFeatures, TransientDetectionConfig {}, transientDiagnostics);

    AssignmentTrackerConfig trackerConfig;
    trackerConfig.analysisMode = config.trackerMode == ModernAtsTrackerMode::ModernAssignment
                                     ? config.analysisMode
                                     : ModernAtsAnalysisMode::ClassicAts;
    trackerConfig.maxCandidatesPerTrack = config.maxCandidatesPerTrack;
    trackerConfig.useFrequencySlopeContinuity = config.trackerMode == ModernAtsTrackerMode::ModernAssignment;
    trackerConfig.maxGapFillFrames = std::max(0, params.min_gap_len);
    trackerConfig.sampleRate = sampleRate;
    trackerConfig.hopSizeSamples = hopSize;
    for (const auto& marker : transients)
        trackerConfig.transientBreakFrameIndices.push_back(marker.frameIndex);

    auto assignment = runAssignmentTrackerPrototype(trackingFrames, trackerConfig);
    pruneShortModernTracks(assignment, params.min_seg_len);

    std::vector<std::vector<double>> residualBandsByFrame;
    residualBandsByFrame.reserve(spectrumBinsByFrame.size());
    for (std::size_t frameIndex = 0; frameIndex < spectrumBinsByFrame.size(); ++frameIndex)
    {
        const auto activeTrackedPeaks =
            trackingFrameFromActivePartials(assignment.partials, frameIndex, spectrumBinsByFrame[frameIndex]);
        residualBandsByFrame.push_back(
            estimateOrphanResidualBands(spectrumBinsByFrame[frameIndex], activeTrackedPeaks));
    }

    auto& model = result.model;
    model.header.schemaVersion = kModernAtsSchemaVersion;
    model.header.analysisMode = config.analysisMode;
    model.header.trackerMode = config.trackerMode;
    model.header.sampleRate = sampleRate;
    model.header.hopSizeSamples = hopSize;
    model.header.windowSizeSamples = windowSize;
    model.header.inputChannelCount = std::max(1, config.inputChannelCount);
    model.header.channelPolicy = defaultModernAtsChannelPolicyForInput(model.header.inputChannelCount);
    model.header.parameterHash = parameterHashForModernAnalysis(params, config, windowSize, hopSize);
    model.header.sourceHash = sourceHashForAudio(monoSamples, sampleRate);
    model.partials = std::move(assignment.partials);
    model.trackLifecycle = std::move(assignment.trackLifecycle);
    model.assignmentCosts = std::move(assignment.assignmentCosts);
    model.transients = std::move(transients);
    populateModernFramesFromTrackedPartials(model,
                                            frameTimes,
                                            frameMeanSquareEnergy,
                                            trackingFrames,
                                            residualBandsByFrame);
    if (config.captureTransientBuffers)
        attachTransientBuffersFromSource(model, monoSamples, sampleRate, config.maxTransientBufferSeconds);

    if (config.useExperimentalPeakEstimator || config.analysisMode == ModernAtsAnalysisMode::Experimental)
    {
        model.warnings.push_back(
            "Experimental direct peak estimator used before modern assignment tracking.");
    }
    else
    {
        model.warnings.push_back(
            "Modern ATS direct source peak analysis: peaks are estimated from audio before tracking.");
    }
    if (config.trackerMode != ModernAtsTrackerMode::ModernAssignment)
    {
        model.warnings.push_back(
            "Modern model used classic-compatible nearest tracking over direct peak frames.");
    }
    if (!model.transientBuffers.empty())
    {
        model.warnings.push_back("Captured " + std::to_string(model.transientBuffers.size())
                                 + " transient source buffers for attack preservation.");
    }
    model.warnings.push_back("Estimated source-derived orphan residual energy in "
                             + std::to_string(kModernAtsDefaultResidualBands)
                             + " Bark bands for modern model diagnostics and morphing.");

    std::string validationError;
    if (!validateModernAtsModel(model, validationError))
    {
        result.errorMessage = validationError;
        result.model = {};
    }

    return result;
}

std::vector<ModernAtsTrackLifecycleEvent> normalizeTrackLifecycleFromActivity(int partialId,
                                                                              const std::vector<bool>& activity,
                                                                              int shortGapFrames,
                                                                              int minSegmentFrames)
{
    return normalizeTrackLifecycleFromActivity(partialId, activity, shortGapFrames, minSegmentFrames, {}, true);
}

std::vector<ModernAtsTrackLifecycleEvent> normalizeTrackLifecycleFromActivity(
    int partialId,
    const std::vector<bool>& activity,
    int shortGapFrames,
    int minSegmentFrames,
    const std::vector<int>& protectedFrameIndices,
    bool allowGapFillAcrossProtectedFrames)
{
    // Build a set for O(1) protected-frame lookup (used in inner gap-scan loop).
    const std::unordered_set<int> protectedSet(protectedFrameIndices.begin(),
                                               protectedFrameIndices.end());

    std::vector<ModernAtsTrackLifecycleEvent> events;
    const int n = static_cast<int>(activity.size());
    int cursor = 0;

    while (cursor < n)
    {
        while (cursor < n && !activity[static_cast<std::size_t>(cursor)])
            ++cursor;
        if (cursor >= n)
            break;

        const int segmentStart = cursor;
        int segmentEnd = cursor;
        std::vector<int> gapFillFrames;

        while (segmentEnd + 1 < n)
        {
            int gapStart = segmentEnd + 1;
            while (gapStart < n && activity[static_cast<std::size_t>(gapStart)])
            {
                segmentEnd = gapStart;
                ++gapStart;
            }
            if (gapStart >= n)
                break;

            int gapEnd = gapStart;
            while (gapEnd < n && !activity[static_cast<std::size_t>(gapEnd)])
                ++gapEnd;
            if (gapEnd >= n)
                break;

            const int gapLength = gapEnd - gapStart;
            if (gapLength > shortGapFrames)
                break;
            bool crossesProtectedFrame = false;
            for (int frame = gapStart; frame < gapEnd; ++frame)
                crossesProtectedFrame = crossesProtectedFrame || containsFrame(protectedSet, frame);
            if (crossesProtectedFrame && !allowGapFillAcrossProtectedFrames)
                break;

            for (int frame = gapStart; frame < gapEnd; ++frame)
                gapFillFrames.push_back(frame);
            segmentEnd = gapEnd;
        }

        const int segmentLength = segmentEnd - segmentStart + 1;
        if (segmentLength < minSegmentFrames)
        {
            events.push_back({ partialId, segmentStart, ModernAtsLifecycleKind::Pruned, "short_segment" });
        }
        else
        {
            events.push_back({ partialId, segmentStart, ModernAtsLifecycleKind::Birth, "active_segment" });
            for (const int frame : gapFillFrames)
                events.push_back({ partialId, frame, ModernAtsLifecycleKind::GapFill, "short_gap" });
            events.push_back({ partialId, segmentEnd, ModernAtsLifecycleKind::Death, "segment_end" });
        }

        cursor = segmentEnd + 1;
    }

    return events;
}

AssignmentTrackerResult runAssignmentTrackerPrototype(const std::vector<TrackingPeakFrame>& frames,
                                                      const AssignmentTrackerConfig& config)
{
    // Build a set for O(1) transient-break frame lookup (checked every frame).
    const std::unordered_set<int> transientBreakSet(config.transientBreakFrameIndices.begin(),
                                                    config.transientBreakFrameIndices.end());

    AssignmentTrackerResult result;
    std::vector<int> activePartialIds;
    std::vector<int> gapFillCounts;
    int nextPartialId = 0;

    for (std::size_t frameIndex = 0; frameIndex < frames.size(); ++frameIndex)
    {
        const auto& framePeaks = frames[frameIndex];
        for (auto& partial : result.partials)
        {
            while (partial.frames.size() < frameIndex)
                partial.frames.push_back(sleptFrame());
        }

        const bool transientBreak = !config.allowInterpolationAcrossTransients
                                    && containsFrame(transientBreakSet,
                                                     static_cast<int>(frameIndex));
        if (transientBreak)
        {
            for (const int partialId : activePartialIds)
            {
                if (partialId < 0 || partialId >= static_cast<int>(result.partials.size()))
                    continue;
                auto& partial = result.partials[static_cast<std::size_t>(partialId)];
                if (partial.frames.size() == frameIndex)
                    partial.frames.push_back(sleptFrame());
                result.trackLifecycle.push_back(
                    { partialId, static_cast<int>(frameIndex), ModernAtsLifecycleKind::Death, "transient_break" });
                if (partialId < static_cast<int>(gapFillCounts.size()))
                    gapFillCounts[static_cast<std::size_t>(partialId)] = 0;
            }
            activePartialIds.clear();
        }

        std::vector<int> activeTrackIds;
        activeTrackIds.reserve(activePartialIds.size());
        for (const int partialId : activePartialIds)
        {
            if (partialId >= 0 && partialId < static_cast<int>(result.partials.size()))
                activeTrackIds.push_back(partialId);
        }

        std::vector<bool> peakAssigned(framePeaks.size(), false);
        std::vector<bool> partialAssigned(result.partials.size(), false);
        std::vector<std::vector<Candidate>> candidatesByTrack(activeTrackIds.size());

        for (std::size_t trackIndex = 0; trackIndex < activeTrackIds.size(); ++trackIndex)
        {
            const int partialId = activeTrackIds[trackIndex];

            std::vector<Candidate> perTrack;
            for (std::size_t peakIndex = 0; peakIndex < framePeaks.size(); ++peakIndex)
            {
                Candidate candidate;
                candidate.partialId = partialId;
                candidate.peakIndex = static_cast<int>(peakIndex);
                candidate.cost = assignmentCostFor(static_cast<int>(frameIndex),
                                                   partialId,
                                                   static_cast<int>(peakIndex),
                                                   result.partials[static_cast<std::size_t>(partialId)],
                                                   framePeaks[peakIndex],
                                                   config);
                perTrack.push_back(candidate);
            }

            std::sort(perTrack.begin(), perTrack.end(), [](const Candidate& a, const Candidate& b) {
                return a.cost.totalCost < b.cost.totalCost;
            });
            if (perTrack.size() > config.maxCandidatesPerTrack)
                perTrack.resize(config.maxCandidatesPerTrack);
            candidatesByTrack[trackIndex] = std::move(perTrack);
        }

        std::vector<int> nextActivePartialIds;
        const std::size_t assignmentSize = activeTrackIds.size() + framePeaks.size();
        if (assignmentSize > 0)
        {
            std::vector<std::vector<double>> assignmentCosts(
                assignmentSize,
                std::vector<double>(assignmentSize, kAssignmentForbiddenCost));

            for (std::size_t trackIndex = 0; trackIndex < activeTrackIds.size(); ++trackIndex)
            {
                for (const auto& candidate : candidatesByTrack[trackIndex])
                {
                    const double alternativeCost = config.deathPenalty + config.spuriousPeakPenalty;
                    const double maxAllowedCost = std::min(config.maxMatchCost, alternativeCost);
                    if (candidate.cost.totalCost > maxAllowedCost)
                        continue;

                    assignmentCosts[trackIndex][static_cast<std::size_t>(candidate.peakIndex)] =
                        candidate.cost.totalCost
                        + kAssignmentTieBreakScale
                              * (static_cast<double>(trackIndex + 1)
                                 + static_cast<double>(candidate.peakIndex + 1) * 0.001);
                }

                assignmentCosts[trackIndex][framePeaks.size() + trackIndex] =
                    config.deathPenalty
                    + kAssignmentTieBreakScale * static_cast<double>(trackIndex + 1);
            }

            for (std::size_t peakIndex = 0; peakIndex < framePeaks.size(); ++peakIndex)
            {
                const std::size_t row = activeTrackIds.size() + peakIndex;
                assignmentCosts[row][peakIndex] =
                    config.birthPenalty
                    + kAssignmentTieBreakScale * static_cast<double>(peakIndex + 1);
                for (std::size_t trackIndex = 0; trackIndex < activeTrackIds.size(); ++trackIndex)
                    assignmentCosts[row][framePeaks.size() + trackIndex] = 0.0;
            }

            const auto rowToColumn = solveMinCostAssignment(assignmentCosts);
            for (std::size_t trackIndex = 0; trackIndex < activeTrackIds.size(); ++trackIndex)
            {
                if (trackIndex >= rowToColumn.size())
                    continue;
                const int column = rowToColumn[trackIndex];
                if (column < 0 || column >= static_cast<int>(framePeaks.size()))
                    continue;

                const int partialId = activeTrackIds[trackIndex];
                const auto candidateIt =
                    std::find_if(candidatesByTrack[trackIndex].begin(),
                                 candidatesByTrack[trackIndex].end(),
                                 [column](const Candidate& candidate) {
                                     return candidate.peakIndex == column;
                                 });
                if (candidateIt == candidatesByTrack[trackIndex].end())
                    continue;

                auto& partial = result.partials[static_cast<std::size_t>(partialId)];
                partial.frames.push_back(frameFromPeak(framePeaks[static_cast<std::size_t>(column)],
                                                       ModernAtsFrameReason::Matched,
                                                       1.0 - clamp(candidateIt->cost.totalCost, 0.0, 1.0)));
                fillInterpolatedGap(partial, static_cast<int>(frameIndex), result.trackLifecycle);
                if (partialId >= 0 && partialId < static_cast<int>(gapFillCounts.size()))
                    gapFillCounts[static_cast<std::size_t>(partialId)] = 0;
                partialAssigned[static_cast<std::size_t>(partialId)] = true;
                peakAssigned[static_cast<std::size_t>(column)] = true;
                nextActivePartialIds.push_back(partialId);
                result.assignmentCosts.push_back(candidateIt->cost);
            }
        }

        for (const int partialId : activePartialIds)
        {
            if (partialId < 0 || partialId >= static_cast<int>(result.partials.size()))
                continue;
            if (partialAssigned[static_cast<std::size_t>(partialId)])
                continue;

            const int nextGapCount =
                partialId < static_cast<int>(gapFillCounts.size())
                    ? gapFillCounts[static_cast<std::size_t>(partialId)] + 1
                    : 1;
            result.partials[static_cast<std::size_t>(partialId)].frames.push_back(sleptFrame());
            if (nextGapCount <= std::max(0, config.maxGapFillFrames))
            {
                if (partialId >= static_cast<int>(gapFillCounts.size()))
                    gapFillCounts.resize(static_cast<std::size_t>(partialId + 1), 0);
                gapFillCounts[static_cast<std::size_t>(partialId)] = nextGapCount;
                result.trackLifecycle.push_back(
                    { partialId, static_cast<int>(frameIndex), ModernAtsLifecycleKind::Sleep, "short_assignment_gap" });
                nextActivePartialIds.push_back(partialId);
                continue;
            }

            if (partialId < static_cast<int>(gapFillCounts.size()))
                gapFillCounts[static_cast<std::size_t>(partialId)] = 0;
            result.trackLifecycle.push_back(
                { partialId, static_cast<int>(frameIndex), ModernAtsLifecycleKind::Death, "unmatched_track" });
        }

        for (std::size_t peakIndex = 0; peakIndex < framePeaks.size(); ++peakIndex)
        {
            if (peakAssigned[peakIndex])
                continue;

            ModernAtsPartial partial;
            partial.id = nextPartialId++;
            partial.label = "track_" + std::to_string(partial.id);
            partial.frames.resize(frameIndex, sleptFrame());
            partial.frames.push_back(frameFromPeak(framePeaks[peakIndex], ModernAtsFrameReason::Born, 1.0));
            result.partials.push_back(partial);
            gapFillCounts.push_back(0);
            nextActivePartialIds.push_back(partial.id);
            result.trackLifecycle.push_back(
                { partial.id, static_cast<int>(frameIndex), ModernAtsLifecycleKind::Birth, "unmatched_peak" });
        }

        for (auto& partial : result.partials)
        {
            if (partial.frames.size() == frameIndex)
                partial.frames.push_back(sleptFrame());
        }

        activePartialIds = nextActivePartialIds;
    }

    for (auto& partial : result.partials)
    {
        while (partial.frames.size() < frames.size())
            partial.frames.push_back(sleptFrame());
    }

    return result;
}

std::vector<double> computeOrphanResidualBandEnergy(const std::vector<double>& totalBandEnergy,
                                                    const std::vector<double>& assignedBandEnergy)
{
    const std::size_t n = std::max(totalBandEnergy.size(), assignedBandEnergy.size());
    std::vector<double> orphan(n, 0.0);
    for (std::size_t i = 0; i < n; ++i)
    {
        const double total = i < totalBandEnergy.size() ? totalBandEnergy[i] : 0.0;
        const double assigned = i < assignedBandEnergy.size() ? assignedBandEnergy[i] : 0.0;
        orphan[i] = std::max(0.0, total - assigned);
    }
    return orphan;
}

std::vector<float> renderOrphanResidualBandNoise(const std::vector<std::vector<double>>& orphanBandEnergyByFrame,
                                                 int hopSizeSamples,
                                                 float gain,
                                                 std::uint32_t seed,
                                                 int sampleRate)
{
    return renderResidualBandNoiseFrames(orphanBandEnergyByFrame, hopSizeSamples, gain, seed, sampleRate);
}

std::vector<ModernAtsTransientMarker> detectTransientMarkers(
    const std::vector<TransientFeatureFrame>& frames,
    const TransientDetectionConfig& config,
    TransientDetectionDiagnostics& diagnostics)
{
    diagnostics = {};
    std::vector<ModernAtsTransientMarker> markers;
    if (frames.empty())
        return markers;

    diagnostics.scores.resize(frames.size(), 0.0);
    for (std::size_t i = 0; i < frames.size(); ++i)
    {
        const double previousEnergy = i == 0 ? frames[i].energy : frames[i - 1].energy;
        const double energyRise = std::max(0.0, frames[i].energy - previousEnergy)
                                  / std::max(1.0e-9, previousEnergy);
        diagnostics.scores[i] = config.spectralFluxWeight * std::max(0.0, frames[i].spectralFlux)
                                + config.energyRiseWeight * energyRise
                                + config.modelDiscontinuityWeight
                                      * std::max(0.0, frames[i].modelDiscontinuity);
    }

    const double mean = std::accumulate(diagnostics.scores.begin(), diagnostics.scores.end(), 0.0)
                        / static_cast<double>(diagnostics.scores.size());
    double variance = 0.0;
    for (const double score : diagnostics.scores)
        variance += (score - mean) * (score - mean);
    variance /= static_cast<double>(diagnostics.scores.size());
    diagnostics.threshold = std::max(config.minimumScore,
                                     mean + std::sqrt(variance) * config.thresholdStdDevMultiplier);

    double lastMarkerTime = -std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < frames.size(); ++i)
    {
        const double score = diagnostics.scores[i];
        const double left = i > 0 ? diagnostics.scores[i - 1] : -std::numeric_limits<double>::infinity();
        const double right =
            i + 1 < diagnostics.scores.size() ? diagnostics.scores[i + 1] : -std::numeric_limits<double>::infinity();
        const bool localPeak = score >= left && score >= right;
        const bool farEnough = frames[i].timeSeconds - lastMarkerTime >= config.minimumGapSeconds;

        if (score >= diagnostics.threshold && localPeak && farEnough)
        {
            ModernAtsTransientMarker marker;
            marker.frameIndex = static_cast<int>(i);
            marker.onsetSeconds = frames[i].timeSeconds;
            marker.windowStartSeconds = std::max(0.0, marker.onsetSeconds - config.preWindowSeconds);
            marker.windowEndSeconds = marker.onsetSeconds + config.postWindowSeconds;
            marker.gain = config.markerGain;
            markers.push_back(marker);
            lastMarkerTime = marker.onsetSeconds;
        }
        else if (score > 0.0)
        {
            diagnostics.rejectedFrameIndices.push_back(static_cast<int>(i));
        }
    }

    return markers;
}

ExperimentalTransientLane buildExperimentalTransientLane(const std::vector<ModernAtsTransientMarker>& markers,
                                                         int sampleRate,
                                                         int coeffsPerAtom)
{
    ExperimentalTransientLane lane;
    lane.sampleRate = sampleRate;
    if (sampleRate <= 0 || coeffsPerAtom <= 0)
        return lane;

    for (const auto& marker : markers)
    {
        ExperimentalTransientAtom atom;
        atom.centerSeconds = marker.onsetSeconds;
        atom.durationSeconds = std::max(0.001, marker.windowEndSeconds - marker.windowStartSeconds);
        atom.gain = marker.gain;
        atom.dctCoefficients.resize(static_cast<std::size_t>(coeffsPerAtom), 0.0f);
        for (int i = 0; i < coeffsPerAtom; ++i)
        {
            const double falloff = std::exp(-0.20 * static_cast<double>(i));
            atom.dctCoefficients[static_cast<std::size_t>(i)] =
                static_cast<float>(atom.gain * falloff * std::cos(0.35 * static_cast<double>(i)));
        }
        lane.atoms.push_back(std::move(atom));
    }
    return lane;
}

std::vector<float> synthesizeExperimentalTransientLane(const ExperimentalTransientLane& lane)
{
    if (lane.sampleRate <= 0 || lane.atoms.empty())
        return {};

    std::size_t totalSamples = 0;
    for (const auto& atom : lane.atoms)
    {
        const std::size_t atomEnd = static_cast<std::size_t>(
            std::max(0.0, atom.centerSeconds + atom.durationSeconds) * static_cast<double>(lane.sampleRate));
        totalSamples = std::max(totalSamples, atomEnd + 1);
    }
    if (totalSamples == 0)
        return {};

    std::vector<float> output(totalSamples, 0.0f);
    const double pi = std::acos(-1.0);
    for (const auto& atom : lane.atoms)
    {
        const std::size_t start = static_cast<std::size_t>(
            std::max(0.0, atom.centerSeconds - 0.5 * atom.durationSeconds) * static_cast<double>(lane.sampleRate));
        const std::size_t len = std::max<std::size_t>(1, static_cast<std::size_t>(
            atom.durationSeconds * static_cast<double>(lane.sampleRate)));
        for (std::size_t n = 0; n < len && (start + n) < output.size(); ++n)
        {
            const double x = static_cast<double>(n) / static_cast<double>(len);
            double v = 0.0;
            for (std::size_t k = 0; k < atom.dctCoefficients.size(); ++k)
            {
                v += static_cast<double>(atom.dctCoefficients[k]) * std::cos(pi * (k + 0.5) * x);
            }
            const double env = std::sin(pi * x);
            output[start + n] += static_cast<float>(v * env);
        }
    }
    return output;
}

TransientLaneComparison compareTransientLanes(const std::vector<ModernAtsTransientBuffer>& preservationBuffers,
                                              const ExperimentalTransientLane& lane)
{
    TransientLaneComparison comparison;
    comparison.bufferTransientCount = static_cast<int>(preservationBuffers.size());
    comparison.laneTransientCount = static_cast<int>(lane.atoms.size());

    for (const auto& buf : preservationBuffers)
    {
        for (const float s : buf.samples)
            comparison.bufferEnergy += static_cast<double>(s) * static_cast<double>(s);
    }
    const auto laneRender = synthesizeExperimentalTransientLane(lane);
    for (const float s : laneRender)
        comparison.laneEnergy += static_cast<double>(s) * static_cast<double>(s);

    const double norm = std::max(1.0e-9, comparison.bufferEnergy);
    comparison.normalizedEnergyDelta = std::abs(comparison.laneEnergy - comparison.bufferEnergy) / norm;
    comparison.graduationDecision = comparison.normalizedEnergyDelta <= 0.25
                                        ? "candidate_for_modern_mode"
                                        : "keep_as_experimental";
    return comparison;
}

DdspHelperAssessment evaluateDdspHelperPolicy(const std::string& helperKind,
                                              const std::vector<std::string>& requestedUseCases,
                                              bool wouldRequireRuntimeModel)
{
    DdspHelperAssessment assessment;
    assessment.helperKind = helperKind;
    assessment.introducesRuntimeDependency = wouldRequireRuntimeModel;
    assessment.approvedUseCases = { "parameter_estimation", "analysis_denoising", "preset_suggestion" };
    assessment.blockedUseCases = { "runtime_core_representation", "replacing_classical_model" };

    for (const auto& useCase : requestedUseCases)
    {
        const bool approved = std::find(assessment.approvedUseCases.begin(),
                                        assessment.approvedUseCases.end(),
                                        useCase) != assessment.approvedUseCases.end();
        if (!approved)
            assessment.keepsClassicModelAsSourceOfTruth = false;
    }

    assessment.recommendation =
        (!assessment.keepsClassicModelAsSourceOfTruth || wouldRequireRuntimeModel)
            ? "defer_ddsp_helper_until_licensing_size_determinism_are_resolved"
            : "allow_offline_assistive_ddsp_helper_only";
    return assessment;
}

} // namespace ats
