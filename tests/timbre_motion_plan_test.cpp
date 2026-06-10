#include "TimbreMotionPlan.h"

#include <cmath>
#include <iostream>
#include <string>

namespace
{

int fail(const std::string& message)
{
    std::cerr << "[FAIL] " << message << '\n';
    return 1;
}

bool near(double a, double b, double tol = 1.0e-6)
{
    return std::abs(a - b) <= tol * std::max(1.0, std::max(std::abs(a), std::abs(b)));
}

// ─── INV-1: empty plan → APVTS fallback ───────────────────────────────────────

int testEmptyPlanFallback()
{
    ats::TimbreMotionPlan plan; // empty
    ats::CompiledTimbrePlan compiled(plan, 100.0);

    const ats::TimbreRenderParams r = compiled.evaluate(0.5, 6.0, -3.0, 0.7);
    if (!near(r.brightnessDb, 6.0))
        return fail("empty plan should fall back to APVTS brightnessDb");
    if (!near(r.tiltDbPerOct, -3.0))
        return fail("empty plan should fall back to APVTS tiltDbPerOct");
    if (!near(r.noiseBalance, 0.7))
        return fail("empty plan should fall back to APVTS noiseBalance");
    return 0;
}

// ─── M0.3: constant curve matches current global slider behavior ──────────────
// A plan with all targets disabled produces identical output to the empty plan.

int testDisabledCurveFallback()
{
    ats::TimbreMotionPlan plan;
    ats::TimbreTargetCurve tc;
    tc.descriptorId = "combined.centroid_axis";
    tc.mode         = ats::TimbreTargetMode::InterpolateAB;
    tc.enabled      = false; // disabled
    tc.points       = { { 0.0, 0.0, 0 }, { 1.0, 1.0, 0 } };
    plan.targetCurves.push_back(tc);

    ats::CompiledTimbrePlan compiled(plan, 100.0);
    const ats::TimbreRenderParams r = compiled.evaluate(0.5, 5.0, 2.0, 0.6);

    if (!near(r.brightnessDb, 5.0))
        return fail("disabled curve should fall back to APVTS brightnessDb");
    if (!near(r.tiltDbPerOct, 2.0))
        return fail("disabled curve should not affect tiltDbPerOct");
    if (!near(r.noiseBalance, 0.6))
        return fail("disabled curve should not affect noiseBalance");
    return 0;
}

// ─── sampleTimbreCurve: basic interpolation ───────────────────────────────────

int testSampleTimbreCurveLinear()
{
    const std::vector<ats::TimbreMotionPoint> pts = {
        { 0.0, 0.0, 0 },
        { 1.0, 1.0, 0 }
    };
    if (!near(ats::sampleTimbreCurve(pts, 0.0), 0.0))
        return fail("linear curve at t=0 should be 0.0");
    if (!near(ats::sampleTimbreCurve(pts, 0.5), 0.5))
        return fail("linear curve at t=0.5 should be 0.5");
    if (!near(ats::sampleTimbreCurve(pts, 1.0), 1.0))
        return fail("linear curve at t=1.0 should be 1.0");
    return 0;
}

int testSampleTimbreCurveEaseOut()
{
    const std::vector<ats::TimbreMotionPoint> pts = {
        { 0.0, 0.0, 2 }, // ease_out on this segment
        { 1.0, 1.0, 0 }
    };
    // ease_out: t * (2 - t), so at raw=0.5 → 0.5*(2-0.5)=0.75
    const double v = ats::sampleTimbreCurve(pts, 0.5);
    if (!near(v, 0.75, 1.0e-5))
        return fail("ease_out curve at t=0.5 should be 0.75");
    return 0;
}

int testSampleTimbreCurveEmpty()
{
    const std::vector<ats::TimbreMotionPoint> pts;
    if (!near(ats::sampleTimbreCurve(pts, 0.5), 0.0))
        return fail("empty curve should return 0.0");
    return 0;
}

int testSampleTimbreCurveClamp()
{
    const std::vector<ats::TimbreMotionPoint> pts = {
        { 0.2, 0.3, 0 },
        { 0.8, 0.9, 0 }
    };
    // Before first point: should return first value
    if (!near(ats::sampleTimbreCurve(pts, 0.0), 0.3))
        return fail("curve before first point should clamp to first value");
    // After last point: should return last value
    if (!near(ats::sampleTimbreCurve(pts, 1.0), 0.9))
        return fail("curve after last point should clamp to last value");
    return 0;
}

// ─── JSON round-trip (INV-8) ──────────────────────────────────────────────────

int testJsonRoundTrip()
{
    ats::TimbreMotionPlan plan;
    plan.schemaVersion = 1;
    plan.engineVersion = "timbre-motion-v1";

    ats::TimbreTargetCurve tc;
    tc.descriptorId = "combined.centroid_axis";
    tc.mode         = ats::TimbreTargetMode::InterpolateAB;
    tc.enabled      = true;
    tc.endpointLocked = true;
    tc.priority     = 10;
    tc.depth        = 1.0;
    tc.smoothingMs  = 40.0;
    tc.points       = {
        { 0.0,  0.0,  0 },
        { 0.35, 1.0,  2 }, // ease_out
        { 1.0,  1.0,  0 }
    };
    plan.targetCurves.push_back(tc);

    ats::TimbreMacro macro;
    macro.macroId = "brightness";
    macro.enabled = true;
    macro.amount  = 1.0;
    ats::TimbreMacroRoute route;
    route.targetDescriptorId = "combined.centroid_axis";
    route.depth        = 1.0;
    route.bipolar      = false;
    route.rangeMinNorm = 0.0;
    route.rangeMaxNorm = 1.0;
    macro.routes.push_back(route);
    plan.macros.push_back(macro);

    plan.focusedDescriptorId = "combined.centroid_axis";
    plan.pinnedDescriptorIds = { "combined.centroid_axis" };

    const std::string json  = ats::serializeTimbreMotionPlan(plan);
    const ats::TimbreMotionPlan rt = ats::deserializeTimbreMotionPlan(json);

    if (rt.schemaVersion != 1)
        return fail("round-trip: schemaVersion mismatch");
    if (rt.engineVersion != "timbre-motion-v1")
        return fail("round-trip: engineVersion mismatch");
    if (rt.targetCurves.size() != 1)
        return fail("round-trip: targetCurves count mismatch");
    if (rt.targetCurves[0].descriptorId != "combined.centroid_axis")
        return fail("round-trip: descriptorId mismatch");
    if (rt.targetCurves[0].mode != ats::TimbreTargetMode::InterpolateAB)
        return fail("round-trip: mode mismatch");
    if (!rt.targetCurves[0].enabled)
        return fail("round-trip: enabled mismatch");
    if (rt.targetCurves[0].points.size() != 3)
        return fail("round-trip: points count mismatch");
    if (!near(rt.targetCurves[0].points[1].timeNorm, 0.35))
        return fail("round-trip: point[1].timeNorm mismatch");
    if (!near(rt.targetCurves[0].points[1].valueNorm, 1.0))
        return fail("round-trip: point[1].valueNorm mismatch");
    if (rt.targetCurves[0].points[1].curve != 2) // ease_out
        return fail("round-trip: point[1].curve mismatch");
    if (rt.macros.size() != 1)
        return fail("round-trip: macros count mismatch");
    if (rt.macros[0].macroId != "brightness")
        return fail("round-trip: macro id mismatch");
    if (rt.macros[0].routes.size() != 1)
        return fail("round-trip: macro routes count mismatch");
    if (rt.pinnedDescriptorIds.size() != 1 || rt.pinnedDescriptorIds[0] != "combined.centroid_axis")
        return fail("round-trip: pinnedDescriptorIds mismatch");
    if (rt.focusedDescriptorId != "combined.centroid_axis")
        return fail("round-trip: focusedDescriptorId mismatch");
    return 0;
}

// ─── Empty JSON → default plan (INV-1, §3.6) ─────────────────────────────────

int testEmptyJsonDeserialize()
{
    const ats::TimbreMotionPlan plan = ats::deserializeTimbreMotionPlan("");
    if (!plan.empty())
        return fail("empty JSON should produce an empty plan");
    return 0;
}

int testMalformedJsonDeserialize()
{
    const ats::TimbreMotionPlan plan = ats::deserializeTimbreMotionPlan("not valid json {{{{");
    if (!plan.empty())
        return fail("malformed JSON should produce an empty plan (fail-safe)");
    return 0;
}

// ─── M1.4 fixture: worked example §6 (Table §6.4) ────────────────────────────
// Measured endpoints: d_A = 8.2 Bark, d_B = 14.1 Bark.
// In v1, the full range [-12, 12] is used. The curve value c(t) is mapped to that range.
// We verify that the brightness increases as the curve rises, not the exact Bark values
// (those need the analyzer in M2). We do verify the relative ordering and that at t=0
// we get the neutral value and at t=0.35 we hit the maximum.

int testWorkedExampleBrightnessShape()
{
    ats::TimbreMotionPlan plan;
    ats::TimbreTargetCurve tc;
    tc.descriptorId = "combined.centroid_axis";
    tc.mode         = ats::TimbreTargetMode::InterpolateAB;
    tc.enabled      = true;
    tc.depth        = 1.0;
    tc.smoothingMs  = 0.0; // no smoothing for determinism
    tc.points       = {
        { 0.0,  0.0, 0 },       // linear
        { 0.35, 1.0, 2 },       // ease_out on segment [0, 0.35]
        { 1.0,  1.0, 0 }        // hold
    };
    plan.targetCurves.push_back(tc);

    // smoothingMs=0 → alpha=0 → immediate response (no lag).
    // Use a very high chunk rate so smoothing coefficient is effectively 0.
    ats::CompiledTimbrePlan compiled(plan, 1.0e9);

    const double kMin = -12.0, kMax = 12.0;

    // t=0: curve=0 → brightnessDb = kMin + 0*(kMax-kMin) = -12
    {
        const auto r = compiled.evaluate(0.0, 0.0, 0.0, 0.5);
        if (r.brightnessDb >= 0.0)
            return fail("at t=0, brightness should be at minimum (-12 dB)");
    }

    // t=0.35: curve=1 → brightnessDb = kMax = 12
    {
        const auto r = compiled.evaluate(0.35, 0.0, 0.0, 0.5);
        if (!near(r.brightnessDb, kMax, 0.01))
            return fail("at t=0.35, brightness should reach maximum (+12 dB)");
    }

    // t=1.0: curve holds at 1 → brightnessDb = kMax
    {
        const auto r = compiled.evaluate(1.0, 0.0, 0.0, 0.5);
        if (!near(r.brightnessDb, kMax, 0.01))
            return fail("at t=1.0, brightness should hold at maximum");
    }

    // Tilt and noise are unaffected (fallback to APVTS 0.0 / 0.5).
    {
        // Re-create since evaluate is stateful and t must be non-decreasing.
        ats::CompiledTimbrePlan c2(plan, 1.0e9);
        const auto r = c2.evaluate(0.5, 0.0, 2.0, 0.7);
        if (!near(r.tiltDbPerOct, 2.0))
            return fail("brightness curve should not affect tiltDbPerOct");
        if (!near(r.noiseBalance, 0.7))
            return fail("brightness curve should not affect noiseBalance");
    }

    return 0;
}

// ─── follow_natural mode: curve ignored, uses APVTS ──────────────────────────

int testFollowNaturalMode()
{
    ats::TimbreMotionPlan plan;
    ats::TimbreTargetCurve tc;
    tc.descriptorId = "combined.centroid_axis";
    tc.mode         = ats::TimbreTargetMode::FollowNatural;
    tc.enabled      = true;
    tc.depth        = 1.0;
    tc.smoothingMs  = 0.0;
    tc.points       = { { 0.0, 0.0, 0 }, { 1.0, 1.0, 0 } };
    plan.targetCurves.push_back(tc);

    ats::CompiledTimbrePlan compiled(plan, 1.0e9);
    const auto r = compiled.evaluate(0.5, 5.0, 2.0, 0.6);
    if (!near(r.brightnessDb, 5.0))
        return fail("follow_natural mode should leave brightnessDb at APVTS value");
    return 0;
}

// ─── offset_natural mode ──────────────────────────────────────────────────────

int testOffsetNaturalMode()
{
    ats::TimbreMotionPlan plan;
    ats::TimbreTargetCurve tc;
    tc.descriptorId = "combined.centroid_axis";
    tc.mode         = ats::TimbreTargetMode::OffsetNatural;
    tc.enabled      = true;
    tc.depth        = 1.0;
    tc.smoothingMs  = 0.0;
    // curve at 1.0 → bipolar = 2*1-1 = +1 → max positive offset
    tc.points       = { { 0.0, 1.0, 0 }, { 1.0, 1.0, 0 } };
    plan.targetCurves.push_back(tc);

    ats::CompiledTimbrePlan compiled(plan, 1.0e9);
    const auto r = compiled.evaluate(0.5, 0.0, 0.0, 0.5);
    // APVTS base = 0.0, offset = 1.0 * (+1) * (24/2) = +12
    if (!near(r.brightnessDb, 12.0, 0.01))
        return fail("offset_natural at curve=1 should reach maximum (+12 dB)");
    return 0;
}

// ─── Unknown descriptor ID round-trips silently (INV-8) ───────────────────────

int testUnknownDescriptorRoundTrip()
{
    ats::TimbreMotionPlan plan;
    ats::TimbreTargetCurve tc;
    tc.descriptorId = "combined.harmonicity"; // AnalysisOnly in registry
    tc.mode         = ats::TimbreTargetMode::InterpolateAB;
    tc.enabled      = true;
    tc.points       = { { 0.0, 0.5, 0 }, { 1.0, 0.5, 0 } };
    plan.targetCurves.push_back(tc);

    const std::string json = ats::serializeTimbreMotionPlan(plan);
    const ats::TimbreMotionPlan rt = ats::deserializeTimbreMotionPlan(json);

    if (rt.targetCurves.size() != 1)
        return fail("unknown descriptor ID should survive JSON round-trip");
    if (rt.targetCurves[0].descriptorId != "combined.harmonicity")
        return fail("unknown descriptor ID should round-trip unchanged");

    // Render must not be affected (APVTS fallback).
    ats::CompiledTimbrePlan compiled(plan, 100.0);
    const auto r = compiled.evaluate(0.5, 3.0, 1.0, 0.4);
    if (!near(r.brightnessDb, 3.0))
        return fail("unknown descriptor ID should not affect render");
    if (!near(r.tiltDbPerOct, 1.0))
        return fail("unknown descriptor ID should not affect tilt");
    if (!near(r.noiseBalance, 0.4))
        return fail("unknown descriptor ID should not affect noise");
    return 0;
}

// ─── Bark centroid model (§8.1) ───────────────────────────────────────────────

int testHzToBarkMonotonic()
{
    // Bark must increase monotonically with frequency over the audible range.
    double prev = ats::hzToBark(20.0);
    for (double hz = 50.0; hz <= 18000.0; hz *= 1.2)
    {
        const double b = ats::hzToBark(hz);
        if (!(b > prev))
            return fail("hzToBark should be strictly increasing with frequency");
        prev = b;
    }
    if (!(ats::hzToBark(0.0) == 0.0))
        return fail("hzToBark(0) should be 0");
    return 0;
}

int testSpectralCentroidBark()
{
    // All energy at one partial → centroid equals that partial's Bark value.
    {
        const std::vector<double> f = { 1000.0 };
        const std::vector<double> a = { 0.5 };
        if (!near(ats::spectralCentroidBark(f, a), ats::hzToBark(1000.0), 1.0e-9))
            return fail("single-partial centroid should equal its Bark value");
    }
    // Shifting energy toward higher partials raises the centroid.
    {
        const std::vector<double> f = { 200.0, 2000.0 };
        const std::vector<double> lowHeavy  = { 1.0, 0.1 };
        const std::vector<double> highHeavy = { 0.1, 1.0 };
        if (!(ats::spectralCentroidBark(f, highHeavy) > ats::spectralCentroidBark(f, lowHeavy)))
            return fail("high-weighted spectrum should have a higher Bark centroid");
    }
    // No usable energy → 0.
    {
        const std::vector<double> f = { 1000.0 };
        const std::vector<double> a = { 0.0 };
        if (!near(ats::spectralCentroidBark(f, a), 0.0))
            return fail("zero-energy spectrum centroid should be 0");
    }
    return 0;
}

int testBrightnessGain()
{
    // At the pivot the gain is unity regardless of brightnessDb.
    if (!near(ats::brightnessGainLinear(1000.0, 6.0, 1000.0), 1.0, 1.0e-9))
        return fail("brightness gain at pivot should be unity");
    // Positive brightness boosts highs and cuts lows.
    if (!(ats::brightnessGainLinear(8000.0, 6.0) > 1.0))
        return fail("positive brightness should boost highs");
    if (!(ats::brightnessGainLinear(125.0, 6.0) < 1.0))
        return fail("positive brightness should attenuate lows");
    return 0;
}

int testSolveBrightnessForCentroid()
{
    // A log-spaced reference spectrum spanning the audible range.
    std::vector<double> f, a;
    for (double hz = 100.0; hz <= 12000.0; hz *= 1.3)
    {
        f.push_back(hz);
        a.push_back(1.0);
    }
    const double natural = ats::spectralCentroidBark(f, a);

    // Target = natural → ~0 dB.
    {
        const double db = ats::solveBrightnessDbForCentroid(f, a, natural);
        if (std::abs(db) > 0.25)
            return fail("solving for the natural centroid should return ~0 dB");
    }
    // Target above natural → positive dB, and the achieved centroid matches the target.
    {
        const double target = natural + 1.5;
        const double db = ats::solveBrightnessDbForCentroid(f, a, target);
        if (!(db > 0.0))
            return fail("a brighter target should solve to positive brightnessDb");
        std::vector<double> ga = a;
        for (std::size_t i = 0; i < f.size(); ++i)
            ga[i] = a[i] * ats::brightnessGainLinear(f[i], db);
        const double achieved = ats::spectralCentroidBark(f, ga);
        if (std::abs(achieved - target) > 0.1)
            return fail("achieved centroid should converge to the requested target");
    }
    // Monotonicity: a higher target solves to a higher (or equal, if clamped) brightnessDb.
    {
        const double dbLow  = ats::solveBrightnessDbForCentroid(f, a, natural + 0.5);
        const double dbHigh = ats::solveBrightnessDbForCentroid(f, a, natural + 2.5);
        if (!(dbHigh >= dbLow))
            return fail("higher centroid targets should not solve to lower brightnessDb");
    }
    // Unreachable target → clamps to the max.
    {
        const double db = ats::solveBrightnessDbForCentroid(f, a, natural + 100.0, -12.0, 12.0);
        if (!near(db, 12.0, 1.0e-6))
            return fail("an unreachable bright target should clamp to maxDb");
    }
    // Degenerate spectrum (no energy) → 0 dB.
    {
        const std::vector<double> ef, ea;
        if (!near(ats::solveBrightnessDbForCentroid(ef, ea, 10.0), 0.0))
            return fail("empty spectrum should solve to 0 dB");
    }
    return 0;
}

// ─── Endpoint-aware InterpolateAB (M1.2) ──────────────────────────────────────

int testEndpointInterpolateTiltNoise()
{
    ats::TimbreMotionPlan plan;
    ats::TimbreTargetCurve tilt;
    tilt.descriptorId = "fitted_envelope.tilt_db_per_oct";
    tilt.mode         = ats::TimbreTargetMode::InterpolateAB;
    tilt.enabled      = true;
    tilt.smoothingMs  = 0.0;
    tilt.points       = { { 0.0, 0.0, 0 }, { 1.0, 1.0, 0 } };
    plan.targetCurves.push_back(tilt);

    ats::TimbreTargetCurve noise;
    noise.descriptorId = "combined.noisiness";
    noise.mode         = ats::TimbreTargetMode::InterpolateAB;
    noise.enabled      = true;
    noise.smoothingMs  = 0.0;
    noise.points       = { { 0.0, 0.0, 0 }, { 1.0, 1.0, 0 } };
    plan.targetCurves.push_back(noise);

    ats::TimbreEndpoints ep;
    ep.hasTilt = true;  ep.tiltDbPerOctA = -2.0; ep.tiltDbPerOctB = 4.0;
    ep.hasNoise = true; ep.noiseBalanceA = 0.2;  ep.noiseBalanceB = 0.8;

    ats::CompiledTimbrePlan compiled(plan, 1.0e9);

    // At t=0 the curve is 0 → endpoint A; at t=1 → endpoint B; midpoint → mean.
    const auto r0 = compiled.evaluate(0.0, 0.0, 0.0, 0.5, ep);
    if (!near(r0.tiltDbPerOct, -2.0, 0.01) || !near(r0.noiseBalance, 0.2, 0.01))
        return fail("endpoint InterpolateAB at t=0 should equal source A descriptors");

    ats::CompiledTimbrePlan compiled2(plan, 1.0e9);
    const auto r1 = compiled2.evaluate(1.0, 0.0, 0.0, 0.5, ep);
    if (!near(r1.tiltDbPerOct, 4.0, 0.01) || !near(r1.noiseBalance, 0.8, 0.01))
        return fail("endpoint InterpolateAB at t=1 should equal source B descriptors");
    return 0;
}

int testEndpointCentroidSolver()
{
    ats::TimbreMotionPlan plan;
    ats::TimbreTargetCurve tc;
    tc.descriptorId = "combined.centroid_axis";
    tc.mode         = ats::TimbreTargetMode::InterpolateAB;
    tc.enabled      = true;
    tc.smoothingMs  = 0.0;
    tc.points       = { { 0.0, 0.0, 0 }, { 1.0, 1.0, 0 } };
    plan.targetCurves.push_back(tc);

    ats::CompiledTimbrePlan compiled(plan, 1.0e9);
    if (!compiled.needsCentroidEndpoints())
        return fail("plan with centroid InterpolateAB curve should request endpoints");

    // Reference spectrum + measured endpoints A (darker) / B (brighter).
    std::vector<double> f, a;
    for (double hz = 100.0; hz <= 12000.0; hz *= 1.3) { f.push_back(hz); a.push_back(1.0); }
    const double natural = ats::spectralCentroidBark(f, a);

    ats::TimbreEndpoints ep;
    ep.hasCentroid = true;
    ep.centroidBarkA = natural - 1.0;
    ep.centroidBarkB = natural + 1.0;
    ep.refFreqsHz = &f;
    ep.refAmps    = &a;

    // At t=1 the requested centroid is barkB (= natural + 1) → positive brightnessDb whose
    // applied gain lands the reference centroid at the requested value.
    const auto r1 = compiled.evaluate(1.0, 0.0, 0.0, 0.5, ep);
    if (!(r1.brightnessDb > 0.0))
        return fail("brighter centroid endpoint should solve to positive brightnessDb");
    std::vector<double> ga = a;
    for (std::size_t i = 0; i < f.size(); ++i)
        ga[i] = a[i] * ats::brightnessGainLinear(f[i], r1.brightnessDb);
    if (std::abs(ats::spectralCentroidBark(f, ga) - ep.centroidBarkB) > 0.15)
        return fail("solved brightness should place reference centroid at endpoint B");
    return 0;
}

} // namespace

int main()
{
    int result = 0;
    result |= testEmptyPlanFallback();
    result |= testDisabledCurveFallback();
    result |= testSampleTimbreCurveLinear();
    result |= testSampleTimbreCurveEaseOut();
    result |= testSampleTimbreCurveEmpty();
    result |= testSampleTimbreCurveClamp();
    result |= testJsonRoundTrip();
    result |= testEmptyJsonDeserialize();
    result |= testMalformedJsonDeserialize();
    result |= testWorkedExampleBrightnessShape();
    result |= testFollowNaturalMode();
    result |= testOffsetNaturalMode();
    result |= testUnknownDescriptorRoundTrip();
    result |= testHzToBarkMonotonic();
    result |= testSpectralCentroidBark();
    result |= testBrightnessGain();
    result |= testSolveBrightnessForCentroid();
    result |= testEndpointInterpolateTiltNoise();
    result |= testEndpointCentroidSolver();

    if (result == 0)
        std::cout << "All timbre_motion_plan tests passed.\n";
    return result;
}
