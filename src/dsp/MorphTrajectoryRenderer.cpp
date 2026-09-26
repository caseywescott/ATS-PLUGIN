#include "MorphTrajectoryRenderer.h"

#include "../core/AtsSpectralEnvelope.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <utility>

namespace ats::dsp
{
namespace
{
constexpr double kPi = 3.141592653589793238462643383279502884;

template <typename T>
[[nodiscard]] constexpr T jmax(T a, T b) noexcept { return a < b ? b : a; }
template <typename T>
[[nodiscard]] constexpr T jmin(T a, T b) noexcept { return b < a ? b : a; }
template <typename T>
[[nodiscard]] constexpr T jlimit(T low, T high, T value) noexcept
{ return value < low ? low : (high < value ? high : value); }

static std::uint32_t morphTrajResidualHash(int bandIndex, int componentIndex) noexcept
{
    std::uint32_t x = static_cast<std::uint32_t>(bandIndex + 1) * 0x9e3779b9u
                    ^ static_cast<std::uint32_t>(componentIndex + 17) * 0x85ebca6bu;
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
    return x;
}

static double morphTrajResidualUnitHash(int bandIndex, int componentIndex) noexcept
{
    return static_cast<double>(morphTrajResidualHash(bandIndex, componentIndex)) / 4294967295.0;
}

class CompiledMorphTimeline
{
public:
    explicit CompiledMorphTimeline(const std::vector<ats::engine::TimelinePoint>* points)
        : points_(points != nullptr ? ats::engine::MorphTimelineService::sanitize(*points)
                                    : std::vector<ats::engine::TimelinePoint> {})
    {
    }

    [[nodiscard]] double sampleMorph(double timeNorm, bool applyCurves) const
    {
        return ats::engine::MorphTimelineService::evaluate(
            points_, ats::engine::TimelinePointKind::morph, timeNorm, applyCurves);
    }

    [[nodiscard]] double sampleEffect(ats::engine::TimelinePointKind kind, double timeNorm) const
    {
        return ats::engine::MorphTimelineService::evaluate(points_, kind, timeNorm, true);
    }

private:
    std::vector<ats::engine::TimelinePoint> points_;
};

// TimbreRenderParams is defined in TimbreMotionPlan.h (ats::TimbreRenderParams).
using TimbreRenderParams = ats::TimbreRenderParams;

// LCG for per-band deterministic noise in renderMorphTrajectoryBuffer
static inline std::uint64_t morphTrajLcgNext(std::uint64_t s) noexcept
{
    return s * 6364136223846793005ULL + 1442695040888963407ULL;
}
static inline double morphTrajLcgDouble(std::uint64_t s) noexcept
{
    return static_cast<double>(s >> 11) / static_cast<double>(1ULL << 53); // [0, 1)
}

// ATSA critical-band center frequencies (Hz), shared by the residual synthesis pass and the
// unmatched-partial MergeIntoResidual policy (which reroutes a discarded partial's power into the
// nearest residual band).
static constexpr double kAtsBandCenterHzTable[ATSA_CRITICAL_BANDS] = {
    50.0, 150.0, 250.0, 350.0, 455.0, 570.0, 700.0, 845.0, 1000.0,
    1175.0, 1375.0, 1600.0, 1860.0, 2160.0, 2510.0, 2925.0, 3425.0,
    4050.0, 4850.0, 5850.0, 7050.0, 8600.0, 10750.0, 13750.0, 17750.0
};

// Nearest ATSA critical band index for a frequency in Hz (log-distance to band centers).
static inline int nearestAtsBandIndex(double hz) noexcept
{
    if (!(hz > 0.0))
        return 0;
    int best = 0;
    double bestDist = std::numeric_limits<double>::max();
    const double lh = std::log(hz);
    for (int b = 0; b < ATSA_CRITICAL_BANDS; ++b)
    {
        const double d = std::abs(lh - std::log(kAtsBandCenterHzTable[b]));
        if (d < bestDist) { bestDist = d; best = b; }
    }
    return best;
}

static bool sampleAnalysisPhase(const ATS_SOUND* sound, int partial, int frame0, int frame1,
                                double fraction, double& phaseRadians) noexcept
{
    if (sound == nullptr || sound->pha == nullptr || partial < 0 || partial >= sound->partials
        || sound->pha[partial] == nullptr)
        return false;
    const double a = sound->pha[partial][frame0];
    const double b = sound->pha[partial][frame1];
    if (!std::isfinite(a) || !std::isfinite(b))
        return false;
    const double x = (1.0 - fraction) * std::cos(a) + fraction * std::cos(b);
    const double y = (1.0 - fraction) * std::sin(a) + fraction * std::sin(b);
    if (x * x + y * y <= 1.0e-18)
        return false;
    phaseRadians = std::atan2(y, x);
    return true;
}

static double transientProtectionAmount(double timeSeconds,
                                        const std::vector<ats::MorphTransientMarker>& markers,
                                        double protectionSeconds,
                                        double rampSeconds) noexcept
{
    double amount = 0.0;
    const double protection = std::max(0.0, protectionSeconds);
    const double ramp = std::max(1.0e-6, rampSeconds);
    for (const auto& marker : markers)
    {
        if (!std::isfinite(marker.timeSeconds))
            continue;
        const double start = marker.timeSeconds - 0.005;
        const double end = marker.timeSeconds + protection;
        if (timeSeconds < start || timeSeconds > end)
            continue;
        const double fadeIn = jlimit(0.0, 1.0, (timeSeconds - start) / ramp);
        const double fadeOut = jlimit(0.0, 1.0, (end - timeSeconds) / ramp);
        const double edge = std::min(fadeIn, fadeOut);
        amount = std::max(amount, 0.5 - 0.5 * std::cos(kPi * edge));
    }
    return amount;
}

ats::BarkSpectralEnvelope buildBarkEnvelopeForFrame(const ATS_SOUND* sound,
                                                    int frame0,
                                                    int frame1,
                                                    double frac)
{
    ats::BarkSpectralEnvelope envelope;
    if (!sound || sound->frq == nullptr || sound->amp == nullptr)
        return envelope;

    std::vector<ats::SpectralEnvelopePoint> points;
    points.reserve(static_cast<size_t>(std::max(0, sound->partials)));
    for (int p = 0; p < sound->partials; ++p)
    {
        if (!sound->frq[p] || !sound->amp[p])
            continue;
        const double fq = sound->frq[p][frame0]
                          + (sound->frq[p][frame1] - sound->frq[p][frame0]) * frac;
        const double am = sound->amp[p][frame0]
                          + (sound->amp[p][frame1] - sound->amp[p][frame0]) * frac;
        points.push_back({ fq, am });
    }

    // True Envelope estimation auto-selects its cepstral order from the partials' Bark spacing,
    // so no per-frame smoothing heuristic is needed here.
    envelope.build(points, ats::BarkSpectralEnvelope::kDefaultBins);
    return envelope;
}

// Cache wrapper around buildBarkEnvelopeForFrame (see MorphEnvelopeCache). Returns a bit-identical
// result to the uncached build: hits are exact (frame indices and frac matched bit-for-bit) and a
// pointer/revision change rotates the relevant slot before any stale envelope can be served. The
// heavy build runs outside the lock; a second check guards against a concurrent cache rotation.
ats::BarkSpectralEnvelope buildBarkEnvelopeCached(MorphEnvelopeCache* cache, bool isSourceA,
                                                  const ATS_SOUND* sound, int frame0, int frame1,
                                                  double frac, uint64_t sourcesRevision)
{
    if (cache == nullptr)
        return buildBarkEnvelopeForFrame(sound, frame0, frame1, frac);

    uint64_t fracBits = 0;
    std::memcpy(&fracBits, &frac, sizeof fracBits);
    const auto key = std::make_tuple(frame0, frame1, fracBits);

    {
        const std::scoped_lock sl(cache->lock);
        if (cache->revision != sourcesRevision)
        {
            cache->revision = sourcesRevision;
            cache->keyA = cache->keyB = nullptr;
            cache->mapA.clear();
            cache->mapB.clear();
            cache->perFrameA.clear();
            cache->perFrameB.clear();
        }
        const ATS_SOUND*& keyPtr = isSourceA ? cache->keyA : cache->keyB;
        auto& map = isSourceA ? cache->mapA : cache->mapB;
        auto& perFrame = isSourceA ? cache->perFrameA : cache->perFrameB;
        if (keyPtr != sound)
        {
            keyPtr = sound;
            map.clear();
            perFrame.clear();
        }
        const auto it = map.find(key);
        if (it != map.end())
            return it->second;
    }

    ats::BarkSpectralEnvelope env = buildBarkEnvelopeForFrame(sound, frame0, frame1, frac);

    {
        const std::scoped_lock sl(cache->lock);
        if (cache->revision == sourcesRevision)
        {
            const ATS_SOUND* keyPtr = isSourceA ? cache->keyA : cache->keyB;
            auto& map = isSourceA ? cache->mapA : cache->mapB;
            if (keyPtr == sound)
                map.emplace(key, env);
        }
    }
    return env;
}

// R-005: cache integer-frame envelopes and lerp log-bins for timeline chunks (approximation;
// see FABLE_PERFORMANCE_REVIEW.md). Fixed-morph preview keeps buildBarkEnvelopeCached (exact).
static ats::BarkSpectralEnvelope getPerFrameBarkEnvelope(MorphEnvelopeCache* cache, bool isSourceA,
                                                         const ATS_SOUND* sound, int frame,
                                                         uint64_t sourcesRevision)
{
    if (cache == nullptr || sound == nullptr)
        return buildBarkEnvelopeForFrame(sound, frame, frame, 0.0);

    {
        const std::scoped_lock sl(cache->lock);
        if (cache->revision != sourcesRevision)
        {
            cache->revision = sourcesRevision;
            cache->keyA = cache->keyB = nullptr;
            cache->mapA.clear();
            cache->mapB.clear();
            cache->perFrameA.clear();
            cache->perFrameB.clear();
        }
        const ATS_SOUND*& keyPtr = isSourceA ? cache->keyA : cache->keyB;
        auto& perFrame = isSourceA ? cache->perFrameA : cache->perFrameB;
        if (keyPtr != sound)
        {
            keyPtr = sound;
            cache->mapA.clear();
            cache->mapB.clear();
            perFrame.clear();
        }
        const auto it = perFrame.find(frame);
        if (it != perFrame.end())
            return it->second;
    }

    ats::BarkSpectralEnvelope env = buildBarkEnvelopeForFrame(sound, frame, frame, 0.0);

    {
        const std::scoped_lock sl(cache->lock);
        if (cache->revision == sourcesRevision)
        {
            const ATS_SOUND* keyPtr = isSourceA ? cache->keyA : cache->keyB;
            auto& perFrame = isSourceA ? cache->perFrameA : cache->perFrameB;
            if (keyPtr == sound)
                perFrame.emplace(frame, env);
        }
    }
    return env;
}

static ats::BarkSpectralEnvelope buildBarkEnvelopeTimelineCached(MorphEnvelopeCache* cache,
                                                                 bool isSourceA,
                                                                 const ATS_SOUND* sound,
                                                                 int frame0, int frame1,
                                                                 double frac,
                                                                 uint64_t sourcesRevision)
{
    if (frame0 == frame1 || frac <= 0.0)
        return getPerFrameBarkEnvelope(cache, isSourceA, sound, frame0, sourcesRevision);
    if (frac >= 1.0)
        return getPerFrameBarkEnvelope(cache, isSourceA, sound, frame1, sourcesRevision);

    const ats::BarkSpectralEnvelope e0 =
        getPerFrameBarkEnvelope(cache, isSourceA, sound, frame0, sourcesRevision);
    const ats::BarkSpectralEnvelope e1 =
        getPerFrameBarkEnvelope(cache, isSourceA, sound, frame1, sourcesRevision);
    if (!e0.isValid())
        return e1;
    if (!e1.isValid())
        return e0;
    return e0.lerpLogBinsWith(e1, frac);
}

ats::HybridizedPartialKind toHybridizedPartialKind(ats::MorphCorrespondenceKind kind)
{
    switch (kind)
    {
        case ats::MorphCorrespondenceKind::Matched:
            return ats::HybridizedPartialKind::Matched;
        case ats::MorphCorrespondenceKind::UnmatchedSource:
            return ats::HybridizedPartialKind::UnmatchedSource;
        case ats::MorphCorrespondenceKind::UnmatchedTarget:
            return ats::HybridizedPartialKind::UnmatchedTarget;
    }
    return ats::HybridizedPartialKind::Matched;
}

// Frame-by-frame oscillator-bank morph synthesis.
//
// Performance design:
//   • Timeline points are sanitized ONCE before the loop (not per-sample).
//   • cos/sin are computed once per ATS frame chunk per partial (every ~hopSecs samples),
//     stored as a complex rotation. The hot loop only does 4 multiplies + 2 adds per partial.
//   • Residual band energy is interpolated per-chunk; several jittered carriers per band advance
//     the same way as partials (no per-sample trig), spreading each band's noise across its range.
//   • This is ~100–300x faster than a naive per-sample std::sin() approach.
MorphTrajectoryBuffer renderMorphTrajectoryImpl(
    const ATS_SOUND* soundA,
    const ATS_SOUND* soundB,
    const ats::MorphRecipe& recipe,
    const MorphTrajectorySettings& settings)
{
    const CompiledMorphTimeline compiledTimeline(settings.timeline);
    const CompiledMorphTimeline* timeline = settings.timeline != nullptr ? &compiledTimeline : nullptr;
    const double fixedMorphAmount = settings.fixedMorphAmount;
    const double sampleRate = settings.sampleRate;
    const double durationSeconds = settings.durationSeconds;
    MorphEnvelopeCache* envCache = settings.envelopeCache;
    const std::uint64_t sourcesRevision = settings.sourcesRevision;
    const ats::TimbreMotionPlan* timbrePlan = settings.timbrePlan;
    const double apvtsBrightnessDb = settings.brightnessDb;
    const double apvtsTiltDbPerOct = settings.tiltDbPerOctave;
    const double apvtsNoiseBalance = settings.noiseBalance;
    const ats::HybridizeDomains interpDomains = settings.interpolationDomains;
    const double smoothingMs = settings.smoothingMs;
    const double performanceMacro = settings.performanceMacro;
    const int totalSamples = jmax(1, (int) std::llround(sampleRate * durationSeconds));
    MorphTrajectoryBuffer out;
    out.partialSamples.assign(static_cast<std::size_t>(totalSamples), 0.0f);
    out.residualSamples.assign(static_cast<std::size_t>(totalSamples), 0.0f);

    if (soundA == nullptr || soundB == nullptr || recipe.correspondenceRecords.empty())
        return out;

    const auto& records = recipe.correspondenceRecords;
    const int nRec = (int) records.size();
    const double hopA = recipe.sourceA.frameHopSeconds;
    const double hopB = recipe.sourceB.frameHopSeconds;
    const int framesA = soundA->frames;
    const int framesB = soundB->frames;
    const double twoPi = 2.0 * kPi;
    const double invSr = 1.0 / sampleRate;

    const bool useTimeline = timeline != nullptr;

    // Chunk size = one ATS frame hop. Trig is computed once per chunk per partial.
    const double smallerHop = (hopA > 0.0 && hopB > 0.0) ? std::min(hopA, hopB)
                            : (hopA > 0.0 ? hopA : (hopB > 0.0 ? hopB : invSr));
    const int chunkSamples = jmax(1, (int) std::floor(smallerHop * sampleRate));

    // Timbre Motion Plan: per-chunk evaluation of time-varying brightness/tilt/noise targets.
    // chunkRateHz = how many evaluate() calls per second (one per ATS frame hop).
    const double chunkRateHz = sampleRate / (double) chunkSamples;
    const ats::TimbreMotionPlan emptyPlan;
    const ats::TimbreMotionPlan& activePlan = (timbrePlan && !timbrePlan->empty()) ? *timbrePlan : emptyPlan;
    ats::CompiledTimbrePlan compiledTimbrePlan(activePlan, chunkRateHz);

    // Measured centroid endpoints for InterpolateAB/AbsoluteTarget brightness curves (§5/§8.1).
    // Built once from a representative (max-energy) frame of each source; the solver later finds
    // the brightnessDb that places the reference spectrum's centroid at the requested Bark value.
    // Only built when the plan actually drives the centroid axis — otherwise the endpoint context
    // stays empty and evaluate() reproduces the analyzer-free behavior exactly.
    ats::TimbreEndpoints timbreEndpoints;
    std::vector<double> timbreRefFreqsHz, timbreRefAmps;
    if (!activePlan.empty() && compiledTimbrePlan.needsCentroidEndpoints())
    {
        auto sourceCentroidSpectrum = [](const ATS_SOUND* snd,
                                         std::vector<double>& freqsOut,
                                         std::vector<double>& ampsOut) -> double
        {
            freqsOut.clear();
            ampsOut.clear();
            if (snd == nullptr || snd->frq == nullptr || snd->amp == nullptr
                || snd->partials <= 0 || snd->frames <= 0)
                return 0.0;
            // Pick the frame carrying the most total partial energy (a representative spectrum).
            int bestFrame = 0;
            double bestEnergy = -1.0;
            for (int f = 0; f < snd->frames; ++f)
            {
                double e = 0.0;
                for (int p = 0; p < snd->partials; ++p)
                    if (snd->amp[p] != nullptr)
                    {
                        const double a = snd->amp[p][f];
                        if (std::isfinite(a) && a > 0.0) e += a * a;
                    }
                if (e > bestEnergy) { bestEnergy = e; bestFrame = f; }
            }
            for (int p = 0; p < snd->partials; ++p)
            {
                if (snd->frq[p] == nullptr || snd->amp[p] == nullptr) continue;
                const double fr = snd->frq[p][bestFrame];
                const double am = snd->amp[p][bestFrame];
                if (std::isfinite(fr) && std::isfinite(am) && fr > 0.0 && am > 0.0)
                {
                    freqsOut.push_back(fr);
                    ampsOut.push_back(am);
                }
            }
            return ats::spectralCentroidBark(freqsOut, ampsOut);
        };

        std::vector<double> freqsB, ampsB;
        const double barkA = sourceCentroidSpectrum(soundA, timbreRefFreqsHz, timbreRefAmps);
        const double barkB = sourceCentroidSpectrum(soundB, freqsB, ampsB);
        if (barkA > 0.0 && barkB > 0.0 && !timbreRefFreqsHz.empty())
        {
            timbreEndpoints.hasCentroid   = true;
            timbreEndpoints.centroidBarkA = barkA;
            timbreEndpoints.centroidBarkB = barkB;
            timbreEndpoints.refFreqsHz    = &timbreRefFreqsHz;
            timbreEndpoints.refAmps       = &timbreRefAmps;
        }
    }

    // Onset/time alignment: A and B are otherwise both sampled at the same wall-clock time, so if
    // their principal attacks sit at different times the morph midpoint hears a doubled/smeared
    // attack (two onsets at distinct instants). Shift B's time axis by a single, bounded offset so
    // its principal onset lands on A's. The shift is capped at the recipe's
    // maxOnsetAlignmentOffsetSeconds (a small nudge, not a full time-warp); a cap of 0 disables it.
    // Sustained material is unaffected to the ear by a ~20 ms global shift; only the attacks gain.
    const double onsetOffsetB = [&]() -> double
    {
        const double maxOff = std::max(0.0, recipe.transient.maxOnsetAlignmentOffsetSeconds);
        if (maxOff <= 0.0)
            return 0.0;
        auto principalOnset = [](const std::vector<ats::MorphTransientMarker>& markers) -> double
        {
            double maxStrength = 0.0;
            for (const auto& m : markers)
                if (std::isfinite(m.strength) && m.strength > maxStrength)
                    maxStrength = m.strength;
            if (maxStrength <= 0.0)
                return std::numeric_limits<double>::quiet_NaN();
            // Earliest onset that is at least half as strong as the strongest: the first salient
            // attack, which is what the ear locks onto, rather than a later louder accent.
            const double threshold = 0.5 * maxStrength;
            double best = std::numeric_limits<double>::quiet_NaN();
            for (const auto& m : markers)
                if (std::isfinite(m.timeSeconds) && std::isfinite(m.strength) && m.strength >= threshold)
                    if (std::isnan(best) || m.timeSeconds < best)
                        best = m.timeSeconds;
            return best;
        };
        const double tA = principalOnset(recipe.sourceATransients);
        const double tB = principalOnset(recipe.sourceBTransients);
        if (std::isnan(tA) || std::isnan(tB))
            return 0.0;
        // Read B at (t + offset) so B's onset tB is heard when playback reaches A's onset tA.
        const double rawOffset = jlimit(-maxOff, maxOff, tB - tA);
        // Time lane scales onset-alignment strength: 0 = no alignment, 1 = full alignment.
        const double timeLaneAmount = ats::evaluateMorphLaneAmount(
            recipe, ats::MorphLane::Time,
            useTimeline ? timeline->sampleMorph(0.0, false) : fixedMorphAmount);
        return rawOffset * jlimit(0.0, 1.0, timeLaneAmount);
    }();

    // Phasor state per partial: complex unit vector {re, im} = {cos(phase), sin(phase)}.
    // Seeded below from the analysis phase (fallback {1, 0}); updated by complex multiplication.
    struct PartialState { double re = 1.0, im = 0.0, rotRe = 1.0, rotIm = 0.0, amp = 0.0, ampInc = 0.0; bool active = false; };
    std::vector<PartialState> ps(nRec);

    // Seed each oscillator through the recipe's phase resolver. OscillatorContinuation deliberately
    // keeps the renderer's neutral phase; donor and blend modes consume the recorded ATS phases.
    // The phasor free-runs after this seed, except for bounded attack-coherence corrections below.
    {
        const double rawMorphAmount0 = jlimit(0.0, 1.0,
            useTimeline ? timeline->sampleMorph(0.0, false) : fixedMorphAmount);
        const double morphAmount0 = jlimit(0.0, 1.0,
            0.5 + (rawMorphAmount0 - 0.5) * jlimit(0.0, 2.0, performanceMacro * 2.0));
        const double phaseMorphAmount0 = ats::evaluateMorphLaneAmount(
            recipe, ats::MorphLane::Phase, morphAmount0);

        for (int ri = 0; ri < nRec; ++ri)
        {
            const auto& rec = records[(size_t) ri];
            ats::MorphPhaseInput input;
            input.kind = rec.kind;
            input.hasSourcePhase = sampleAnalysisPhase(soundA, rec.sourcePartialIndex, 0, 0, 0.0,
                                                       input.sourcePhaseRadians);
            input.hasTargetPhase = sampleAnalysisPhase(soundB, rec.targetPartialIndex, 0, 0, 0.0,
                                                       input.targetPhaseRadians);
            const auto resolved = ats::resolveMorphPhase(input, phaseMorphAmount0, recipe.phase);
            if (resolved.valid && !resolved.usesRendererContinuation)
            {
                ps[(size_t) ri].re = std::cos(resolved.phaseRadians);
                ps[(size_t) ri].im = std::sin(resolved.phaseRadians);
            }
        }
    }

    // Residual band noise: one phasor per (band × component) with fixed frequency and
    // time-varying amplitude driven by the morphed per-frame band_energy.
    constexpr int kResBands = ATSA_CRITICAL_BANDS;
    const bool hasResidualA = soundA->band_energy != nullptr;
    const bool hasResidualB = soundB->band_energy != nullptr;
    const bool doResidual   = hasResidualA || hasResidualB;

    // One carrier per band at the ATSA band center frequency, amplitude-modulated by
    // piecewise-linear band-limited noise (randi), matching do_synthesis exactly.
    // Effective output RMS per band = amp × RMS(sin) × RMS(randi) = amp/3, so
    // amp = morphed ENG_RMS (no extra scaling needed).
    struct ResState
    {
        double re = 1.0, im = 0.0, rotRe = 1.0, rotIm = 0.0; // carrier phasor
        double randiA1 = 0.0, randiA2 = 0.0;                  // randi interpolation endpoints
        int randiCnt = 0, randiSize = 1;                       // randi counter / period
        std::uint64_t randSeed = 1;                            // LCG state for next randi value
        double amp = 0.0, ampInc = 0.0;
        bool active = false;
        bool usable = false;
    };

    // Multiple carriers per critical band, each at a distinct (jittered) frequency inside the
    // band and driven by independent band-limited noise. A single band-center carrier collapses
    // each band's energy into a narrow tone, leaving spectral gaps that read as a hollow/phasey
    // texture on sustained residual (pads). Spreading carriers across the band fills it with
    // smoother broadband noise. Mirrors the recipe's residual component layout.
    constexpr int kResCarriers = 5;
    std::vector<ResState> rs;
    std::vector<int> bandUsable;
    if (doResidual)
    {
        rs.resize((size_t) (kResBands * kResCarriers));
        bandUsable.assign((size_t) kResBands, 0);
        for (int band = 0; band < kResBands; ++band)
        {
            const double lo        = ATSA_CRITICAL_BAND_EDGES[band];
            const double hi        = ATSA_CRITICAL_BAND_EDGES[band + 1];
            const double bandwidth = jmax(1.0, hi - lo);
            // Each carrier owns ~one sub-band; its modulator bandwidth matches so neighbouring
            // carriers overlap into continuous coverage rather than discrete tones.
            const double subBw = bandwidth / (double) kResCarriers;
            for (int c = 0; c < kResCarriers; ++c)
            {
                auto& r = rs[(size_t) (band * kResCarriers + c)];

                const double slot   = ((double) c + 0.5) / (double) kResCarriers;
                const double jitter = (morphTrajResidualUnitHash(band, c) - 0.5) / (double) kResCarriers;
                const double amount = jlimit(0.05, 0.95, slot + jitter);
                const double freqHz = lo + amount * (hi - lo);
                if (freqHz <= 0.0 || freqHz >= sampleRate * 0.5)
                    continue;

                const double omega = twoPi * freqHz * invSr;
                r.rotRe = std::cos(omega);
                r.rotIm = std::sin(omega);

                const double phaseStart = morphTrajResidualUnitHash(band, c + 101) * twoPi;
                r.re = std::cos(phaseStart);
                r.im = std::sin(phaseStart);

                r.randiSize = jmax(1, (int) std::round(sampleRate / jmax(1.0, subBw)));

                // Deterministic per-(band,carrier) seed for reproducible playback
                std::uint64_t seed = static_cast<std::uint64_t>(morphTrajResidualHash(band, c))
                                     * 2654435761ULL + 1234567891ULL;
                r.randSeed = (seed == 0) ? 1 : seed;
                r.randSeed  = morphTrajLcgNext(r.randSeed);
                r.randiA1   = morphTrajLcgDouble(r.randSeed) * 2.0 - 1.0;
                r.randSeed  = morphTrajLcgNext(r.randSeed);
                r.randiA2   = morphTrajLcgDouble(r.randSeed) * 2.0 - 1.0;
                r.randiCnt  = 0;
                r.usable    = true;
                ++bandUsable[(size_t) band];
            }
        }
    }

    // Ceiling on hybrid envelope-transfer boost. The de-color/re-color ratio (barkB/barkA)
    // grows without bound for partials sitting in a spectral valley of source A — and as a
    // source decays toward its end its high partials shrink, so barkA shrinks and the boost
    // runs away into a rising high-frequency whistle. +30 dB is generous for genuine timbre
    // transfer while bounding the noise-floor runaway.
    constexpr double kMaxEnvBoostLinear = 31.6227766; // +30 dB

    // De-color divisor floor, relative to each source-A envelope's own peak. Where source A
    // has no real spectral support (e.g. its high partials have died near the end of the
    // sound), barkA edge-holds a tiny value and barkB/barkA runs away into a high-frequency
    // whistle/chirp. Flooring envA at -48 dB below A's peak bounds the de-color gain to A's
    // intrinsic dynamic range and keeps it stable frame-to-frame, so unsupported high bands
    // are not synthesised out of the noise floor.
    constexpr double kEnvDecolorRelFloor = 0.00398107; // -48 dB

    // Active partials/carriers are fixed for the duration of a chunk (set in per-chunk setup).
    // Collect their indices once per chunk so the per-sample hot loop iterates only live
    // entries instead of branching on `active` for every dead partial on every sample.
    std::vector<int> activePartials;
    activePartials.reserve((size_t) nRec);
    std::vector<int> activeCarriers;
    activeCarriers.reserve(rs.size());

    // Per-frame (time-varying) correspondence. The static records pair A and B partials by their
    // lifetime-average position, so a partial that glides and crosses another between frames stays
    // wrongly paired for its whole life. Here the matched pairs within each critical band are
    // re-assigned every chunk to the partials that are actually closest in pitch at that instant.
    // Phase continuity is preserved because each oscillator stays anchored to its A partial (the
    // record's sourcePartialIndex); only its B partner — which the blend glides toward — is
    // re-chosen, and swaps happen at frequency crossings where the two B partners nearly coincide,
    // so freqB barely steps. Group the matched records by band once (record identity is static).
    std::vector<std::vector<int>> matchedBandGroups; // each: record indices sharing a band (size>=2)
    {
        std::vector<std::vector<int>> byBand;
        std::vector<int> bandOf; // parallel: which band each group represents
        for (int ri = 0; ri < nRec; ++ri)
        {
            const auto& rec = records[(size_t) ri];
            if (rec.kind != ats::MorphCorrespondenceKind::Matched)
                continue;
            int g = -1;
            for (size_t k = 0; k < bandOf.size(); ++k)
                if (bandOf[k] == rec.bandIndex) { g = (int) k; break; }
            if (g < 0) { bandOf.push_back(rec.bandIndex); byBand.emplace_back(); g = (int) byBand.size() - 1; }
            byBand[(size_t) g].push_back(ri);
        }
        for (auto& grp : byBand)
            if (grp.size() >= 2)
                matchedBandGroups.push_back(std::move(grp));
    }
    // assignedTargetPartial[ri] is the B partial the matched record currently blends toward; it
    // starts at the static pairing and is updated per chunk by the re-assignment below.
    std::vector<int> assignedTargetPartial((size_t) nRec, -1);
    for (int ri = 0; ri < nRec; ++ri)
        if (records[(size_t) ri].kind == ats::MorphCorrespondenceKind::Matched)
            assignedTargetPartial[(size_t) ri] = records[(size_t) ri].targetPartialIndex;
    // Reused scratch so the chunk loop does not allocate.
    std::vector<double> reSrcLog, reTgtLog;
    std::vector<int> rePrev, reTargets;
    constexpr double kRepairHysteresisLogHz = 0.02; // ~0.35 semitone stickiness against flicker

    // One-pole IIR trajectory smoother: alpha = 0 → instant (no filter).
    const double smoothingAlpha = smoothingMs > 0.0
        ? std::exp(-1.0 / (smoothingMs * 1.0e-3 * chunkRateHz))
        : 0.0;
    double smoothedMorphAmount = jlimit(0.0, 1.0,
        useTimeline ? timeline->sampleMorph(0.0, false) : fixedMorphAmount);
    double previousTransientProtection = 0.0;

    // Unmatched-partial policies (§ MorphUnmatchedPartialPolicies). The render's default
    // crossfade (resolveHybridizedPartialRender) already implements FadeOut for unmatched-source
    // and FadeIn for unmatched-target, so the default recipe is bit-identical. Hold/Discard/
    // MergeIntoResidual only diverge when the user selects a non-default policy.
    const ats::MorphUnmatchedPartialPolicy unmatchedSourcePolicy = recipe.unmatchedPartialPolicies.sourcePolicy;
    const ats::MorphUnmatchedPartialPolicy unmatchedTargetPolicy = recipe.unmatchedPartialPolicies.targetPolicy;
    const bool mergeUnmatchedIntoResidual =
        unmatchedSourcePolicy == ats::MorphUnmatchedPartialPolicy::MergeIntoResidual
        || unmatchedTargetPolicy == ats::MorphUnmatchedPartialPolicy::MergeIntoResidual;
    // Per-chunk power (sine RMS², i.e. amp²/2) rerouted from merged unmatched partials into each
    // critical band. Filled in the partial loop, consumed in the residual loop of the same chunk.
    std::array<double, ATSA_CRITICAL_BANDS> mergedBandPower{};

    for (int s = 0; s < totalSamples; )
    {
        const int chunkEnd = jmin(s + chunkSamples, totalSamples);
        const int chunkLen = jmax(1, chunkEnd - s);
        const double t = (double) s * invSr;
        const double timeNorm = totalSamples > 1 ? (double) s / (double) (totalSamples - 1) : 0.0;

        {
            const double rawMorphAmount = jlimit(0.0, 1.0,
                useTimeline ? timeline->sampleMorph(timeNorm, false) : fixedMorphAmount);
            smoothedMorphAmount = smoothingAlpha * smoothedMorphAmount
                                  + (1.0 - smoothingAlpha) * rawMorphAmount;
        }
        // Performance macro: scales the morph deviation from the midpoint (0 = frozen at 0.5,
        // 0.5 = passthrough, 1.0 = exaggerated). Useful for live expression control.
        const double morphAmount = jlimit(0.0, 1.0,
            0.5 + (smoothedMorphAmount - 0.5) * jlimit(0.0, 2.0, performanceMacro * 2.0));

        double envTransferAmount = jlimit(0.0, 1.0,
            useTimeline ? timeline->sampleEffect(ats::engine::TimelinePointKind::hybrid, timeNorm) : 0.0);

        // Envelope-driven amplitude morph ("true morph"): blend A/B Bark envelopes and
        // resample each partial's amplitude from the interpolated envelope.
        const double envMorphAmount = jlimit(0.0, 1.0,
            useTimeline ? timeline->sampleEffect(ats::engine::TimelinePointKind::envelopeMorph, timeNorm)
                        : 0.0);

        // Per-chunk timbre: evaluate plan if active, otherwise fall back to APVTS values (§7.1).
        const TimbreRenderParams timbre = compiledTimbrePlan.evaluate(
            timeNorm, apvtsBrightnessDb, apvtsTiltDbPerOct, apvtsNoiseBalance, timbreEndpoints);

        // Per-lane morph amounts: frequency, amplitude, residual-noise may be decoupled
        // from the main morph amount via recipe lane settings (linkedToMainMorph + curve).
        // Default: all lanes linked → same value as morphAmount (bit-identical to old behavior).
        const double freqMorphAmount = ats::evaluateMorphLaneAmount(
            recipe, ats::MorphLane::Frequency, morphAmount);
        double ampMorphAmount = ats::evaluateMorphLaneAmount(
            recipe, ats::MorphLane::Amplitude, morphAmount);
        const double residualNoiseMorphAmount = ats::evaluateMorphLaneAmount(
            recipe, ats::MorphLane::ResidualNoise, morphAmount);

        // Protect attacks from ordinary amplitude/envelope crossfading. The marker set follows
        // the currently dominant source; SourceA/SourceB choose the protected attack donor,
        // while SustainedOnlyAfterAttack preserves whichever source is dominant at this point.
        const auto& dominantMarkers = morphAmount < 0.5
            ? recipe.sourceATransients : recipe.sourceBTransients;
        const double protectionRamp = std::max(0.010, smallerHop);
        const double transientProtection = recipe.transient.policy == ats::MorphTransientPolicy::Crossfade
            ? 0.0
            : transientProtectionAmount(t, dominantMarkers,
                                        recipe.transient.protectionWindowSeconds, protectionRamp);
        double transientDonorAmount = morphAmount < 0.5 ? 0.0 : 1.0;
        if (recipe.transient.policy == ats::MorphTransientPolicy::SourceA)
            transientDonorAmount = 0.0;
        else if (recipe.transient.policy == ats::MorphTransientPolicy::SourceB)
            transientDonorAmount = 1.0;
        ampMorphAmount += transientProtection * (transientDonorAmount - ampMorphAmount);
        envTransferAmount += transientProtection * (transientDonorAmount - envTransferAmount);

        // Fractional frame positions at this chunk's start
        const double fpA = hopA > 0.0 ? t / hopA : 0.0;
        const int fA0   = jlimit(0, framesA - 1, (int) fpA);
        const int fA1   = jmin(fA0 + 1, framesA - 1);
        const double fracA = fpA - (double) fA0;

        const double tB = std::max(0.0, t + onsetOffsetB); // bounded onset-aligned time for B
        const double fpB = hopB > 0.0 ? tB / hopB : 0.0;
        const int fB0   = jlimit(0, framesB - 1, (int) fpB);
        const int fB1   = jmin(fB0 + 1, framesB - 1);
        const double fracB = fpB - (double) fB0;

        // End-of-source fade. When a source overruns its last frame, fA0==fA1 and its partials
        // would otherwise hold the last-frame value forever, so each source is faded to silence
        // past its end. The fade is a raised cosine (zero slope at both ends) over ~20 ms rather
        // than a single linear hop: a source of finite length is usually truncated while still
        // energetic, and a linear one-hop ramp leaves slope corners that splatter as a broadband
        // "pop" when the other source is still sounding (any morph < 1). The smooth, longer
        // window removes the corners. Applied symmetrically to A and B.
        constexpr double kEndFadeSeconds = 0.020;
        auto endOfSourceFade = [&](double fp, int frames, double hop) -> double
        {
            const double overSec = (fp - (double) (frames - 1)) * hop; // seconds past last frame
            if (overSec <= 0.0)
                return 1.0;
            const double dur = std::max(hop, kEndFadeSeconds);
            if (overSec >= dur)
                return 0.0;
            return 0.5 * (1.0 + std::cos(kPi * (overSec / dur)));
        };
        const double fadeA = endOfSourceFade(fpA, framesA, hopA > 0.0 ? hopA : invSr);
        const double fadeB = endOfSourceFade(fpB, framesB, hopB > 0.0 ? hopB : invSr);

        // Scale transfer amount by fadeA: as source A ends, the spectral gain exponent
        // approaches 0 so boosted partials fade naturally instead of cutting abruptly.
        const double effectiveEnvTransfer = envTransferAmount * fadeA;

        // Build Bark envelopes once per chunk — shared by Hybrid envelope transfer and the
        // EnvelopeMorph lane (both read A's and B's spectral envelope at partial frequencies).
        ats::BarkSpectralEnvelope barkA, barkB;
        if (effectiveEnvTransfer > 1.0e-9 || envMorphAmount > 1.0e-9)
        {
            if (useTimeline && envCache != nullptr)
            {
                barkA = buildBarkEnvelopeTimelineCached(envCache, true, soundA, fA0, fA1, fracA,
                                                        sourcesRevision);
                barkB = buildBarkEnvelopeTimelineCached(envCache, false, soundB, fB0, fB1, fracB,
                                                        sourcesRevision);
            }
            else
            {
                barkA = buildBarkEnvelopeCached(envCache, true, soundA, fA0, fA1, fracA, sourcesRevision);
                barkB = buildBarkEnvelopeCached(envCache, false, soundB, fB0, fB1, fracB, sourcesRevision);
            }
        }

        // For the EnvelopeMorph lane, align the two envelopes' formants and warp the Bark axis
        // so corresponding formants glide together before the log-amplitude blend (Caetano/Rodet).
        // Built once per chunk and read per partial below.
        ats::FormantAlignedEnvelopeMorph morphedEnv;
        if (envMorphAmount > 1.0e-9 && barkA.isValid() && barkB.isValid())
            morphedEnv.build(barkA, barkB, kEnvDecolorRelFloor);

        // Per-frame correspondence: re-pair matched partials within each band by their
        // instantaneous log-frequency at this chunk (see the precompute comment above).
        {
            auto instLogFreq = [](const ATS_SOUND* snd, int p, int f0, int f1, double frac) -> double
            {
                if (p < 0 || snd->frq == nullptr || p >= snd->partials || snd->frq[p] == nullptr)
                    return std::numeric_limits<double>::quiet_NaN();
                const double a = snd->frq[p][f0], b = snd->frq[p][f1];
                if (!std::isfinite(a) || !std::isfinite(b) || a <= 0.0 || b <= 0.0)
                    return std::numeric_limits<double>::quiet_NaN();
                return std::log(a) + (std::log(b) - std::log(a)) * frac;
            };
            for (const auto& group : matchedBandGroups)
            {
                const size_t k = group.size();
                reSrcLog.assign(k, 0.0);
                reTgtLog.assign(k, 0.0);
                reTargets.assign(k, -1);
                rePrev.assign(k, 0);
                for (size_t i = 0; i < k; ++i)
                {
                    const auto& rec = records[(size_t) group[i]];
                    reSrcLog[i] = instLogFreq(soundA, rec.sourcePartialIndex, fA0, fA1, fracA);
                    reTgtLog[i] = instLogFreq(soundB, rec.targetPartialIndex, fB0, fB1, fracB);
                    reTargets[i] = rec.targetPartialIndex; // static B-partial set for this band
                }
                // Map the current assignment to slice indices for hysteresis.
                for (size_t i = 0; i < k; ++i)
                {
                    const int cur = assignedTargetPartial[(size_t) group[i]];
                    int slice = (int) i;
                    for (size_t j = 0; j < k; ++j)
                        if (reTargets[j] == cur) { slice = (int) j; break; }
                    rePrev[i] = slice;
                }
                const auto newAssign = ats::reassignCorrespondenceByLogFrequency(
                    reSrcLog, reTgtLog, rePrev, kRepairHysteresisLogHz);
                for (size_t i = 0; i < k; ++i)
                    assignedTargetPartial[(size_t) group[i]] = reTargets[(size_t) newAssign[i]];
            }
        }

        // As a protected attack fades in, pull each oscillator toward the dominant source's
        // measured phase on the unit circle. Only the rising edge corrects phase; sustain and the
        // release edge remain free-running, avoiding repeated phase resets or pitch modulation.
        if (transientProtection > previousTransientProtection + 1.0e-9)
        {
            const double correction = jlimit(0.0, 1.0,
                (transientProtection - previousTransientProtection)
                    / std::max(1.0e-9, 1.0 - previousTransientProtection));
            const bool donorIsB = morphAmount >= 0.5;
            for (int ri = 0; ri < nRec; ++ri)
            {
                const auto& rec = records[(size_t) ri];
                const int partial = donorIsB
                    ? (rec.kind == ats::MorphCorrespondenceKind::Matched
                           ? assignedTargetPartial[(size_t) ri] : rec.targetPartialIndex)
                    : rec.sourcePartialIndex;
                double donorPhase = 0.0;
                const bool hasDonor = donorIsB
                    ? sampleAnalysisPhase(soundB, partial, fB0, fB1, fracB, donorPhase)
                    : sampleAnalysisPhase(soundA, partial, fA0, fA1, fracA, donorPhase);
                if (!hasDonor)
                    continue;
                auto& state = ps[(size_t) ri];
                const double x = (1.0 - correction) * state.re + correction * std::cos(donorPhase);
                const double y = (1.0 - correction) * state.im + correction * std::sin(donorPhase);
                const double magnitude = std::sqrt(x * x + y * y);
                if (magnitude > 1.0e-9)
                {
                    state.re = x / magnitude;
                    state.im = y / magnitude;
                }
            }
        }

        if (mergeUnmatchedIntoResidual)
            mergedBandPower.fill(0.0);

        // Per-chunk setup: compute freq/amp and the phasor rotation (cos/sin once per chunk).
        for (int ri = 0; ri < nRec; ++ri)
        {
            const auto& rec = records[(size_t) ri];
            double freqA = 0.0, ampA = 0.0, freqB = 0.0, ampB = 0.0;

            if (rec.kind != ats::MorphCorrespondenceKind::UnmatchedTarget)
            {
                const int pA = rec.sourcePartialIndex;
                if (pA >= 0 && pA < soundA->partials
                    && soundA->frq != nullptr && soundA->frq[pA] != nullptr
                    && soundA->amp != nullptr && soundA->amp[pA] != nullptr)
                {
                    const double fq0 = soundA->frq[pA][fA0], fq1 = soundA->frq[pA][fA1];
                    const double am0 = soundA->amp[pA][fA0], am1 = soundA->amp[pA][fA1];
                    // Log-frequency interp between adjacent analysis frames: a gliding partial's
                    // perceptual pitch is the geometric mean, so a linear-Hz step combs slightly
                    // sharp at the hop midpoint. Amplitude stays linear (ATS/MQ standard for a
                    // single partial's own envelope between frames).
                    if (std::isfinite(fq0) && std::isfinite(fq1))
                        freqA = (fq0 > 0.0 && fq1 > 0.0)
                                    ? std::exp(std::log(fq0) + (std::log(fq1) - std::log(fq0)) * fracA)
                                    : jmax(0.0, fq0 + (fq1 - fq0) * fracA);
                    if (std::isfinite(am0) && std::isfinite(am1))
                        ampA  = jmax(0.0, am0 + (am1 - am0) * fracA) * fadeA;
                }
            }

            if (rec.kind != ats::MorphCorrespondenceKind::UnmatchedSource)
            {
                const int pB = rec.kind == ats::MorphCorrespondenceKind::Matched
                                   ? assignedTargetPartial[(size_t) ri]
                                   : rec.targetPartialIndex;
                if (pB >= 0 && pB < soundB->partials
                    && soundB->frq != nullptr && soundB->frq[pB] != nullptr
                    && soundB->amp != nullptr && soundB->amp[pB] != nullptr)
                {
                    const double fq0 = soundB->frq[pB][fB0], fq1 = soundB->frq[pB][fB1];
                    const double am0 = soundB->amp[pB][fB0], am1 = soundB->amp[pB][fB1];
                    if (std::isfinite(fq0) && std::isfinite(fq1))
                        freqB = (fq0 > 0.0 && fq1 > 0.0)
                                    ? std::exp(std::log(fq0) + (std::log(fq1) - std::log(fq0)) * fracB)
                                    : jmax(0.0, fq0 + (fq1 - fq0) * fracB);
                    if (std::isfinite(am0) && std::isfinite(am1))
                        ampB  = jmax(0.0, am0 + (am1 - am0) * fracB) * fadeB;
                }
            }

            // Hybrid envelope transfer: de-color A by its envelope, then color it
            // by B's envelope at A's partial frequency.
            if (effectiveEnvTransfer > 1.0e-9 && freqA > 0.0 && ampA > 0.0
                && barkA.isValid() && barkB.isValid())
            {
                const double relFloorA = barkA.getPeakAmplitude() * kEnvDecolorRelFloor;
                const double envA = std::max(relFloorA, barkA.sampleAtHz(freqA));
                const double envB = barkB.sampleAtHz(freqA);
                const auto transfer = ats::applySpectralEnvelopeTransfer(ampA,
                                                                         envA,
                                                                         envB,
                                                                         effectiveEnvTransfer);
                if (transfer.ok)
                    ampA = ats::softCapEnvelopeBoost(transfer.outputAmplitude, ampA,
                                                     kMaxEnvBoostLinear);
            }

            // Unmatched partials cross-fade in place at their own frequency (no glide toward a
            // nearest neighbour): with dissimilar sources that turned the blend into a mass of
            // partials sliding to arbitrary targets — audible downward sweeps and tones present
            // in neither source. The midpoint stays a clean union of the two spectra.
            const double nearestTargetHz = 0.0;
            const double nearestSourceHz = 0.0;
            const auto resolved = ats::resolveHybridizedPartialRender(toHybridizedPartialKind(rec.kind),
                                                                      freqA,
                                                                      ampA,
                                                                      freqB,
                                                                      ampB,
                                                                      freqMorphAmount,
                                                                      ampMorphAmount,
                                                                      nearestTargetHz,
                                                                      nearestSourceHz,
                                                                      interpDomains);
            const double freq = resolved.frequencyHz;
            double amp = resolved.amplitude;

            // Unmatched-partial policy override. resolved() already implements the default
            // FadeOut (source) / FadeIn (target) crossfade, so those branches are no-ops and the
            // default recipe stays bit-identical. Hold keeps the partial at its own amplitude
            // (no fade); Discard silences it; MergeIntoResidual silences the partial but reroutes
            // its power into the nearest critical band (consumed by the residual pass below).
            if (rec.kind != ats::MorphCorrespondenceKind::Matched)
            {
                const ats::MorphUnmatchedPartialPolicy policy =
                    rec.kind == ats::MorphCorrespondenceKind::UnmatchedSource
                        ? unmatchedSourcePolicy : unmatchedTargetPolicy;
                const double availableAmp =
                    rec.kind == ats::MorphCorrespondenceKind::UnmatchedSource ? ampA : ampB;
                switch (policy)
                {
                    case ats::MorphUnmatchedPartialPolicy::FadeOut:
                    case ats::MorphUnmatchedPartialPolicy::FadeIn:
                        break; // default crossfade already applied by resolved()
                    case ats::MorphUnmatchedPartialPolicy::Hold:
                        amp = availableAmp; // hold at full amplitude, no fade
                        break;
                    case ats::MorphUnmatchedPartialPolicy::Discard:
                        amp = 0.0;
                        break;
                    case ats::MorphUnmatchedPartialPolicy::MergeIntoResidual:
                        if (freq > 0.0 && availableAmp > 0.0)
                            mergedBandPower[(size_t) nearestAtsBandIndex(freq)] += 0.5 * availableAmp * availableAmp;
                        amp = 0.0;
                        break;
                }
            }

            // Envelope-driven amplitude morph: interpolate A's and B's Bark envelopes by the
            // morph position and resample this partial's amplitude from the blended envelope.
            // This decouples the formant/timbre shape from individual partial amplitudes, so
            // imperfect A↔B correspondence no longer combs the spectrum (Caetano/Rodet). The
            // partial's own presence is preserved: a partial that is already silent stays
            // silent (no resurrection), and the result is bounded by the sources' envelopes.
            if (envMorphAmount > 1.0e-9 && freq > 0.0 && amp > 1.0e-7 && morphedEnv.isValid())
            {
                // Formant-aligned blend: the envelopes' formant peaks are matched and the Bark
                // axis is warped so a formant that moves from A to B glides as a single peak
                // rather than double-imaging or dipping at the midpoint. Reduces to the plain
                // fixed-frequency log blend when no formants match (identity warp).
                const double blendedEnv = morphedEnv.sampleAtHz(freq, ampMorphAmount);
                // Crossfade the independent per-partial amplitude toward its position on the
                // blended envelope. Geometric (dB-domain) interpolation when both endpoints are
                // audible: loudness is logarithmic, so a linear-amplitude crossfade biases toward
                // the louder endpoint and dips perceived level mid-sweep. The geometric blend is
                // still bounded by [min,max] of the two values. Falls back to linear when the
                // envelope sample is at/below the floor so the result stays zero-safe.
                constexpr double kEnvMorphGeomFloor = 1.0e-7;
                if (blendedEnv > kEnvMorphGeomFloor)
                    amp = std::exp((1.0 - envMorphAmount) * std::log(amp)
                                   + envMorphAmount * std::log(blendedEnv));
                else
                    amp += envMorphAmount * (blendedEnv - amp);
            }

            // Timbre transforms — resolution order §8.6: tonal/noise → tilt → brightness
            if (freq > 0.0 && amp > 0.0)
            {
                constexpr double kTimbrePivotHz = 1000.0;
                if (std::abs(timbre.noiseBalance - 0.5) > 0.001)
                    amp *= std::sqrt(2.0 * std::max(0.0, 1.0 - timbre.noiseBalance));
                if (std::abs(timbre.tiltDbPerOct) > 0.001)
                {
                    const double oct = std::log2(freq / kTimbrePivotHz);
                    amp *= std::pow(10.0, timbre.tiltDbPerOct * oct / 20.0);
                }
                if (std::abs(timbre.brightnessDb) > 0.001)
                {
                    const double oct = std::log2(freq / kTimbrePivotHz);
                    amp *= std::pow(10.0, timbre.brightnessDb * std::tanh(oct / 3.0) / 20.0);
                }
                amp = std::max(0.0, amp);
            }

            auto& p = ps[(size_t) ri];
            double targetAmp = 0.0;
            if (freq > 0.0 && amp > 0.0)
            {
                const double omega = twoPi * freq * invSr;
                p.rotRe = std::cos(omega); // only trig call — once per chunk per partial
                p.rotIm = std::sin(omega);
                targetAmp = amp;
            }
            // When targetAmp == 0 (partial dying) the previous rotor is kept so the
            // ramp-to-zero tail holds its last frequency — no pitch/phase discontinuity.

            // Linearly ramp amplitude across the chunk toward the target. This turns every
            // inter-chunk amplitude step — including the end-of-source fade to zero, which
            // otherwise cliffs in a single sample at the chunk boundary — into a click-free
            // glide. A partial stays active while it still has amplitude to ramp down.
            p.ampInc  = (targetAmp - p.amp) / (double) chunkLen;
            p.active  = (p.amp > 1.0e-7) || (targetAmp > 1.0e-7);
        }

        // Per-chunk residual setup: compute ENG_RMS per source via amplitude-space interpolation
        // (matching do_synthesis LIN3_INTERP-in-amplitude behavior).  The /3 level factor is
        // implicit: carrier × randi gives output RMS = ENG_RMS × (1/√2) × (√2/3) = ENG_RMS/3.
        if (doResidual)
        {
            const double wsA = hasResidualA ? (double) jmax(1, soundA->window_size) : 1.0;
            const double wsB = hasResidualB ? (double) jmax(1, soundB->window_size) : 1.0;
            constexpr double kNoiseVar = (double) ATSA_NOISE_VARIANCE;

            for (int band = 0; band < kResBands; ++band)
            {
                double rmsA = 0.0;
                if (hasResidualA && soundA->band_energy[band] != nullptr)
                {
                    const double e0 = soundA->band_energy[band][fA0];
                    const double e1 = soundA->band_energy[band][fA1];
                    if (std::isfinite(e0) && std::isfinite(e1))
                    {
                        // Interpolate in amplitude space (not energy space) to match do_synthesis
                        const double r0 = std::sqrt(jmax(0.0, e0) / (wsA * kNoiseVar));
                        const double r1 = std::sqrt(jmax(0.0, e1) / (wsA * kNoiseVar));
                        rmsA = (r0 + (r1 - r0) * fracA) * fadeA;
                    }
                }
                double rmsB = 0.0;
                if (hasResidualB && soundB->band_energy[band] != nullptr)
                {
                    const double e0 = soundB->band_energy[band][fB0];
                    const double e1 = soundB->band_energy[band][fB1];
                    if (std::isfinite(e0) && std::isfinite(e1))
                    {
                        const double r0 = std::sqrt(jmax(0.0, e0) / (wsB * kNoiseVar));
                        const double r1 = std::sqrt(jmax(0.0, e1) / (wsB * kNoiseVar));
                        rmsB = (r0 + (r1 - r0) * fracB) * fadeB;
                    }
                }
                // Apply Bark envelope transfer to residual noise — same Hybrid lane
                // that colors the deterministic partials, now extended to stochastic bands.
                static constexpr double kAtsBandCenterHz[ATSA_CRITICAL_BANDS] = {
                    50.0, 150.0, 250.0, 350.0, 455.0, 570.0, 700.0, 845.0, 1000.0,
                    1175.0, 1375.0, 1600.0, 1860.0, 2160.0, 2510.0, 2925.0, 3425.0,
                    4050.0, 4850.0, 5850.0, 7050.0, 8600.0, 10750.0, 13750.0, 17750.0
                };
                if (effectiveEnvTransfer > 1.0e-9 && rmsA > 0.0
                    && barkA.isValid() && barkB.isValid())
                {
                    const double centerHz = kAtsBandCenterHz[band];
                    const double relFloorA = barkA.getPeakAmplitude() * kEnvDecolorRelFloor;
                    const double envA = std::max(relFloorA, barkA.sampleAtHz(centerHz));
                    const double envB = barkB.sampleAtHz(centerHz);
                    const auto xfr = ats::applySpectralEnvelopeTransfer(rmsA, envA, envB,
                                                                        effectiveEnvTransfer);
                    if (xfr.ok)
                        rmsA = ats::softCapEnvelopeBoost(xfr.outputAmplitude, rmsA,
                                                         kMaxEnvBoostLinear);
                }
                // Equal-power (constant-energy) blend: the two bands are uncorrelated noise,
                // so their powers add. Interpolating RMS linearly dips ~3 dB at the midpoint
                // (the classic equal-gain crossfade hole); blending in the power domain keeps
                // the perceived loudness constant across the morph.
                double morphedRms = std::sqrt(jmax(0.0,
                    (1.0 - residualNoiseMorphAmount) * rmsA * rmsA
                    + residualNoiseMorphAmount * rmsB * rmsB));
                // Add any power rerouted from unmatched partials under the MergeIntoResidual
                // policy. Powers (RMS²) add for uncorrelated sources, so combine in the power
                // domain. Zero unless a non-default merge policy is active.
                if (mergeUnmatchedIntoResidual && mergedBandPower[(size_t) band] > 0.0)
                    morphedRms = std::sqrt(morphedRms * morphedRms + mergedBandPower[(size_t) band]);
                // Timbre transforms on residual — tonal/noise → tilt → brightness (§8.6)
                if (morphedRms > 0.0)
                {
                    constexpr double kTimbrePivotHz = 1000.0;
                    const double centerHz = kAtsBandCenterHz[band];
                    if (std::abs(timbre.noiseBalance - 0.5) > 0.001)
                        morphedRms *= std::sqrt(2.0 * timbre.noiseBalance);
                    if (std::abs(timbre.tiltDbPerOct) > 0.001)
                    {
                        const double oct = std::log2(centerHz / kTimbrePivotHz);
                        morphedRms *= std::pow(10.0, timbre.tiltDbPerOct * oct / 20.0);
                    }
                    if (std::abs(timbre.brightnessDb) > 0.001)
                    {
                        const double oct = std::log2(centerHz / kTimbrePivotHz);
                        morphedRms *= std::pow(10.0, timbre.brightnessDb * std::tanh(oct / 3.0) / 20.0);
                    }
                    morphedRms = std::max(0.0, morphedRms);
                }
                // Split the band's energy across its carriers: with N uncorrelated carriers the
                // powers add, so amp/sqrt(N) per carrier preserves the band's total RMS.
                const int nUsable = bandUsable[(size_t) band];
                const double carrierTarget = nUsable > 0
                    ? morphedRms / std::sqrt((double) nUsable) : 0.0;
                for (int c = 0; c < kResCarriers; ++c)
                {
                    auto& rState = rs[(size_t) (band * kResCarriers + c)];
                    if (!rState.usable) { rState.active = false; continue; }
                    rState.ampInc = (carrierTarget - rState.amp) / (double) chunkLen;
                    rState.active = (rState.amp > 1.0e-9) || (carrierTarget > 1.0e-9);
                }
            }
        }

        // Collect this chunk's live entries so the per-sample loop skips dead partials/carriers
        // without a per-iteration branch (active state is constant across the chunk).
        activePartials.clear();
        for (int ri = 0; ri < nRec; ++ri)
            if (ps[(size_t) ri].active)
                activePartials.push_back(ri);

        activeCarriers.clear();
        if (doResidual)
            for (int idx = 0; idx < (int) rs.size(); ++idx)
                if (rs[(size_t) idx].active)
                    activeCarriers.push_back(idx);

        // Hot loop (R-001): loop interchange — per partial/carrier, run the whole chunk so
        // phasor state stays register-resident; accumulate into the output buffers.
        float* const outL = out.partialSamples.data();
        float* const outR = out.residualSamples.data();
        const int nActiveP = (int) activePartials.size();
        const int nActiveR = (int) activeCarriers.size();
        const int* const activeP = activePartials.data();
        const int* const activeR = activeCarriers.data();
        if (chunkLen > 0)
        {
            for (int si = s; si < chunkEnd; ++si)
            {
                outL[si] = 0.0f;
                if (doResidual)
                    outR[si] = 0.0f;
            }

            for (int ai = 0; ai < nActiveP; ++ai)
            {
                auto& p = ps[(size_t) activeP[ai]];
                double re = p.re;
                double im = p.im;
                double amp = p.amp;
                const double rotRe = p.rotRe;
                const double rotIm = p.rotIm;
                const double ampInc = p.ampInc;
                for (int si = 0; si < chunkLen; ++si)
                {
                    const double newRe = re * rotRe - im * rotIm;
                    const double newIm = re * rotIm + im * rotRe;
                    re = newRe;
                    im = newIm;
                    outL[s + si] += (float) (newIm * amp);
                    amp += ampInc;
                    if (amp < 0.0)
                        amp = 0.0;
                }
                p.re = re;
                p.im = im;
                p.amp = amp;
            }

            if (doResidual)
            {
                for (int ai = 0; ai < nActiveR; ++ai)
                {
                    auto& r = rs[(size_t) activeR[ai]];
                    double re = r.re;
                    double im = r.im;
                    double amp = r.amp;
                    const double rotRe = r.rotRe;
                    const double rotIm = r.rotIm;
                    const double ampInc = r.ampInc;
                    uint32_t randSeed = r.randSeed;
                    double randiA1 = r.randiA1;
                    double randiA2 = r.randiA2;
                    int randiCnt = r.randiCnt;
                    const int randiSize = r.randiSize;
                    for (int si = 0; si < chunkLen; ++si)
                    {
                        const double newRe = re * rotRe - im * rotIm;
                        const double newIm = re * rotIm + im * rotRe;
                        re = newRe;
                        im = newIm;

                        if (randiCnt >= randiSize)
                        {
                            randiA1 = randiA2;
                            randSeed = morphTrajLcgNext(randSeed);
                            randiA2 = morphTrajLcgDouble(randSeed) * 2.0 - 1.0;
                            randiCnt = 0;
                        }
                        const double noiseVal = randiA1
                                                + (randiA2 - randiA1)
                                                    * ((double) randiCnt / (double) randiSize);
                        ++randiCnt;

                        outR[s + si] += (float) (newIm * noiseVal * amp);
                        amp += ampInc;
                        if (amp < 0.0)
                            amp = 0.0;
                    }
                    r.re = re;
                    r.im = im;
                    r.amp = amp;
                    r.randSeed = randSeed;
                    r.randiA1 = randiA1;
                    r.randiA2 = randiA2;
                    r.randiCnt = randiCnt;
                }
            }
        }

        previousTransientProtection = transientProtection;
        s = chunkEnd;
    }

    return out;
}

} // namespace

MorphTrajectoryBuffer renderMorphTrajectory(const ATS_SOUND* soundA,
                                             const ATS_SOUND* soundB,
                                             const ats::MorphRecipe& recipe,
                                             const MorphTrajectorySettings& settings)
{
    return renderMorphTrajectoryImpl(soundA, soundB, recipe, settings);
}

} // namespace ats::dsp
