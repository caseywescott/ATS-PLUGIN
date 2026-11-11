#pragma once

#include "EditorHost.h"
#include "SpectralTraceBuilder.h"
#include "SpectrumGeometry.h"
#include "SpectrumPathBuilder.h"

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

struct SpectrumCapabilities
{
    bool hasCurrentSource = false;
    bool hasResidual = false;
    bool hasSourceA = false;
    bool hasSourceB = false;
    std::string_view currentName = "Current";
    std::string_view sourceAName = "Source A";
    std::string_view sourceBName = "Source B";
    std::array<bool, 3> residualAvailable{};

    [[nodiscard]] bool hasResidualForSource(int source) const noexcept
    {
        const auto any = residualAvailable[0] || residualAvailable[1] || residualAvailable[2];
        return !any ? hasResidual
                    : source >= 0 && source < static_cast<int>(residualAvailable.size())
                        && residualAvailable[static_cast<std::size_t>(source)];
    }
};

enum class OnsetHandleKind : std::uint8_t { none, marker, leftEdge, rightEdge };

struct OnsetHandle
{
    OnsetHandleKind kind = OnsetHandleKind::none;
    int index = -1;
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
    int hoveredResidualFrame = -1;
    int residualFirstFrame = 0;
    int residualLastFrame = 0;
    std::vector<int> selectedBands;
    std::vector<int> selectedPartials;
    bool envelopeEditing = false;
    float envelopeTimeStart = 0.0f;
    float envelopeTimeEnd = 1.0f;
    float envelopeFrequencyStart = 0.0f;
    float envelopeFrequencyEnd = 1.0f;
    float envRangeStartNorm = -1.0f;
    float envRangeEndNorm = -1.0f;
    float envFreqStartNorm = -1.0f;
    float envFreqEndNorm = -1.0f;
    bool lassoActive = false;
    UiRect lassoBounds{};
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

    [[nodiscard]] const SpectrumNavigationState& navigation() const noexcept { return navigation_; }
    [[nodiscard]] SpectrumViewport viewport() const noexcept { return currentViewport(); }
    [[nodiscard]] SpectralTraceSource source() const noexcept { return source_; }
    [[nodiscard]] SpectralTraceMode mode() const noexcept { return mode_; }

    void setSnapshot(const SpectralTraceSnapshot* snapshot, UiRect localBounds = {}) noexcept
    {
        const auto rebuilt = snapshot_ != snapshot || snapshot_ == nullptr || snapshot == nullptr
            || snapshotSourceRevision_ != snapshot->sourceRevision
            || snapshotDisplayShapeRevision_ != snapshot->displayShapeRevision;
        snapshot_ = snapshot;
        bounds_ = localBounds;
        if (snapshot_ == nullptr) return;
        snapshotSourceRevision_ = snapshot->sourceRevision;
        snapshotDisplayShapeRevision_ = snapshot->displayShapeRevision;
        // JUCE rebuilds the view from a fresh snapshot and drops transient
        // hover state.  Keeping it across the 10 Hz model publication makes a
        // stale readout survive after its trace/band has disappeared.
        if (!rebuilt) return;
        navigation_.hoveredPartial = -1;
        navigation_.hoveredBand = -1;
        navigation_.hoveredResidualFrame = -1;
        navigation_.selectedPartials.erase(
            std::remove_if(navigation_.selectedPartials.begin(), navigation_.selectedPartials.end(),
                [this](int index) {
                    return std::none_of(snapshot_->partials.begin(), snapshot_->partials.end(),
                        [index](const auto& trace) { return trace.partialIndex == index; });
                }), navigation_.selectedPartials.end());
        if (navigation_.selectedPartial >= 0
            && std::none_of(snapshot_->partials.begin(), snapshot_->partials.end(),
                [this](const auto& trace) { return trace.partialIndex == navigation_.selectedPartial; }))
            navigation_.selectedPartial = -1;
        navigation_.selectedBands.erase(
            std::remove_if(navigation_.selectedBands.begin(), navigation_.selectedBands.end(),
                [this](int index) {
                    return std::none_of(snapshot_->residualBands.begin(), snapshot_->residualBands.end(),
                        [index](const auto& band) { return band.bandIndex == index; });
                }), navigation_.selectedBands.end());
        if (navigation_.selectedBand >= 0
            && std::none_of(snapshot_->residualBands.begin(), snapshot_->residualBands.end(),
                [this](const auto& band) { return band.bandIndex == navigation_.selectedBand; }))
            navigation_.selectedBand = -1;
    }
    void setGeometry(UiRect localBounds) noexcept { bounds_ = localBounds; }
    [[nodiscard]] UiRect interactionPlotBounds() const noexcept { return plotBounds(); }
    void clampViewport() noexcept
    { clampAxis(navigation_.timeStart, navigation_.timeEnd); clampAxis(navigation_.frequencyStart, navigation_.frequencyEnd); touch(); }

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
        navigation_.hoveredResidualFrame = -1;
        navigation_.envelopeEditing = false;
        touch();
    }
    void setMode(SpectralTraceMode mode) noexcept
    {
        if (mode_ == mode) return;
        mode_ = mode;
        // The legacy panel changes painters without changing its zoomed
        // viewport.  Only a source replacement calls fit().
        navigation_.selectedPartial = -1;
        navigation_.selectedBand = -1;
        navigation_.selectedBands.clear();
        navigation_.selectedPartials.clear();
        navigation_.hoveredPartial = -1;
        navigation_.hoveredBand = -1;
        navigation_.hoveredResidualFrame = -1;
        navigation_.envelopeEditing = false;
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

    [[nodiscard]] std::array<SpectrumMenuItem, kModeCount> menu(
        const SpectrumCapabilities& capabilities) const noexcept
    {
        auto result = menu(capabilities.hasCurrentSource || capabilities.hasSourceA || capabilities.hasSourceB,
                           capabilities.hasResidualForSource(sourceIndex(source_)));
        return result;
    }

    [[nodiscard]] std::array<std::string_view, 3> sourceMenuLabels(
        const SpectrumCapabilities& capabilities) const noexcept
    {
        return { capabilities.currentName,
                 capabilities.hasSourceA ? capabilities.sourceAName : std::string_view {},
                 capabilities.hasSourceB ? capabilities.sourceBName : std::string_view {} };
    }

    [[nodiscard]] SetSpectralViewAction selectMenuItem(int id, bool hasSource,
                                                        bool hasResidual) const noexcept
    {
        if (id >= 100)
        {
            const auto source = id / 100 - 1;
            const auto mode = id % 100 - 1;
            if (source < 0 || source > 2 || mode < 0 || mode >= static_cast<int>(kModeCount)
                || !hasSource || (mode == static_cast<int>(SpectralTraceMode::residual) && !hasResidual))
                return { sourceIndex(source_), -1 };
            return { source, mode };
        }
        const auto items = menu(hasSource, hasResidual);
        const auto index = id < 0 ? kModeCount : static_cast<std::size_t>(id);
        if (index >= items.size() || !items[index].enabled)
            return { sourceIndex(source_), -1 };
        return { sourceIndex(source_), static_cast<int>(items[index].mode) };
    }

    [[nodiscard]] SetSpectralViewAction selectSourceMenuItem(
        int source, int mode, const SpectrumCapabilities& capabilities) const noexcept
    {
        const auto sourceAvailable = source == 0 ? capabilities.hasCurrentSource
            : source == 1 ? capabilities.hasSourceA : source == 2 ? capabilities.hasSourceB : false;
        if (!sourceAvailable || mode < 0 || mode >= static_cast<int>(kModeCount))
            return { sourceIndex(source_), -1 };
        if (mode == static_cast<int>(SpectralTraceMode::residual)
            && !capabilities.hasResidualForSource(source))
            return { sourceIndex(source_), -1 };
        return { source, mode };
    }

    bool zoom(float factor, float anchorTime, float anchorFrequency) noexcept
    { return zoomAxes(factor, factor, anchorTime, anchorFrequency); }

    bool zoomAxes(float timeFactor, float frequencyFactor,
                  float anchorTime, float anchorFrequency) noexcept
    {
        if (!std::isfinite(timeFactor) || timeFactor <= 0.0f
            || !std::isfinite(frequencyFactor) || frequencyFactor <= 0.0f
            || !std::isfinite(anchorTime) || !std::isfinite(anchorFrequency))
            return false;
        anchorTime = std::clamp(anchorTime, 0.0f, 1.0f);
        anchorFrequency = std::clamp(anchorFrequency, 0.0f, 1.0f);
        zoomAxis(navigation_.timeStart, navigation_.timeEnd, timeFactor, anchorTime);
        zoomAxis(navigation_.frequencyStart, navigation_.frequencyEnd, frequencyFactor, anchorFrequency);
        touch();
        return true;
    }

    // Apply magnification using the pointer's planar screen position.  The
    // legacy JUCE panel deliberately uses its X/Y time/frequency mapping for
    // pinch gestures, even while an isometric painter is active; using the
    // isometric inverse here would make the gesture jump to a different
    // anchor when switching display modes.
    bool zoomAtScreenPoint(UiPoint point, float timeFactor, float frequencyFactor) noexcept
    {
        const auto plot = plotBounds();
        if (plot.width <= 1.0f || plot.height <= 1.0f
            || point.x < plot.x || point.x >= plot.x + plot.width
            || point.y < plot.y || point.y >= plot.y + plot.height)
            return false;
        if (!std::isfinite(timeFactor) || timeFactor <= 0.0f
            || !std::isfinite(frequencyFactor) || frequencyFactor <= 0.0f)
            return false;

        return zoomAxes(timeFactor, frequencyFactor,
                        timeNormFromScreenX(point.x), freqNormFromScreenY(point.y));
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

    void beginPan(UiPoint point) noexcept
    {
        panStart_ = point;
        panStartTimeCenter_ = (navigation_.timeStart + navigation_.timeEnd) * 0.5f;
        panStartFrequencyCenter_ = (navigation_.frequencyStart + navigation_.frequencyEnd) * 0.5f;
        panActive_ = true;
    }

    bool updatePan(UiPoint point) noexcept
    {
        if (!panActive_ || bounds_.width <= 1.0f || bounds_.height <= 1.0f)
            return false;
        const auto plot = plotBounds();
        if (plot.width <= 1.0f || plot.height <= 1.0f)
            return false;
        const auto timeRange = SpectrumGeometry::visibleTimeRange(currentViewport());
        const auto freqRange = SpectrumGeometry::visibleFrequencyRange(currentViewport());
        const auto dx = point.x - panStart_.x;
        const auto dy = point.y - panStart_.y;
        const auto timeCenter = panStartTimeCenter_ - dx / plot.width * timeRange.length();
        const auto frequencyCenter = panStartFrequencyCenter_ + dy / plot.height * freqRange.length();
        const auto timeSpan = timeRange.length();
        const auto freqSpan = freqRange.length();
        navigation_.timeStart = timeCenter - timeSpan * 0.5f;
        navigation_.timeEnd = timeCenter + timeSpan * 0.5f;
        navigation_.frequencyStart = frequencyCenter - freqSpan * 0.5f;
        navigation_.frequencyEnd = frequencyCenter + freqSpan * 0.5f;
        clampAxis(navigation_.timeStart, navigation_.timeEnd);
        clampAxis(navigation_.frequencyStart, navigation_.frequencyEnd);
        touch();
        return true;
    }

    void endPan() noexcept { panActive_ = false; }

    bool scrollTimeTo(float center) noexcept
    {
        return scrollAxis(navigation_.timeStart, navigation_.timeEnd, center);
    }

    bool scrollFrequencyTo(float center) noexcept
    {
        return scrollAxis(navigation_.frequencyStart, navigation_.frequencyEnd, center);
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
        navigation_.selectedPartials.clear();
        if (partial >= 0) navigation_.selectedPartials.push_back(partial);
        touch();
        return true;
    }

    void setSelectedPartials(std::vector<int> partials) noexcept
    {
        navigation_.selectedPartials.clear();
        for (const auto partial : partials)
            if (partial >= 0 && std::find(navigation_.selectedPartials.begin(),
                                          navigation_.selectedPartials.end(), partial)
                == navigation_.selectedPartials.end())
                navigation_.selectedPartials.push_back(partial);
        navigation_.selectedPartial = navigation_.selectedPartials.size() == 1
            ? navigation_.selectedPartials.front() : -1;
        touch();
    }

    bool togglePartialSelection(int partial) noexcept
    {
        if (partial < 0) return false;
        const auto it = std::find(navigation_.selectedPartials.begin(),
                                  navigation_.selectedPartials.end(), partial);
        if (it == navigation_.selectedPartials.end())
            navigation_.selectedPartials.push_back(partial);
        else
            navigation_.selectedPartials.erase(it);
        navigation_.selectedPartial = navigation_.selectedPartials.size() == 1
            ? navigation_.selectedPartials.front() : -1;
        touch();
        return true;
    }

    std::size_t selectPartialsInLasso(UiRect lasso, bool additive = false) noexcept
    {
        if (snapshot_ == nullptr) return 0;
        if (!additive) navigation_.selectedPartials.clear();
        for (const auto& trace : snapshot_->partials)
        {
            if (!traceIntersectsLasso(trace, lasso)) continue;
            if (std::find(navigation_.selectedPartials.begin(),
                          navigation_.selectedPartials.end(), trace.partialIndex)
                == navigation_.selectedPartials.end())
                navigation_.selectedPartials.push_back(trace.partialIndex);
        }
        navigation_.selectedPartial = navigation_.selectedPartials.size() == 1
            ? navigation_.selectedPartials.front() : -1;
        touch();
        return navigation_.selectedPartials.size();
    }

    void beginLasso(UiPoint start) noexcept
    {
        navigation_.lassoActive = true;
        navigation_.lassoBounds = { start.x, start.y, 0.0f, 0.0f };
        touch();
    }

    void updateLasso(UiPoint current) noexcept
    {
        if (!navigation_.lassoActive) return;
        const auto start = UiPoint { navigation_.lassoBounds.x, navigation_.lassoBounds.y };
        navigation_.lassoBounds = normalizedRect(start, current);
        touch();
    }

    std::size_t endLasso(bool additive = false) noexcept
    {
        if (!navigation_.lassoActive) return navigation_.selectedPartials.size();
        navigation_.lassoActive = false;
        const auto selected = selectPartialsInLasso(navigation_.lassoBounds, additive);
        navigation_.lassoBounds = {};
        touch();
        return selected;
    }

    bool selectBand(int band) noexcept
    {
        if (band < -1)
            return false;
        navigation_.selectedBand = band;
        navigation_.selectedBands.clear();
        if (band >= 0)
            navigation_.selectedBands.push_back(band);
        touch();
        return true;
    }

    bool toggleBandSelection(int band) noexcept
    {
        if (band < 0) return false;
        const auto it = std::find(navigation_.selectedBands.begin(),
                                  navigation_.selectedBands.end(), band);
        if (it == navigation_.selectedBands.end())
            navigation_.selectedBands.push_back(band);
        else
            navigation_.selectedBands.erase(it);
        navigation_.selectedBand = navigation_.selectedBands.size() == 1
            ? navigation_.selectedBands.front() : -1;
        touch();
        return true;
    }

    void setHoveredPartial(int partial) noexcept
    { navigation_.hoveredPartial = partial >= 0 ? partial : -1; touch(); }

    void setHoveredBand(int band) noexcept
    { navigation_.hoveredBand = band >= 0 ? band : -1; touch(); }

    [[nodiscard]] int findNearestPartial(UiPoint point, float maxDistancePx) const noexcept
    {
        if (snapshot_ == nullptr || maxDistancePx < 0.0f) return -1;
        const auto plot = plotBounds();
        if (!containsExpanded(plot, point, maxDistancePx)) return -1;
        const auto viewport = currentViewport();
        const auto iso = SpectrumGeometry::isIsometricDisplayMode(mode_);
        float best = maxDistancePx;
        int result = -1;
        // The isometric painter culls points outside the visible range (they
        // would project beside the floor); never pick what is not drawn.
        const auto timeRange = SpectrumGeometry::visibleTimeRange(viewport);
        const auto freqRange = SpectrumGeometry::visibleFrequencyRange(viewport);
        const auto drawn = [&](const SpectralTracePoint& p) {
            return p.active && (!iso
                || (p.timeNorm >= timeRange.start - timeRange.length() * 0.025f
                    && p.timeNorm <= timeRange.end + timeRange.length() * 0.025f
                    && p.freqNorm >= freqRange.start - freqRange.length() * 0.04f
                    && p.freqNorm <= freqRange.end + freqRange.length() * 0.04f));
        };
        for (const auto& trace : snapshot_->partials)
            for (std::size_t i = 1; i < trace.points.size(); ++i)
            {
                const auto& a = trace.points[i - 1];
                const auto& b = trace.points[i];
                if (!drawn(a) || !drawn(b)) continue;
                const auto pa = iso ? SpectrumGeometry::positionForIsoCoords(a.timeNorm, a.freqNorm, a.ampNorm, plot, viewport)
                                    : SpectrumGeometry::positionForPoint(a, plot, viewport);
                const auto pb = iso ? SpectrumGeometry::positionForIsoCoords(b.timeNorm, b.freqNorm, b.ampNorm, plot, viewport)
                                    : SpectrumGeometry::positionForPoint(b, plot, viewport);
                const auto distance = spectrum_path_detail::distanceToSegment(point, pa, pb);
                if (distance < best) { best = distance; result = trace.partialIndex; }
            }
        return result;
    }

    [[nodiscard]] int findNearestPartial(UiPoint point, UiRect plot, float maxDistancePx) const noexcept
    { auto copy = *this; copy.bounds_ = plot; return copy.findNearestPartial(point, maxDistancePx); }

    [[nodiscard]] int residualBandForY(float y) const noexcept
    {
        if (snapshot_ == nullptr) return -1;
        const auto plot = plotBounds();
        int nearest = -1;
        float nearestDistance = 8.0f;
        for (const auto& band : snapshot_->residualBands)
        {
            const auto range = yRangeForBand(band, plot);
            if (y >= range.start - 1.5f && y <= range.end + 1.5f) return band.bandIndex;
            const auto distance = std::abs(y - (range.start + range.end) * 0.5f);
            if (distance < nearestDistance)
            {
                nearestDistance = distance;
                nearest = band.bandIndex;
            }
        }
        return nearest;
    }
    [[nodiscard]] int residualBandForPoint(UiPoint point) const noexcept
    {
        if (snapshot_ == nullptr) return -1;
        const auto plot = plotBounds();
        if (mode_ == SpectralTraceMode::residual && point.x < plot.x)
            return residualBandForGutterPoint(point);
        if (mode_ == SpectralTraceMode::isometricResidual)
        {
            if (!containsExpanded(plot, point, 2.0f)) return -1;
            const auto timeRange = SpectrumGeometry::visibleTimeRange(currentViewport());
            const auto freqRange = SpectrumGeometry::visibleFrequencyRange(currentViewport());
            int bestBand = -1;
            float bestDistance = 16.0f;
            const auto minHz = std::max(1.0, snapshot_->minFrequencyHz);
            const auto maxHz = std::max(minHz * 1.2, snapshot_->maxFrequencyHz);
            const auto logRange = std::max(1.0e-6, std::log(maxHz / minHz));
            const auto normForHz = [&](double hz) {
                return static_cast<float>(std::clamp(
                    std::log(std::clamp(std::max(1.0, hz), minHz, maxHz) / minHz) / logRange, 0.0, 1.0));
            };
            for (const auto& band : snapshot_->residualBands)
            {
                // Same band placement as the isometric residual painter.
                const auto freq = std::clamp(0.5f * (normForHz(band.lowHz) + normForHz(band.highHz)),
                                             0.0f, 1.0f);
                if (freq < freqRange.start - freqRange.length() * 0.05f
                    || freq > freqRange.end + freqRange.length() * 0.05f)
                    continue;
                const auto& energy = band.energyNorm;
                if (energy.size() < 2) continue;
                std::optional<UiPoint> previousBase;
                std::optional<UiPoint> previousRidge;
                for (std::size_t c = 0; c < energy.size(); ++c)
                {
                    const auto time = SpectralTraceBuilder::residualColumnTimeNorm(
                        c, energy.size(), snapshot_->frameCount);
                    if (time < timeRange.start - timeRange.length() * 0.03f
                        || time > timeRange.end + timeRange.length() * 0.03f)
                        continue;
                    const auto base = SpectrumGeometry::positionForIsoCoords(time, freq, 0.0f,
                                                                                plot, currentViewport());
                    const auto ridge = SpectrumGeometry::positionForIsoCoords(time, freq,
                        std::clamp(energy[c] * 1.15f, 0.0f, 1.15f), plot, currentViewport());
                    if (previousBase && previousRidge)
                    {
                        const auto distance = std::min(
                            spectrum_path_detail::distanceToSegment(point, *previousBase, base),
                            spectrum_path_detail::distanceToSegment(point, *previousRidge, ridge));
                        if (distance < bestDistance) { bestDistance = distance; bestBand = band.bandIndex; }
                    }
                    previousBase = base;
                    previousRidge = ridge;
                }
            }
            return bestBand;
        }
        return residualBandForY(point.y);
    }
    [[nodiscard]] int residualBandForGutterPoint(UiPoint point) const noexcept
    {
        if (snapshot_ == nullptr) return -1;
        const auto plot = plotBounds();
        int nearest = -1;
        float distance = 14.0f;
        for (const auto& band : snapshot_->residualBands)
        {
            const auto range = yRangeForBand(band, plot);
            const auto centre = (range.start + range.end) * 0.5f;
            if (std::abs(point.y - centre) <= 9.0f) return band.bandIndex;
            if (std::abs(point.y - centre) < distance)
            { distance = std::abs(point.y - centre); nearest = band.bandIndex; }
        }
        return nearest;
    }
    [[nodiscard]] std::vector<int> residualBandsForRect(UiRect rect) const
    {
        std::vector<int> result;
        if (snapshot_ == nullptr) return result;
        const auto plot = plotBounds();
        for (const auto& band : snapshot_->residualBands)
        {
            const auto range = yRangeForBand(band, plot);
            if (rect.y < range.end && rect.y + rect.height > range.start)
                result.push_back(band.bandIndex);
        }
        return result;
    }
    [[nodiscard]] int frameForX(float x) const noexcept
    {
        if (snapshot_ == nullptr || snapshot_->frameCount == 0) return -1;
        return frameForPoint({ x, plotBounds().y + plotBounds().height * 0.5f });
    }
    [[nodiscard]] int frameForPoint(UiPoint point) const noexcept
    {
        if (snapshot_ == nullptr || snapshot_->frameCount == 0) return -1;
        return std::clamp(static_cast<int>(std::lround(
            timeNormFromScreenPoint(point) * static_cast<float>(snapshot_->frameCount - 1))),
            0, static_cast<int>(snapshot_->frameCount - 1));
    }
    [[nodiscard]] float timeNormFromScreenX(float x) const noexcept
    {
        const auto plot = plotBounds();
        const auto range = SpectrumGeometry::visibleTimeRange(currentViewport());
        return std::clamp(range.start + std::clamp((x - plot.x) / std::max(1.0f, plot.width), 0.0f, 1.0f) * range.length(), 0.0f, 1.0f);
    }
    [[nodiscard]] float timeNormFromScreenPoint(UiPoint point) const noexcept
    {
        if (SpectrumGeometry::isIsometricDisplayMode(mode_))
        {
            float time = 0.0f, frequency = 0.0f;
            SpectrumGeometry::isoTimeFreqFromPoint(point, plotBounds(), currentViewport(), time, frequency);
            return time;
        }
        return timeNormFromScreenX(point.x);
    }
    [[nodiscard]] float freqNormFromScreenY(float y) const noexcept
    {
        const auto plot = plotBounds();
        const auto range = SpectrumGeometry::visibleFrequencyRange(currentViewport());
        return std::clamp(range.start + std::clamp((plot.y + plot.height - y) / std::max(1.0f, plot.height), 0.0f, 1.0f) * range.length(), 0.0f, 1.0f);
    }
    [[nodiscard]] float freqNormFromScreenPoint(UiPoint point) const noexcept
    {
        if (SpectrumGeometry::isIsometricDisplayMode(mode_))
        {
            float time = 0.0f, frequency = 0.0f;
            SpectrumGeometry::isoTimeFreqFromPoint(point, plotBounds(), currentViewport(), time, frequency);
            return frequency;
        }
        return freqNormFromScreenY(point.y);
    }
    [[nodiscard]] bool traceIntersectsLasso(const SpectralTrace& trace, UiRect lasso) const noexcept
    {
        if (snapshot_ == nullptr) return false;
        const auto plot = plotBounds(); const auto viewport = currentViewport();
        std::optional<UiPoint> previous;
        for (const auto& point : trace.points)
        {
            if (!point.active) { previous.reset(); continue; }
            const auto p = SpectrumGeometry::isIsometricDisplayMode(mode_)
                ? SpectrumGeometry::positionForIsoCoords(point.timeNorm, point.freqNorm, point.ampNorm, plot, viewport)
                : SpectrumGeometry::positionForPoint(point, plot, viewport);
            if (pointInRect(p, lasso)
                || (previous.has_value() && segmentIntersectsRect(*previous, p, lasso)))
                return true;
            previous = p;
        }
        return false;
    }

    [[nodiscard]] OnsetHandle onsetHandleAtPoint(UiPoint point, float maxDistancePx) const noexcept
    {
        OnsetHandle result;
        if (snapshot_ == nullptr || maxDistancePx < 0.0f) return result;
        const auto plot = plotBounds();
        std::vector<SpectralOnset> legacy;
        if (snapshot_->onsets.empty())
        {
            legacy.reserve(snapshot_->onsetNorms.size());
            for (const auto value : snapshot_->onsetNorms)
                legacy.push_back({ value, value, value, 0.0f });
        }
        const auto& onsets = snapshot_->onsets.empty() ? legacy : snapshot_->onsets;
        const auto timeRange = SpectrumGeometry::visibleTimeRange(currentViewport());
        float best = maxDistancePx;
        for (std::size_t i = 0; i < onsets.size(); ++i)
        {
            const auto candidates = std::array<std::pair<float, OnsetHandleKind>, 3> {{
                { onsets[i].norm, OnsetHandleKind::marker },
                { onsets[i].preNorm, OnsetHandleKind::leftEdge },
                { onsets[i].peakNorm, OnsetHandleKind::rightEdge }
            }};
            for (const auto& candidate : candidates)
            {
                if (candidate.second != OnsetHandleKind::marker && candidate.first < 0.0f) continue;
                // Same viewport mapping the renderer uses for the markers.
                const auto x = plot.x + plot.width * (std::clamp(candidate.first, 0.0f, 1.0f)
                                                          - timeRange.start)
                    / std::max(1.0e-6f, timeRange.length());
                const auto distance = std::abs(point.x - x);
                if (distance < best && point.y >= plot.y - maxDistancePx
                    && point.y <= plot.y + plot.height + maxDistancePx)
                {
                    best = distance;
                    result = { candidate.second, static_cast<int>(i) };
                }
            }
        }
        return result;
    }

    [[nodiscard]] SpectrumEditAction partialCommand(SpectrumEditCommand command, float value = 0.0f) const
    {
        const auto lastFrame = snapshot_ == nullptr || snapshot_->frameCount == 0
            ? 0 : static_cast<int>(snapshot_->frameCount) - 1;
        return { command, navigation_.selectedPartials, 0, lastFrame, value };
    }

    [[nodiscard]] SpectrumEditAction runPartialPrune(float thresholdPercent) const
    {
        auto result = partialCommand(SpectrumEditCommand::deletePartials);
        result.bands.clear();
        if (snapshot_ == nullptr || !std::isfinite(thresholdPercent)) return result;
        for (const auto& trace : snapshot_->partials)
            if (trace.peakAmpNorm < std::max(0.0f, thresholdPercent) / 100.0f)
                result.bands.push_back(trace.partialIndex);
        return result;
    }

    bool selectResidualRange(std::vector<int> bands, int firstFrame, int lastFrame) noexcept
    {
        if (firstFrame < 0 || lastFrame < firstFrame)
            return false;
        navigation_.selectedBands.clear();
        for (const auto band : bands)
            if (band >= 0 && std::find(navigation_.selectedBands.begin(),
                                       navigation_.selectedBands.end(), band)
                == navigation_.selectedBands.end())
                navigation_.selectedBands.push_back(band);
        navigation_.selectedBand       = navigation_.selectedBands.size() == 1
            ? navigation_.selectedBands.front() : -1;
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
        navigation_.envRangeStartNorm = navigation_.envelopeTimeStart;
        navigation_.envRangeEndNorm = navigation_.envelopeTimeEnd;
        navigation_.envFreqStartNorm = navigation_.envelopeFrequencyStart;
        navigation_.envFreqEndNorm = navigation_.envelopeFrequencyEnd;
        touch();
    }

    void clearEnvelope() noexcept
    {
        navigation_.envelopeEditing = false;
        navigation_.envRangeStartNorm = navigation_.envRangeEndNorm = -1.0f;
        navigation_.envFreqStartNorm = navigation_.envFreqEndNorm = -1.0f;
        touch();
    }

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
        if (keyCode == kKeyDelete || keyCode == kKeyBackspace)
        {
            if (mode_ == SpectralTraceMode::residual
                || mode_ == SpectralTraceMode::isometricResidual)
            {
                auto bands = navigation_.selectedBands;
                if (bands.empty() && navigation_.selectedBand >= 0)
                    bands.push_back(navigation_.selectedBand);
                if (!bands.empty())
                    return EditorAction{ SpectrumEditAction {
                        SpectrumEditCommand::muteResidual, std::move(bands),
                        navigation_.residualFirstFrame, navigation_.residualLastFrame, 0.0f } };
            }
            else
            {
                auto partials = navigation_.selectedPartials;
                if (partials.empty() && navigation_.selectedPartial >= 0)
                    partials.push_back(navigation_.selectedPartial);
                if (!partials.empty())
                    return EditorAction{ SpectrumEditAction {
                        SpectrumEditCommand::deletePartials, std::move(partials),
                        0, snapshot_ == nullptr || snapshot_->frameCount == 0
                            ? 0 : static_cast<int>(snapshot_->frameCount) - 1, 0.0f } };
            }
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
    [[nodiscard]] static UiRect normalizedRect(UiPoint a, UiPoint b) noexcept
    {
        return { std::min(a.x, b.x), std::min(a.y, b.y),
                 std::abs(b.x - a.x), std::abs(b.y - a.y) };
    }
    [[nodiscard]] static bool pointInRect(UiPoint p, UiRect r) noexcept
    { return p.x >= r.x && p.x <= r.x + r.width && p.y >= r.y && p.y <= r.y + r.height; }

    [[nodiscard]] static bool segmentIntersectsRect(UiPoint a, UiPoint b, UiRect r) noexcept
    {
        if (pointInRect(a, r) || pointInRect(b, r)) return true;
        const auto minX = std::min(a.x, b.x), maxX = std::max(a.x, b.x);
        const auto minY = std::min(a.y, b.y), maxY = std::max(a.y, b.y);
        if (maxX < r.x || minX > r.x + r.width || maxY < r.y || minY > r.y + r.height)
            return false;
        const auto crosses = [](UiPoint p0, UiPoint p1, UiPoint q0, UiPoint q1) {
            const auto cross = [](UiPoint u, UiPoint v) { return u.x * v.y - u.y * v.x; };
            const UiPoint a { p1.x - p0.x, p1.y - p0.y };
            const UiPoint b { q1.x - q0.x, q1.y - q0.y };
            const UiPoint c { q0.x - p0.x, q0.y - p0.y };
            const auto denominator = cross(a, b);
            if (std::abs(denominator) < 1.0e-6f)
                return std::abs(cross(c, a)) < 1.0e-5f;
            const auto t = cross(c, b) / denominator;
            const auto u = cross(c, a) / denominator;
            return t >= 0.0f && t <= 1.0f && u >= 0.0f && u <= 1.0f;
        };
        const UiPoint topLeft { r.x, r.y }, topRight { r.x + r.width, r.y };
        const UiPoint bottomRight { r.x + r.width, r.y + r.height }, bottomLeft { r.x, r.y + r.height };
        return crosses(a, b, topLeft, topRight) || crosses(a, b, topRight, bottomRight)
            || crosses(a, b, bottomRight, bottomLeft) || crosses(a, b, bottomLeft, topLeft);
    }

    [[nodiscard]] SpectrumViewport currentViewport() const noexcept
    {
        const auto timeSpan = std::max(1.0e-6f, navigation_.timeEnd - navigation_.timeStart);
        const auto freqSpan = std::max(1.0e-6f, navigation_.frequencyEnd - navigation_.frequencyStart);
        return { (navigation_.timeStart + navigation_.timeEnd) * 0.5f, 1.0f / timeSpan,
                 (navigation_.frequencyStart + navigation_.frequencyEnd) * 0.5f, 1.0f / freqSpan };
    }
    [[nodiscard]] UiRect plotBounds() const noexcept
    { return snapshot_ == nullptr ? bounds_ : SpectrumGeometry::getPlotBounds(bounds_, mode_, snapshot_->residualBands.size(), snapshot_->frameCount); }
    [[nodiscard]] static bool containsExpanded(UiRect r, UiPoint p, float amount) noexcept
    { return p.x >= r.x - amount && p.x <= r.x + r.width + amount && p.y >= r.y - amount && p.y <= r.y + r.height + amount; }
    [[nodiscard]] SpectrumRange yRangeForBand(const SpectralResidualBandTrace& band, UiRect plot) const noexcept
    {
        const auto minHz = std::max(1.0, snapshot_->minFrequencyHz);
        const auto maxHz = std::max(minHz * 1.2, snapshot_->maxFrequencyHz);
        const auto logRange = std::log(maxHz / minHz);
        const auto normalized = [&](double hz) {
            return static_cast<float>(std::clamp(std::log(std::clamp(hz, minHz, maxHz) / minHz) / logRange, 0.0, 1.0));
        };
        const auto freq = SpectrumGeometry::visibleFrequencyRange(currentViewport());
        const auto y = [&](float norm) { return plot.y + plot.height - plot.height * (norm - freq.start) / std::max(1.0e-6f, freq.length()); };
        const auto a = y(normalized(band.highHz)), b = y(normalized(band.lowHz));
        return { std::min(a, b), std::max(a, b) };
    }
    void touch() noexcept { ++navigation_.revision; }

    bool scrollAxis(float& start, float& end, float center) noexcept
    {
        if (!std::isfinite(center)) return false;
        const auto span = std::clamp(end - start, 1.0e-6f, 1.0f);
        const auto boundedCenter = std::clamp(center, span * 0.5f, 1.0f - span * 0.5f);
        start = boundedCenter - span * 0.5f;
        end = boundedCenter + span * 0.5f;
        touch();
        return true;
    }

    static void zoomAxis(float& start, float& end, float factor, float anchor) noexcept
    {
        const auto currentWidth = std::clamp(end - start,
                                             1.0f / SpectrumGeometry::kMaxZoom,
                                             1.0f / SpectrumGeometry::kMinZoom);
        const auto currentZoom = 1.0f / currentWidth;
        const auto targetZoom = std::clamp(currentZoom * factor,
                                           SpectrumGeometry::kMinZoom,
                                           SpectrumGeometry::kMaxZoom);
        const auto effectiveFactor = targetZoom / currentZoom;
        const auto width = currentWidth / effectiveFactor;
        start = anchor - (anchor - start) / effectiveFactor;
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
        const auto width = std::clamp(end - start,
                                     1.0f / SpectrumGeometry::kMaxZoom,
                                     1.0f / SpectrumGeometry::kMinZoom);
        if (start < 0.0f) { start = 0.0f; end = width; }
        if (end > 1.0f) { end = 1.0f; start = end - width; }
        start = std::clamp(start, 0.0f, 1.0f);
        end = std::clamp(end, start + 1.0f / SpectrumGeometry::kMaxZoom, 1.0f);
    }

    SpectralTraceSource source_ = SpectralTraceSource::current;
    SpectralTraceMode mode_ = SpectralTraceMode::combined;
    SpectrumNavigationState navigation_{};
    std::vector<int> availablePartials_;
    std::vector<int> availableBands_;
    const SpectralTraceSnapshot* snapshot_ = nullptr;
    UiRect bounds_{};
    std::uint64_t snapshotSourceRevision_ = 0;
    std::uint64_t snapshotDisplayShapeRevision_ = 0;
    bool panActive_ = false;
    UiPoint panStart_{};
    float panStartTimeCenter_ = 0.5f;
    float panStartFrequencyCenter_ = 0.5f;
};

} // namespace ats::ui
