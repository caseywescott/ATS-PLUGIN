#pragma once

#include "EditorHost.h"
#include "SpectralTraceBuilder.h"

#include <array>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace ats::ui
{

struct SpectrumMenuItem
{
    int id = 0;
    SpectralTraceMode mode = SpectralTraceMode::combined;
    std::string_view label;
    bool checked = false;
    bool enabled = true;
};

struct SpectrumNavigationState
{
    float timeStart = 0.0f;
    float timeEnd = 1.0f;
    float frequencyStart = 0.0f;
    float frequencyEnd = 1.0f;
    bool pointerCaptured = false;
    int selectedPartial = -1;
    int selectedBand = -1;
};

class SpectrumInteractionModel
{
public:
    static constexpr std::size_t kModeCount = 7;

    [[nodiscard]] const SpectrumNavigationState& navigation() const noexcept { return navigation_; }
    [[nodiscard]] SpectralTraceSource source() const noexcept { return source_; }
    [[nodiscard]] SpectralTraceMode mode() const noexcept { return mode_; }

    void setSource(SpectralTraceSource source) noexcept { source_ = source; }
    void setMode(SpectralTraceMode mode) noexcept { mode_ = mode; }

    [[nodiscard]] std::array<SpectrumMenuItem, kModeCount> menu(bool hasSource,
                                                                 bool hasResidual) const noexcept
    {
        constexpr std::array<std::string_view, kModeCount> labels {
            "Combined", "Partials", "Residual", "Isometric", "Isometric residual",
            "Isometric envelope", "Isometric envelope RDC"
        };
        constexpr std::array<SpectralTraceMode, kModeCount> modes {
            SpectralTraceMode::combined, SpectralTraceMode::partials, SpectralTraceMode::residual,
            SpectralTraceMode::isometric, SpectralTraceMode::isometricResidual,
            SpectralTraceMode::isometricEnvelope, SpectralTraceMode::isometricEnvelopeRdc
        };
        std::array<SpectrumMenuItem, kModeCount> result{};
        for (std::size_t i = 0; i < kModeCount; ++i)
            result[i] = { static_cast<int>(i), modes[i], labels[i], modes[i] == mode_, hasSource
                && (modes[i] != SpectralTraceMode::residual || hasResidual) };
        return result;
    }

    [[nodiscard]] SetSpectralViewAction selectMenuItem(int id, bool hasSource,
                                                        bool hasResidual) const noexcept
    {
        const auto items = menu(hasSource, hasResidual);
        const auto index = id < 0 ? kModeCount : static_cast<std::size_t>(id);
        if (index >= items.size() || !items[index].enabled)
            return { sourceIndex(source_), -1 };
        return { sourceIndex(source_), static_cast<int>(items[index].mode) };
    }

    bool zoom(float factor, float anchorTime, float anchorFrequency) noexcept
    {
        if (!std::isfinite(factor) || factor <= 0.0f
            || !std::isfinite(anchorTime) || !std::isfinite(anchorFrequency))
            return false;
        anchorTime = std::clamp(anchorTime, 0.0f, 1.0f);
        anchorFrequency = std::clamp(anchorFrequency, 0.0f, 1.0f);
        zoomAxis(navigation_.timeStart, navigation_.timeEnd, factor, anchorTime);
        zoomAxis(navigation_.frequencyStart, navigation_.frequencyEnd, factor, anchorFrequency);
        return true;
    }

    bool pan(float deltaTime, float deltaFrequency) noexcept
    {
        if (!std::isfinite(deltaTime) || !std::isfinite(deltaFrequency))
            return false;
        panAxis(navigation_.timeStart, navigation_.timeEnd, deltaTime);
        panAxis(navigation_.frequencyStart, navigation_.frequencyEnd, deltaFrequency);
        return true;
    }

    void fit() noexcept { navigation_ = {}; }
    void setPointerCaptured(bool captured) noexcept { navigation_.pointerCaptured = captured; }

    bool selectPartial(int partial) noexcept
    {
        if (partial < -1)
            return false;
        navigation_.selectedPartial = partial;
        return true;
    }

    bool selectBand(int band) noexcept
    {
        if (band < -1)
            return false;
        navigation_.selectedBand = band;
        return true;
    }

    [[nodiscard]] PartialEditAction applyPartialEdit(int partial, int frame, bool apply) const noexcept
    { return { partial, frame, apply }; }

    [[nodiscard]] ResidualEditAction applyResidualEdit(int band, int frame, float value) const noexcept
    { return { band, frame, value }; }

    [[nodiscard]] EditSmearnovOnsetsAction setOnsetOverlay(std::vector<float> onsets) const
    { return { std::move(onsets) }; }

    static constexpr int sourceIndex(SpectralTraceSource source) noexcept
    {
        return source == SpectralTraceSource::sourceA ? 1
             : source == SpectralTraceSource::sourceB ? 2 : 0;
    }

private:
    static void zoomAxis(float& start, float& end, float factor, float anchor) noexcept
    {
        const auto width = (end - start) / factor;
        start = anchor - (anchor - start) / factor;
        end = start + width;
        clampAxis(start, end);
    }

    static void panAxis(float& start, float& end, float delta) noexcept
    {
        const auto width = end - start;
        start += delta;
        end += delta;
        clampAxis(start, end);
        if (end - start != width)
        {
            start = std::clamp(start, 0.0f, 1.0f - width);
            end = start + width;
        }
    }

    static void clampAxis(float& start, float& end) noexcept
    {
        const auto width = std::clamp(end - start, 0.0001f, 1.0f);
        if (start < 0.0f) { start = 0.0f; end = width; }
        if (end > 1.0f) { end = 1.0f; start = end - width; }
        start = std::clamp(start, 0.0f, 1.0f);
        end = std::clamp(end, start + 0.0001f, 1.0f);
    }

    SpectralTraceSource source_ = SpectralTraceSource::current;
    SpectralTraceMode mode_ = SpectralTraceMode::combined;
    SpectrumNavigationState navigation_{};
};

} // namespace ats::ui
