#include "../src/engine/AtsEngine.h"
#include "../src/engine/PluginModel.h"
#include "../src/ui/EditorActionProtocol.h"
#include "../src/ui/EditorShell.h"

#include <cassert>
#include <cmath>
#include <filesystem>
#include <vector>

namespace
{
std::filesystem::path writeFixture()
{
    std::vector<double> samples(32768);
    for (std::size_t index = 0; index < samples.size(); ++index)
        samples[index] = 0.25 * std::sin(2.0 * 3.141592653589793 * 440.0 *
                                          static_cast<double>(index) / 48000.0);
    ats::CancellationToken live;
    const auto analysed = ats::AtsEngine::analyze({ 1 }, live,
        { samples.data(), samples.size(), 48000, {} });
    assert(analysed.status == ats::JobResult<ats::AnalysisOutput>::Status::success);

    const auto path = std::filesystem::temp_directory_path() / "ats-plugin-model-surface-actions.ats";
    assert(ats_save(analysed.value.sound.get(), path.string().c_str(), -120.0, 1) >= 0);
    return path;
}

ats::ui::ActionResult popResult(ats::engine::PluginModel& model)
{
    const auto result = model.results().tryPop();
    assert(result.has_value());
    return *result;
}

void submitAndProcess(ats::engine::PluginModel& model, ats::ui::EditorAction action)
{
    assert(model.actions().submit(std::move(action)));
    assert(model.processActions() == 1);
}
}

int main()
{
    assert(!ats::ui::validateAction(ats::ui::EditorAction {
        ats::ui::PartialEditAction { 0, 0, true, -1.0, 440.0 } }).valid);
    assert(!ats::ui::validateAction(ats::ui::EditorAction {
        ats::ui::ApplyMathAction { " \n\t" } }).valid);

    ats::ui::EditorShellModel shell;
    assert(shell.selectPage(ats::ui::EditorPage::partials));
    shell.partials().beginCell(1, 2, 0.5, 440.0);
    assert(shell.partials().view().editing());
    assert(shell.selectPage(ats::ui::EditorPage::math));
    assert(!shell.partials().view().editing());
    shell.math().setDraft("a=a * 2");
    assert(shell.selectPage(ats::ui::EditorPage::analysis));
    assert(shell.math().draft() == "a=a * 2");

    ats::engine::PluginModel model(1, 32);
    const auto path = writeFixture();

    submitAndProcess(model, ats::ui::ImportAtsAction { { path.string() }, 0 });
    assert(popResult(model).status == ats::ui::ActionStatus::success);
    assert(model.source(0) != nullptr && model.source(0)->sound);

    const auto originalAmplitude = model.source(0)->sound.get()->amp[0][0];
    submitAndProcess(model, ats::ui::PartialEditAction { 0, 0, true, 0.75, 330.0 });
    auto result = popResult(model);
    assert(result.status == ats::ui::ActionStatus::success);
    assert(result.code == "partial_edit_applied");
    assert(std::abs(model.source(0)->sound.get()->amp[0][0] - 0.75) < 1.0e-9);
    assert(std::abs(model.source(0)->sound.get()->frq[0][0] - 330.0) < 1.0e-9);
    assert(model.telemetry().snapshot().status == "partial edit applied");

    submitAndProcess(model, ats::ui::HistoryAction { false });
    assert(popResult(model).code == "undo_applied");
    assert(std::abs(model.source(0)->sound.get()->amp[0][0] - originalAmplitude) < 1.0e-9);
    submitAndProcess(model, ats::ui::HistoryAction { true });
    assert(popResult(model).code == "redo_applied");
    assert(std::abs(model.source(0)->sound.get()->amp[0][0] - 0.75) < 1.0e-9);

    submitAndProcess(model, ats::ui::ApplyMathAction { "a=a * 2\nf=f + 10" });
    result = popResult(model);
    assert(result.status == ats::ui::ActionStatus::success);
    assert(result.code == "math_applied");
    assert(std::abs(model.source(0)->sound.get()->amp[0][0] - 1.5) < 1.0e-9);
    assert(std::abs(model.source(0)->sound.get()->frq[0][0] - 340.0) < 1.0e-9);
    assert(model.telemetry().snapshot().status == "MathExpr applied");

    const auto afterMath = model.source(0)->sound.get()->amp[0][0];
    submitAndProcess(model, ats::ui::ApplyMathAction { "amp = invalid$" });
    result = popResult(model);
    assert(result.status == ats::ui::ActionStatus::failed);
    assert(result.code == "math_failed");
    assert(model.source(0)->sound.get()->amp[0][0] == afterMath);
    assert(model.telemetry().snapshot().state == ats::ui::UiState::error);

    submitAndProcess(model, ats::ui::SetParameterAction { "freq", 2.0f, true, false });
    assert(popResult(model).code == "synthesis_parameter_updated");
    submitAndProcess(model, ats::ui::SetParameterAction { "freq", 2.5f, false, false });
    assert(popResult(model).code == "synthesis_parameter_updated");
    submitAndProcess(model, ats::ui::SetParameterAction { "freq", 3.0f, false, true });
    assert(popResult(model).code == "synthesis_parameter_updated");
    assert(model.parameters().read("freq") == 3.0f);
    assert(model.telemetry().snapshot().state == ats::ui::UiState::resynthesizing);

    submitAndProcess(model, ats::ui::HistoryAction { false });
    assert(popResult(model).code == "undo_applied");
    assert(model.parameters().read("freq") == 1.0f);
    submitAndProcess(model, ats::ui::HistoryAction { true });
    assert(popResult(model).code == "redo_applied");
    assert(model.parameters().read("freq") == 3.0f);

    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    return 0;
}
