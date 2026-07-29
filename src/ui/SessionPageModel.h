#pragma once

#include "EditorHost.h"

#include <array>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace ats::ui
{

// Framework-free state for the Session page.  It is deliberately small and
// descriptive: the backend owns widgets, while this model owns enablement,
// selection, and the action payloads those widgets produce.
enum class SessionSource : std::uint8_t { current, sourceA, sourceB };
enum class SessionLayer : std::uint8_t { both, partials, residual, smear };
enum class MorphLane : std::uint8_t { morph, hybrid, envelope };
enum class MorphPolicy : std::uint8_t { hold, fade, mergeToResidual, discard };

struct SessionSourceCard
{
    bool hasModel = false;
    bool analyzed = false;
    bool dropTarget = true;
    std::string name;
    float durationSeconds = 0.0f;
    std::uint32_t partialCount = 0;
    bool hasResidual = false;
};

struct SessionTransportState
{
    SessionSource source = SessionSource::current;
    SessionLayer layer = SessionLayer::both;
    bool playing = false;
    bool smearEnabled = false;
    float normalizedPlayhead = 0.0f;
};

struct SessionMorphState
{
    MorphLane selectedLane = MorphLane::morph;
    std::array<bool, 3> variationStored{};
    std::vector<float> timeline { 0.0f, 1.0f };
    std::array<MorphPolicy, 4> policies {
        MorphPolicy::fade, MorphPolicy::fade, MorphPolicy::mergeToResidual, MorphPolicy::hold
    };
    bool discardOptIn = false;
    std::size_t selectedBand = 0;
    std::size_t bandCount = 0;
    bool bounceInFlight = false;
};

struct SessionTimbreState
{
    float brightness = 0.5f;
    float tilt = 0.5f;
    float tonalNoise = 0.5f;
    std::vector<float> curve { 0.0f, 0.5f, 1.0f };
};

struct SessionSmearnovState
{
    bool panelVisible = false;
    bool knobsExpanded = false;
    bool advancedExpanded = false;
    bool detectionAvailable = false;
    bool detectionInFlight = false;
    bool applyInFlight = false;
    std::vector<float> onsetOverlay;
};

struct SessionPageState
{
    std::array<SessionSourceCard, 2> sources{};
    std::string presetName;
    bool presetDirty = false;
    bool presetSaveEnabled = false;
    SessionTransportState transport{};
    SessionMorphState morph{};
    SessionTimbreState timbre{};
    SessionSmearnovState smearnov{};
};

class SessionPageModel
{
public:
    static constexpr std::size_t kSourceCount = 2;
    static constexpr std::size_t kVariationCount = 3;
    static constexpr std::size_t kPolicyCount = 4;

    [[nodiscard]] const SessionPageState& state() const noexcept { return state_; }

    void setSource(std::size_t slot, SessionSourceCard card)
    {
        if (slot < kSourceCount)
            state_.sources[slot] = std::move(card);
    }

    void setPreset(std::string name, bool dirty)
    {
        state_.presetName = std::move(name);
        state_.presetDirty = dirty;
        state_.presetSaveEnabled = dirty && !state_.presetName.empty();
    }

    void setTransport(SessionSource source, SessionLayer layer) noexcept
    {
        state_.transport.source = source;
        state_.transport.layer = layer;
    }

    bool setPlayhead(float normalized) noexcept
    {
        if (normalized < 0.0f || normalized > 1.0f)
            return false;
        state_.transport.normalizedPlayhead = normalized;
        return true;
    }

    void setPlaying(bool playing) noexcept { state_.transport.playing = playing; }
    void setSmearEnabled(bool enabled) noexcept { state_.transport.smearEnabled = enabled; }

    bool setTimeline(std::vector<float> points)
    {
        if (points.empty() || points.size() > 32)
            return false;
        for (std::size_t i = 0; i < points.size(); ++i)
        {
            if (points[i] < 0.0f || points[i] > 1.0f || (i > 0 && points[i] < points[i - 1]))
                return false;
        }
        state_.morph.timeline = std::move(points);
        return true;
    }

    bool setSelectedBand(std::size_t band) noexcept
    {
        if (band >= state_.morph.bandCount)
            return false;
        state_.morph.selectedBand = band;
        return true;
    }

    void setBandCount(std::size_t count) noexcept
    {
        state_.morph.bandCount = count;
        if (count == 0)
            state_.morph.selectedBand = 0;
        else if (state_.morph.selectedBand >= count)
            state_.morph.selectedBand = count - 1;
    }

    bool setVariationStored(std::size_t index, bool stored) noexcept
    {
        if (index >= kVariationCount)
            return false;
        state_.morph.variationStored[index] = stored;
        return true;
    }

    bool setPolicy(std::size_t index, MorphPolicy policy) noexcept
    {
        if (index >= kPolicyCount)
            return false;
        if (policy == MorphPolicy::discard && !state_.morph.discardOptIn)
            return false;
        state_.morph.policies[index] = policy;
        return true;
    }

    void setDiscardOptIn(bool enabled) noexcept { state_.morph.discardOptIn = enabled; }

    void setTimbre(float brightness, float tilt, float tonalNoise,
                   std::vector<float> curve)
    {
        state_.timbre.brightness = brightness;
        state_.timbre.tilt = tilt;
        state_.timbre.tonalNoise = tonalNoise;
        state_.timbre.curve = std::move(curve);
    }

    void setSmearnovPanel(bool visible, bool knobsExpanded, bool advancedExpanded) noexcept
    {
        state_.smearnov.panelVisible = visible;
        state_.smearnov.knobsExpanded = knobsExpanded;
        state_.smearnov.advancedExpanded = advancedExpanded;
    }

    void setSmearnovDetection(bool available, bool inFlight) noexcept
    {
        state_.smearnov.detectionAvailable = available;
        state_.smearnov.detectionInFlight = inFlight;
    }

    void setSmearnovApplyInFlight(bool inFlight) noexcept { state_.smearnov.applyInFlight = inFlight; }

    void setOnsetOverlay(std::vector<float> onsets)
    {
        state_.smearnov.onsetOverlay = std::move(onsets);
    }

    [[nodiscard]] ImportWavAction importWav(SessionSource slot, ats::Path path) const
    {
        return { std::move(path), sourceIndex(slot) };
    }

    [[nodiscard]] ImportAtsAction importAts(SessionSource slot, ats::Path path) const
    {
        return { std::move(path), sourceIndex(slot) };
    }

    [[nodiscard]] ExportAtsAction exportAts(SessionSource slot, ats::Path path) const
    {
        return { std::move(path), sourceIndex(slot) };
    }

    [[nodiscard]] PreviewAction previewAction(bool start) const noexcept
    {
        return { sourceIndex(state_.transport.source), layerIndex(state_.transport.layer), start };
    }

    [[nodiscard]] SeekPreviewAction seekAction() const noexcept
    {
        return { state_.transport.normalizedPlayhead };
    }

    [[nodiscard]] SetPreviewSmearAction smearAction() const noexcept
    {
        return { state_.transport.smearEnabled };
    }

    [[nodiscard]] MorphVariationAction variationAction(std::size_t index, bool store) const noexcept
    {
        return { static_cast<int>(index), store };
    }

    [[nodiscard]] SetMorphTimelineAction timelineAction() const
    {
        return { state_.morph.timeline };
    }

    [[nodiscard]] EditSmearnovOnsetsAction onsetAction() const
    {
        return { state_.smearnov.onsetOverlay };
    }

    static constexpr int sourceIndex(SessionSource source) noexcept
    {
        return source == SessionSource::sourceA ? 0 : source == SessionSource::sourceB ? 1 : 0;
    }

    static constexpr int layerIndex(SessionLayer layer) noexcept
    {
        return static_cast<int>(layer);
    }

private:
    SessionPageState state_{};
};

} // namespace ats::ui
