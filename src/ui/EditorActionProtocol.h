#pragma once

#include "EditorHost.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <string_view>
#include <type_traits>

namespace ats::ui
{

enum class ActionThreadDomain : std::uint8_t { ui, worker, audioRt, host };
enum class ActionCancellation : std::uint8_t { none, explicitCancel, superseded, teardown };
enum class ActionUndoPolicy : std::uint8_t { none, oneTransaction, gesture }; 

struct ActionContract
{
    std::string_view name;
    ActionThreadDomain owner = ActionThreadDomain::ui;
    ActionCancellation cancellation = ActionCancellation::none;
    ActionUndoPolicy undo = ActionUndoPolicy::none;
};

struct ActionValidation
{
    bool valid = true;
    std::string_view code;
    std::string_view text;
};

template <typename T>
[[nodiscard]] constexpr ActionContract actionContractFor() noexcept
{
    if constexpr (std::is_same_v<T, SetParameterAction>) return { "SetParameter", ActionThreadDomain::ui, ActionCancellation::none, ActionUndoPolicy::gesture };
    if constexpr (std::is_same_v<T, ImportWavAction>) return { "ImportWav", ActionThreadDomain::worker, ActionCancellation::explicitCancel, ActionUndoPolicy::oneTransaction };
    if constexpr (std::is_same_v<T, ImportAtsAction>) return { "ImportAts", ActionThreadDomain::worker, ActionCancellation::explicitCancel, ActionUndoPolicy::oneTransaction };
    if constexpr (std::is_same_v<T, ExportAtsAction>) return { "ExportAts", ActionThreadDomain::worker, ActionCancellation::explicitCancel, ActionUndoPolicy::none };
    if constexpr (std::is_same_v<T, AnalyzeCurrentAction>) return { "AnalyzeCurrent", ActionThreadDomain::worker, ActionCancellation::explicitCancel, ActionUndoPolicy::none };
    if constexpr (std::is_same_v<T, AnalyzeMorphSourceAction>) return { "AnalyzeMorphSource", ActionThreadDomain::worker, ActionCancellation::explicitCancel, ActionUndoPolicy::none };
    if constexpr (std::is_same_v<T, ApplyAnalysisProfileAction>) return { "ApplyAnalysisProfile", ActionThreadDomain::ui, ActionCancellation::superseded, ActionUndoPolicy::oneTransaction };
    if constexpr (std::is_same_v<T, CancelJobsAction>) return { "CancelJobs", ActionThreadDomain::ui, ActionCancellation::explicitCancel, ActionUndoPolicy::none };
    if constexpr (std::is_same_v<T, PreviewAction>) return { "PlayPreview", ActionThreadDomain::audioRt, ActionCancellation::teardown, ActionUndoPolicy::none };
    if constexpr (std::is_same_v<T, StopPreviewAction>) return { "StopPreview", ActionThreadDomain::audioRt, ActionCancellation::teardown, ActionUndoPolicy::none };
    if constexpr (std::is_same_v<T, SeekPreviewAction>) return { "SeekPreview", ActionThreadDomain::audioRt, ActionCancellation::none, ActionUndoPolicy::none };
    if constexpr (std::is_same_v<T, SetPreviewSmearAction>) return { "SetPreviewSmear", ActionThreadDomain::ui, ActionCancellation::superseded, ActionUndoPolicy::none };
    if constexpr (std::is_same_v<T, MorphVariationAction>) return { "MorphVariation", ActionThreadDomain::ui, ActionCancellation::none, ActionUndoPolicy::oneTransaction };
    if constexpr (std::is_same_v<T, SetMorphTimelineAction>) return { "SetMorphTimeline", ActionThreadDomain::ui, ActionCancellation::superseded, ActionUndoPolicy::oneTransaction };
    if constexpr (std::is_same_v<T, BounceMorphTimelineAction>) return { "BounceMorphTimeline", ActionThreadDomain::worker, ActionCancellation::explicitCancel, ActionUndoPolicy::oneTransaction };
    if constexpr (std::is_same_v<T, SetSpectralViewAction>) return { "SetSpectralView", ActionThreadDomain::ui, ActionCancellation::none, ActionUndoPolicy::none };
    if constexpr (std::is_same_v<T, ApplyFactoryPresetAction>) return { "ApplyFactoryPreset", ActionThreadDomain::ui, ActionCancellation::none, ActionUndoPolicy::oneTransaction };
    if constexpr (std::is_same_v<T, SavePresetAction>) return { "SavePreset", ActionThreadDomain::worker, ActionCancellation::explicitCancel, ActionUndoPolicy::none };
    if constexpr (std::is_same_v<T, LoadPresetAction>) return { "LoadPreset", ActionThreadDomain::worker, ActionCancellation::explicitCancel, ActionUndoPolicy::oneTransaction };
    if constexpr (std::is_same_v<T, NoteAction>) return { "Note", ActionThreadDomain::audioRt, ActionCancellation::none, ActionUndoPolicy::none };
    if constexpr (std::is_same_v<T, HistoryAction>) return { "History", ActionThreadDomain::ui, ActionCancellation::none, ActionUndoPolicy::oneTransaction };
    if constexpr (std::is_same_v<T, PartialEditAction>) return { "PartialEdit", ActionThreadDomain::ui, ActionCancellation::teardown, ActionUndoPolicy::oneTransaction };
    if constexpr (std::is_same_v<T, ResidualEditAction>) return { "ResidualEdit", ActionThreadDomain::ui, ActionCancellation::teardown, ActionUndoPolicy::oneTransaction };
    if constexpr (std::is_same_v<T, SpectrumEditAction>) return { "SpectrumEdit", ActionThreadDomain::ui, ActionCancellation::teardown, ActionUndoPolicy::oneTransaction };
    if constexpr (std::is_same_v<T, ApplyMathAction>) return { "ApplyMathExpr", ActionThreadDomain::worker, ActionCancellation::explicitCancel, ActionUndoPolicy::oneTransaction };
    if constexpr (std::is_same_v<T, DetectSmearnovAction>) return { "DetectSmearnov", ActionThreadDomain::worker, ActionCancellation::explicitCancel, ActionUndoPolicy::oneTransaction };
    if constexpr (std::is_same_v<T, ApplySmearnovAction>) return { "ApplySmearnov", ActionThreadDomain::worker, ActionCancellation::explicitCancel, ActionUndoPolicy::oneTransaction };
    if constexpr (std::is_same_v<T, EditSmearnovOnsetsAction>) return { "EditSmearnovOnsets", ActionThreadDomain::ui, ActionCancellation::teardown, ActionUndoPolicy::oneTransaction };
    if constexpr (std::is_same_v<T, EditSmearnovOnsetHandlesAction>) return { "EditSmearnovOnsetHandles", ActionThreadDomain::ui, ActionCancellation::teardown, ActionUndoPolicy::oneTransaction };
    if constexpr (std::is_same_v<T, SetTimbreMotionPlanAction>) return { "SetTimbreMotionPlan", ActionThreadDomain::worker, ActionCancellation::superseded, ActionUndoPolicy::oneTransaction };
    if constexpr (std::is_same_v<T, AutoTuneAnalysisAction>) return { "AutoTuneAnalysis", ActionThreadDomain::worker, ActionCancellation::explicitCancel, ActionUndoPolicy::oneTransaction };
    if constexpr (std::is_same_v<T, TimeEditAction>) return { "TimeEdit", ActionThreadDomain::ui, ActionCancellation::teardown, ActionUndoPolicy::oneTransaction };
    if constexpr (std::is_same_v<T, SetSoundCurveAction>) return { "SetSoundCurve", ActionThreadDomain::ui, ActionCancellation::superseded, ActionUndoPolicy::oneTransaction };
    return { "UnknownAction", ActionThreadDomain::ui, ActionCancellation::none, ActionUndoPolicy::none };
}

template <typename T>
[[nodiscard]] ActionValidation validateAction(const T& action) noexcept
{
    if constexpr (std::is_same_v<T, SetParameterAction>)
    {
        if (action.id.empty()) return { false, "missing_parameter_id", "parameter ID is required" };
        if (!std::isfinite(action.value)) return { false, "invalid_parameter", "parameter value must be finite" };
    }
    else if constexpr (std::is_same_v<T, ImportWavAction> || std::is_same_v<T, ImportAtsAction>)
    {
        if (action.path.empty()) return { false, "missing_path", "a path is required" };
        if (action.slot < 0) return { false, "invalid_slot", "source slot must be non-negative" };
    }
    else if constexpr (std::is_same_v<T, ExportAtsAction>)
    {
        if (action.path.empty()) return { false, "missing_path", "a path is required" };
        if (action.source < 0) return { false, "invalid_source", "source must be non-negative" };
    }
    else if constexpr (std::is_same_v<T, SavePresetAction> || std::is_same_v<T, LoadPresetAction>)
    {
        if (action.path.empty()) return { false, "missing_path", "a path is required" };
    }
    else if constexpr (std::is_same_v<T, BounceMorphTimelineAction>)
    {
        if (action.path.empty()) return { false, "missing_path", "a path is required" };
        if (action.layer < 0) return { false, "invalid_layer", "layer must be non-negative" };
    }
    else if constexpr (std::is_same_v<T, AnalyzeMorphSourceAction>)
    {
        if (action.slot < 0) return { false, "invalid_slot", "source slot must be non-negative" };
    }
    else if constexpr (std::is_same_v<T, ApplyAnalysisProfileAction>)
    {
        if (action.profile < 0 || action.profile > 3)
            return { false, "invalid_analysis_profile", "analysis profile must be in range" };
    }
    else if constexpr (std::is_same_v<T, PreviewAction>)
    {
        if (action.source < 0 || action.layer < 0) return { false, "invalid_preview", "preview source and layer must be non-negative" };
    }
    else if constexpr (std::is_same_v<T, SeekPreviewAction>)
    {
        if (!std::isfinite(action.normalizedTime) || action.normalizedTime < 0.0f || action.normalizedTime > 1.0f)
            return { false, "invalid_seek", "preview seek must be normalized" };
    }
    else if constexpr (std::is_same_v<T, MorphVariationAction>)
    {
        if (action.slot < 0 || action.slot >= 3)
            return { false, "invalid_variation", "variation slot must be in range" };
    }
    else if constexpr (std::is_same_v<T, SetMorphTimelineAction>)
    {
        if (action.normalizedPoints.size() > 32 || action.points.size() > 32)
            return { false, "timeline_too_large", "timeline is limited to 32 points" };
        for (const auto point : action.normalizedPoints)
            if (!std::isfinite(point)) return { false, "invalid_timeline", "timeline points must be finite" };
        for (const auto& point : action.points)
            if (!std::isfinite(point.timeNorm) || !std::isfinite(point.amount) || point.timeNorm < 0.0f ||
                point.timeNorm > 1.0f || point.amount < 0.0f || point.amount > 1.0f ||
                point.kind > 2 || point.curve > 3)
                return { false, "invalid_timeline", "typed timeline points are invalid" };
    }
    else if constexpr (std::is_same_v<T, SetSpectralViewAction>)
    {
        if (action.source < 0 || action.mode < 0) return { false, "invalid_spectral_view", "source and mode must be non-negative" };
    }
    else if constexpr (std::is_same_v<T, ApplyFactoryPresetAction>)
    {
        if (action.index < 0) return { false, "invalid_factory_preset", "factory preset index is required" };
    }
    else if constexpr (std::is_same_v<T, PartialEditAction>)
    {
        if (action.partial < 0 || action.frame < 0) return { false, "invalid_partial_cell", "partial and frame are required" };
        if (action.apply && (!std::isfinite(action.amplitude) || action.amplitude < 0.0
                             || !std::isfinite(action.frequencyHz) || action.frequencyHz <= 0.0))
            return { false, "invalid_partial_values", "amplitude must be finite and non-negative and frequency must be positive" };
    }
    else if constexpr (std::is_same_v<T, ResidualEditAction>)
    {
        if (action.band < 0 || action.frame < 0 || !std::isfinite(action.value))
            return { false, "invalid_residual_edit", "residual band, frame, and value are required" };
    }
    else if constexpr (std::is_same_v<T, SetSoundCurveAction>)
    {
        if (static_cast<int>(action.curve) > static_cast<int>(SoundCurveKind::spectralWarp))
            return { false, "invalid_curve", "curve kind is unknown" };
        if (action.points.size() % 2 != 0 || action.points.size() > 128)
            return { false, "invalid_curve", "curve points are x, y pairs, at most 64" };
        for (const auto v : action.points)
            if (!std::isfinite(v)) return { false, "invalid_curve", "curve points must be finite" };
    }
    else if constexpr (std::is_same_v<T, TimeEditAction>)
    {
        if (static_cast<int>(action.kind) > static_cast<int>(TimeEditKind::paste))
            return { false, "invalid_time_edit", "time edit kind is unknown" };
        if (action.firstFrame < 0 || (action.kind != TimeEditKind::paste && action.lastFrame < action.firstFrame))
            return { false, "invalid_time_edit", "time edit requires an ordered, non-negative frame range" };
        if (action.crossfadeFrames < 0 || action.crossfadeFrames > 4096)
            return { false, "invalid_time_edit", "crossfade must be 0 to 4096 frames" };
    }
    else if constexpr (std::is_same_v<T, SpectrumEditAction>)
    {
        if (action.bands.empty() || action.firstFrame < 0 || action.lastFrame < action.firstFrame)
            return { false, "invalid_spectrum_edit", "spectrum edit requires bands and an ordered frame range" };
        if (action.bands.size() > 220)
            return { false, "too_many_spectrum_bands", "spectrum edit exceeds the residual band limit" };
        if (action.points.size() > 4096)
            return { false, "too_many_spectrum_points", "spectrum edit exceeds the point limit" };
        for (const auto band : action.bands)
            if (band < 0) return { false, "invalid_spectrum_band", "spectrum band must be non-negative" };
        for (const auto& point : action.points)
            if (point.first < 0 || !std::isfinite(point.second))
                return { false, "invalid_spectrum_point", "spectrum edit points must be finite and non-negative" };
        if (!std::isfinite(action.value))
            return { false, "invalid_spectrum_value", "spectrum edit value must be finite" };
    }
    else if constexpr (std::is_same_v<T, ApplyMathAction>)
    {
        if (action.script.empty() || std::all_of(action.script.begin(), action.script.end(), [](unsigned char character) {
                return std::isspace(character) != 0;
            }))
            return { false, "empty_math_script", "MathExpr script is required" };
    }
    else if constexpr (std::is_same_v<T, EditSmearnovOnsetsAction>)
    {
        if (action.normalizedOnsets.size() > 4096) return { false, "too_many_onsets", "onset count exceeds the bounded contract" };
        for (const auto onset : action.normalizedOnsets)
            if (!std::isfinite(onset) || onset < 0.0f || onset > 1.0f)
                return { false, "invalid_onset", "onsets must be normalized" };
    }
    else if constexpr (std::is_same_v<T, EditSmearnovOnsetHandlesAction>)
    {
        if (action.onsets.size() > 4096)
            return { false, "too_many_onsets", "onset count exceeds the bounded contract" };
        float previous = -1.0f;
        for (const auto& onset : action.onsets)
        {
            if (!std::isfinite(onset.norm) || !std::isfinite(onset.preNorm)
                || !std::isfinite(onset.peakNorm) || !std::isfinite(onset.strength)
                || onset.norm < 0.0f || onset.norm > 1.0f
                || onset.preNorm < 0.0f || onset.preNorm > onset.norm
                || onset.peakNorm < onset.norm || onset.peakNorm > 1.0f
                || onset.strength < 0.0f || onset.norm <= previous)
                return { false, "invalid_onset_handles", "onset handles must be ordered and bounded" };
            previous = onset.norm;
        }
    }
    else if constexpr (std::is_same_v<T, SetTimbreMotionPlanAction>)
    {
        if (action.normalizedPlan.size() > 4096) return { false, "plan_too_large", "timbre plan exceeds the bounded contract" };
        for (const auto value : action.normalizedPlan)
            if (!std::isfinite(value)) return { false, "invalid_timbre_plan", "timbre plan values must be finite" };
        if (action.serializedPlan.size() > 1024 * 1024)
            return { false, "plan_too_large", "serialized timbre plan exceeds one megabyte" };
    }
    return {};
}

[[nodiscard]] inline ActionContract actionContract(const EditorAction& action) noexcept
{
    return std::visit([](const auto& value) { return actionContractFor<std::decay_t<decltype(value)>>(); }, action);
}

[[nodiscard]] inline ActionValidation validateAction(const EditorAction& action) noexcept
{
    return std::visit([](const auto& value) { return validateAction(value); }, action);
}

} // namespace ats::ui
