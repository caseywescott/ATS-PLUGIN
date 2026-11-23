#pragma once

#include "UiBackend.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

namespace ats::ui
{

/** The SESSION timeline's editable curves: the morph amount and the three
    render-time automation curves (SoundCurveKind order after MORPH). */
enum class CurveTab : std::uint8_t { morph, pitch, timeWarp, spectralWarp };

struct CurvePoint
{
    float x = 0.0f; // normalised time
    float y = 0.0f; // curve units
    // MORPH only: easing of the segment that starts here (MorphCurveShape
    // order: 0 linear, 1 ease in, 2 ease out, 3 ease in-out).
    std::uint8_t shape = 0;
};

struct CurveRange
{
    float minY = 0.0f;
    float maxY = 1.0f;
    float identity = 0.0f;   // value drawn when the curve is empty
    bool diagonal = false;   // identity is y = x (time warp)
    const char* label = "";
    const char* unit = "";
};

/**
    Node-editor logic for the SESSION timeline (spec §8.2): hit-testing,
    click to add a point (and drag it), double-click a point to remove it,
    drag with neighbours as limits, and the "{t}s  {value}" readout.  Pure
    so it can be tested without a window; VisageEditor draws it and turns a
    finished edit into SetMorphTimelineAction / SetSoundCurveAction.
*/
class CurveEditorModel
{
public:
    static constexpr float kHitRadius = 7.0f;
    static constexpr std::size_t kMaxPoints = 64;

    [[nodiscard]] static CurveRange range(CurveTab tab) noexcept
    {
        switch (tab)
        {
        case CurveTab::morph: return { 0.0f, 1.0f, 0.0f, false, "MORPH", "" };
        case CurveTab::pitch: return { -24.0f, 24.0f, 0.0f, false, "PITCH", "st" };
        case CurveTab::timeWarp: return { 0.0f, 1.0f, 0.0f, true, "TIME", "" };
        case CurveTab::spectralWarp: return { 0.5f, 2.0f, 1.0f, false, "WARP", "" };
        }
        return {};
    }

    void setPlot(UiRect plot) noexcept { plot_ = plot; }
    [[nodiscard]] UiRect plot() const noexcept { return plot_; }

    void setTab(CurveTab tab) noexcept { tab_ = tab; dragging_ = -1; }
    [[nodiscard]] CurveTab tab() const noexcept { return tab_; }

    /** Replaces a curve from the host unless that curve is being dragged. */
    void setPoints(CurveTab tab, std::vector<CurvePoint> points)
    {
        if (dragging_ >= 0 && tab == tab_)
            return;
        points_[index(tab)] = std::move(points);
    }
    [[nodiscard]] const std::vector<CurvePoint>& points(CurveTab tab) const noexcept { return points_[index(tab)]; }
    [[nodiscard]] const std::vector<CurvePoint>& points() const noexcept { return points(tab_); }
    [[nodiscard]] int dragging() const noexcept { return dragging_; }

    [[nodiscard]] UiPoint toScreen(CurvePoint p) const noexcept
    {
        const auto r = range(tab_);
        const auto v = (p.y - r.minY) / std::max(1.0e-6f, r.maxY - r.minY);
        return { plot_.x + plot_.width * p.x, plot_.y + plot_.height * (1.0f - v) };
    }

    [[nodiscard]] CurvePoint fromScreen(UiPoint s) const noexcept
    {
        const auto r = range(tab_);
        const auto x = std::clamp((s.x - plot_.x) / std::max(1.0f, plot_.width), 0.0f, 1.0f);
        const auto v = std::clamp(1.0f - (s.y - plot_.y) / std::max(1.0f, plot_.height), 0.0f, 1.0f);
        return { x, r.minY + v * (r.maxY - r.minY) };
    }

    /** Index of the point within kHitRadius of `s`, or -1. */
    [[nodiscard]] int hit(UiPoint s) const noexcept
    {
        int best = -1;
        float bestDistance = kHitRadius;
        const auto& pts = points();
        for (std::size_t i = 0; i < pts.size(); ++i)
        {
            const auto p = toScreen(pts[i]);
            const auto d = std::hypot(p.x - s.x, p.y - s.y);
            if (d <= bestDistance) { bestDistance = d; best = static_cast<int>(i); }
        }
        return best;
    }

    [[nodiscard]] bool inPlot(UiPoint s) const noexcept
    {
        return s.x >= plot_.x - kHitRadius && s.x <= plot_.x + plot_.width + kHitRadius
            && s.y >= plot_.y - kHitRadius && s.y <= plot_.y + plot_.height + kHitRadius;
    }

    /**
        A press in the plot: double-click on a point removes it; a press on a
        point starts dragging it; a press elsewhere adds a point there and
        starts dragging it.  Returns true when the curve changed (removal or
        addition); a drag's change is reported by release().
    */
    bool press(UiPoint s, bool doubleClick)
    {
        if (!inPlot(s))
            return false;
        auto& pts = points_[index(tab_)];
        const auto at = hit(s);
        if (at >= 0)
        {
            if (doubleClick)
            {
                pts.erase(pts.begin() + at);
                dragging_ = -1;
                return true;
            }
            dragging_ = at;
            return false;
        }
        if (pts.size() >= kMaxPoints)
            return false;
        // An empty time warp is the diagonal; seed it so one new point bends
        // it instead of freezing playback at a single position.
        if (pts.empty() && range(tab_).diagonal)
            pts = { { 0.0f, 0.0f }, { 1.0f, 1.0f } };
        const auto p = fromScreen(s);
        const auto insert = std::upper_bound(pts.begin(), pts.end(), p.x,
            [](float x, const CurvePoint& q) { return x < q.x; });
        // Insert first: it may reallocate, so begin() must be taken afterwards.
        const auto inserted = pts.insert(insert, p);
        dragging_ = static_cast<int>(std::distance(pts.begin(), inserted));
        return true;
    }

    /** Moves the dragged point; x stays between its neighbours. */
    void drag(UiPoint s)
    {
        if (dragging_ < 0)
            return;
        auto& pts = points_[index(tab_)];
        auto p = fromScreen(s);
        const auto i = static_cast<std::size_t>(dragging_);
        const auto lo = i > 0 ? pts[i - 1].x : 0.0f;
        const auto hi = i + 1 < pts.size() ? pts[i + 1].x : 1.0f;
        p.x = std::clamp(p.x, lo, hi);
        pts[i] = p;
        moved_ = true;
    }

    /** Ends a drag; true when the dragged point moved. */
    bool release() noexcept
    {
        const auto changed = dragging_ >= 0 && moved_;
        dragging_ = -1;
        moved_ = false;
        return changed;
    }

    /** MORPH segment easing, matching applyMorphCurve in AtsMorphRecipe.cpp. */
    [[nodiscard]] static double ease(double t, std::uint8_t shape) noexcept
    {
        t = std::clamp(t, 0.0, 1.0);
        switch (shape)
        {
        case 1: return t * t;
        case 2: return 1.0 - (1.0 - t) * (1.0 - t);
        case 3: return t * t * (3.0 - 2.0 * t);
        default: return t;
        }
    }

    [[nodiscard]] static const char* shapeName(std::uint8_t shape) noexcept
    {
        switch (shape)
        {
        case 1: return "ease in";
        case 2: return "ease out";
        case 3: return "ease in-out";
        default: return "linear";
        }
    }

    /** MORPH: cycles the easing of the segment starting at the point under
        `s`.  True when a point was hit. */
    bool cycleShape(UiPoint s)
    {
        if (tab_ != CurveTab::morph)
            return false;
        const auto at = hit(s);
        if (at < 0)
            return false;
        auto& point = points_[index(tab_)][static_cast<std::size_t>(at)];
        point.shape = static_cast<std::uint8_t>((point.shape + 1) % 4);
        return true;
    }

    /** Screen polyline of the current curve, held to both ends, with MORPH
        segments sampled through their easing. */
    [[nodiscard]] std::vector<UiPoint> polyline() const
    {
        std::vector<UiPoint> line;
        const auto& pts = points();
        if (pts.empty())
            return line;
        line.push_back(toScreen({ 0.0f, pts.front().y }));
        for (std::size_t i = 0; i < pts.size(); ++i)
        {
            if (i > 0 && tab_ == CurveTab::morph && pts[i - 1].shape != 0)
                for (int k = 1; k < 16; ++k)
                {
                    const auto t = k / 16.0;
                    const auto e = ease(t, pts[i - 1].shape);
                    line.push_back(toScreen({ static_cast<float>(pts[i - 1].x + t * (pts[i].x - pts[i - 1].x)),
                                              static_cast<float>(pts[i - 1].y + e * (pts[i].y - pts[i - 1].y)) }));
                }
            line.push_back(toScreen(pts[i]));
        }
        line.push_back(toScreen({ 1.0f, pts.back().y }));
        return line;
    }

    /** Empties the current curve (its identity), or MORPH back to 0 -> 1. */
    void reset()
    {
        dragging_ = -1;
        auto& pts = points_[index(tab_)];
        pts.clear();
        if (tab_ == CurveTab::morph)
            pts = { { 0.0f, 0.0f }, { 1.0f, 1.0f } };
    }

    /** "{t}s  {value}" for the dragged point; empty when not dragging. */
    [[nodiscard]] std::string readout(double durationSeconds) const
    {
        if (dragging_ < 0 || dragging_ >= static_cast<int>(points().size()))
            return {};
        return formatPoint(points()[static_cast<std::size_t>(dragging_)], durationSeconds);
    }

    [[nodiscard]] std::string formatPoint(CurvePoint p, double durationSeconds) const
    {
        char text[64];
        const auto t = durationSeconds > 0.0 ? p.x * durationSeconds : p.x;
        const char* unit = durationSeconds > 0.0 ? "s" : "";
        switch (tab_)
        {
        case CurveTab::morph: std::snprintf(text, sizeof text, "%.2f%s  %.2f", t, unit, p.y); break;
        case CurveTab::pitch: std::snprintf(text, sizeof text, "%.2f%s  %+.1f st", t, unit, p.y); break;
        case CurveTab::timeWarp:
            std::snprintf(text, sizeof text, "%.2f%s  -> %.2f%s", t, unit,
                          durationSeconds > 0.0 ? p.y * durationSeconds : p.y, unit);
            break;
        case CurveTab::spectralWarp: std::snprintf(text, sizeof text, "%.2f%s  x%.2f", t, unit, p.y); break;
        }
        return text;
    }

    /** Flattened x, y pairs (SetSoundCurveAction / state layout). */
    [[nodiscard]] static std::vector<float> flatten(const std::vector<CurvePoint>& pts)
    {
        std::vector<float> out;
        out.reserve(pts.size() * 2);
        for (const auto& p : pts) { out.push_back(p.x); out.push_back(p.y); }
        return out;
    }

    [[nodiscard]] static std::vector<CurvePoint> unflatten(const std::vector<float>& flat)
    {
        std::vector<CurvePoint> out;
        for (std::size_t i = 0; i + 1 < flat.size(); i += 2)
            out.push_back({ flat[i], flat[i + 1] });
        return out;
    }

private:
    [[nodiscard]] static std::size_t index(CurveTab tab) noexcept { return static_cast<std::size_t>(tab); }

    UiRect plot_ {};
    CurveTab tab_ = CurveTab::morph;
    std::array<std::vector<CurvePoint>, 4> points_ = [] {
        std::array<std::vector<CurvePoint>, 4> initial;
        initial[static_cast<std::size_t>(CurveTab::morph)] = { CurvePoint { 0.0f, 0.0f }, CurvePoint { 1.0f, 1.0f } };
        return initial;
    }();
    int dragging_ = -1;
    bool moved_ = false;
};

} // namespace ats::ui
