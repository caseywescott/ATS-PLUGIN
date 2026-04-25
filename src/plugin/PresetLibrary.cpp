#include "PresetLibrary.h"

#include "PluginProcessor.h"

namespace ats_preset
{

juce::File getUserPresetDirectory()
{
    auto dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                   .getChildFile("ATS Resynth")
                   .getChildFile("Presets");
    dir.createDirectory();
    return dir;
}

juce::StringArray factoryPresetNames()
{
    return juce::StringArray(
        // Synthesis presets
        "Default",
        "Bright partials",
        "Residual emphasis",
        "Residuals only",
        // Envelope transfer presets (envDecolor / envRecolor / envMix / envResidualMix)
        "Env: All-purpose (conservative)",
        "Env: Sustained instrument",
        "Env: Voice / singing",
        "Env: Inharmonic metal / bell",
        "Env: Noisy texture",
        "Env: Plucked / percussive");
}

static void setNorm(juce::AudioProcessorValueTreeState& apvts, const char* id, float norm01)
{
    if (auto* p = apvts.getParameter(id))
        p->setValueNotifyingHost(norm01);
}

static void applyDefaults(AtsAudioProcessor& proc)
{
    auto& apvts = proc.apvts;
    setNorm(apvts, "partGain", apvts.getParameterRange("partGain").convertTo0to1(1.0f));
    setNorm(apvts, "resGain", apvts.getParameterRange("resGain").convertTo0to1(1.0f));
    setNorm(apvts, "freq", apvts.getParameterRange("freq").convertTo0to1(1.0f));
    setNorm(apvts, "timeStretch", apvts.getParameterRange("timeStretch").convertTo0to1(1.0f));
    setNorm(apvts, "synStart", apvts.getParameterRange("synStart").convertTo0to1(0.0f));
    setNorm(apvts, "synDur", apvts.getParameterRange("synDur").convertTo0to1(0.0f));
    if (auto* p = apvts.getParameter("usePhase"))
        p->setValueNotifyingHost(0.0f);
    if (auto* p = apvts.getParameter("useAllPartials"))
        p->setValueNotifyingHost(1.0f);
    if (auto* p = apvts.getParameter("residualOnly"))
        p->setValueNotifyingHost(0.0f);
    setNorm(apvts, "outputTrim", apvts.getParameterRange("outputTrim").convertTo0to1(1.0f));
}

static void applyEnvelopeTransferDefaults(AtsAudioProcessor& proc)
{
    // Reset envDecolor / envRecolor / envMix / envResidualMix to neutral values;
    // transient policy defaults to "Preserve source" (choice index 0).
    auto& apvts = proc.apvts;
    setNorm(apvts, "envDecolor",     apvts.getParameterRange("envDecolor").convertTo0to1(1.0f));
    setNorm(apvts, "envRecolor",     apvts.getParameterRange("envRecolor").convertTo0to1(1.0f));
    setNorm(apvts, "envMix",         apvts.getParameterRange("envMix").convertTo0to1(1.0f));
    setNorm(apvts, "envResidualMix", apvts.getParameterRange("envResidualMix").convertTo0to1(0.0f));
    if (auto* p = apvts.getParameter("envTransientPolicy"))
        p->setValueNotifyingHost(0.0f); // "Preserve source"
}

void applyFactoryPreset(AtsAudioProcessor& proc, int factoryIndex)
{
    auto& apvts = proc.apvts;

    switch (factoryIndex)
    {
    // ---- Synthesis presets ----
    case 0:
        applyDefaults(proc);
        applyEnvelopeTransferDefaults(proc);
        break;
    case 1:
        applyDefaults(proc);
        applyEnvelopeTransferDefaults(proc);
        setNorm(apvts, "partGain", apvts.getParameterRange("partGain").convertTo0to1(1.6f));
        setNorm(apvts, "resGain",  apvts.getParameterRange("resGain").convertTo0to1(0.45f));
        setNorm(apvts, "freq",     apvts.getParameterRange("freq").convertTo0to1(1.05f));
        break;
    case 2:
        applyDefaults(proc);
        applyEnvelopeTransferDefaults(proc);
        setNorm(apvts, "partGain", apvts.getParameterRange("partGain").convertTo0to1(0.55f));
        setNorm(apvts, "resGain",  apvts.getParameterRange("resGain").convertTo0to1(1.75f));
        break;
    case 3:
        applyDefaults(proc);
        applyEnvelopeTransferDefaults(proc);
        if (auto* p = apvts.getParameter("residualOnly"))
            p->setValueNotifyingHost(1.0f);
        setNorm(apvts, "resGain",  apvts.getParameterRange("resGain").convertTo0to1(1.0f));
        setNorm(apvts, "partGain", apvts.getParameterRange("partGain").convertTo0to1(0.0f));
        break;

    // ---- Envelope transfer presets ----

    // Case 4: All-purpose (conservative)
    // Estimator: RDC Bark order 16, moderate regularization
    // Half de-color so source character is preserved; half re-color to blend in target.
    // Residual unchanged; preserve source transients.
    case 4:
        applyDefaults(proc);
        setNorm(apvts, "envDecolor",     apvts.getParameterRange("envDecolor").convertTo0to1(0.55f));
        setNorm(apvts, "envRecolor",     apvts.getParameterRange("envRecolor").convertTo0to1(0.55f));
        setNorm(apvts, "envMix",         apvts.getParameterRange("envMix").convertTo0to1(0.75f));
        setNorm(apvts, "envResidualMix", apvts.getParameterRange("envResidualMix").convertTo0to1(0.0f));
        if (auto* p = apvts.getParameter("envTransientPolicy"))
            p->setValueNotifyingHost(0.0f); // Preserve source
        break;

    // Case 5: Sustained instrument
    // Full de-color + full re-color for maximal timbral transfer.
    // Residual receives some target color to match overall brightness.
    // Transients: preserve source so attacks stay clean.
    case 5:
        applyDefaults(proc);
        setNorm(apvts, "envDecolor",     apvts.getParameterRange("envDecolor").convertTo0to1(1.0f));
        setNorm(apvts, "envRecolor",     apvts.getParameterRange("envRecolor").convertTo0to1(1.0f));
        setNorm(apvts, "envMix",         apvts.getParameterRange("envMix").convertTo0to1(1.0f));
        setNorm(apvts, "envResidualMix", apvts.getParameterRange("envResidualMix").convertTo0to1(0.3f));
        if (auto* p = apvts.getParameter("envTransientPolicy"))
            p->setValueNotifyingHost(0.0f); // Preserve source
        break;

    // Case 6: Voice / singing
    // Full de-color, moderate re-color; envelope mix at 0.85 to leave a trace of source formants.
    // Residual gets moderate coloring to carry breath/vowel character.
    // Transients: crossfade so plosives don't click.
    case 6:
        applyDefaults(proc);
        setNorm(apvts, "envDecolor",     apvts.getParameterRange("envDecolor").convertTo0to1(1.0f));
        setNorm(apvts, "envRecolor",     apvts.getParameterRange("envRecolor").convertTo0to1(0.85f));
        setNorm(apvts, "envMix",         apvts.getParameterRange("envMix").convertTo0to1(0.85f));
        setNorm(apvts, "envResidualMix", apvts.getParameterRange("envResidualMix").convertTo0to1(0.5f));
        if (auto* p = apvts.getParameter("envTransientPolicy"))
            p->setValueNotifyingHost(2.0f / 3.0f); // Crossfade (index 2)
        break;

    // Case 7: Inharmonic metal / bell
    // Full de-color to strip irregular spectral bumps; full re-color with target shape.
    // Residual unchanged (inharmonic sources have meaningful residual character).
    // Transients: bypass during attacks to preserve metallic click.
    case 7:
        applyDefaults(proc);
        setNorm(apvts, "envDecolor",     apvts.getParameterRange("envDecolor").convertTo0to1(1.0f));
        setNorm(apvts, "envRecolor",     apvts.getParameterRange("envRecolor").convertTo0to1(1.0f));
        setNorm(apvts, "envMix",         apvts.getParameterRange("envMix").convertTo0to1(1.0f));
        setNorm(apvts, "envResidualMix", apvts.getParameterRange("envResidualMix").convertTo0to1(0.0f));
        if (auto* p = apvts.getParameter("envTransientPolicy"))
            p->setValueNotifyingHost(1.0f); // Bypass (index 3 -> normalized 1.0)
        break;

    // Case 8: Noisy texture
    // Partial transfer is light (source noise character preserved); residual gets full color.
    // Transients: target color so the overall noise texture matches target brightness.
    case 8:
        applyDefaults(proc);
        setNorm(apvts, "envDecolor",     apvts.getParameterRange("envDecolor").convertTo0to1(0.4f));
        setNorm(apvts, "envRecolor",     apvts.getParameterRange("envRecolor").convertTo0to1(0.6f));
        setNorm(apvts, "envMix",         apvts.getParameterRange("envMix").convertTo0to1(0.6f));
        setNorm(apvts, "envResidualMix", apvts.getParameterRange("envResidualMix").convertTo0to1(1.0f));
        if (auto* p = apvts.getParameter("envTransientPolicy"))
            p->setValueNotifyingHost(1.0f / 3.0f); // Target color (index 1)
        break;

    // Case 9: Plucked / percussive
    // Moderate de-color + re-color; attack sharpness preserved by bypassing transfer on transients.
    // Residual left unchanged to avoid smearing decay noise.
    case 9:
        applyDefaults(proc);
        setNorm(apvts, "envDecolor",     apvts.getParameterRange("envDecolor").convertTo0to1(0.7f));
        setNorm(apvts, "envRecolor",     apvts.getParameterRange("envRecolor").convertTo0to1(0.7f));
        setNorm(apvts, "envMix",         apvts.getParameterRange("envMix").convertTo0to1(0.8f));
        setNorm(apvts, "envResidualMix", apvts.getParameterRange("envResidualMix").convertTo0to1(0.0f));
        if (auto* p = apvts.getParameter("envTransientPolicy"))
            p->setValueNotifyingHost(1.0f); // Bypass on transients (index 3)
        break;

    default:
        break;
    }
}

} // namespace ats_preset
