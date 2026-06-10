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
