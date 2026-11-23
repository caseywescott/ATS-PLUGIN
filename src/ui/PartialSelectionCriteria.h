#pragma once

#include "SpectralTraceBuilder.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ats::ui
{

/**
    Select-by-criteria for partials, adapted from pyats (selection.py
    select_by_criteria): every enabled filter must hold for a partial's
    whole-trace summary (mean frequency, mean level, active duration, mean
    SMR).  An unset bound is no constraint; criteria with nothing set select
    nothing.
*/
struct PartialCriteria
{
    std::optional<double> minFrequencyHz;
    std::optional<double> maxFrequencyHz;
    std::optional<double> minSmrDb;
    std::optional<double> minLevelDb;
    std::optional<double> minSeconds;
    std::optional<double> maxSeconds;

    [[nodiscard]] bool empty() const noexcept
    {
        return !minFrequencyHz && !maxFrequencyHz && !minSmrDb && !minLevelDb && !minSeconds
            && !maxSeconds;
    }
};

/** A criteria field: blank (or whitespace) is "no constraint"; anything else
    must be a finite number, otherwise the field is invalid (nullopt + false). */
[[nodiscard]] inline bool parseCriterion(std::string_view text, std::optional<double>& out)
{
    out.reset();
    const auto first = text.find_first_not_of(" \t");
    if (first == std::string_view::npos)
        return true;
    const auto last = text.find_last_not_of(" \t");
    const std::string trimmed(text.substr(first, last - first + 1));
    char* end = nullptr;
    const double value = std::strtod(trimmed.c_str(), &end);
    if (end == trimmed.c_str() || *end != '\0' || !std::isfinite(value))
        return false;
    out = value;
    return true;
}

/** Partial indices (SpectralTrace::partialIndex) matching every set bound. */
[[nodiscard]] inline std::vector<int> matchPartials(const SpectralTraceSnapshot& snapshot,
                                                    const PartialCriteria& criteria)
{
    std::vector<int> result;
    if (criteria.empty())
        return result;
    auto lowHz = criteria.minFrequencyHz, highHz = criteria.maxFrequencyHz;
    if (lowHz && highHz && *lowHz > *highHz)
        std::swap(lowHz, highHz);
    auto shortest = criteria.minSeconds, longest = criteria.maxSeconds;
    if (shortest && longest && *shortest > *longest)
        std::swap(shortest, longest);
    for (const auto& trace : snapshot.partials)
    {
        if (!(trace.activeSeconds > 0.0))
            continue;
        const auto hz = trace.meanFrequencyHz;
        if (lowHz && !(hz >= *lowHz)) continue;
        if (highHz && !(hz <= *highHz)) continue;
        if (criteria.minLevelDb && !(trace.meanLevelDb >= *criteria.minLevelDb)) continue;
        if (shortest && !(trace.activeSeconds >= *shortest)) continue;
        if (longest && !(trace.activeSeconds <= *longest)) continue;
        // An unknown SMR never satisfies an SMR bound.
        if (criteria.minSmrDb && !(trace.meanSmrDb >= *criteria.minSmrDb)) continue;
        result.push_back(trace.partialIndex);
    }
    std::sort(result.begin(), result.end());
    return result;
}

/** The displayed partials not in `selected`. */
[[nodiscard]] inline std::vector<int> invertPartials(const SpectralTraceSnapshot& snapshot,
                                                     const std::vector<int>& selected)
{
    std::vector<int> result;
    for (const auto& trace : snapshot.partials)
        if (std::find(selected.begin(), selected.end(), trace.partialIndex) == selected.end())
            result.push_back(trace.partialIndex);
    std::sort(result.begin(), result.end());
    return result;
}

/** Sorted union of two partial-index sets. */
[[nodiscard]] inline std::vector<int> unitePartials(std::vector<int> a, const std::vector<int>& b)
{
    a.insert(a.end(), b.begin(), b.end());
    std::sort(a.begin(), a.end());
    a.erase(std::unique(a.begin(), a.end()), a.end());
    return a;
}

} // namespace ats::ui
