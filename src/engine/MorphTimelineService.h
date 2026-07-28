#pragma once

#include "../core/AtsMorphRecipe.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace ats::engine
{

enum class TimelinePointKind : std::uint8_t { morph, hybrid, envelopeMorph };

struct TimelinePoint
{
    double timeNorm = 0.0;
    double amount = 0.0;
    TimelinePointKind kind = TimelinePointKind::morph;
    ats::MorphCurveShape curve = ats::MorphCurveShape::Linear;

    friend bool operator==(const TimelinePoint& a, const TimelinePoint& b) noexcept
    {
        return a.timeNorm == b.timeNorm && a.amount == b.amount && a.kind == b.kind && a.curve == b.curve;
    }
    friend bool operator!=(const TimelinePoint& a, const TimelinePoint& b) noexcept
    { return !(a == b); }
};

enum class TimelineEditOperation : std::uint8_t { add, move, remove, changeKind };

struct TimelineEdit
{
    TimelineEditOperation operation = TimelineEditOperation::add;
    std::size_t pointIndex = 0;
    TimelinePoint point{};
    TimelinePointKind newKind = TimelinePointKind::morph;
};

struct TimelineEditResult
{
    bool success = false;
    bool changed = false;
    std::vector<TimelinePoint> points;
    std::string error;
};

class MorphTimelineService
{
public:
    static constexpr std::size_t kMaximumPoints = 32;
    static constexpr double kDuplicateTimeTolerance = 1.0e-4;
    static constexpr double kEndpointTolerance = 1.0e-6;

    [[nodiscard]] static std::vector<TimelinePoint> sanitize(std::vector<TimelinePoint> points)
    {
        if (points.empty()) points = { { 0.0, 0.0 }, { 1.0, 1.0 } };
        for (auto& point : points)
        {
            point.timeNorm = clampUnit(point.timeNorm);
            point.amount = clampUnit(point.amount);
        }
        sortPoints(points);
        std::vector<TimelinePoint> unique;
        unique.reserve(std::min(points.size(), kMaximumPoints));
        for (const auto& point : points)
        {
            auto existing = std::find_if(unique.begin(), unique.end(), [&point](const auto& value) {
                return value.kind == point.kind
                    && std::abs(value.timeNorm - point.timeNorm) <= kDuplicateTimeTolerance;
            });
            if (existing != unique.end()) *existing = point;
            else if (unique.size() < kMaximumPoints) unique.push_back(point);
        }

        bool hasMorph = false;
        bool hasStart = false;
        bool hasEnd = false;
        for (auto& point : unique)
        {
            if (point.kind != TimelinePointKind::morph) continue;
            hasMorph = true;
            if (point.timeNorm <= kDuplicateTimeTolerance) { point.timeNorm = 0.0; hasStart = true; }
            if (point.timeNorm >= 1.0 - kDuplicateTimeTolerance) { point.timeNorm = 1.0; hasEnd = true; }
        }
        if (!hasMorph) unique.push_back({ 0.0, 0.0 });
        else
        {
            if (!hasStart) unique.push_back({ 0.0, 0.0 });
            if (!hasEnd) unique.push_back({ 1.0, 1.0 });
        }
        sortPoints(unique);
        if (unique.size() > kMaximumPoints) unique.resize(kMaximumPoints);
        return unique;
    }

    [[nodiscard]] static double evaluate(const std::vector<TimelinePoint>& input,
                                          TimelinePointKind kind,
                                          double timeNorm,
                                          bool applyCurves = true)
    {
        const auto points = lane(sanitize(input), kind);
        if (points.empty()) return 0.0;
        const auto time = clampUnit(timeNorm);
        if (time <= points.front().timeNorm) return points.front().amount;
        for (std::size_t i = 1; i < points.size(); ++i)
        {
            if (time <= points[i].timeNorm)
            {
                const auto span = points[i].timeNorm - points[i - 1].timeNorm;
                if (span <= 1.0e-9) return points[i].amount;
                auto t = (time - points[i - 1].timeNorm) / span;
                if (applyCurves) t = ats::applyMorphCurve(t, points[i - 1].curve);
                return points[i - 1].amount + t * (points[i].amount - points[i - 1].amount);
            }
        }
        return points.back().amount;
    }

    [[nodiscard]] static bool constantEndpointSource(const std::vector<TimelinePoint>& input,
                                                      int& sourceIndex)
    {
        bool hasMorph = false;
        bool hasEffect = false;
        bool allA = true;
        bool allB = true;
        for (const auto& point : sanitize(input))
        {
            if (point.kind != TimelinePointKind::morph) { hasEffect = true; continue; }
            hasMorph = true;
            allA = allA && point.amount <= kEndpointTolerance;
            allB = allB && point.amount >= 1.0 - kEndpointTolerance;
        }
        if (hasEffect || !hasMorph) { sourceIndex = -1; return false; }
        if (allA) { sourceIndex = 0; return true; }
        if (allB) { sourceIndex = 1; return true; }
        sourceIndex = -1;
        return false;
    }

    [[nodiscard]] static TimelineEditResult edit(const std::vector<TimelinePoint>& input,
                                                  const TimelineEdit& edit)
    {
        auto points = sanitize(input);
        if (edit.operation == TimelineEditOperation::add)
        {
            if (points.size() >= kMaximumPoints) return failure("Timeline point limit reached.");
            points.push_back(edit.point);
        }
        else if (edit.pointIndex >= points.size())
            return failure("Timeline point index is out of range.");
        else if (edit.operation == TimelineEditOperation::remove)
        {
            const auto& point = points[edit.pointIndex];
            if (point.kind == TimelinePointKind::morph
                && (point.timeNorm <= kEndpointTolerance || point.timeNorm >= 1.0 - kEndpointTolerance))
                return failure("Morph endpoints are protected.");
            points.erase(points.begin() + static_cast<std::ptrdiff_t>(edit.pointIndex));
        }
        else if (edit.operation == TimelineEditOperation::changeKind)
            points[edit.pointIndex].kind = edit.newKind;
        else if (edit.operation == TimelineEditOperation::move)
        {
            auto& point = points[edit.pointIndex];
            auto time = clampUnit(edit.point.timeNorm);
            const auto sameLane = lane(sanitize(input), point.kind);
            for (const auto& neighbor : sameLane)
            {
                if (neighbor.timeNorm < point.timeNorm) time = std::max(time, neighbor.timeNorm + kDuplicateTimeTolerance);
                if (neighbor.timeNorm > point.timeNorm) time = std::min(time, neighbor.timeNorm - kDuplicateTimeTolerance);
            }
            if (point.kind == TimelinePointKind::morph && point.timeNorm <= kEndpointTolerance) time = 0.0;
            if (point.kind == TimelinePointKind::morph && point.timeNorm >= 1.0 - kEndpointTolerance) time = 1.0;
            point.timeNorm = time;
            point.amount = clampUnit(edit.point.amount);
            point.curve = edit.point.curve;
        }
        auto safe = sanitize(std::move(points));
        return { true, safe != sanitize(input), std::move(safe), {} };
    }

private:
    [[nodiscard]] static double clampUnit(double value) noexcept
    { return std::isfinite(value) ? std::clamp(value, 0.0, 1.0) : 0.0; }

    static void sortPoints(std::vector<TimelinePoint>& points)
    {
        std::stable_sort(points.begin(), points.end(), [](const auto& a, const auto& b) {
            if (a.timeNorm != b.timeNorm) return a.timeNorm < b.timeNorm;
            return static_cast<int>(a.kind) < static_cast<int>(b.kind);
        });
    }

    [[nodiscard]] static std::vector<TimelinePoint> lane(const std::vector<TimelinePoint>& points,
                                                         TimelinePointKind kind)
    {
        std::vector<TimelinePoint> result;
        for (const auto& point : points) if (point.kind == kind) result.push_back(point);
        return result;
    }

    [[nodiscard]] static TimelineEditResult failure(std::string error)
    { return { false, false, {}, std::move(error) }; }
};

} // namespace ats::engine
