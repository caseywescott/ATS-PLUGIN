#include "PluginModel.h"
#include "../state/FactoryPresetState.h"

#include <cmath>
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

ats::ui::ActionResult PluginModel::handle(const ats::ui::EditorAction& action)
{
    ats::ui::ActionResult result;
    result.generation = generations_.next().value;

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
        if (!parameters_.write(parameter->id, parameter->value))
        {
            unsupported("parameter_rejected", "parameter value was rejected by its descriptor");
            return result;
        }

        ++modelRevision_;
        const auto invalidationGeneration = invalidation_.request(InvalidationKind::preview, MonotonicClock::now());
        (void)invalidationGeneration;
        auto snapshot = telemetry_.snapshot();
        snapshot.modelRevision = modelRevision_;
        snapshot.displayRevision = modelRevision_;
        snapshot.canUndo = history_.canUndo();
        snapshot.canRedo = history_.canRedo();
        snapshot.status = parameter->end ? "parameter gesture committed" : "parameter updated";
        snapshot.error.clear();
        publishTelemetry(std::move(snapshot));
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
        if (!ats::state::applyFactoryPreset(factory->index, parameters_))
        {
            unsupported("invalid_factory_preset", "factory preset index is out of range");
            return result;
        }
        ++modelRevision_;
        auto snapshot = telemetry_.snapshot();
        snapshot.modelRevision = modelRevision_;
        snapshot.displayRevision = modelRevision_;
        snapshot.status = "factory preset applied";
        snapshot.error.clear();
        publishTelemetry(std::move(snapshot));
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
