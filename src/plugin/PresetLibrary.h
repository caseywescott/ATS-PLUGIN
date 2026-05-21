#pragma once

#include <juce_core/juce_core.h>

class AtsAudioProcessor;

namespace ats_preset
{

juce::File getUserPresetDirectory();

/** Built-in parameter sets (no embedded ATS model). */
juce::StringArray factoryPresetNames();
juce::String factoryPresetMetadata(int factoryIndex);
void applyFactoryPreset(AtsAudioProcessor& proc, int factoryIndex);

} // namespace ats_preset
