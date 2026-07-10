#pragma once

#include <array>
#include <vector>

#include <JuceHeader.h>

// Rendering helpers for the spectrum trace display (SpectrogramPanel).
// See docs/spectrum-view-overhaul-spec.md.
namespace ats_spectrum
{
// ---------------------------------------------------------------------------
// Theme (spec §2.1). Message-thread only.
// ---------------------------------------------------------------------------
namespace theme
{
inline juce::Colour background()     { return juce::Colour(0xff0a1220); }
inline juce::Colour bgVignetteEdge() { return juce::Colour(0xff050a12); }
inline juce::Colour gridMinor()      { return juce::Colour(0xff0e2a33).withAlpha(0.22f); }
inline juce::Colour gridMajor()      { return juce::Colour(0xff14454f).withAlpha(0.35f); }
inline juce::Colour axisFrame()      { return juce::Colour(0xff1e6e6e).withAlpha(0.55f); }
inline juce::Colour selection()      { return juce::Colour(0xffffe28a); }
inline juce::Colour hover()          { return juce::Colour(0xffeafffb); }
inline juce::Colour playhead()       { return juce::Colour(0xff7affd4); }
inline juce::Colour residualLow()    { return juce::Colour(0xff123b4a); }
inline juce::Colour residualHigh()   { return juce::Colour(0xff1e7a6a); }
} // namespace theme

/** Quiet -> loud partial colour: deep blue -> cyan -> mint -> lime (spec §2.1). */
inline juce::Colour ampColour(float ampNorm)
{
    static const auto lut = [] {
        constexpr uint32_t stops[4] = { 0xff1b4fa0, 0xff22a7c4, 0xff3fe8b0, 0xffb8ff7a };
        std::array<juce::Colour, 256> table;
        for (int i = 0; i < 256; ++i)
        {
            const float f = (float) i / 255.0f * 3.0f;
            const int s = juce::jmin(2, (int) f);
            table[(size_t) i] = juce::Colour(stops[s])
                                    .interpolatedWith(juce::Colour(stops[s + 1]), f - (float) s);
        }
        return table;
    }();
    return lut[(size_t) juce::jlimit(0, 255, (int) (ampNorm * 255.0f))];
}

// ---------------------------------------------------------------------------
// Trace path building (spec §2.2): decimate -> Catmull-Rom smooth -> chunk.
// ---------------------------------------------------------------------------
struct TraceVertex
{
    juce::Point<float> pos;   // screen space (already projected)
    float amp = 0.0f;         // amplitude, 0..~2 (drives width + colour)
    float shade = 1.0f;       // depth factor, 0 = far/dim, 1 = near/bright
};

/** One colour stop of a gradient trace. */
struct TraceStop
{
    float proportion = 0.0f;  // 0..1 along the gradient axis
    float amp = 0.0f;         // amplitude, 0..2
    float shade = 1.0f;       // depth factor, 0..1
    float fade = 1.0f;        // endpoint taper, multiply into final alpha
};

/**
    A whole visible run as one stroke-ready path plus colour stops. The stops
    are positioned by projecting each vertex onto the run's start->end axis, so
    a screen-space linear gradient approximates a smooth along-the-line colour
    ramp (traces advance monotonically along the time axis, which keeps the
    projection faithful). Replaces the old quantized-chunk pipeline whose flat
    per-chunk colours read as banding.
*/
struct GradientTrace
{
    juce::Path path;
    juce::Point<float> gradStart, gradEnd;
    std::vector<TraceStop> stops; // strictly increasing proportion
    float maxAmp = 0.0f;
    bool flat = false;            // axis too short to resolve a gradient
};

namespace detail
{
inline float distanceToSegment(juce::Point<float> p, juce::Point<float> a, juce::Point<float> b)
{
    const auto ab = b - a;
    const float len2 = ab.x * ab.x + ab.y * ab.y;
    if (len2 < 1.0e-6f)
        return p.getDistanceFrom(a);
    const auto ap = p - a;
    const float t = juce::jlimit(0.0f, 1.0f, (ap.x * ab.x + ap.y * ab.y) / len2);
    return p.getDistanceFrom({ a.x + ab.x * t, a.y + ab.y * t });
}
} // namespace detail

/**
    Binomial [1,2,1] smoothing over position and amplitude, endpoints pinned.
    Kills frame-to-frame jitter (both freq wiggle and amp bristle) before the
    Catmull-Rom pass so noisy models render as flowing lines, not scribbles.
*/
inline void smoothInPlace(std::vector<TraceVertex>& v, int passes = 1)
{
    if (v.size() < 3)
        return;

    std::vector<TraceVertex> tmp;
    for (int pass = 0; pass < passes; ++pass)
    {
        tmp = v;
        for (size_t i = 1; i + 1 < v.size(); ++i)
        {
            const auto& a = tmp[i - 1];
            const auto& b = tmp[i];
            const auto& c = tmp[i + 1];
            v[i].pos.x = (a.pos.x + 2.0f * b.pos.x + c.pos.x) * 0.25f;
            v[i].pos.y = (a.pos.y + 2.0f * b.pos.y + c.pos.y) * 0.25f;
            v[i].amp = (a.amp + 2.0f * b.amp + c.amp) * 0.25f;
        }
    }
}

/**
    Removes vertices that deviate less than `tolPx` from the local line direction.
    Dense .ats models emit hundreds of sub-pixel segments per trace; collapsing
    them is both the anti-shimmer fix and a perf win. Points further than
    `maxSkipPx` apart are always kept so amplitude gradients keep tracking.
*/
inline void decimateInPlace(std::vector<TraceVertex>& v,
                            float tolPx = 0.75f,
                            float maxSkipPx = 24.0f)
{
    if (v.size() < 3)
        return;

    std::vector<TraceVertex> out;
    out.reserve(v.size());
    out.push_back(v.front());

    for (size_t i = 1; i + 1 < v.size(); ++i)
    {
        const float deviation = detail::distanceToSegment(v[i].pos, out.back().pos, v[i + 1].pos);
        const float span = v[i].pos.getDistanceFrom(out.back().pos);
        if (deviation > tolPx || span > maxSkipPx
            || std::abs(v[i].amp - out.back().amp) > 0.08f)
            out.push_back(v[i]);
    }

    out.push_back(v.back());
    v.swap(out);
}

/**
    Builds one smooth Catmull-Rom path for the whole run plus gradient colour
    stops. Stops with sub-visible amp/shade deltas are dropped so the resulting
    ColourGradient stays small.
*/
inline GradientTrace buildGradientTrace(const std::vector<TraceVertex>& v)
{
    GradientTrace t;
    if (v.size() < 2)
        return t;

    auto tangent = [&](size_t i) {
        const auto& prev = v[i == 0 ? 0 : i - 1].pos;
        const auto& next = v[juce::jmin(v.size() - 1, i + 1)].pos;
        return juce::Point<float>((next.x - prev.x) / 6.0f, (next.y - prev.y) / 6.0f);
    };

    t.path.startNewSubPath(v[0].pos);
    for (size_t i = 0; i + 1 < v.size(); ++i)
        t.path.cubicTo(v[i].pos + tangent(i), v[i + 1].pos - tangent(i + 1), v[i + 1].pos);

    for (const auto& vert : v)
        t.maxAmp = juce::jmax(t.maxAmp, vert.amp);

    // Endpoint taper: fade alpha in/out over the first/last few pixels so
    // partial births/deaths dissolve instead of reading as hard confetti.
    constexpr float taperPx = 7.0f;
    std::vector<float> arc(v.size(), 0.0f);
    for (size_t i = 1; i < v.size(); ++i)
        arc[i] = arc[i - 1] + v[i].pos.getDistanceFrom(v[i - 1].pos);
    const float total = arc.back();
    auto fadeAt = [&](size_t i) {
        const float in = juce::jmin(1.0f, arc[i] / taperPx);
        const float out = juce::jmin(1.0f, (total - arc[i]) / taperPx);
        return 0.18f + 0.82f * in * out;
    };

    t.gradStart = v.front().pos;
    t.gradEnd = v.back().pos;
    const auto axis = t.gradEnd - t.gradStart;
    const float len2 = axis.x * axis.x + axis.y * axis.y;

    if (len2 < 4.0f) // under ~2px a gradient can't resolve anyway
    {
        t.flat = true;
        float ampSum = 0.0f, shadeSum = 0.0f;
        for (const auto& vert : v)
        {
            ampSum += vert.amp;
            shadeSum += vert.shade;
        }
        const float shortFade = 0.18f + 0.82f * juce::jmin(1.0f, total / (taperPx * 2.0f));
        t.stops.push_back({ 0.0f, ampSum / (float) v.size(), shadeSum / (float) v.size(), shortFade });
        return t;
    }

    t.stops.reserve(v.size());
    t.stops.push_back({ 0.0f, v.front().amp, v.front().shade, fadeAt(0) });
    for (size_t i = 1; i + 1 < v.size(); ++i)
    {
        const auto d = v[i].pos - t.gradStart;
        const float p = (d.x * axis.x + d.y * axis.y) / len2;
        const auto& last = t.stops.back();
        if (p <= last.proportion + 0.01f || p >= 0.995f)
            continue;
        const float fade = fadeAt(i);
        if (std::abs(v[i].amp - last.amp) < 0.02f
            && std::abs(v[i].shade - last.shade) < 0.05f
            && std::abs(fade - last.fade) < 0.08f)
            continue;
        t.stops.push_back({ p, v[i].amp, v[i].shade, fade });
    }
    t.stops.push_back({ 1.0f, v.back().amp, v.back().shade, fadeAt(v.size() - 1) });
    return t;
}

// ---------------------------------------------------------------------------
// Envelope waterfall ridgelines (spec §3.2).
// ---------------------------------------------------------------------------
struct RidgeSlice
{
    juce::Path top;                       // envelope top edge, screen space
    juce::Point<float> floorStart;        // slice floor line endpoints (amp = 0)
    juce::Point<float> floorEnd;
    float depth = 0.0f;                   // 0 = back, 1 = front
    std::vector<TraceStop> stops;         // amp colour stops along floorStart->floorEnd
};

/**
    Draws waterfall slices back-to-front as clean stacked ridgelines: a faint
    crest-hugging tint below each top edge, then a thin crisp stroke. No heavy
    occluding fills — the compounded haze read as a solid dark wedge. Ridge
    colour follows `ampToColour` along the slice (the amp LUT for source A, so
    the AMP legend applies to the waterfall views too; a distinct warm ramp for
    source B); depth is conveyed by alpha dimming only.
*/
inline void drawRidgeSlices(juce::Graphics& g,
                            const std::vector<RidgeSlice>& slices,
                            float alphaScale,
                            juce::Colour (*ampToColour)(float))
{
    for (const auto& s : slices)
    {
        if (s.stops.empty())
            continue;

        const float depthAlpha = (0.40f + 0.60f * s.depth) * alphaScale;

        float peakAmp = 0.0f;
        for (const auto& stop : s.stops)
            peakAmp = juce::jmax(peakAmp, stop.amp);
        const auto peakCol = ampToColour(juce::jmin(1.0f, peakAmp));

        const float crestY = s.top.getBounds().getY();
        const float floorY = juce::jmax(s.floorStart.y, s.floorEnd.y);

        // Faint body tint: crest -> transparent, fading out well above the floor
        // so overlapping slices never pile up into a dark mass.
        if (floorY - crestY > 2.0f)
        {
            juce::Path fill = s.top;
            fill.lineTo(s.floorEnd);
            fill.lineTo(s.floorStart);
            fill.closeSubPath();

            const float fadeY = crestY + (floorY - crestY) * 0.65f;
            juce::ColourGradient tint(peakCol.withAlpha(0.10f * depthAlpha),
                                      { s.floorStart.x, crestY },
                                      peakCol.withAlpha(0.0f),
                                      { s.floorStart.x, fadeY },
                                      false);
            g.setGradientFill(tint);
            g.fillPath(fill);
        }

        auto colourAt = [&](const TraceStop& stop, float alpha) {
            return ampToColour(juce::jmin(1.0f, stop.amp)).withAlpha(alpha);
        };
        juce::ColourGradient ramp(colourAt(s.stops.front(), depthAlpha), s.floorStart,
                                  colourAt(s.stops.back(), depthAlpha), s.floorEnd, false);
        for (size_t i = 1; i + 1 < s.stops.size(); ++i)
            ramp.addColour(s.stops[i].proportion, colourAt(s.stops[i], depthAlpha));

        juce::ColourGradient haloRamp(ramp);
        for (int i = 0; i < haloRamp.getNumColours(); ++i)
        {
            const auto c = haloRamp.getColour(i);
            haloRamp.setColour(i, c.withAlpha(c.getFloatAlpha() * 0.20f));
        }

        const juce::PathStrokeType core(0.9f + 0.4f * s.depth,
                                        juce::PathStrokeType::curved,
                                        juce::PathStrokeType::rounded);
        g.setGradientFill(haloRamp);
        g.strokePath(s.top, juce::PathStrokeType(2.2f,
                                                 juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
        g.setGradientFill(ramp);
        g.strokePath(s.top, core);
    }
}

/**
    Strokes a gradient trace at constant width — amplitude/depth are encoded in
    colour and alpha only, which keeps lines Hydrosynth-thin and even.
    `colourFor` maps a TraceStop to its final colour (alpha included). One
    subtle wide bloom pass is reserved for runs that ever get bright.
*/
template <typename ColourForStop>
inline void strokeGradientTrace(juce::Graphics& g,
                                const GradientTrace& t,
                                float width,
                                ColourForStop&& colourFor,
                                bool allowGlow = true)
{
    if (t.stops.empty())
        return;

    const juce::PathStrokeType core(width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    const juce::PathStrokeType halo(width * 2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    const bool glow = allowGlow && t.maxAmp > 0.6f;

    if (t.flat)
    {
        const auto col = colourFor(t.stops.front());
        if (glow)
        {
            g.setColour(col.withAlpha(col.getFloatAlpha() * 0.12f));
            g.strokePath(t.path, halo);
        }
        g.setColour(col);
        g.strokePath(t.path, core);
        return;
    }

    juce::ColourGradient grad(colourFor(t.stops.front()), t.gradStart,
                              colourFor(t.stops.back()), t.gradEnd, false);
    for (size_t i = 1; i + 1 < t.stops.size(); ++i)
        grad.addColour(t.stops[i].proportion, colourFor(t.stops[i]));

    if (glow)
    {
        juce::ColourGradient dim(grad);
        for (int i = 0; i < dim.getNumColours(); ++i)
        {
            const auto c = dim.getColour(i);
            dim.setColour(i, c.withAlpha(c.getFloatAlpha() * 0.12f));
        }
        g.setGradientFill(dim);
        g.strokePath(t.path, halo);
    }

    g.setGradientFill(grad);
    g.strokePath(t.path, core);
}
} // namespace ats_spectrum
