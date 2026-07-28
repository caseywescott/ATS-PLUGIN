#include "../src/engine/PluginModel.h"
#include "../src/ui/ParameterBinding.h"

#include <cassert>
#include <cmath>
#include <iostream>

int main()
{
    ats::engine::PluginModel model(1, 8);
    ats::ui::ParameterBinding binding(model.parameters(), "outputTrim", model);
    assert(binding.beginGesture(1.0f));
    assert(binding.updateGesture(1.25f));
    assert(binding.endGesture(1.25f));
    assert(model.processActions() == 3);
    auto result = model.results().tryPop();
    assert(result && result->status == ats::ui::ActionStatus::success);
    result = model.results().tryPop();
    assert(result && result->status == ats::ui::ActionStatus::success);
    result = model.results().tryPop();
    assert(result && result->status == ats::ui::ActionStatus::success);
    assert(std::abs(model.parameters().read("outputTrim") - 1.25f) < 1.0e-6f);
    const auto revision = model.telemetry().snapshot().modelRevision;
    assert(revision == 3);

    assert(model.actions().submit(ats::ui::SetParameterAction { "missing", 0.5f }));
    assert(model.processActions() == 1);
    result = model.results().tryPop();
    assert(result && result->status == ats::ui::ActionStatus::failed);
    assert(result->code == "unknown_parameter");
    assert(model.telemetry().snapshot().modelRevision == revision);

    assert(model.actions().submit(ats::ui::SetParameterAction { "outputTrim", NAN }));
    assert(model.processActions() == 1);
    result = model.results().tryPop();
    assert(result && result->code == "invalid_parameter");

    assert(model.actions().submit(ats::ui::CancelJobsAction {}));
    assert(model.processActions() == 1);
    result = model.results().tryPop();
    assert(result && result->status == ats::ui::ActionStatus::success);
    assert(model.telemetry().snapshot().status == "jobs cancelled");

    assert(model.actions().submit(ats::ui::PreviewAction { 0, 0, true }));
    assert(model.processActions() == 1);
    result = model.results().tryPop();
    assert(result && result->status == ats::ui::ActionStatus::failed);
    assert(result->code == "action_not_bound");
    assert(model.results().tryPop() == std::nullopt);
    std::cout << "plugin_model_test: typed action ownership and results ok\n";
}
