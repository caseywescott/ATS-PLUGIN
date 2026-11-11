#pragma once

#include "UiBackend.h"
#include "ThemeTokens.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace ats::ui
{

[[nodiscard]] inline UiColor ampColour(float ampNorm) noexcept
{
    const auto t = std::clamp(ampNorm, 0.0f, 1.0f);
    constexpr std::uint32_t stops[] = { 0xff1b4fa0u, 0xff22a7c4u,
                                         0xff3fe8b0u, 0xffb8ff7au };
    const auto scaled = t * 3.0f;
    const auto i = std::min(2, static_cast<int>(scaled));
    const auto mix = scaled - static_cast<float>(i);
    const auto channel = [mix](std::uint32_t a, std::uint32_t b) {
        return static_cast<std::uint32_t>(std::clamp(
            static_cast<float>(a) + (static_cast<float>(b) - static_cast<float>(a)) * mix,
            0.0f, 255.0f));
    };
    return { 0xff000000u | (channel(stops[i] >> 16 & 0xffu, stops[i + 1] >> 16 & 0xffu) << 16)
             | (channel(stops[i] >> 8 & 0xffu, stops[i + 1] >> 8 & 0xffu) << 8)
             | channel(stops[i] & 0xffu, stops[i + 1] & 0xffu) };
}

// Source B in the envelope overlay keeps its own purple->orange ramp so an
// A+B comparison stays readable; both ramps are driven by amplitude.
[[nodiscard]] inline UiColor overlayAmpColour(float ampNorm) noexcept
{
    const auto t = std::clamp(ampNorm, 0.0f, 1.0f);
    constexpr std::uint32_t from = 0xff8a2f8eu, to = 0xffe8a04au;
    const auto channel = [t](std::uint32_t a, std::uint32_t b) {
        return static_cast<std::uint32_t>(std::clamp(
            static_cast<float>(a) + (static_cast<float>(b) - static_cast<float>(a)) * t,
            0.0f, 255.0f));
    };
    return { 0xff000000u | (channel(from >> 16 & 0xffu, to >> 16 & 0xffu) << 16)
             | (channel(from >> 8 & 0xffu, to >> 8 & 0xffu) << 8)
             | channel(from & 0xffu, to & 0xffu) };
}

struct TraceVertex
{
    UiPoint pos{};
    float amp = 0.0f;
    float shade = 1.0f;
};

struct TraceStop
{
    float proportion = 0.0f;
    float amp = 0.0f;
    float shade = 1.0f;
    float fade = 1.0f;
    // Per-stop opacity multiplier.  The legacy painter derived each stop's
    // alpha from its own amplitude/depth, so a loud partial still fades
    // where it is quiet; painters multiply this into the command alpha.
    float alpha = 1.0f;
};

struct GradientTrace
{
    std::vector<UiPoint> vertices;
    UiPoint gradStart{}, gradEnd{};
    std::vector<TraceStop> stops;
    float maxAmp = 0.0f;
    bool flat = false;
};

namespace spectrum_path_detail
{
inline float distanceToSegment(UiPoint p, UiPoint a, UiPoint b) noexcept
{
    const auto dx = b.x - a.x;
    const auto dy = b.y - a.y;
    const auto len2 = dx * dx + dy * dy;
    if (len2 < 1.0e-6f)
        return std::hypot(p.x - a.x, p.y - a.y);
    const auto t = std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / len2, 0.0f, 1.0f);
    return std::hypot(p.x - (a.x + dx * t), p.y - (a.y + dy * t));
}

inline bool finite(const TraceVertex& vertex) noexcept
{
    return std::isfinite(vertex.pos.x) && std::isfinite(vertex.pos.y)
        && std::isfinite(vertex.amp) && std::isfinite(vertex.shade);
}

inline TraceVertex interpolate(const TraceVertex& a, const TraceVertex& b, float amount) noexcept
{
    return { { a.pos.x + (b.pos.x - a.pos.x) * amount,
               a.pos.y + (b.pos.y - a.pos.y) * amount },
             a.amp + (b.amp - a.amp) * amount,
             a.shade + (b.shade - a.shade) * amount };
}

inline bool clipSegment(TraceVertex& a, TraceVertex& b, UiRect bounds) noexcept
{
    if (!finite(a) || !finite(b) || !(bounds.width >= 0.0f) || !(bounds.height >= 0.0f))
        return false;

    const auto dx = b.pos.x - a.pos.x;
    const auto dy = b.pos.y - a.pos.y;
    const float p[] { -dx, dx, -dy, dy };
    const float q[] { a.pos.x - bounds.x, bounds.x + bounds.width - a.pos.x,
                      a.pos.y - bounds.y, bounds.y + bounds.height - a.pos.y };
    float first = 0.0f;
    float last = 1.0f;
    for (int edge = 0; edge < 4; ++edge)
    {
        if (std::abs(p[edge]) < 1.0e-7f)
        {
            if (q[edge] < 0.0f)
                return false;
            continue;
        }
        const auto amount = q[edge] / p[edge];
        if (p[edge] < 0.0f)
            first = std::max(first, amount);
        else
            last = std::min(last, amount);
        if (first > last)
            return false;
    }

    const auto sourceA = a;
    const auto sourceB = b;
    a = interpolate(sourceA, sourceB, first);
    b = interpolate(sourceA, sourceB, last);
    a.pos.x = std::clamp(a.pos.x, bounds.x, bounds.x + bounds.width);
    a.pos.y = std::clamp(a.pos.y, bounds.y, bounds.y + bounds.height);
    b.pos.x = std::clamp(b.pos.x, bounds.x, bounds.x + bounds.width);
    b.pos.y = std::clamp(b.pos.y, bounds.y, bounds.y + bounds.height);
    return true;
}
}

/**
    Clips a projected trace into finite, connected runs before it reaches a
    native path backend.  Scissoring alone is too late for atlas renderers:
    they allocate storage for the complete path bounds before applying the
    clip.  Keeping each run close to the plot also rejects malformed numeric
    input without losing a segment that crosses the visible area.
*/
inline std::vector<std::vector<TraceVertex>> clipTraceRuns(
    const std::vector<TraceVertex>& vertices, UiRect bounds)
{
    std::vector<std::vector<TraceVertex>> result;
    if (vertices.size() < 2)
        return result;

    constexpr float joinTolerance = 0.01f;
    const auto samePoint = [](UiPoint a, UiPoint b) {
        return std::abs(a.x - b.x) <= joinTolerance
            && std::abs(a.y - b.y) <= joinTolerance;
    };
    for (std::size_t i = 1; i < vertices.size(); ++i)
    {
        auto a = vertices[i - 1];
        auto b = vertices[i];
        if (!spectrum_path_detail::clipSegment(a, b, bounds))
            continue;
        if (result.empty() || result.back().empty()
            || !samePoint(result.back().back().pos, a.pos))
            result.push_back({ a, b });
        else if (!samePoint(result.back().back().pos, b.pos))
            result.back().push_back(b);
    }
    return result;
}

inline void smoothInPlace(std::vector<TraceVertex>& vertices, int passes = 1)
{
    if (vertices.size() < 3) return;
    std::vector<TraceVertex> tmp;
    for (int pass = 0; pass < passes; ++pass)
    {
        tmp = vertices;
        for (std::size_t i = 1; i + 1 < vertices.size(); ++i)
        {
            vertices[i].pos.x = (tmp[i - 1].pos.x + 2.0f * tmp[i].pos.x + tmp[i + 1].pos.x) * 0.25f;
            vertices[i].pos.y = (tmp[i - 1].pos.y + 2.0f * tmp[i].pos.y + tmp[i + 1].pos.y) * 0.25f;
            vertices[i].amp = (tmp[i - 1].amp + 2.0f * tmp[i].amp + tmp[i + 1].amp) * 0.25f;
        }
    }
}

inline void decimateInPlace(std::vector<TraceVertex>& vertices,
                            float tolerancePx = 0.75f, float maxSkipPx = 24.0f)
{
    if (vertices.size() < 3) return;
    std::vector<TraceVertex> result { vertices.front() };
    for (std::size_t i = 1; i + 1 < vertices.size(); ++i)
    {
        const auto dx = vertices[i + 1].pos.x - result.back().pos.x;
        const auto dy = vertices[i + 1].pos.y - result.back().pos.y;
        const auto len2 = dx * dx + dy * dy;
        const auto px = vertices[i].pos.x - result.back().pos.x;
        const auto py = vertices[i].pos.y - result.back().pos.y;
        const auto projection = len2 < 1.0e-6f
            ? 0.0f : std::clamp((px * dx + py * dy) / len2, 0.0f, 1.0f);
        const auto ex = px - dx * projection;
        const auto ey = py - dy * projection;
        const auto deviation2 = ex * ex + ey * ey;
        const auto span2 = (vertices[i].pos.x - result.back().pos.x)
                              * (vertices[i].pos.x - result.back().pos.x)
                         + (vertices[i].pos.y - result.back().pos.y)
                              * (vertices[i].pos.y - result.back().pos.y);
        if (deviation2 > tolerancePx * tolerancePx || span2 > maxSkipPx * maxSkipPx
            || std::abs(vertices[i].amp - result.back().amp) > 0.08f)
            result.push_back(vertices[i]);
    }
    result.push_back(vertices.back());
    vertices.swap(result);
}

inline GradientTrace buildGradientTrace(const std::vector<TraceVertex>& vertices)
{
    GradientTrace result;
    if (vertices.size() < 2) return result;
    result.vertices.reserve(vertices.size());
    for (const auto& vertex : vertices)
        result.vertices.push_back(vertex.pos);
    // The command carries the post-decimation vertices. Native painters
    // reconstruct the Catmull-Rom controls from these points and stroke the
    // resulting path once, keeping gradient seams out of the display.
    result.gradStart = vertices.front().pos;
    result.gradEnd = vertices.back().pos;
    const auto axisX = result.gradEnd.x - result.gradStart.x;
    const auto axisY = result.gradEnd.y - result.gradStart.y;
    const auto len2 = axisX * axisX + axisY * axisY;
    std::vector<float> arc(vertices.size(), 0.0f);
    for (std::size_t i = 1; i < vertices.size(); ++i)
    {
        const auto dx = vertices[i].pos.x - vertices[i - 1].pos.x;
        const auto dy = vertices[i].pos.y - vertices[i - 1].pos.y;
        arc[i] = arc[i - 1] + std::sqrt(dx * dx + dy * dy);
    }
    const auto total = arc.back();
    auto fadeAt = [&](std::size_t i) {
        const auto in = std::min(1.0f, arc[i] / 7.0f);
        const auto out = std::min(1.0f, (total - arc[i]) / 7.0f);
        return 0.18f + 0.82f * in * out;
    };
    for (const auto& vertex : vertices) result.maxAmp = std::max(result.maxAmp, vertex.amp);
    if (len2 < 4.0f)
    {
        result.flat = true;
        float amp = 0.0f, shade = 0.0f;
        for (const auto& vertex : vertices) { amp += vertex.amp; shade += vertex.shade; }
        const auto fade = 0.18f + 0.82f * std::min(1.0f, total / 14.0f);
        result.stops.push_back({ 0.0f, amp / vertices.size(), shade / vertices.size(), fade });
        return result;
    }
    result.stops.push_back({ 0.0f, vertices.front().amp, vertices.front().shade, fadeAt(0) });
    for (std::size_t i = 1; i + 1 < vertices.size(); ++i)
    {
        const auto dx = vertices[i].pos.x - result.gradStart.x;
        const auto dy = vertices[i].pos.y - result.gradStart.y;
        const auto p = (dx * axisX + dy * axisY) / len2;
        const auto fade = fadeAt(i);
        const auto& last = result.stops.back();
        if (p <= last.proportion + 0.01f || p >= 0.995f
            || (std::abs(vertices[i].amp - last.amp) < 0.02f
                && std::abs(vertices[i].shade - last.shade) < 0.05f
                && std::abs(fade - last.fade) < 0.08f))
            continue;
        result.stops.push_back({ p, vertices[i].amp, vertices[i].shade, fade });
    }
    result.stops.push_back({ 1.0f, vertices.back().amp, vertices.back().shade,
                             fadeAt(vertices.size() - 1) });
    return result;
}

} // namespace ats::ui
