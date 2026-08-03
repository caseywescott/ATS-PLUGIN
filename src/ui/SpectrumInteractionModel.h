#pragma once

#include "EditorHost.h"
#include "SpectralTraceBuilder.h"

#include <array>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
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
    int hoveredPartial = -1;
    int hoveredBand = -1;
    int residualFirstFrame = 0;
    int residualLastFrame = 0;
    std::vector<int> selectedBands;
    std::vector<int> selectedPartials;
    bool envelopeEditing = false;
    float envelopeTimeStart = 0.0f;
    float envelopeTimeEnd = 1.0f;
    float envelopeFrequencyStart = 0.0f;
    float envelopeFrequencyEnd = 1.0f;
    std::uint64_t revision = 0;
};

class SpectrumInteractionModel
{
public:
    static constexpr std::size_t kModeCount = 7;

    static constexpr int kKeyDelete    = 127;
    static constexpr int kKeyBackspace = 8;
    static constexpr int kKeyEscape    = 27;
    static constexpr int kKeyHome      = 36;
    static constexpr int kKeyFit       = '0';
    static constexpr int kKeyReturn    = 13;
    static constexpr int kKeyH         = 'h';

    [[nodiscard]] const SpectrumNavigationState& navigation() const noexcept { return navigation_; }
    [[nodiscard]] SpectralTraceSource source() const noexcept { return source_; }
    [[nodiscard]] SpectralTraceMode mode() const noexcept { return mode_; }

    void setSource(SpectralTraceSource source) noexcept
    {
        if (source_ == source) return;
        source_ = source;
        fit();
        navigation_.selectedPartial = -1;
        navigation_.selectedBand = -1;
        navigation_.selectedBands.clear();
        navigation_.selectedPartials.clear();
        navigation_.hoveredPartial = -1;
        navigation_.hoveredBand = -1;
        touch();
    }
    void setMode(SpectralTraceMode mode) noexcept
    {
        if (mode_ == mode) return;
        mode_ = mode;
        fit();
        navigation_.selectedPartial = -1;
        navigation_.selectedBand = -1;
        navigation_.selectedBands.clear();
        navigation_.selectedPartials.clear();
        touch();
    }
    [[nodiscard]] std::uint64_t revision() const noexcept { return navigation_.revision; }

    [[nodiscard]] std::array<SpectrumMenuItem, kModeCount> menu(bool hasSource,
                                                                 bool hasResidual) const noexcept
    {
        constexpr std::array<std::string_view, kModeCount> labels {
            "Trace + Residual", "Partials", "Residual", "Iso 3D", "Iso 3D Residual",
            "Iso 3D Envelope", "Iso 3D Env (RDC)"
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
        touch();
        return true;
    }

    bool pan(float deltaTime, float deltaFrequency) noexcept
    {
        if (!std::isfinite(deltaTime) || !std::isfinite(deltaFrequency))
            return false;
        panAxis(navigation_.timeStart, navigation_.timeEnd, deltaTime);
        panAxis(navigation_.frequencyStart, navigation_.frequencyEnd, deltaFrequency);
        touch();
        return true;
    }

    void fit() noexcept
    {
        navigation_.timeStart = 0.0f;
        navigation_.timeEnd = 1.0f;
        navigation_.frequencyStart = 0.0f;
        navigation_.frequencyEnd = 1.0f;
        touch();
    }
    void setPointerCaptured(bool captured) noexcept
    { navigation_.pointerCaptured = captured; touch(); }

    bool selectPartial(int partial) noexcept
    {
        if (partial < -1)
            return false;
        navigation_.selectedPartial = partial;
        touch();
        return true;
    }

    bool selectBand(int band) noexcept
    {
        if (band < -1)
            return false;
        navigation_.selectedBand = band;
        touch();
        return true;
    }

    void setHoveredPartial(int partial) noexcept
    { navigation_.hoveredPartial = partial >= 0 ? partial : -1; touch(); }

    void setHoveredBand(int band) noexcept
    { navigation_.hoveredBand = band >= 0 ? band : -1; touch(); }

    bool selectResidualRange(std::vector<int> bands, int firstFrame, int lastFrame) noexcept
    {
        if (firstFrame < 0 || lastFrame < firstFrame)
            return false;
        navigation_.selectedBands      = std::move(bands);
        navigation_.residualFirstFrame = firstFrame;
        navigation_.residualLastFrame  = lastFrame;
        touch();
        return true;
    }

    void setAvailablePartials(std::vector<int> partials) { availablePartials_ = std::move(partials); }
    void setAvailableBands(std::vector<int> bands) { availableBands_ = std::move(bands); }

    void setEnvelopeRange(float timeStart, float timeEnd,
                          float frequencyStart, float frequencyEnd) noexcept
    {
        if (!std::isfinite(timeStart) || !std::isfinite(timeEnd)
            || !std::isfinite(frequencyStart) || !std::isfinite(frequencyEnd))
            return;
        navigation_.envelopeEditing = true;
        navigation_.envelopeTimeStart = std::clamp(std::min(timeStart, timeEnd), 0.0f, 1.0f);
        navigation_.envelopeTimeEnd = std::clamp(std::max(timeStart, timeEnd), 0.0f, 1.0f);
        navigation_.envelopeFrequencyStart = std::clamp(std::min(frequencyStart, frequencyEnd), 0.0f, 1.0f);
        navigation_.envelopeFrequencyEnd = std::clamp(std::max(frequencyStart, frequencyEnd), 0.0f, 1.0f);
        touch();
    }

    void clearEnvelope() noexcept { navigation_.envelopeEditing = false; touch(); }

    [[nodiscard]] std::optional<EditorAction> handleKey(int keyCode, bool shift, bool cmd) noexcept
    {
        if (keyCode == kKeyReturn)
            return std::nullopt; // text editors commit at this boundary.
        if (keyCode == kKeyFit || keyCode == kKeyHome)
        {
            fit();
            return std::nullopt;
        }
        if (keyCode == kKeyEscape)
        {
            navigation_.selectedPartial = -1;
            navigation_.selectedBand    = -1;
            navigation_.selectedBands.clear();
            navigation_.selectedPartials.clear();
            navigation_.hoveredPartial  = -1;
            navigation_.hoveredBand     = -1;
            navigation_.envelopeEditing = false;
            touch();
            return std::nullopt;
        }
        if (cmd && (keyCode == 'a' || keyCode == 'A'))
        {
            navigation_.selectedBands = availableBands_;
            navigation_.selectedPartials = availablePartials_;
            if (navigation_.selectedBands.empty() && navigation_.selectedBand >= 0)
                navigation_.selectedBands.push_back(navigation_.selectedBand);
            if (navigation_.selectedPartials.empty() && navigation_.hoveredPartial >= 0)
                navigation_.selectedPartials.push_back(navigation_.hoveredPartial);
            touch();
            return std::nullopt;
        }
        if (cmd && navigation_.selectedBands.size() > 0 &&
            (keyCode == 'c' || keyCode == 'C' || keyCode == 'v' || keyCode == 'V'
             || keyCode == 'x' || keyCode == 'X' || keyCode == 's' || keyCode == 'S'
             || keyCode == 't' || keyCode == 'T' || keyCode == 'r' || keyCode == 'R'))
        {
            const auto command = keyCode == 'c' || keyCode == 'C'
                ? SpectrumEditCommand::copyResidual
                : keyCode == 'v' || keyCode == 'V'
                    ? SpectrumEditCommand::pasteResidual
                    : keyCode == 'x' || keyCode == 'X' ? SpectrumEditCommand::swapResidual
                    : keyCode == 's' || keyCode == 'S' ? SpectrumEditCommand::smoothResidual
                    : keyCode == 't' || keyCode == 'T' ? SpectrumEditCommand::tiltResidual
                    : SpectrumEditCommand::redistResidual;
            return EditorAction{ SpectrumEditAction { command, navigation_.selectedBands,
                                                       navigation_.residualFirstFrame,
                                                       navigation_.residualLastFrame, 0.0f } };
        }
        if ((keyCode == kKeyH || keyCode == 'H') && navigation_.hoveredPartial >= 0)
        {
            if (!shift)
                navigation_.selectedPartials.clear();
            for (const auto partial : availablePartials_)
                if (partial == navigation_.hoveredPartial || partial % 12 == navigation_.hoveredPartial % 12)
                    navigation_.selectedPartials.push_back(partial);
            if (navigation_.selectedPartials.empty())
                navigation_.selectedPartials.push_back(navigation_.hoveredPartial);
            touch();
            return std::nullopt;
        }
        if (keyCode == kKeyDelete || keyCode == kKeyBackspace)
        {
            if (navigation_.selectedPartial >= 0)
                return EditorAction{ PartialEditAction{ navigation_.selectedPartial, 0, false } };
            if (navigation_.selectedBand >= 0)
                return EditorAction{ ResidualEditAction{ navigation_.selectedBand, 0, 0.0f } };
            if (!navigation_.selectedBands.empty())
                return EditorAction{ ResidualEditAction{ navigation_.selectedBands.front(),
                                                          navigation_.residualFirstFrame, 0.0f } };
        }
        return std::nullopt;
    }

    [[nodiscard]] EditSmearnovOnsetsAction toggleOnset(
        std::vector<float> onsets, float norm, float snapRadius = 0.02f) const
    {
        if (!std::isfinite(norm))
            return { std::move(onsets) };
        norm = std::clamp(norm, 0.0f, 1.0f);
        snapRadius = std::max(0.0f, std::isfinite(snapRadius) ? snapRadius : 0.02f);
        onsets.erase(std::remove_if(onsets.begin(), onsets.end(),
            [](float value) { return !std::isfinite(value); }), onsets.end());
        for (auto& onset : onsets)
            onset = std::clamp(onset, 0.0f, 1.0f);
        auto it = std::find_if(onsets.begin(), onsets.end(),
            [&](float v) { return std::abs(v - norm) <= snapRadius; });
        if (it != onsets.end())
            onsets.erase(it);
        else
        {
            onsets.push_back(norm);
            std::sort(onsets.begin(), onsets.end());
        }
        return { std::move(onsets) };
    }

    [[nodiscard]] PartialEditAction applyPartialEdit(int partial, int frame, bool apply) const noexcept
    { return { partial, frame, apply }; }

    [[nodiscard]] ResidualEditAction applyResidualEdit(int band, int frame, float value) const noexcept
    { return { band, frame, value }; }

    [[nodiscard]] ResidualEditAction applyEnvelopeGain(int band, int frame, float gainLinear) const noexcept
    { return { band, frame, std::isfinite(gainLinear) && gainLinear > 0.0f ? gainLinear : 1.0f }; }

    [[nodiscard]] EditSmearnovOnsetsAction setOnsetOverlay(std::vector<float> onsets) const
    {
        onsets.erase(std::remove_if(onsets.begin(), onsets.end(),
            [](float value) { return !std::isfinite(value); }), onsets.end());
        for (auto& onset : onsets)
            onset = std::clamp(onset, 0.0f, 1.0f);
        std::sort(onsets.begin(), onsets.end());
        return { std::move(onsets) };
    }

    static constexpr int sourceIndex(SpectralTraceSource source) noexcept
    {
        return source == SpectralTraceSource::sourceA ? 1
             : source == SpectralTraceSource::sourceB ? 2 : 0;
    }

private:
    void touch() noexcept { ++navigation_.revision; }

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
    std::vector<int> availablePartials_;
    std::vector<int> availableBands_;
};

} // namespace ats::ui
