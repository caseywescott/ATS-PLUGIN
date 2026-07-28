#pragma once

#include "AtsPrimitives.h"

#include <cstdint>
#include <string>
#include <variant>

namespace ats::ui
{

struct SetParameterAction { std::string id; float value = 0.0f; bool begin = false; bool end = false; };
struct ImportWavAction { ats::Path path; int slot = 0; };
struct ImportAtsAction { ats::Path path; int slot = 0; };
struct ExportAtsAction { ats::Path path; int source = 0; };
struct PreviewAction { int source = 0; int layer = 0; bool start = false; };
struct SetSpectralViewAction { int source = 0; int mode = 0; };
struct NoteAction { bool on = false; std::uint8_t note = 60; std::uint8_t velocity = 0; };
struct HistoryAction { bool redo = false; };
struct ApplyMathAction { std::string script; };

using EditorAction = std::variant<SetParameterAction,
                                  ImportWavAction,
                                  ImportAtsAction,
                                  ExportAtsAction,
                                  PreviewAction,
                                  SetSpectralViewAction,
                                  NoteAction,
                                  HistoryAction,
                                  ApplyMathAction>;

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
