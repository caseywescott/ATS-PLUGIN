#include "TimbreMotionPlan.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <string_view>

namespace ats
{

// ─── Curve sampling ───────────────────────────────────────────────────────────

static double applyTimbreCurveShape(double t, int kind) noexcept
{
    switch (kind)
    {
        case 1: return t * t;                                          // ease_in
        case 2: return t * (2.0 - t);                                  // ease_out
        case 3: return t < 0.5 ? 2.0 * t * t : -1.0 + (4.0 - 2.0 * t) * t; // ease_in_out
        default: return t;                                             // linear
    }
}

double sampleTimbreCurve(const std::vector<TimbreMotionPoint>& points, double timeNorm) noexcept
{
    if (points.empty())
        return 0.0;

    const double t = std::max(0.0, std::min(1.0, timeNorm));

    if (t <= points.front().timeNorm)
        return points.front().valueNorm;

    for (std::size_t i = 1; i < points.size(); ++i)
    {
        if (t <= points[i].timeNorm)
        {
            const double span = points[i].timeNorm - points[i - 1].timeNorm;
            if (span <= 1.0e-9)
                return points[i].valueNorm;
            const double raw = (t - points[i - 1].timeNorm) / span;
            const double shaped = applyTimbreCurveShape(raw, points[i - 1].curve);
            return points[i - 1].valueNorm + shaped * (points[i].valueNorm - points[i - 1].valueNorm);
        }
    }
    return points.back().valueNorm;
}

// ─── Descriptor → physical param mapping (v1 without analyzer) ───────────────

// Known descriptor IDs from the registry (§4).
static constexpr std::string_view kDescCentroidAxis  = "combined.centroid_axis";        // → brightnessDb
static constexpr std::string_view kDescTiltDbPerOct  = "fitted_envelope.tilt_db_per_oct"; // → tiltDbPerOct
static constexpr std::string_view kDescNoisiness     = "combined.noisiness";             // → noiseBalance

// Physical range for each controllable descriptor (matching APVTS param ranges).
static constexpr double kBrightnessMin  = -12.0;  static constexpr double kBrightnessMax  = 12.0;
static constexpr double kTiltMin        = -6.0;   static constexpr double kTiltMax        = 6.0;
static constexpr double kNoiseMin       = 0.0;    static constexpr double kNoiseMax       = 1.0;

// Map c (smoothed, depth-scaled, [0,1]) to physical brightnessDb in interpolate_ab/absolute mode.
// v1: endpoints are the full parameter range (d_A/d_B measured in Phase M2).
static double centroidAxisToDb(double cNorm)
{
    return kBrightnessMin + cNorm * (kBrightnessMax - kBrightnessMin);
}
static double centroidAxisTiltToDb(double cNorm)
{
    return kTiltMin + cNorm * (kTiltMax - kTiltMin);
}
static double centroidAxisToNoise(double cNorm)
{
    return kNoiseMin + cNorm * (kNoiseMax - kNoiseMin);
}

// Clamp to param physical range.
static double clampBrightness(double v) { return std::max(kBrightnessMin, std::min(kBrightnessMax, v)); }
static double clampTilt(double v)       { return std::max(kTiltMin,       std::min(kTiltMax,       v)); }
static double clampNoise(double v)      { return std::max(kNoiseMin,      std::min(kNoiseMax,      v)); }

// ─── Spectral centroid model + brightness solver (§8.1) ───────────────────────

double hzToBark(double hz) noexcept
{
    if (!(hz > 0.0))
        return 0.0;
    // Traunmüller (1990) analytic Hz→Bark, with the standard low/high corrections.
    double bark = (26.81 * hz) / (1960.0 + hz) - 0.53;
    if (bark < 2.0)
        bark += 0.15 * (2.0 - bark);
    else if (bark > 20.1)
        bark += 0.22 * (bark - 20.1);
    return bark;
}

double spectralCentroidBark(const std::vector<double>& freqsHz,
                            const std::vector<double>& amps) noexcept
{
    const std::size_t n = std::min(freqsHz.size(), amps.size());
    double weightedSum = 0.0;
    double energySum   = 0.0;
    for (std::size_t i = 0; i < n; ++i)
    {
        const double f = freqsHz[i];
        const double a = amps[i];
        if (!std::isfinite(f) || !std::isfinite(a) || f <= 0.0 || a <= 0.0)
            continue;
        const double power = a * a; // energy weighting
        weightedSum += power * hzToBark(f);
        energySum   += power;
    }
    return energySum > 0.0 ? weightedSum / energySum : 0.0;
}

double brightnessGainLinear(double freqHz, double brightnessDb, double pivotHz) noexcept
{
    if (!(freqHz > 0.0) || !(pivotHz > 0.0) || std::abs(brightnessDb) < 1.0e-12)
        return 1.0;
    const double oct = std::log2(freqHz / pivotHz);
    const double gainDb = brightnessDb * std::tanh(oct / 3.0);
    return std::pow(10.0, gainDb / 20.0);
}

// Centroid (Bark) of the spectrum after applying a candidate brightnessDb gain.
static double centroidWithBrightness(const std::vector<double>& freqsHz,
                                     const std::vector<double>& amps,
                                     double brightnessDb,
                                     double pivotHz) noexcept
{
    const std::size_t n = std::min(freqsHz.size(), amps.size());
    double weightedSum = 0.0;
    double energySum   = 0.0;
    for (std::size_t i = 0; i < n; ++i)
    {
        const double f = freqsHz[i];
        const double a = amps[i];
        if (!std::isfinite(f) || !std::isfinite(a) || f <= 0.0 || a <= 0.0)
            continue;
        const double g = brightnessGainLinear(f, brightnessDb, pivotHz);
        const double power = (a * g) * (a * g);
        weightedSum += power * hzToBark(f);
        energySum   += power;
    }
    return energySum > 0.0 ? weightedSum / energySum : 0.0;
}

double solveBrightnessDbForCentroid(const std::vector<double>& freqsHz,
                                    const std::vector<double>& amps,
                                    double targetBark,
                                    double minDb,
                                    double maxDb,
                                    double pivotHz,
                                    int iterations) noexcept
{
    // centroid(minDb) <= centroid(maxDb) since higher brightnessDb monotonically raises the
    // centroid. If the target lies outside [centroidLo, centroidHi] it is unreachable → clamp.
    const double centroidLo = centroidWithBrightness(freqsHz, amps, minDb, pivotHz);
    const double centroidHi = centroidWithBrightness(freqsHz, amps, maxDb, pivotHz);
    if (centroidHi <= centroidLo) // no usable energy or degenerate spectrum
        return 0.0;
    if (targetBark <= centroidLo)
        return minDb;
    if (targetBark >= centroidHi)
        return maxDb;

    double lo = minDb;
    double hi = maxDb;
    for (int it = 0; it < iterations; ++it)
    {
        const double mid = 0.5 * (lo + hi);
        const double c = centroidWithBrightness(freqsHz, amps, mid, pivotHz);
        if (c < targetBark)
            lo = mid;
        else
            hi = mid;
    }
    return 0.5 * (lo + hi);
}

// ─── CompiledTimbrePlan ───────────────────────────────────────────────────────

CompiledTimbrePlan::CompiledTimbrePlan(const TimbreMotionPlan& plan, double chunkRateHz)
    : plan_(plan)
{
    if (plan_.empty())
        return;

    const std::size_t n = plan_.targetCurves.size();
    smoothAlpha_.resize(n, 0.0); // default 0 → no smoothing (immediate response)
    smoothState_.resize(n, 0.0);

    for (std::size_t i = 0; i < n; ++i)
    {
        const double tau = plan_.targetCurves[i].smoothingMs * 0.001; // seconds
        if (chunkRateHz > 0.0 && tau > 0.0)
            smoothAlpha_[i] = std::exp(-1.0 / (tau * chunkRateHz));
        // else alpha = 0.0 → instant tracking (no lag)

        // Seed state to the curve's value at t=0 (avoids a level jump on the first chunk).
        smoothState_[i] = sampleTimbreCurve(plan_.targetCurves[i].points, 0.0);
    }
}

double CompiledTimbrePlan::applyCurveShape(double t, int curveKind) noexcept
{
    return applyTimbreCurveShape(t, curveKind);
}

const std::vector<TimbreMotionPoint>* CompiledTimbrePlan::findSharedCurvePoints(
    const std::string& id) const
{
    for (const auto& sc : plan_.sharedCurves)
        if (sc.curveId == id)
            return &sc.points;
    return nullptr;
}

TimbreRenderParams CompiledTimbrePlan::evaluate(double timeNorm,
                                                double apvtsBrightnessDb,
                                                double apvtsTiltDbPerOct,
                                                double apvtsNoiseBalance)
{
    return evaluate(timeNorm, apvtsBrightnessDb, apvtsTiltDbPerOct, apvtsNoiseBalance, TimbreEndpoints{});
}

bool CompiledTimbrePlan::needsCentroidEndpoints() const
{
    for (const auto& tc : plan_.targetCurves)
        if (tc.enabled && !tc.points.empty()
            && std::string_view(tc.descriptorId) == kDescCentroidAxis
            && (tc.mode == TimbreTargetMode::InterpolateAB
                || tc.mode == TimbreTargetMode::AbsoluteTarget))
            return true;
    for (const auto& macro : plan_.macros)
        if (macro.enabled)
            for (const auto& route : macro.routes)
                if (std::string_view(route.targetDescriptorId) == kDescCentroidAxis)
                    return true;
    return false;
}

TimbreRenderParams CompiledTimbrePlan::evaluate(double timeNorm,
                                                double apvtsBrightnessDb,
                                                double apvtsTiltDbPerOct,
                                                double apvtsNoiseBalance,
                                                const TimbreEndpoints& endpoints)
{
    // Empty plan: bit-identical to pre-motion behavior (INV-1).
    if (plan_.empty())
        return { apvtsBrightnessDb, apvtsTiltDbPerOct, apvtsNoiseBalance };

    // Accumulate per-descriptor effective values. Start with APVTS fallbacks (priority 3).
    double brightnessDb  = apvtsBrightnessDb;
    double tiltDbPerOct  = apvtsTiltDbPerOct;
    double noiseBalance  = apvtsNoiseBalance;

    // Track which descriptors have been handled by an enabled curve (to avoid double-applying).
    bool centroidHandled = false;
    bool tiltHandled     = false;
    bool noiseHandled    = false;

    const auto clamp01 = [](double v) { return std::max(0.0, std::min(1.0, v)); };
    const auto lerp    = [](double a, double b, double t) { return a + (b - a) * t; };

    // InterpolateAB / AbsoluteTarget brightness: use measured endpoints + the §8.1 solver when a
    // reference spectrum is available, otherwise fall back to the bounded heuristic gain mapping.
    const auto centroidBrightnessFor = [&](double cAmount) -> double
    {
        const double c01 = clamp01(cAmount);
        if (endpoints.hasCentroid)
        {
            const double requestedBark = lerp(endpoints.centroidBarkA, endpoints.centroidBarkB, c01);
            if (endpoints.refFreqsHz != nullptr && endpoints.refAmps != nullptr
                && !endpoints.refFreqsHz->empty())
                return clampBrightness(solveBrightnessDbForCentroid(
                    *endpoints.refFreqsHz, *endpoints.refAmps, requestedBark,
                    kBrightnessMin, kBrightnessMax));
        }
        return clampBrightness(centroidAxisToDb(c01)); // analyzer-free heuristic
    };

    // Priority 1: enabled target curves (highest priority).
    for (std::size_t i = 0; i < plan_.targetCurves.size(); ++i)
    {
        const auto& tc = plan_.targetCurves[i];
        if (!tc.enabled || tc.points.empty())
            continue;

        // Sample and smooth the normalized curve value.
        // smoothAlpha_/smoothState_ are always sized to match plan_.targetCurves (constructor).
        const double raw = sampleTimbreCurve(tc.points, timeNorm);
        smoothState_[i] = smoothAlpha_[i] * smoothState_[i] + (1.0 - smoothAlpha_[i]) * raw;

        const double c = std::max(0.0, std::min(1.0, smoothState_[i])); // normalised [0,1]

        const std::string_view id(tc.descriptorId);

        if (id == kDescCentroidAxis)
        {
            switch (tc.mode)
            {
                case TimbreTargetMode::FollowNatural:
                    break; // no change; natural passes through
                case TimbreTargetMode::InterpolateAB:
                case TimbreTargetMode::AbsoluteTarget:
                    brightnessDb = centroidBrightnessFor(c * tc.depth);
                    centroidHandled = true;
                    break;
                case TimbreTargetMode::OffsetNatural:
                {
                    const double bipolar = 2.0 * c - 1.0;
                    brightnessDb = clampBrightness(
                        apvtsBrightnessDb + tc.depth * bipolar * (kBrightnessMax - kBrightnessMin) * 0.5);
                    centroidHandled = true;
                    break;
                }
            }
        }
        else if (id == kDescTiltDbPerOct)
        {
            switch (tc.mode)
            {
                case TimbreTargetMode::FollowNatural:
                    break;
                case TimbreTargetMode::InterpolateAB:
                case TimbreTargetMode::AbsoluteTarget:
                    tiltDbPerOct = endpoints.hasTilt
                        ? clampTilt(lerp(endpoints.tiltDbPerOctA, endpoints.tiltDbPerOctB, clamp01(c * tc.depth)))
                        : clampTilt(centroidAxisTiltToDb(c * tc.depth));
                    tiltHandled = true;
                    break;
                case TimbreTargetMode::OffsetNatural:
                {
                    const double bipolar = 2.0 * c - 1.0;
                    tiltDbPerOct = clampTilt(
                        apvtsTiltDbPerOct + tc.depth * bipolar * (kTiltMax - kTiltMin) * 0.5);
                    tiltHandled = true;
                    break;
                }
            }
        }
        else if (id == kDescNoisiness)
        {
            switch (tc.mode)
            {
                case TimbreTargetMode::FollowNatural:
                    break;
                case TimbreTargetMode::InterpolateAB:
                case TimbreTargetMode::AbsoluteTarget:
                    noiseBalance = endpoints.hasNoise
                        ? clampNoise(lerp(endpoints.noiseBalanceA, endpoints.noiseBalanceB, clamp01(c * tc.depth)))
                        : clampNoise(centroidAxisToNoise(c * tc.depth));
                    noiseHandled = true;
                    break;
                case TimbreTargetMode::OffsetNatural:
                {
                    const double bipolar = 2.0 * c - 1.0;
                    noiseBalance = clampNoise(
                        apvtsNoiseBalance + tc.depth * bipolar * (kNoiseMax - kNoiseMin) * 0.5);
                    noiseHandled = true;
                    break;
                }
            }
        }
        // Unknown descriptor IDs are silently ignored (INV-8).
    }

    // Priority 1: macro routes (if the target was not already handled by a direct curve).
    // Macros add their shared-curve value * route.depth * macro.amount as an offset.
    for (const auto& macro : plan_.macros)
    {
        if (!macro.enabled)
            continue;

        // Get the macro's curve value (from shared curve if set, else 1.0).
        double macroVal = 1.0;
        if (!macro.sharedCurveId.empty())
        {
            const auto* pts = findSharedCurvePoints(macro.sharedCurveId);
            if (pts)
                macroVal = sampleTimbreCurve(*pts, timeNorm);
        }
        const double macroEffect = macro.amount * macroVal; // [0, 1]

        for (const auto& route : macro.routes)
        {
            const std::string_view rid(route.targetDescriptorId);
            // Macro routes only fire if no direct target curve is active for this descriptor.
            if (rid == kDescCentroidAxis && !centroidHandled)
            {
                brightnessDb = centroidBrightnessFor(macroEffect * route.depth);
                centroidHandled = true;
            }
            else if (rid == kDescTiltDbPerOct && !tiltHandled)
            {
                const double c = std::max(0.0, std::min(1.0, macroEffect * route.depth));
                tiltDbPerOct = endpoints.hasTilt
                    ? clampTilt(lerp(endpoints.tiltDbPerOctA, endpoints.tiltDbPerOctB, c))
                    : clampTilt(centroidAxisTiltToDb(c));
                tiltHandled = true;
            }
            else if (rid == kDescNoisiness && !noiseHandled)
            {
                const double c = std::max(0.0, std::min(1.0, macroEffect * route.depth));
                noiseBalance = endpoints.hasNoise
                    ? clampNoise(lerp(endpoints.noiseBalanceA, endpoints.noiseBalanceB, c))
                    : clampNoise(centroidAxisToNoise(c));
                noiseHandled = true;
            }
        }
    }

    return { brightnessDb, tiltDbPerOct, noiseBalance };
}

// ─── JSON helpers ─────────────────────────────────────────────────────────────

namespace json
{

static void skip(const std::string& s, std::size_t& pos)
{
    while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\n' || s[pos] == '\r'))
        ++pos;
}

static bool expectChar(const std::string& s, std::size_t& pos, char ch)
{
    skip(s, pos);
    if (pos >= s.size() || s[pos] != ch)
        return false;
    ++pos;
    return true;
}

static bool parseString(const std::string& s, std::size_t& pos, std::string& out)
{
    skip(s, pos);
    if (pos >= s.size() || s[pos] != '"')
        return false;
    ++pos;
    out.clear();
    while (pos < s.size() && s[pos] != '"')
    {
        if (s[pos] == '\\')
        {
            ++pos;
            if (pos >= s.size()) return false;
            switch (s[pos])
            {
                case '"':  out += '"';  break;
                case '\\': out += '\\'; break;
                case '/':  out += '/';  break;
                case 'n':  out += '\n'; break;
                case 'r':  out += '\r'; break;
                case 't':  out += '\t'; break;
                default:   out += s[pos]; break;
            }
        }
        else
        {
            out += s[pos];
        }
        ++pos;
    }
    if (pos >= s.size()) return false;
    ++pos; // closing '"'
    return true;
}

static bool parseDouble(const std::string& s, std::size_t& pos, double& out)
{
    skip(s, pos);
    if (pos >= s.size()) return false;
    char* end = nullptr;
    out = std::strtod(s.data() + pos, &end);
    if (end == s.data() + pos) return false;
    pos = static_cast<std::size_t>(end - s.data());
    return true;
}

static bool parseInt(const std::string& s, std::size_t& pos, int& out)
{
    double d = 0.0;
    if (!parseDouble(s, pos, d)) return false;
    out = static_cast<int>(d);
    return true;
}

static bool parseBool(const std::string& s, std::size_t& pos, bool& out)
{
    skip(s, pos);
    if (pos + 4 <= s.size() && s.substr(pos, 4) == "true")  { out = true;  pos += 4; return true; }
    if (pos + 5 <= s.size() && s.substr(pos, 5) == "false") { out = false; pos += 5; return true; }
    return false;
}

// Skip past the end of any JSON value (for unknown fields).
static void skipValue(const std::string& s, std::size_t& pos)
{
    skip(s, pos);
    if (pos >= s.size()) return;
    if (s[pos] == '"')
    {
        std::string dummy;
        parseString(s, pos, dummy);
        return;
    }
    if (s[pos] == '{' || s[pos] == '[')
    {
        const char open = s[pos];
        const char close = (open == '{') ? '}' : ']';
        int depth = 1; ++pos;
        while (pos < s.size() && depth > 0)
        {
            if (s[pos] == '"')      { std::string d; parseString(s, pos, d); continue; }
            if (s[pos] == open)     { ++depth; }
            else if (s[pos] == close) { --depth; }
            ++pos;
        }
        return;
    }
    // null/true/false/number
    while (pos < s.size() && s[pos] != ',' && s[pos] != '}' && s[pos] != ']'
           && s[pos] != ' ' && s[pos] != '\t' && s[pos] != '\n' && s[pos] != '\r')
        ++pos;
}

// Parse a string field that names a TimbreTargetMode.
static TimbreTargetMode parseModeString(const std::string& val)
{
    if (val == "interpolate_ab")  return TimbreTargetMode::InterpolateAB;
    if (val == "offset_natural")  return TimbreTargetMode::OffsetNatural;
    if (val == "absolute_target") return TimbreTargetMode::AbsoluteTarget;
    return TimbreTargetMode::FollowNatural; // "follow_natural" and unknown
}

// Parse a string that names a curve shape (returns int matching TimbreMotionPoint::curve).
static int parseCurveString(const std::string& val)
{
    if (val == "ease_in")     return 1;
    if (val == "ease_out")    return 2;
    if (val == "ease_in_out") return 3;
    return 0; // "linear" and unknown
}

// Parse `{ "timeNorm": N, "valueNorm": N, "curve": "..." }`.
static bool parsePoint(const std::string& s, std::size_t& pos, TimbreMotionPoint& out)
{
    if (!expectChar(s, pos, '{')) return false;
    skip(s, pos);
    while (pos < s.size() && s[pos] != '}')
    {
        std::string key;
        if (!parseString(s, pos, key) || !expectChar(s, pos, ':')) return false;
        if      (key == "timeNorm")  { if (!parseDouble(s, pos, out.timeNorm))  return false; }
        else if (key == "valueNorm") { if (!parseDouble(s, pos, out.valueNorm)) return false; }
        else if (key == "curve")
        {
            std::string cv;
            if (!parseString(s, pos, cv)) return false;
            out.curve = parseCurveString(cv);
        }
        else { skipValue(s, pos); }
        skip(s, pos);
        if (pos < s.size() && s[pos] == ',') { ++pos; skip(s, pos); }
    }
    return expectChar(s, pos, '}');
}

static bool parsePoints(const std::string& s, std::size_t& pos,
                         std::vector<TimbreMotionPoint>& out)
{
    if (!expectChar(s, pos, '[')) return false;
    skip(s, pos);
    while (pos < s.size() && s[pos] != ']')
    {
        TimbreMotionPoint pt;
        if (!parsePoint(s, pos, pt)) return false;
        out.push_back(pt);
        skip(s, pos);
        if (pos < s.size() && s[pos] == ',') { ++pos; skip(s, pos); }
    }
    return expectChar(s, pos, ']');
}

static bool parseTargetCurve(const std::string& s, std::size_t& pos, TimbreTargetCurve& out)
{
    if (!expectChar(s, pos, '{')) return false;
    skip(s, pos);
    while (pos < s.size() && s[pos] != '}')
    {
        std::string key;
        if (!parseString(s, pos, key) || !expectChar(s, pos, ':')) return false;
        if      (key == "descriptorId")   { if (!parseString(s, pos, out.descriptorId))        return false; }
        else if (key == "mode")           { std::string v; if (!parseString(s, pos, v)) return false; out.mode = parseModeString(v); }
        else if (key == "enabled")        { if (!parseBool(s, pos, out.enabled))                return false; }
        else if (key == "endpointLocked") { if (!parseBool(s, pos, out.endpointLocked))         return false; }
        else if (key == "priority")       { if (!parseInt(s, pos, out.priority))                return false; }
        else if (key == "depth")          { if (!parseDouble(s, pos, out.depth))                return false; }
        else if (key == "smoothingMs")    { if (!parseDouble(s, pos, out.smoothingMs))          return false; }
        else if (key == "points")         { if (!parsePoints(s, pos, out.points))               return false; }
        else                              { skipValue(s, pos); }
        skip(s, pos);
        if (pos < s.size() && s[pos] == ',') { ++pos; skip(s, pos); }
    }
    return expectChar(s, pos, '}');
}

static bool parseSharedCurve(const std::string& s, std::size_t& pos, TimbreSharedCurve& out)
{
    if (!expectChar(s, pos, '{')) return false;
    skip(s, pos);
    while (pos < s.size() && s[pos] != '}')
    {
        std::string key;
        if (!parseString(s, pos, key) || !expectChar(s, pos, ':')) return false;
        if      (key == "curveId") { if (!parseString(s, pos, out.curveId)) return false; }
        else if (key == "points")  { if (!parsePoints(s, pos, out.points))  return false; }
        else                       { skipValue(s, pos); }
        skip(s, pos);
        if (pos < s.size() && s[pos] == ',') { ++pos; skip(s, pos); }
    }
    return expectChar(s, pos, '}');
}

static bool parseMacroRoute(const std::string& s, std::size_t& pos, TimbreMacroRoute& out)
{
    if (!expectChar(s, pos, '{')) return false;
    skip(s, pos);
    while (pos < s.size() && s[pos] != '}')
    {
        std::string key;
        if (!parseString(s, pos, key) || !expectChar(s, pos, ':')) return false;
        if      (key == "targetDescriptorId") { if (!parseString(s, pos, out.targetDescriptorId)) return false; }
        else if (key == "depth")              { if (!parseDouble(s, pos, out.depth))              return false; }
        else if (key == "bipolar")            { if (!parseBool(s, pos, out.bipolar))              return false; }
        else if (key == "rangeMinNorm")       { if (!parseDouble(s, pos, out.rangeMinNorm))       return false; }
        else if (key == "rangeMaxNorm")       { if (!parseDouble(s, pos, out.rangeMaxNorm))       return false; }
        else                                  { skipValue(s, pos); }
        skip(s, pos);
        if (pos < s.size() && s[pos] == ',') { ++pos; skip(s, pos); }
    }
    return expectChar(s, pos, '}');
}

static bool parseMacro(const std::string& s, std::size_t& pos, TimbreMacro& out)
{
    if (!expectChar(s, pos, '{')) return false;
    skip(s, pos);
    while (pos < s.size() && s[pos] != '}')
    {
        std::string key;
        if (!parseString(s, pos, key) || !expectChar(s, pos, ':')) return false;
        if (key == "macroId")      { if (!parseString(s, pos, out.macroId))     return false; }
        else if (key == "enabled") { if (!parseBool(s, pos, out.enabled))       return false; }
        else if (key == "amount")  { if (!parseDouble(s, pos, out.amount))      return false; }
        else if (key == "sharedCurveId") { if (!parseString(s, pos, out.sharedCurveId)) return false; }
        else if (key == "routes")
        {
            if (!expectChar(s, pos, '[')) return false;
            skip(s, pos);
            while (pos < s.size() && s[pos] != ']')
            {
                TimbreMacroRoute route;
                if (!parseMacroRoute(s, pos, route)) return false;
                out.routes.push_back(route);
                skip(s, pos);
                if (pos < s.size() && s[pos] == ',') { ++pos; skip(s, pos); }
            }
            if (!expectChar(s, pos, ']')) return false;
        }
        else { skipValue(s, pos); }
        skip(s, pos);
        if (pos < s.size() && s[pos] == ',') { ++pos; skip(s, pos); }
    }
    return expectChar(s, pos, '}');
}

static bool parseStringArray(const std::string& s, std::size_t& pos,
                               std::vector<std::string>& out)
{
    if (!expectChar(s, pos, '[')) return false;
    skip(s, pos);
    while (pos < s.size() && s[pos] != ']')
    {
        std::string v;
        if (!parseString(s, pos, v)) return false;
        out.push_back(v);
        skip(s, pos);
        if (pos < s.size() && s[pos] == ',') { ++pos; skip(s, pos); }
    }
    return expectChar(s, pos, ']');
}

} // namespace json

// ─── Public (de)serialize ─────────────────────────────────────────────────────

TimbreMotionPlan deserializeTimbreMotionPlan(const std::string& jsonText)
{
    TimbreMotionPlan plan;
    if (jsonText.empty())
        return plan;

    std::size_t pos = 0;
    if (!json::expectChar(jsonText, pos, '{'))
        return plan;

    json::skip(jsonText, pos);
    while (pos < jsonText.size() && jsonText[pos] != '}')
    {
        std::string key;
        if (!json::parseString(jsonText, pos, key) || !json::expectChar(jsonText, pos, ':'))
            return plan;

        if      (key == "schemaVersion")        { json::parseInt(jsonText, pos, plan.schemaVersion); }
        else if (key == "engineVersion")        { json::parseString(jsonText, pos, plan.engineVersion); }
        else if (key == "focusedDescriptorId")  { json::parseString(jsonText, pos, plan.focusedDescriptorId); }
        else if (key == "pinnedDescriptorIds")  { json::parseStringArray(jsonText, pos, plan.pinnedDescriptorIds); }
        else if (key == "targetCurves")
        {
            if (!json::expectChar(jsonText, pos, '[')) return plan;
            json::skip(jsonText, pos);
            while (pos < jsonText.size() && jsonText[pos] != ']')
            {
                TimbreTargetCurve tc;
                if (!json::parseTargetCurve(jsonText, pos, tc)) return plan;
                plan.targetCurves.push_back(tc);
                json::skip(jsonText, pos);
                if (pos < jsonText.size() && jsonText[pos] == ',') { ++pos; json::skip(jsonText, pos); }
            }
            if (!json::expectChar(jsonText, pos, ']')) return plan;
        }
        else if (key == "macros")
        {
            if (!json::expectChar(jsonText, pos, '[')) return plan;
            json::skip(jsonText, pos);
            while (pos < jsonText.size() && jsonText[pos] != ']')
            {
                TimbreMacro macro;
                if (!json::parseMacro(jsonText, pos, macro)) return plan;
                plan.macros.push_back(macro);
                json::skip(jsonText, pos);
                if (pos < jsonText.size() && jsonText[pos] == ',') { ++pos; json::skip(jsonText, pos); }
            }
            if (!json::expectChar(jsonText, pos, ']')) return plan;
        }
        else if (key == "sharedCurves")
        {
            if (!json::expectChar(jsonText, pos, '[')) return plan;
            json::skip(jsonText, pos);
            while (pos < jsonText.size() && jsonText[pos] != ']')
            {
                TimbreSharedCurve sc;
                if (!json::parseSharedCurve(jsonText, pos, sc)) return plan;
                plan.sharedCurves.push_back(sc);
                json::skip(jsonText, pos);
                if (pos < jsonText.size() && jsonText[pos] == ',') { ++pos; json::skip(jsonText, pos); }
            }
            if (!json::expectChar(jsonText, pos, ']')) return plan;
        }
        else
        {
            json::skipValue(jsonText, pos);
        }

        json::skip(jsonText, pos);
        if (pos < jsonText.size() && jsonText[pos] == ',') { ++pos; json::skip(jsonText, pos); }
    }

    return plan;
}

// ─── Serializer ───────────────────────────────────────────────────────────────

namespace
{

static const char* modeToString(TimbreTargetMode mode)
{
    switch (mode)
    {
        case TimbreTargetMode::InterpolateAB:  return "interpolate_ab";
        case TimbreTargetMode::OffsetNatural:  return "offset_natural";
        case TimbreTargetMode::AbsoluteTarget: return "absolute_target";
        default:                               return "follow_natural";
    }
}

static const char* curveKindToString(int kind)
{
    switch (kind)
    {
        case 1:  return "ease_in";
        case 2:  return "ease_out";
        case 3:  return "ease_in_out";
        default: return "linear";
    }
}

static std::string jsonString(const std::string& s)
{
    std::string out;
    out.reserve(s.size() + 2);
    out += '"';
    for (char c : s)
    {
        if      (c == '"')  out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else                out += c;
    }
    out += '"';
    return out;
}

static std::string jsonDouble(double v)
{
    std::ostringstream ss;
    ss << std::setprecision(10) << v;
    return ss.str();
}

static std::string serializePoints(const std::vector<TimbreMotionPoint>& pts, int indent)
{
    const std::string pad(static_cast<std::size_t>(indent), ' ');
    const std::string pad2(static_cast<std::size_t>(indent + 2), ' ');
    std::ostringstream out;
    out << "[\n";
    for (std::size_t i = 0; i < pts.size(); ++i)
    {
        const auto& p = pts[i];
        out << pad2 << "{";
        out << "\"timeNorm\":" << jsonDouble(p.timeNorm) << ",";
        out << "\"valueNorm\":" << jsonDouble(p.valueNorm) << ",";
        out << "\"curve\":" << jsonString(curveKindToString(p.curve)) << "}";
        if (i + 1 < pts.size()) out << ",";
        out << "\n";
    }
    out << pad << "]";
    return out.str();
}

} // anonymous namespace

std::string serializeTimbreMotionPlan(const TimbreMotionPlan& plan)
{
    std::ostringstream out;
    out << "{\n";
    out << "  \"schemaVersion\":" << plan.schemaVersion << ",\n";
    out << "  \"engineVersion\":" << jsonString(plan.engineVersion) << ",\n";

    // targetCurves
    out << "  \"targetCurves\":[\n";
    for (std::size_t i = 0; i < plan.targetCurves.size(); ++i)
    {
        const auto& tc = plan.targetCurves[i];
        out << "    {\n";
        out << "      \"descriptorId\":"  << jsonString(tc.descriptorId)           << ",\n";
        out << "      \"mode\":"          << jsonString(modeToString(tc.mode))      << ",\n";
        out << "      \"enabled\":"       << (tc.enabled        ? "true" : "false") << ",\n";
        out << "      \"endpointLocked\":" << (tc.endpointLocked ? "true" : "false") << ",\n";
        out << "      \"priority\":"      << tc.priority                           << ",\n";
        out << "      \"depth\":"         << jsonDouble(tc.depth)                  << ",\n";
        out << "      \"smoothingMs\":"   << jsonDouble(tc.smoothingMs)            << ",\n";
        out << "      \"points\":"        << serializePoints(tc.points, 6)         << "\n";
        out << "    }";
        if (i + 1 < plan.targetCurves.size()) out << ",";
        out << "\n";
    }
    out << "  ],\n";

    // macros
    out << "  \"macros\":[\n";
    for (std::size_t i = 0; i < plan.macros.size(); ++i)
    {
        const auto& m = plan.macros[i];
        out << "    {\n";
        out << "      \"macroId\":"      << jsonString(m.macroId)             << ",\n";
        out << "      \"enabled\":"      << (m.enabled ? "true" : "false")    << ",\n";
        out << "      \"amount\":"       << jsonDouble(m.amount)              << ",\n";
        out << "      \"sharedCurveId\":" << jsonString(m.sharedCurveId)      << ",\n";
        out << "      \"routes\":[\n";
        for (std::size_t j = 0; j < m.routes.size(); ++j)
        {
            const auto& r = m.routes[j];
            out << "        {";
            out << "\"targetDescriptorId\":" << jsonString(r.targetDescriptorId) << ",";
            out << "\"depth\":"              << jsonDouble(r.depth)              << ",";
            out << "\"bipolar\":"            << (r.bipolar ? "true" : "false")   << ",";
            out << "\"rangeMinNorm\":"       << jsonDouble(r.rangeMinNorm)       << ",";
            out << "\"rangeMaxNorm\":"       << jsonDouble(r.rangeMaxNorm)       << "}";
            if (j + 1 < m.routes.size()) out << ",";
            out << "\n";
        }
        out << "      ]\n";
        out << "    }";
        if (i + 1 < plan.macros.size()) out << ",";
        out << "\n";
    }
    out << "  ],\n";

    // sharedCurves
    out << "  \"sharedCurves\":[\n";
    for (std::size_t i = 0; i < plan.sharedCurves.size(); ++i)
    {
        const auto& sc = plan.sharedCurves[i];
        out << "    {";
        out << "\"curveId\":" << jsonString(sc.curveId) << ",";
        out << "\"points\":"  << serializePoints(sc.points, 4) << "}";
        if (i + 1 < plan.sharedCurves.size()) out << ",";
        out << "\n";
    }
    out << "  ],\n";

    // pinnedDescriptorIds
    out << "  \"pinnedDescriptorIds\":[";
    for (std::size_t i = 0; i < plan.pinnedDescriptorIds.size(); ++i)
    {
        out << jsonString(plan.pinnedDescriptorIds[i]);
        if (i + 1 < plan.pinnedDescriptorIds.size()) out << ",";
    }
    out << "],\n";

    out << "  \"focusedDescriptorId\":" << jsonString(plan.focusedDescriptorId) << "\n";
    out << "}";
    return out.str();
}

} // namespace ats
