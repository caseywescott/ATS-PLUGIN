#pragma once

#include "RenderInvalidationCoordinator.h"
#include "SourceImportService.h"
#include "../core/JobScheduler.h"
#include "../core/ParameterRegistry.h"
#include "../core/UndoHistory.h"
#include "../ui/UiActionDispatcher.h"

#include <cstddef>
#include <array>
#include <optional>
#include <utility>

namespace ats::engine
{

// Composition root for the host-neutral processor model. It is deliberately
// independent of a plugin ABI, UI backend, and filesystem implementation.
class PluginModel final : public ats::ui::ActionSink
{
public:
    explicit PluginModel(std::size_t workerCount = 1, std::size_t actionCapacity = 256);
    PluginModel(const PluginModel&) = delete;
    PluginModel& operator=(const PluginModel&) = delete;

    [[nodiscard]] ats::ParameterStore& parameters() noexcept { return parameters_; }
    [[nodiscard]] const ats::ParameterStore& parameters() const noexcept { return parameters_; }
    [[nodiscard]] ats::ui::UiActionDispatcher& actions() noexcept { return actions_; }
    [[nodiscard]] ats::ui::ActionResultDispatcher& results() noexcept { return results_; }
    [[nodiscard]] ats::ui::TelemetryPublisher& telemetry() noexcept { return telemetry_; }
    [[nodiscard]] const ats::ui::TelemetryPublisher& telemetry() const noexcept { return telemetry_; }
    [[nodiscard]] RenderInvalidationCoordinator& invalidation() noexcept { return invalidation_; }
    [[nodiscard]] JobScheduler& scheduler() noexcept { return scheduler_; }
    [[nodiscard]] const ImportedSource* source(int slot) const noexcept
    {
        return slot >= 0 && slot < static_cast<int>(sources_.size()) && sources_[static_cast<std::size_t>(slot)]
                   ? &*sources_[static_cast<std::size_t>(slot)]
                   : nullptr;
    }

    void dispatch(ats::ui::EditorAction action) override
    { actions_.submit(std::move(action)); }

    // Runs on the model/UI dispatcher domain. Each consumed action produces
    // exactly one result, including validation and unsupported-action errors.
    [[nodiscard]] std::size_t processActions(std::size_t maximum = 64);

    void cancelJobs() noexcept;

private:
    [[nodiscard]] ats::ui::ActionResult handle(const ats::ui::EditorAction& action);
    void publishTelemetry(ats::ui::TelemetrySnapshot snapshot);
    [[nodiscard]] ats::ui::ActionResult importSource(const SourceImportRequest& request,
                                                      std::uint64_t generation);

    ats::ParameterStore parameters_;
    ats::ui::UiActionDispatcher actions_;
    ats::ui::ActionResultDispatcher results_;
    ats::ui::TelemetryPublisher telemetry_;
    ats::GenerationSource generations_;
    ats::CancellationSource cancellation_;
    JobScheduler scheduler_;
    RenderInvalidationCoordinator invalidation_;
    ats::UndoHistory history_;
    std::array<std::optional<ImportedSource>, 2> sources_;
    std::uint64_t modelRevision_ = 0;
};

} // namespace ats::engine
