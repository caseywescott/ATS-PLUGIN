#include "../src/ui/ParameterDisplay.h"

#include <cassert>
#include <iostream>

int main()
{
    const auto* duration = ats::ParameterRegistry::find("synDur");
    const auto* pivot = ats::ParameterRegistry::find("freqShapePivotHz");
    const auto* gain = ats::ParameterRegistry::find("partGain");
    assert(duration && pivot && gain);
    // Legacy SynthTabPanel textFromValueFunction formats.
    assert(ats::ui::sliderValueText(*duration, 0.0f) == "FULL");
    assert(ats::ui::sliderValueText(*duration, 1.5f) == "1.50 s");
    assert(ats::ui::sliderValueText(*pivot, 0.0f) == "Auto");
    assert(ats::ui::sliderValueText(*pivot, 440.0f) == "440 Hz");
    // Everything else keeps the descriptor text.
    assert(ats::ui::sliderValueText(*gain, 1.0f) == gain->text(1.0f));
    std::cout << "parameter_display_test: legacy slider readouts ok\n";
    return 0;
}
