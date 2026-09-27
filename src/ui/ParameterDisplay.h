#pragma once

#include "../core/ParameterRegistry.h"

#include <cstdio>
#include <string>

namespace ats::ui
{

// Editor-side value readouts. Most parameters show the descriptor's text; the
// legacy SynthTabPanel gave two sliders their own textFromValueFunction:
// synDur shows "FULL" for 0 (the whole sound) and seconds otherwise, and the
// frequency-shape pivot shows "Auto" for 0 and whole hertz otherwise.
inline std::string sliderValueText(const ParameterDescriptor& descriptor, float value)
{
    const auto snapped = descriptor.snap(value);
    char buffer[32] {};
    if (descriptor.id == "synDur")
    {
        if (snapped <= 0.0005f)
            return "FULL";
        std::snprintf(buffer, sizeof(buffer), "%.2f s", static_cast<double>(snapped));
        return buffer;
    }
    if (descriptor.id == "freqShapePivotHz")
    {
        if (snapped <= 0.005f)
            return "Auto";
        std::snprintf(buffer, sizeof(buffer), "%.0f Hz", static_cast<double>(snapped));
        return buffer;
    }
    return descriptor.text(value);
}

} // namespace ats::ui
