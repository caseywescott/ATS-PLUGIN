#include "../src/ui/EditorPageModels.h"

#include <cassert>
#include <chrono>
#include <limits>

int main()
{
    ats::ui::AnalysisPageModel analysis;
    assert(analysis.controls().size() == 18);
    assert(analysis.controls()[0].id == "analysisType");
    assert(analysis.setControl(9, -48.0f));
    assert(analysis.profile() == ats::ui::AnalysisProfile::custom);
    assert(!analysis.setControl(9, std::numeric_limits<float>::quiet_NaN()));
    analysis.setHasSource(true);
    analysis.setWorkerRunning(true);
    assert(analysis.canCancel());
    assert(analysis.analyzeSourceAction(1).slot == 1);

    ats::ui::SynthesisPageModel synthesis;
    assert(synthesis.shapeControlsDimmed());
    synthesis.setShapeMode(ats::ui::FrequencyShapeMode::stretch);
    assert(!synthesis.shapeControlsDimmed());
    assert(synthesis.setDuration(2.5f));
    assert(synthesis.durationLabel().find("2.5") != std::string::npos);
    assert(synthesis.setDuration(0.0f));
    assert(synthesis.durationLabel() == "FULL");
    assert(!synthesis.setDuration(-1.0f));

    ats::ui::PartialPageModel partials;
    partials.setDimensions(8, 16);
    partials.beginCell(3, 4, 0.25, 440.0);
    partials.setDraft(0.5, 880.0);
    assert(partials.commitCell().has_value());
    partials.beginCell(9, 4, 0.5, 880.0);
    assert(!partials.commitCell().has_value());

    ats::ui::MathPageModel math;
    math.setDraft("amp = amp * 0.9");
    assert(math.canApply());
    assert(math.applyAction()->script == "amp = amp * 0.9");
    assert(math.beginApply());
    assert(!math.canApply());
    const auto now = std::chrono::steady_clock::now();
    math.complete(true, {}, now);
    assert(math.successFlashActive(now + std::chrono::milliseconds(599)));
    assert(!math.successFlashActive(now + std::chrono::milliseconds(601)));
    return 0;
}
