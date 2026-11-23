#pragma once

#include "EditorHost.h"
#include "PartialEditView.h"

#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace ats::ui
{

enum class AnalysisProfile : std::uint8_t { defaultProfile, speech, percussive, custom };

struct AnalysisControl
{
    std::string_view id;
    float value = 0.0f;
    bool integer = false;
};

class AnalysisPageModel
{
public:
    static constexpr std::size_t kControlCount = 20;

    [[nodiscard]] static constexpr std::array<std::string_view, kControlCount> controlIds() noexcept
    {
        return { "analysisType", "analysisStart", "analysisDuration", "analysisLowestFreq",
                 "analysisHighestFreq", "analysisFreqDev", "analysisWinCycles", "analysisWinType",
                 "analysisHopSize", "analysisLowestMag", "analysisTrackLen", "analysisMinSegLen",
                 "analysisMinGapLen", "analysisSmrThreshold", "analysisMinSegSmr",
                 "analysisLastPeakContribution", "analysisSmrContribution", "analysisMultiRes",
                 "analysisReverseTracking", "analysisClampResidual" };
    }

    [[nodiscard]] const std::array<AnalysisControl, kControlCount>& controls() const noexcept
    { return controls_; }
    [[nodiscard]] AnalysisProfile profile() const noexcept { return profile_; }
    [[nodiscard]] bool hasSource() const noexcept { return hasSource_; }
    [[nodiscard]] bool workerRunning() const noexcept { return workerRunning_; }
    [[nodiscard]] bool canCancel() const noexcept { return workerRunning_; }
    [[nodiscard]] bool applied() const noexcept { return applied_; }

    void setHasSource(bool hasSource) noexcept { hasSource_ = hasSource; }
    void setWorkerRunning(bool running) noexcept { workerRunning_ = running; }
    void setProfile(AnalysisProfile profile) noexcept
    {
        profile_ = profile;
        applied_ = false;
    }

    bool setControl(std::size_t index, float value) noexcept
    {
        if (index >= kControlCount || !std::isfinite(value))
            return false;
        controls_[index].value = value;
        profile_ = AnalysisProfile::custom;
        applied_ = false;
        return true;
    }

    void markApplied() noexcept { applied_ = true; }
    [[nodiscard]] ApplyAnalysisProfileAction profileAction() const noexcept
    { return { static_cast<int>(profile_) }; }
    [[nodiscard]] AnalyzeCurrentAction applyAction() const noexcept { return {}; }
    [[nodiscard]] AnalyzeMorphSourceAction analyzeSourceAction(int slot) const noexcept { return { slot }; }

private:
    std::array<AnalysisControl, kControlCount> controls_ = [] {
        std::array<AnalysisControl, kControlCount> result{};
        const auto ids = controlIds();
        for (std::size_t i = 0; i < ids.size(); ++i)
            result[i] = { ids[i], 0.0f, i == 0 || i == 6 || i == 7 || i == 10 || i == 11 || i == 12 || i == 17 };
        return result;
    }();
    AnalysisProfile profile_ = AnalysisProfile::defaultProfile;
    bool hasSource_ = false;
    bool workerRunning_ = false;
    bool applied_ = false;
};

struct SynthesisPageState
{
    bool hasSource = false;
    float frequencyFactor = 1.0f;
    FrequencyShapeMode shapeMode = FrequencyShapeMode::linear;
    float shapeAmount = 0.0f;
    float shapePivotHz = 1000.0f;
    float timeStretch = 1.0f;
    float startSeconds = 0.0f;
    float durationSeconds = 0.0f;
    bool usePhase = true;
    bool useAllPartials = true;
    bool residualOnly = false;
    bool useIfftSynth = false;
    bool layerMixPlayback = false;
};

class SynthesisPageModel
{
public:
    [[nodiscard]] const SynthesisPageState& state() const noexcept { return state_; }
    void setHasSource(bool value) noexcept { state_.hasSource = value; }
    void setShapeMode(FrequencyShapeMode mode) noexcept { state_.shapeMode = mode; }
    void setUsePhase(bool value) noexcept { state_.usePhase = value; }
    void setUseAllPartials(bool value) noexcept { state_.useAllPartials = value; }
    void setUseIfftSynth(bool value) noexcept { state_.useIfftSynth = value; }
    void setResidualOnly(bool value) noexcept { state_.residualOnly = value; }
    void setLayerMixPlayback(bool value) noexcept { state_.layerMixPlayback = value; }

    [[nodiscard]] bool shapeControlsDimmed() const noexcept
    { return state_.shapeMode == FrequencyShapeMode::linear; }

    bool setFrequencyFactor(float value) noexcept
    {
        if (!std::isfinite(value) || value <= 0.0f) return false;
        state_.frequencyFactor = value;
        return true;
    }

    bool setShapeAmount(float value) noexcept
    {
        if (!std::isfinite(value)) return false;
        state_.shapeAmount = value;
        return true;
    }

    bool setShapePivotHz(float value) noexcept
    {
        if (!std::isfinite(value) || value < 0.0f) return false;
        state_.shapePivotHz = value;
        return true;
    }

    bool setTimeStretch(float value) noexcept
    {
        if (!std::isfinite(value) || value <= 0.0f) return false;
        state_.timeStretch = value;
        return true;
    }

    bool setStartSeconds(float value) noexcept
    {
        if (!std::isfinite(value) || value < 0.0f) return false;
        state_.startSeconds = value;
        return true;
    }

    bool setDuration(float seconds) noexcept
    {
        if (!std::isfinite(seconds) || seconds < 0.0f)
            return false;
        state_.durationSeconds = seconds;
        return true;
    }

    [[nodiscard]] std::string durationLabel() const
    { return state_.durationSeconds <= 0.0005f ? "FULL" : std::to_string(state_.durationSeconds); }

    [[nodiscard]] std::string pivotLabel() const
    {
        if (state_.shapePivotHz <= 0.005f) return "Auto";
        return std::to_string(static_cast<int>(state_.shapePivotHz + 0.5f)) + " Hz";
    }

    [[nodiscard]] std::string bothPlaybackLabel() const
    { return state_.layerMixPlayback ? "Layer mix" : "Classic combined"; }

private:
    SynthesisPageState state_{};
};

class PartialPageModel
{
public:
    void setDimensions(int partials, int frames) noexcept
    {
        partialCount_ = partials < 0 ? 0 : partials;
        frameCount_ = frames < 0 ? 0 : frames;
    }

    [[nodiscard]] PartialEditView& view() noexcept { return view_; }
    [[nodiscard]] const PartialEditView& view() const noexcept { return view_; }

    [[nodiscard]] bool cellIsValid(int partial, int frame, double amplitude, double frequencyHz) const noexcept
    {
        return partial >= 0 && partial < partialCount_ && frame >= 0 && frame < frameCount_
            && std::isfinite(amplitude) && amplitude >= 0.0
            && std::isfinite(frequencyHz) && frequencyHz > 0.0;
    }

    [[nodiscard]] std::optional<ats::PartialEditCommand> commitCell() noexcept
    {
        if (!view_.editing() || !cellIsValid(view_.focusedPartial(), view_.focusedFrame(),
                                              draftAmplitude_, draftFrequencyHz_))
            return std::nullopt;
        return view_.commitCellEdit();
    }

    void beginCell(int partial, int frame, double amplitude, double frequencyHz) noexcept
    {
        draftAmplitude_ = amplitude;
        draftFrequencyHz_ = frequencyHz;
        view_.beginCellEdit(partial, frame, amplitude, frequencyHz);
    }
    void setDraft(double amplitude, double frequencyHz) noexcept
    {
        draftAmplitude_ = amplitude;
        draftFrequencyHz_ = frequencyHz;
        view_.setDraftAmplitude(amplitude);
        view_.setDraftFrequencyHz(frequencyHz);
    }

    void focusLoss() noexcept { view_.cancelCellEdit(); }

private:
    PartialEditView view_{};
    int partialCount_ = 0;
    int frameCount_ = 0;
    double draftAmplitude_ = 0.0;
    double draftFrequencyHz_ = 1.0;
};

enum class MathStatus : std::uint8_t { idle, running, success, error, cancelled };

class MathPageModel
{
public:
    static constexpr std::string_view helpText() noexcept
    { return "MathExpr applies validated expressions to the selected ATS layer."; }
    static constexpr std::string_view starterText() noexcept
    { return "a=a * 0.9\\nf=f"; }

    [[nodiscard]] const std::string& draft() const noexcept { return draft_; }
    [[nodiscard]] MathStatus status() const noexcept { return status_; }
    [[nodiscard]] const std::string& error() const noexcept { return error_; }

    void setDraft(std::string draft) { draft_ = std::move(draft); }
    [[nodiscard]] bool canApply() const noexcept
    {
        return std::any_of(draft_.begin(), draft_.end(), [](unsigned char character) {
            return std::isspace(character) == 0;
        }) && status_ != MathStatus::running;
    }

    [[nodiscard]] std::optional<ApplyMathAction> applyAction() const
    {
        if (!canApply())
            return std::nullopt;
        return ApplyMathAction { draft_ };
    }

    bool beginApply() noexcept
    {
        if (!canApply())
            return false;
        status_ = MathStatus::running;
        error_.clear();
        return true;
    }

    void complete(bool success, std::string error,
                  std::chrono::steady_clock::time_point now)
    {
        status_ = success ? MathStatus::success : MathStatus::error;
        error_ = std::move(error);
        flashUntil_ = success ? now + std::chrono::milliseconds(600) : now;
    }

    void cancel() noexcept { status_ = MathStatus::cancelled; }

    // Called when the script editor loses focus. The draft is preserved so the
    // user can return to it; in-flight workers are not interrupted.
    void focusLoss() noexcept { if (status_ == MathStatus::idle) flashUntil_ = {}; }

    [[nodiscard]] bool successFlashActive(std::chrono::steady_clock::time_point now) const noexcept
    { return status_ == MathStatus::success && now < flashUntil_; }

private:
    std::string draft_;
    std::string error_;
    MathStatus status_ = MathStatus::idle;
    std::chrono::steady_clock::time_point flashUntil_{};
};

} // namespace ats::ui
