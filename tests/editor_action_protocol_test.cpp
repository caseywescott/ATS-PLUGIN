#include "../src/ui/EditorActionProtocol.h"

#include <cassert>
#include <cmath>
#include <string>
#include <vector>

int main()
{
    using namespace ats::ui;
    std::vector<EditorAction> actions {
        SetParameterAction { "outputTrim", 1.0f, true, false }, ImportWavAction { { "a.wav" }, 0 },
        ImportAtsAction { { "a.ats" }, 0 }, ExportAtsAction { { "a.ats" }, 0 }, AnalyzeCurrentAction {},
        AnalyzeMorphSourceAction { 1 }, ApplyAnalysisProfileAction { 1 }, CancelJobsAction {}, PreviewAction { 0, 0, true }, StopPreviewAction {},
        SeekPreviewAction { 0.5f }, SetPreviewSmearAction { true }, MorphVariationAction { 1, true },
        SetMorphTimelineAction { { 0.0f, 1.0f } },
        BounceMorphTimelineAction { { "b.wav" }, 0 }, SetSpectralViewAction { 0, 0 }, ApplyFactoryPresetAction { 0 },
        SavePresetAction { { "a.atspreset" } }, LoadPresetAction { { "a.atspreset" } }, NoteAction { true, 60, 100 },
        HistoryAction { false }, PartialEditAction { 1, 2, true }, ResidualEditAction { 1, 2, 0.5f },
        SpectrumEditAction { SpectrumEditCommand::copyResidual, { 1, 2 }, 0, 4, 0.0f },
        ApplyMathAction { "amp *= 0.5;" }, DetectSmearnovAction {}, ApplySmearnovAction {},
        EditSmearnovOnsetsAction { { 0.2f, 0.8f } }, SetTimbreMotionPlanAction { { 0.0f, 1.0f } }
    };
    assert(actions.size() == 29);
    for (const auto& action : actions)
    {
        const auto contract = actionContract(action);
        const auto validation = validateAction(action);
        assert(!contract.name.empty());
        assert(validation.valid);
    }

    assert(!validateAction(EditorAction { SetParameterAction { {}, NAN, false, false } }).valid);
    assert(!validateAction(EditorAction { SeekPreviewAction { 2.0f } }).valid);
    assert(!validateAction(EditorAction { ApplyAnalysisProfileAction { 4 } }).valid);
    assert(!validateAction(EditorAction { MorphVariationAction { 3, true } }).valid);
    assert(!validateAction(EditorAction { ApplyMathAction { {} } }).valid);
    assert(!validateAction(EditorAction { EditSmearnovOnsetsAction { { -0.1f } } }).valid);
    assert(actionContract(EditorAction { ApplyMathAction { "x" } }).owner == ActionThreadDomain::worker);
    assert(actionContract(EditorAction { NoteAction { true, 60, 100 } }).owner == ActionThreadDomain::audioRt);
    assert(actionContract(EditorAction { SetParameterAction { "x", 0.5f, true, false } }).undo == ActionUndoPolicy::gesture);
    return 0;
}
