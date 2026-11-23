#pragma once

#include "AtsPrimitives.h"
#include "SpectralTraceBuilder.h"

#include <cstdint>
#include <array>
#include <string>
#include <mutex>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

namespace ats::ui
{

struct SetParameterAction { std::string id; float value = 0.0f; bool begin = false; bool end = false; };
struct ImportWavAction { ats::Path path; int slot = 0; };
struct ImportAtsAction { ats::Path path; int slot = 0; };
struct ExportAtsAction { ats::Path path; int source = 0; };
struct AnalyzeCurrentAction {};
// Analysis-by-synthesis search for the current model's resolution settings
// (lowest / highest frequency, window cycles, hop); applies the result and
// re-analyses.  Runs on the analysis worker and is cancelled by CancelJobs.
struct AutoTuneAnalysisAction {};
// Structural time edits on the edited model (adapted from pyats clipboard):
// copy / cut / remove / crop act on frames [firstFrame, lastFrame]; paste
// inserts the clipboard at firstFrame with a crossfade at each seam.
enum class TimeEditKind : std::uint8_t { copy, cut, remove, crop, paste };
// Render-time automation curves over normalised output time.  points are
// flattened x0, y0, x1, y1, ... (x in [0, 1]); y is the analysis position
// (time warp, 0-1), semitones (pitch, +/-24) or gamma (spectral warp,
// 0.5-2).  An empty list resets the curve.
enum class SoundCurveKind : std::uint8_t { timeWarp, pitch, spectralWarp };
struct SetSoundCurveAction
{
    SoundCurveKind curve = SoundCurveKind::pitch;
    std::vector<float> points;
};
struct TimeEditAction
{
    TimeEditKind kind = TimeEditKind::copy;
    std::int32_t firstFrame = 0;
    std::int32_t lastFrame = 0;
    std::int32_t crossfadeFrames = 0;
};
struct AnalyzeMorphSourceAction { int slot = 0; };
struct ApplyAnalysisProfileAction { int profile = 0; };
struct CancelJobsAction {};
struct PreviewAction { int source = 0; int layer = 0; bool start = false; };
struct StopPreviewAction {};
struct SeekPreviewAction { float normalizedTime = 0.0f; };
struct SetPreviewSmearAction { bool enabled = false; };
struct MorphVariationAction { int slot = 0; bool store = true; };
struct MorphTimelineActionPoint
{
    float timeNorm = 0.0f;
    float amount = 0.0f;
    std::uint8_t kind = 0;  // 0=morph, 1=hybrid, 2=envelope morph
    std::uint8_t curve = 0; // MorphCurveShape ordinal
};
struct SetMorphTimelineAction
{
    // Backward-compatible simple curve: values are placed at evenly spaced times.
    std::vector<float> normalizedPoints;
    // Full former-host contract. When populated this takes precedence over normalizedPoints.
    std::vector<MorphTimelineActionPoint> points;
};
struct BounceMorphTimelineAction { ats::Path path; int layer = 0; };
struct SetSpectralViewAction { int source = 0; int mode = 0; };
struct ApplyFactoryPresetAction { std::int32_t index = -1; };
struct SavePresetAction { ats::Path path; };
struct LoadPresetAction { ats::Path path; };
struct NoteAction { bool on = false; std::uint8_t note = 60; std::uint8_t velocity = 0; };
struct HistoryAction { bool redo = false; };
struct PartialEditAction
{
    std::int32_t partial = -1;
    std::int32_t frame = -1;
    bool apply = false;
    // The first three fields preserve the original focus/select action ABI;
    // these values are populated for a committed cell write.
    double amplitude = 0.0;
    double frequencyHz = 1.0;
};
struct ResidualEditAction { std::int32_t band = -1; std::int32_t frame = -1; float value = 0.0f; };
enum class SpectrumEditCommand : std::uint8_t
{
    copyResidual,
    pasteResidual,
    swapResidual,
    smoothResidual,
    tiltResidual,
    redistResidual,
    mutePartials,
    scalePartials,
    drawPartialFrequency,
    gainPartialEnvelope,
    smoothPartialEnvelope,
    tiltPartialEnvelope,
    soloPartials,
    freezePartial,
    deletePartials,
    prunePartials,
    muteResidual,
    scaleResidual,
    drawResidual,
    readResidual,
    // UI-only selection commands (like prunePartials); never sent to the host.
    selectPartialsByCriteria,
    invertPartialSelection,
    timeEditMenu
};
struct SpectrumEditAction
{
    SpectrumEditCommand command = SpectrumEditCommand::copyResidual;
    std::vector<int> bands;
    std::int32_t firstFrame = 0;
    std::int32_t lastFrame = 0;
    float value = 0.0f;
    std::vector<std::pair<std::int32_t, float>> points;
};
struct ApplyMathAction { std::string script; };
struct DetectSmearnovAction {};
struct ApplySmearnovAction {};
struct EditSmearnovOnsetsAction { std::vector<float> normalizedOnsets; };
struct EditSmearnovOnsetHandlesAction { std::vector<SpectralOnset> onsets; };
struct SetTimbreMotionPlanAction
{
    std::vector<float> normalizedPlan;
    // Full TimbreMotionPlan JSON. The simple curve remains supported for the compact editor.
    std::string serializedPlan;
};

using EditorAction = std::variant<SetParameterAction,
                                  ImportWavAction,
                                  ImportAtsAction,
                                  ExportAtsAction,
                                  AnalyzeCurrentAction,
                                  AnalyzeMorphSourceAction,
                                  ApplyAnalysisProfileAction,
                                  CancelJobsAction,
                                  PreviewAction,
                                  StopPreviewAction,
                                  SeekPreviewAction,
                                  SetPreviewSmearAction,
                                  MorphVariationAction,
                                  SetMorphTimelineAction,
                                  BounceMorphTimelineAction,
                                  SetSpectralViewAction,
                                  ApplyFactoryPresetAction,
                                  SavePresetAction,
                                  LoadPresetAction,
                                  NoteAction,
                                  HistoryAction,
                                  PartialEditAction,
                                  ResidualEditAction,
                                  SpectrumEditAction,
                                  ApplyMathAction,
                                  DetectSmearnovAction,
                                  ApplySmearnovAction,
                                  EditSmearnovOnsetsAction,
                                  EditSmearnovOnsetHandlesAction,
                                  SetTimbreMotionPlanAction,
                                  AutoTuneAnalysisAction,
                                  TimeEditAction,
                                  SetSoundCurveAction>;

enum class ActionStatus : std::uint8_t { success, failed, cancelled, stale };

struct ActionResult
{
    std::uint64_t generation = 0;
    ActionStatus status = ActionStatus::success;
    std::string code;
    std::string text;
};

enum class UiState : std::uint8_t
{
    noSource, readyToAnalyze, analyzing, analyzed, resynthesizing, applyingMath, error
};

struct TelemetrySnapshot
{
    UiState state = UiState::noSource;
    std::uint64_t generation = 0;
    std::uint64_t modelRevision = 0;
    std::uint64_t displayRevision = 0;
    float progress = 0.0f;
    bool analysisRunning = false;
    bool renderRunning = false;
    bool canUndo = false;
    bool canRedo = false;
    bool hasCurrentModel = false;
    bool hasSourceA = false;
    bool hasSourceB = false;
    bool hasResidual = false;
    bool hasPhase = false;
    bool previewPlaying = false;
    float normalizedPlayhead = 0.0f;
    std::uint64_t morphRevision = 0;
    std::uint64_t overlayRevision = 0;
    std::uint64_t shapeRevision = 0;
    std::string status;
    std::string error;
};

// Immutable UI publication assembled by the host on its UI/worker side. The
// native backends forward this value without translating product semantics;
// the editor only projects it into widgets. Values are stored in registry
// dense-index order so the snapshot remains stable even when display labels
// change.
struct EditorSnapshot
{
    static constexpr std::size_t kParameterCount = 81;

    std::uint64_t revision = 0;
    std::array<float, kParameterCount> parameters {};
    TelemetrySnapshot telemetry {};
    std::array<std::string, 3> sourceNames {};
    std::array<float, 3> sourceDurations {};
    std::array<int, 3> sourcePartialCounts {};
    std::array<bool, 3> sourceAvailable {};
    std::array<bool, 3> sourceResidualAvailable {};
    std::vector<float> timeline { 0.0f, 1.0f };
    std::vector<float> timbrePlan;
    std::vector<float> onsets;
    // Automation curves, indexed by SoundCurveKind, flattened as in SetSoundCurveAction.
    std::array<std::vector<float>, 3> soundCurves {};
    // The morph curve's typed points with their times, flattened (time, amount).
    std::vector<float> morphCurve { 0.0f, 0.0f, 1.0f, 1.0f };
    // Easing per morph point (MorphCurveShape ordinal), same order as morphCurve.
    std::vector<std::uint8_t> morphCurveShapes { 0, 0 };
    std::string presetName;
    std::string undoLabel;
    std::string redoLabel;
    std::optional<SpectralTraceSnapshot> spectrum;
    std::optional<SpectralTraceSnapshot> spectrumOverlay;
};

class ActionSink
{
public:
    virtual ~ActionSink() = default;
    virtual void dispatch(EditorAction action) = 0;
};

class TelemetryPublisher
{
public:
    void publish(TelemetrySnapshot snapshot)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        snapshot_ = std::move(snapshot);
    }

    [[nodiscard]] TelemetrySnapshot snapshot() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return snapshot_;
    }

private:
    mutable std::mutex mutex_;
    TelemetrySnapshot snapshot_;
};

class EditorHost
{
public:
    virtual ~EditorHost() = default;
    virtual void setLogicalSize(float width, float height) = 0;
    virtual void requestResize(float width, float height) = 0;
    [[nodiscard]] virtual float scale() const noexcept = 0;
    [[nodiscard]] virtual TelemetrySnapshot telemetry() const = 0;
    virtual void dispatch(EditorAction action) = 0;
};

enum class EditorPage : std::uint8_t { session, analysis, synthesis, partials, math };
enum class EditorLifecycle : std::uint8_t { detached, attached, closing };
enum class FocusOwner : std::uint8_t { none, parameter, textEntry, popup };
enum class PopupKind : std::uint8_t { none, sourceMenu, presetMenu, viewMenu, timelineMenu };

// Renderer-free editor state. A backend maps this state to windows and
// widgets; it does not become a second source of model truth.
class HeadlessEditorHost final : public EditorHost
{
public:
    HeadlessEditorHost(ActionSink& sink, const TelemetryPublisher& telemetry) noexcept
        : sink_(sink), telemetry_(telemetry) {}

    bool attach(float scale, float width, float height) noexcept
    {
        if (scale <= 0.0f || width <= 0.0f || height <= 0.0f) return false;
        lifecycle_ = EditorLifecycle::attached;
        scale_ = scale;
        width_ = width;
        height_ = height;
        return true;
    }

    void close() noexcept
    {
        lifecycle_ = EditorLifecycle::closing;
        popup_ = PopupKind::none;
        focus_ = FocusOwner::none;
        lifecycle_ = EditorLifecycle::detached;
    }

    [[nodiscard]] EditorLifecycle lifecycle() const noexcept { return lifecycle_; }
    [[nodiscard]] EditorPage page() const noexcept { return page_; }
    [[nodiscard]] PopupKind popup() const noexcept { return popup_; }
    [[nodiscard]] FocusOwner focus() const noexcept { return focus_; }
    [[nodiscard]] std::array<float, 2> logicalSize() const noexcept { return { width_, height_ }; }
    [[nodiscard]] std::array<float, 2> requestedSize() const noexcept
    { return { requestedWidth_, requestedHeight_ }; }

    void setPage(EditorPage page) noexcept { if (lifecycle_ == EditorLifecycle::attached) page_ = page; }
    void setFocus(FocusOwner focus) noexcept { if (lifecycle_ == EditorLifecycle::attached) focus_ = focus; }
    void openPopup(PopupKind popup) noexcept
    {
        if (lifecycle_ == EditorLifecycle::attached)
        {
            popup_ = popup;
            if (popup != PopupKind::none) focus_ = FocusOwner::popup;
        }
    }
    void dismissPopup() noexcept
    {
        popup_ = PopupKind::none;
        if (focus_ == FocusOwner::popup) focus_ = FocusOwner::none;
    }

    void setLogicalSize(float width, float height) override
    { if (width > 0.0f && height > 0.0f) { width_ = width; height_ = height; } }

    void requestResize(float width, float height) override
    { if (width > 0.0f && height > 0.0f) { requestedWidth_ = width; requestedHeight_ = height; } }

    [[nodiscard]] float scale() const noexcept override { return scale_; }
    [[nodiscard]] TelemetrySnapshot telemetry() const override { return telemetry_.snapshot(); }
    void dispatch(EditorAction action) override
    { if (lifecycle_ == EditorLifecycle::attached) sink_.dispatch(std::move(action)); }

private:
    ActionSink& sink_;
    const TelemetryPublisher& telemetry_;
    EditorLifecycle lifecycle_ = EditorLifecycle::detached;
    EditorPage page_ = EditorPage::session;
    FocusOwner focus_ = FocusOwner::none;
    PopupKind popup_ = PopupKind::none;
    float scale_ = 1.0f;
    float width_ = 0.0f;
    float height_ = 0.0f;
    float requestedWidth_ = 0.0f;
    float requestedHeight_ = 0.0f;
};

} // namespace ats::ui
