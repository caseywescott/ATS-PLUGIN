#include "PluginModel.h"
#include "../state/FactoryPresetState.h"
#include "../ui/EditorActionProtocol.h"
#include "../core/AtsMathExpr.h"

#include <cmath>
#include <cstring>
#include <string>
#include <utility>

namespace ats::engine
{

PluginModel::PluginModel(std::size_t workerCount, std::size_t actionCapacity)
    : actions_(actionCapacity), results_(actionCapacity), scheduler_(workerCount)
{
    ats::ui::TelemetrySnapshot initial;
    initial.state = ats::ui::UiState::noSource;
    initial.modelRevision = modelRevision_;
    initial.canUndo = history_.canUndo();
    initial.canRedo = history_.canRedo();
    publishTelemetry(std::move(initial));
}

std::size_t PluginModel::processActions(std::size_t maximum)
{
    std::size_t processed = 0;
    while (processed < maximum)
    {
        const auto action = actions_.tryPop();
        if (!action) break;

        auto result = handle(*action);
        results_.publish(result);
        ++processed;
    }
    return processed;
}

void PluginModel::cancelJobs() noexcept
{
    cancellation_.cancel();
    cancellation_ = ats::CancellationSource {};
    invalidation_.invalidate(InvalidationKind::preview);
}

ats::ui::ActionResult PluginModel::importSource(const SourceImportRequest& request,
                                                std::uint64_t generation)
{
    ats::ui::ActionResult result;
    result.generation = generation;
    auto imported = SourceImportService::import({ generation }, cancellation_.token(), request);
    if (imported.status != ats::JobResult<ImportedSource>::Status::success)
    {
        result.status = imported.status == ats::JobResult<ImportedSource>::Status::cancelled
                            ? ats::ui::ActionStatus::cancelled
                            : ats::ui::ActionStatus::failed;
        result.code = imported.error.code;
        result.text = imported.error.text;

        auto snapshot = telemetry_.snapshot();
        snapshot.state = result.status == ats::ui::ActionStatus::cancelled
                             ? ats::ui::UiState::noSource
                             : ats::ui::UiState::error;
        snapshot.status = result.status == ats::ui::ActionStatus::cancelled
                              ? "source import cancelled"
                              : "source import failed";
        snapshot.error = result.text;
        publishTelemetry(std::move(snapshot));
        return result;
    }

    const auto slot = static_cast<std::size_t>(request.slot);
    const bool importedAts = imported.value.kind == SourceImportKind::ats;
    sources_[slot] = std::move(imported.value);
    activeSourceSlot_ = request.slot;
    ++modelRevision_;
    invalidation_.invalidate(InvalidationKind::preview);

    auto snapshot = telemetry_.snapshot();
    snapshot.state = importedAts ? ats::ui::UiState::analyzed : ats::ui::UiState::readyToAnalyze;
    snapshot.modelRevision = modelRevision_;
    snapshot.displayRevision = modelRevision_;
    snapshot.hasSourceA = sources_[0].has_value();
    snapshot.hasSourceB = sources_[1].has_value();
    const auto* active = sources_[slot] ? &*sources_[slot] : nullptr;
    snapshot.hasCurrentModel = active != nullptr && static_cast<bool>(active->sound);
    snapshot.hasResidual = snapshot.hasCurrentModel && active->sound.get()->res != nullptr;
    snapshot.hasPhase = snapshot.hasCurrentModel && active->sound.get()->pha != nullptr;
    snapshot.status = importedAts ? "ATS model imported" : "audio source decoded";
    snapshot.error.clear();
    publishTelemetry(std::move(snapshot));
    return result;
}

std::vector<std::byte> PluginModel::captureSnapshot()
{
    auto snapshot = std::make_shared<ModelSnapshot>();
    snapshot->parameters = parameters_.snapshot();
    for (std::size_t slot = 0; slot < sources_.size(); ++slot)
    {
        if (sources_[slot] && sources_[slot]->sound)
        {
            if (auto* copied = copy_sound(sources_[slot]->sound.get()))
                snapshot->sounds[slot].reset(copied);
        }
    }
    snapshots_.push_back(std::move(snapshot));

    // The history remains value-based from the caller's point of view.  The
    // opaque ID keeps large ATS matrices out of the byte deque while the
    // snapshot store owns the immutable deep copy.
    const auto id = static_cast<std::uint64_t>(snapshots_.size());
    std::vector<std::byte> bytes(sizeof(id));
    std::memcpy(bytes.data(), &id, sizeof(id));
    return bytes;
}

bool PluginModel::restoreSnapshot(const std::vector<std::byte>& bytes)
{
    if (bytes.size() != sizeof(std::uint64_t))
        return false;
    std::uint64_t id = 0;
    std::memcpy(&id, bytes.data(), sizeof(id));
    return restoreSnapshotId(id);
}

bool PluginModel::restoreSnapshotId(std::uint64_t id)
{
    if (id == 0 || id > snapshots_.size() || !snapshots_[id - 1])
        return false;
    const auto& snapshot = *snapshots_[id - 1];
    if (snapshot.parameters.size() != parameters_.size())
        return false;
    for (std::size_t index = 0; index < snapshot.parameters.size(); ++index)
        (void) parameters_.write(index, snapshot.parameters[index]);

    for (std::size_t slot = 0; slot < sources_.size(); ++slot)
    {
        if (!sources_[slot])
            continue;
        if (snapshot.sounds[slot])
        {
            auto* copied = copy_sound(snapshot.sounds[slot].get());
            if (copied == nullptr)
                return false;
            sources_[slot]->sound.reset(copied);
        }
        else
            sources_[slot]->sound.reset();
    }

    ++modelRevision_;
    invalidation_.invalidate(InvalidationKind::preview);
    return true;
}

int PluginModel::activeSourceSlot() const noexcept
{
    if (activeSourceSlot_ >= 0 && activeSourceSlot_ < static_cast<int>(sources_.size())
        && sources_[static_cast<std::size_t>(activeSourceSlot_)]
        && sources_[static_cast<std::size_t>(activeSourceSlot_)]->sound)
        return activeSourceSlot_;
    for (std::size_t slot = 0; slot < sources_.size(); ++slot)
        if (sources_[slot] && sources_[slot]->sound)
            return static_cast<int>(slot);
    return -1;
}

void PluginModel::publishModelMutation(std::string status, ats::ui::UiState state,
                                       std::string error)
{
    auto snapshot = telemetry_.snapshot();
    snapshot.state = state;
    snapshot.modelRevision = modelRevision_;
    snapshot.displayRevision = modelRevision_;
    snapshot.canUndo = history_.canUndo();
    snapshot.canRedo = history_.canRedo();
    snapshot.hasSourceA = sources_[0].has_value();
    snapshot.hasSourceB = sources_[1].has_value();
    const auto slot = activeSourceSlot();
    const auto* active = slot >= 0 ? &*sources_[static_cast<std::size_t>(slot)] : nullptr;
    snapshot.hasCurrentModel = active != nullptr && static_cast<bool>(active->sound);
    snapshot.hasResidual = snapshot.hasCurrentModel && active->sound.get()->res != nullptr;
    snapshot.hasPhase = snapshot.hasCurrentModel && active->sound.get()->pha != nullptr;
    snapshot.status = std::move(status);
    snapshot.error = std::move(error);
    snapshot.renderRunning = false;
    publishTelemetry(std::move(snapshot));
}

ats::ui::ActionResult PluginModel::handle(const ats::ui::EditorAction& action)
{
    ats::ui::ActionResult result;
    result.generation = generations_.next().value;

    const auto validation = ats::ui::validateAction(action);
    if (!validation.valid)
    {
        result.status = ats::ui::ActionStatus::failed;
        result.code = std::string(validation.code);
        result.text = std::string(validation.text);
        return result;
    }

    const auto unsupported = [&result](const char* code, const char* text) {
        result.status = ats::ui::ActionStatus::failed;
        result.code = code;
        result.text = text;
    };

    if (const auto* parameter = std::get_if<ats::ui::SetParameterAction>(&action))
    {
        const auto* descriptor = ats::ParameterRegistry::find(parameter->id);
        if (descriptor == nullptr)
        {
            unsupported("unknown_parameter", "parameter ID is not in the stable registry");
            return result;
        }
        if (!std::isfinite(parameter->value))
        {
            unsupported("invalid_parameter", "parameter value must be finite");
            return result;
        }
        const bool synthesis = descriptor->group == "Synthesis";
        std::vector<std::byte> directBefore;
        if (parameter->begin)
            activeGestureBefore_ = captureSnapshot();
        else if (!parameter->end && !activeGestureBefore_)
            directBefore = captureSnapshot();
        else if (parameter->end && !activeGestureBefore_)
            directBefore = captureSnapshot();

        if (!parameters_.write(parameter->id, parameter->value))
        {
            if (parameter->begin)
                activeGestureBefore_.reset();
            unsupported("parameter_rejected", "parameter value was rejected by its descriptor");
            return result;
        }

        if (parameter->end)
        {
            auto before = activeGestureBefore_.has_value()
                              ? std::move(*activeGestureBefore_)
                              : std::move(directBefore);
            activeGestureBefore_.reset();
            history_.begin(synthesis ? "Synthesis gesture" : "Parameter gesture", std::move(before));
            (void) history_.commit(captureSnapshot());
        }
        else if (!parameter->begin && !activeGestureBefore_)
        {
            history_.begin(synthesis ? "Synthesis parameter" : "Parameter", std::move(directBefore));
            (void) history_.commit(captureSnapshot());
        }

        ++modelRevision_;
        const auto invalidationGeneration = invalidation_.request(InvalidationKind::preview, MonotonicClock::now());
        (void)invalidationGeneration;
        auto snapshot = telemetry_.snapshot();
        snapshot.modelRevision = modelRevision_;
        snapshot.displayRevision = modelRevision_;
        snapshot.canUndo = history_.canUndo();
        snapshot.canRedo = history_.canRedo();
        snapshot.status = synthesis
                              ? (parameter->end ? "synthesis gesture committed" : "synthesis parameter updated")
                              : (parameter->end ? "parameter gesture committed" : "parameter updated");
        snapshot.state = synthesis ? ats::ui::UiState::resynthesizing : snapshot.state;
        snapshot.error.clear();
        publishTelemetry(std::move(snapshot));
        result.code = synthesis ? "synthesis_parameter_updated" : "parameter_updated";
        return result;
    }

    if (std::holds_alternative<ats::ui::CancelJobsAction>(action))
    {
        cancelJobs();
        auto snapshot = telemetry_.snapshot();
        snapshot.analysisRunning = false;
        snapshot.renderRunning = false;
        snapshot.progress = 0.0f;
        snapshot.status = "jobs cancelled";
        snapshot.error.clear();
        publishTelemetry(std::move(snapshot));
        return result;
    }

    if (const auto* wav = std::get_if<ats::ui::ImportWavAction>(&action))
        return importSource({ wav->path, wav->slot, SourceImportKind::audio }, result.generation);

    if (const auto* ats = std::get_if<ats::ui::ImportAtsAction>(&action))
        return importSource({ ats->path, ats->slot, SourceImportKind::ats }, result.generation);

    if (const auto* factory = std::get_if<ats::ui::ApplyFactoryPresetAction>(&action))
    {
        auto before = captureSnapshot();
        if (!ats::state::applyFactoryPreset(factory->index, parameters_))
        {
            unsupported("invalid_factory_preset", "factory preset index is out of range");
            return result;
        }
        history_.begin("Factory preset", std::move(before));
        (void) history_.commit(captureSnapshot());
        ++modelRevision_;
        auto snapshot = telemetry_.snapshot();
        snapshot.modelRevision = modelRevision_;
        snapshot.displayRevision = modelRevision_;
        snapshot.status = "factory preset applied";
        snapshot.canUndo = history_.canUndo();
        snapshot.canRedo = history_.canRedo();
        snapshot.error.clear();
        publishTelemetry(std::move(snapshot));
        return result;
    }

    if (const auto* history = std::get_if<ats::ui::HistoryAction>(&action))
    {
        const auto current = captureSnapshot();
        const auto target = history->redo ? history_.redo(current) : history_.undo(current);
        if (!target || !restoreSnapshot(*target))
        {
            result.status = ats::ui::ActionStatus::failed;
            result.code = history->redo ? "nothing_to_redo" : "nothing_to_undo";
            result.text = history->redo ? "redo history is empty" : "undo history is empty";
            return result;
        }
        publishModelMutation(history->redo ? "redo applied" : "undo applied",
                             telemetry_.snapshot().state);
        result.code = history->redo ? "redo_applied" : "undo_applied";
        return result;
    }

    if (const auto* partial = std::get_if<ats::ui::PartialEditAction>(&action))
    {
        const auto slot = activeSourceSlot();
        if (!partial->apply)
        {
            if (slot < 0)
            {
                unsupported("no_model", "analyse a source before selecting partials");
                return result;
            }
            publishModelMutation("partial cell focused", telemetry_.snapshot().state);
            result.code = "partial_focused";
            return result;
        }
        if (slot < 0 || !sources_[static_cast<std::size_t>(slot)]
            || !sources_[static_cast<std::size_t>(slot)]->sound)
        {
            unsupported("no_model", "analyse a source before editing partials");
            return result;
        }
        auto* sound = sources_[static_cast<std::size_t>(slot)]->sound.get();
        if (sound->amp == nullptr || sound->frq == nullptr
            || partial->partial >= sound->partials || partial->frame >= sound->frames)
        {
            unsupported("invalid_partial_cell", "partial or frame is outside the current model");
            return result;
        }
        auto before = captureSnapshot();
        sound->amp[partial->partial][partial->frame] = partial->amplitude;
        sound->frq[partial->partial][partial->frame] = partial->frequencyHz;
        history_.begin("Partial cell edit", std::move(before));
        (void) history_.commit(captureSnapshot());
        ++modelRevision_;
        invalidation_.invalidate(InvalidationKind::preview);
        publishModelMutation("partial edit applied", ats::ui::UiState::analyzed);
        result.code = "partial_edit_applied";
        return result;
    }

    if (const auto* math = std::get_if<ats::ui::ApplyMathAction>(&action))
    {
        const auto slot = activeSourceSlot();
        if (slot < 0 || !sources_[static_cast<std::size_t>(slot)]
            || !sources_[static_cast<std::size_t>(slot)]->sound)
        {
            unsupported("no_model", "analyse a source before applying math");
            return result;
        }
        auto running = telemetry_.snapshot();
        running.state = ats::ui::UiState::applyingMath;
        running.status = "applying MathExpr";
        running.error.clear();
        publishTelemetry(std::move(running));

        auto* copy = copy_sound(sources_[static_cast<std::size_t>(slot)]->sound.get());
        if (copy == nullptr)
        {
            unsupported("copy_failed", "could not snapshot the current model for MathExpr");
            publishModelMutation("MathExpr failed", ats::ui::UiState::error, result.text);
            return result;
        }
        ats::AtsSoundPtr edited { copy };
        std::string error;
        if (!ats::applyMathExprScript(edited.get(), math->script, error))
        {
            result.status = ats::ui::ActionStatus::failed;
            result.code = "math_failed";
            result.text = error;
            publishModelMutation("MathExpr failed", ats::ui::UiState::error, error);
            return result;
        }
        auto before = captureSnapshot();
        sources_[static_cast<std::size_t>(slot)]->sound = std::move(edited);
        history_.begin("MathExpr", std::move(before));
        (void) history_.commit(captureSnapshot());
        ++modelRevision_;
        invalidation_.invalidate(InvalidationKind::preview);
        publishModelMutation("MathExpr applied", ats::ui::UiState::analyzed);
        result.code = "math_applied";
        result.text = "MathExpr applied successfully";
        return result;
    }

    unsupported("action_not_bound", "action has no model service in the neutral composition root");
    return result;
}

void PluginModel::publishTelemetry(ats::ui::TelemetrySnapshot snapshot)
{
    snapshot.generation = generations_.current().value;
    telemetry_.publish(std::move(snapshot));
}

} // namespace ats::engine
