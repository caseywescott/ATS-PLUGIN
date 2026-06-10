#pragma once

#include <cmath>
#include <string>
#include <vector>

namespace ats
{

// Per-chunk timbre parameters applied inside renderMorphTrajectoryBuffer.
// Moved here from PluginProcessor.cpp so TimbreMotionPlan can return it.
struct TimbreRenderParams
{
    double brightnessDb = 0.0; // tanh-bounded centroid shift (§8.1)
    double tiltDbPerOct = 0.0; // spectral slope dB/octave around 1 kHz (§8.2)
    double noiseBalance = 0.5; // 0=full tonal, 1=full noise; 0.5=neutral (§8.3)
};

// ─── Plan schema types ────────────────────────────────────────────────────────

enum class TimbreTargetMode
{
    FollowNatural,    // curve ignored; natural value passes through
    InterpolateAB,    // lerp(d_A, d_B, c(t)); v1: maps c(t) to full physical range
    OffsetNatural,    // d_natural(t) + depth * bipolar(c(t)); v1: offsets APVTS value
    AbsoluteTarget    // lerp(physMin, physMax, c(t)); expert mode
};

// Breakpoint on a timbre motion curve — same semantics as morph timeline breakpoints.
struct TimbreMotionPoint
{
    double timeNorm  = 0.0; // [0, 1]
    double valueNorm = 0.0; // [0, 1]
    int    curve     = 0;   // 0=linear, 1=ease_in, 2=ease_out, 3=ease_in_out
};

struct TimbreTargetCurve
{
    std::string     descriptorId;
    TimbreTargetMode mode           = TimbreTargetMode::FollowNatural;
    bool            enabled         = true;
    bool            endpointLocked  = true;
    int             priority        = 10;
    double          depth           = 1.0;
    double          smoothingMs     = 40.0;
    std::vector<TimbreMotionPoint> points;
};

struct TimbreSharedCurve
{
    std::string curveId;
    std::vector<TimbreMotionPoint> points;
};

struct TimbreMacroRoute
{
    std::string targetDescriptorId;
    double      depth        = 1.0;
    bool        bipolar      = false;
    double      rangeMinNorm = 0.0;
    double      rangeMaxNorm = 1.0;
};

struct TimbreMacro
{
    std::string macroId;
    bool        enabled      = true;
    double      amount       = 1.0;
    std::string sharedCurveId; // empty if no shared curve
    std::vector<TimbreMacroRoute> routes;
};

struct TimbreMotionPlan
{
    int         schemaVersion      = 1;
    std::string engineVersion      = "timbre-motion-v1";
    std::vector<TimbreTargetCurve>  targetCurves;
    std::vector<TimbreMacro>        macros;
    std::vector<TimbreSharedCurve>  sharedCurves;
    std::vector<std::string>        pinnedDescriptorIds;
    std::string                     focusedDescriptorId;

    [[nodiscard]] bool empty() const { return targetCurves.empty() && macros.empty(); }
};

// ─── JSON (de)serialization ───────────────────────────────────────────────────

// Parse the JSON blob stored in the recipe's `timbre_motion_plan` unknown field.
// Returns a default empty plan on any parse error — old sessions render unchanged (INV-1).
[[nodiscard]] TimbreMotionPlan deserializeTimbreMotionPlan(const std::string& json);

// Serialize to JSON for storage in recipe unknown fields.
[[nodiscard]] std::string serializeTimbreMotionPlan(const TimbreMotionPlan& plan);

// ─── Curve sampling ───────────────────────────────────────────────────────────

// Sample a piecewise curve at normalized time, applying curve-shape easing.
// Returns 0.0 for empty point arrays; clamps to endpoint values outside [0,1].
[[nodiscard]] double sampleTimbreCurve(const std::vector<TimbreMotionPoint>& points,
                                       double timeNorm) noexcept;

// ─── Spectral centroid model + brightness solver (§8.1) ───────────────────────

// Hz → Bark (Traunmüller 1990). Monotonic; used as the perceptual "centroid axis".
[[nodiscard]] double hzToBark(double hz) noexcept;

// Energy-weighted spectral centroid on the Bark axis for a partial spectrum.
// freqsHz and amps are parallel; entries with non-finite/non-positive values are skipped.
// Returns 0.0 when there is no usable energy.
[[nodiscard]] double spectralCentroidBark(const std::vector<double>& freqsHz,
                                          const std::vector<double>& amps) noexcept;

// Multiplicative brightness gain (linear) at freqHz for a tanh-bounded dB tilt about pivotHz.
// Mirrors the render post-pass exactly: gainDb = brightnessDb * tanh(log2(f/pivot) / 3).
// Positive brightnessDb boosts highs and cuts lows, raising the centroid; negative lowers it.
[[nodiscard]] double brightnessGainLinear(double freqHz, double brightnessDb,
                                          double pivotHz = 1000.0) noexcept;

// Bisection solve (§8.1): find brightnessDb in [minDb, maxDb] so the centroid of the
// brightness-weighted spectrum is as close as possible to targetBark. The centroid is monotonic
// in brightnessDb, so bisection converges; the result clamps to the range when targetBark is
// unreachable. Returns 0.0 when the spectrum has no usable energy.
[[nodiscard]] double solveBrightnessDbForCentroid(const std::vector<double>& freqsHz,
                                                  const std::vector<double>& amps,
                                                  double targetBark,
                                                  double minDb = -12.0,
                                                  double maxDb = 12.0,
                                                  double pivotHz = 1000.0,
                                                  int iterations = 48) noexcept;

// ─── Measured descriptor endpoints (Stage 1/2 context) ───────────────────────

// Measured descriptor values on source A and B, used by InterpolateAB / AbsoluteTarget modes so
// the curve interpolates between the two sounds' real descriptors (spec §5) instead of the full
// parameter range. Any field left unset falls back to the v1 full-range heuristic for that
// descriptor, so an empty context reproduces the analyzer-free behavior exactly (INV-1).
struct TimbreEndpoints
{
    bool   hasCentroid   = false; double centroidBarkA = 0.0, centroidBarkB = 0.0;
    bool   hasTilt       = false; double tiltDbPerOctA  = 0.0, tiltDbPerOctB  = 0.0;
    bool   hasNoise      = false; double noiseBalanceA  = 0.0, noiseBalanceB  = 0.0;

    // Optional reference spectrum for the centroid brightness solver (§8.1). When provided, a
    // centroid InterpolateAB/AbsoluteTarget curve solves for the brightnessDb that places this
    // spectrum's centroid at the requested Bark value; otherwise the requested centroid maps
    // through the bounded heuristic gain.
    const std::vector<double>* refFreqsHz = nullptr;
    const std::vector<double>* refAmps    = nullptr;
};

// ─── Compiled plan (stateful per-render) ─────────────────────────────────────

// Pre-compiled plan ready for per-chunk evaluation in a single render pass.
// Maintains per-target one-pole smoothing state across chunks.
// chunkRateHz = sampleRate / chunkSamples (the rate at which evaluate() is called).
class CompiledTimbrePlan
{
public:
    CompiledTimbrePlan() = default;
    CompiledTimbrePlan(const TimbreMotionPlan& plan, double chunkRateHz);

    // Evaluate at normalized time, returning per-chunk TimbreRenderParams.
    // APVTS values are the fallback when no plan curve targets a descriptor.
    // Must be called in monotonically non-decreasing timeNorm order within a render.
    [[nodiscard]] TimbreRenderParams evaluate(double timeNorm,
                                              double apvtsBrightnessDb,
                                              double apvtsTiltDbPerOct,
                                              double apvtsNoiseBalance);

    // Endpoint-aware evaluation: InterpolateAB / AbsoluteTarget modes interpolate the measured
    // source A/B descriptors in `endpoints` when present. Falls back to the analyzer-free
    // heuristic per descriptor when the corresponding endpoint is unset.
    [[nodiscard]] TimbreRenderParams evaluate(double timeNorm,
                                              double apvtsBrightnessDb,
                                              double apvtsTiltDbPerOct,
                                              double apvtsNoiseBalance,
                                              const TimbreEndpoints& endpoints);

    // True when any enabled curve (or macro route) drives combined.centroid_axis in a mode that
    // benefits from a measured reference spectrum (InterpolateAB / AbsoluteTarget). Lets the
    // render skip building source centroids/spectra when they are not needed.
    [[nodiscard]] bool needsCentroidEndpoints() const;

    [[nodiscard]] bool empty() const { return plan_.empty(); }

private:
    static double applyCurveShape(double t, int curveKind) noexcept;

    // Find the points array for a shared curve by ID; returns nullptr if not found.
    const std::vector<TimbreMotionPoint>* findSharedCurvePoints(const std::string& id) const;

    TimbreMotionPlan       plan_;
    std::vector<double>    smoothAlpha_; // one per targetCurves entry
    std::vector<double>    smoothState_; // current smoothed normalized value, one per target
    bool                   initialized_ = false;
};

} // namespace ats
