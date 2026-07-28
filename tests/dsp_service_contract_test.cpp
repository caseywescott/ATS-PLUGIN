#include "../src/engine/AtsAnalysisService.h"
#include "../src/dsp/AtsResynthesisService.h"
#include "../src/dsp/FrequencyShapeProcessor.h"
#include "../src/dsp/MathExpressionService.h"
#include "../src/dsp/MorphCorrespondenceBuilder.h"
#include "../src/dsp/MorphGridBuilder.h"
#include "../src/dsp/MorphPhaseService.h"
#include "../src/dsp/MorphRecipeService.h"
#include "../src/dsp/MorphResidualService.h"
#include "../src/dsp/MorphTransientService.h"
#include "../src/dsp/Resampler.h"
#include "../src/dsp/SmearnovDetector.h"
#include "../src/dsp/SmearnovTransformer.h"
#include "../src/dsp/TimbreMotionService.h"

#include <cassert>
#include <cmath>

int main()
{
    const float input[] { 0.0f, 1.0f, 0.0f };
    std::vector<float> output;
    ats::dsp::Resampler::renderMono(input, 3, 48000.0, output, 48000.0);
    assert(output.size() == 3 && output[1] == 1.0f);

    ats::FrequencyShapeSettings shape;
    shape.mode = ats::FrequencyShapeMode::Stretch;
    shape.amount = 2.0;
    shape.resolvedPivotHz = 200.0;
    assert(std::abs(ats::dsp::FrequencyShapeProcessor::apply(400.0, 1, shape, 24000.0) - 800.0) < 1.0e-9);

    auto plan = ats::dsp::TimbreMotionService::deserialize("");
    assert(plan.empty());
    assert(!ats::dsp::MorphRecipeService::serialize(ats::dsp::MorphRecipeService::defaults()).empty());
    const auto grid = ats::dsp::MorphGridBuilder::bark(8, 20.0, 20000.0);
    assert(grid.edgeFrequenciesHz.size() == 9);

    ats::MorphPhaseInput phase;
    phase.hasSourcePhase = phase.hasTargetPhase = true;
    const auto resolved = ats::dsp::MorphPhaseService::resolve(phase, 0.5);
    assert(resolved.valid);
    const auto transient = ats::dsp::MorphTransientService::analyze({}, {});
    assert(transient.warningCount == 0);
    const auto residual = ats::dsp::MorphResidualService::evaluate({}, {});
    assert(residual.bands.empty());
    return 0;
}
