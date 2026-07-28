#pragma once

#include "AtsPrimitives.h"

#include <cstdint>
#include <array>
#include <string>
#include <variant>
#include <vector>

namespace ats::ui
{

struct SetParameterAction { std::string id; float value = 0.0f; bool begin = false; bool end = false; };
struct ImportWavAction { ats::Path path; int slot = 0; };
struct ImportAtsAction { ats::Path path; int slot = 0; };
struct ExportAtsAction { ats::Path path; int source = 0; };
struct AnalyzeCurrentAction {};
struct AnalyzeMorphSourceAction { int slot = 0; };
struct CancelJobsAction {};
struct PreviewAction { int source = 0; int layer = 0; bool start = false; };
struct StopPreviewAction {};
struct SeekPreviewAction { float normalizedTime = 0.0f; };
struct SetMorphTimelineAction { std::vector<float> normalizedPoints; };
struct BounceMorphTimelineAction { ats::Path path; int layer = 0; };
struct SetSpectralViewAction { int source = 0; int mode = 0; };
struct ApplyFactoryPresetAction { std::int32_t index = -1; };
struct SavePresetAction { ats::Path path; };
struct LoadPresetAction { ats::Path path; };
struct NoteAction { bool on = false; std::uint8_t note = 60; std::uint8_t velocity = 0; };
struct HistoryAction { bool redo = false; };
struct PartialEditAction { std::int32_t partial = -1; std::int32_t frame = -1; bool apply = false; };
struct ResidualEditAction { std::int32_t band = -1; std::int32_t frame = -1; float value = 0.0f; };
struct ApplyMathAction { std::string script; };
struct DetectSmearnovAction {};
struct ApplySmearnovAction {};
struct EditSmearnovOnsetsAction { std::vector<float> normalizedOnsets; };
struct SetTimbreMotionPlanAction { std::vector<float> normalizedPlan; };

using EditorAction = std::variant<SetParameterAction,
                                  ImportWavAction,
                                  ImportAtsAction,
                                  ExportAtsAction,
                                  AnalyzeCurrentAction,
                                  AnalyzeMorphSourceAction,
                                  CancelJobsAction,
                                  PreviewAction,
                                  StopPreviewAction,
                                  SeekPreviewAction,
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
                                  ApplyMathAction,
                                  DetectSmearnovAction,
                                  ApplySmearnovAction,
                                  EditSmearnovOnsetsAction,
                                  SetTimbreMotionPlanAction>;

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

class ActionSink
{
public:
    virtual ~ActionSink() = default;
    virtual void dispatch(EditorAction action) = 0;
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

} // namespace ats::ui
