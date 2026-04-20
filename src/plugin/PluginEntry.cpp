#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AtsAudioProcessor();
}
