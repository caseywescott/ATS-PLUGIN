#include "AnalysisTabPanel.h"
#include "UiFonts.h"

#include "PluginProcessor.h"

#include <cmath>

namespace
{
struct SliderSpec
{
    const char* label;
    const char* parameterID;
    const char* tooltip;
};

struct AnalysisProfile
{
    const char* name;
    const char* description;
    float type;
    float start;
    float duration;
    float lowestFreq;
    float highestFreq;
    float freqDev;
    float winCycles;
    float winType;
    float hopSize;
    float lowestMag;
    float trackLen;
    float minSegLen;
    float minGapLen;
    float smrThreshold;
    float minSegSmr;
    float lastPeakContribution;
    float smrContribution;
};

const std::array<SliderSpec, AnalysisTabPanel::kSliderCount> kSliderSpecs {{
    { "Start (s)", ats_param::kAnalysisStart, "Where analysis begins in the source file. Raise this to skip silence or noisy lead-ins." },
    { "Duration (s)", ats_param::kAnalysisDuration, "How much audio to analyze. Use 0 for full file, or shorten for faster iteration." },
    { "Lowest frequency (Hz)", ats_param::kAnalysisLowestFreq, "Lowest partial frequency to track. Raise for voice/instruments without bass to reduce muddy tracks." },
    { "Highest frequency (Hz)", ats_param::kAnalysisHighestFreq, "Highest partial frequency to track. Lower it to avoid noisy high partials; raise for bright sounds." },
    { "Frequency deviation", ats_param::kAnalysisFreqDev, "How far a partial may move between frames. Raise for vibrato/glides; lower for stable pitched tones." },
    { "Window cycles", ats_param::kAnalysisWinCycles, "Analysis window length in low-frequency cycles. Raise for cleaner pitch detail; lower for sharper attacks." },
    { "Hop size ratio", ats_param::kAnalysisHopSize, "Frame spacing relative to window size. Lower gives smoother tracking but slower analysis." },
    { "Peak threshold (dB)", ats_param::kAnalysisLowestMag, "Quietest peaks to keep. More negative keeps detail; less negative gives cleaner, simpler models." },
    { "Track memory (frames)", ats_param::kAnalysisTrackLen, "How many frames the tracker uses to continue partials. Raise for legato/vibrato; lower for fast attacks." },
    { "Min segment length", ats_param::kAnalysisMinSegLen, "Drops partial tracks shorter than this. Lower keeps brief details; raise to remove flickery artifacts." },
    { "Min gap length", ats_param::kAnalysisMinGapLen, "Fills short gaps in partial tracks. Raise for smoother sustained tones; lower for percussive material." },
    { "SMR threshold (dB)", ats_param::kAnalysisSmrThreshold, "Minimum signal-to-mask ratio for kept partials. Lower keeps more partials; higher gives cleaner synthesis." },
    { "Min segment SMR (dB)", ats_param::kAnalysisMinSegSmr, "SMR required for short segments. Lower preserves weak details; higher removes short noisy tracks." },
    { "Last-peak weight", ats_param::kAnalysisLastPeakContribution, "Tracking bias toward the previous frequency. Raise for smoother partial paths; lower for agile movement." },
    { "SMR weight", ats_param::kAnalysisSmrContribution, "Tracking bias toward similar masking strength. Raise when tracks jump incorrectly between nearby peaks." },
}};

constexpr int kCustomProfileId = 1;
constexpr int kFirstProfileId = 2;

const std::array<AnalysisProfile, 9> kAnalysisProfiles {{
    { "Balanced",
      "General-purpose full analysis for most sources.",
      3.0f, 0.0f, 0.0f, 20.0f, 18000.0f, 0.10f, 4.0f, 1.0f, 0.25f, -60.0f, 3.0f, 3.0f, 3.0f, 30.0f, 60.0f, 0.20f, 0.50f },
    { "Pitched / Sustained",
      "Stable tracking for vocals, pads, bowed strings, winds, and held notes.",
      3.0f, 0.0f, 0.0f, 30.0f, 16000.0f, 0.08f, 6.0f, 1.0f, 0.22f, -65.0f, 6.0f, 5.0f, 8.0f, 28.0f, 55.0f, 0.35f, 0.55f },
    { "Percussive / Transient",
      "Shorter windows and less gap filling for drums, hits, and sharp attacks.",
      3.0f, 0.0f, 0.0f, 40.0f, 18000.0f, 0.22f, 2.0f, 2.0f, 0.16f, -58.0f, 2.0f, 1.0f, 1.0f, 35.0f, 70.0f, 0.12f, 0.45f },
    { "Plucked / Decay",
      "Hybrid tracking for guitar, piano, mallets, and decaying resonances.",
      3.0f, 0.0f, 0.0f, 30.0f, 18000.0f, 0.16f, 3.0f, 1.0f, 0.20f, -66.0f, 4.0f, 2.0f, 3.0f, 30.0f, 60.0f, 0.20f, 0.50f },
    { "Noisy / Textural",
      "Keeps more weak peaks and residual/noise detail for breath, cymbals, and noisy synths.",
      3.0f, 0.0f, 0.0f, 20.0f, 22000.0f, 0.18f, 3.0f, 1.0f, 0.18f, -78.0f, 3.0f, 1.0f, 2.0f, 18.0f, 45.0f, 0.12f, 0.35f },
    { "Vocal / Monophonic",
      "Voice and lead-line tracking with moderate vibrato tolerance.",
      3.0f, 0.0f, 0.0f, 50.0f, 14000.0f, 0.12f, 5.0f, 1.0f, 0.22f, -70.0f, 6.0f, 4.0f, 6.0f, 24.0f, 50.0f, 0.32f, 0.55f },
    { "Bright / Harmonic",
      "For bells, electric piano, metallic tones, and bright harmonic sources.",
      3.0f, 0.0f, 0.0f, 40.0f, 22000.0f, 0.12f, 4.0f, 1.0f, 0.20f, -72.0f, 4.0f, 3.0f, 4.0f, 32.0f, 62.0f, 0.22f, 0.58f },
    { "Draft / Fast",
      "Faster low-detail checks before committing to a heavier analysis.",
      0.0f, 0.0f, 0.0f, 50.0f, 12000.0f, 0.18f, 3.0f, 1.0f, 0.40f, -48.0f, 2.0f, 3.0f, 2.0f, 40.0f, 75.0f, 0.15f, 0.45f },
    { "High Detail / Morph",
      "Slower, richer full analysis for final morph source preparation.",
      3.0f, 0.0f, 0.0f, 20.0f, 22000.0f, 0.10f, 5.0f, 1.0f, 0.16f, -78.0f, 6.0f, 2.0f, 5.0f, 22.0f, 45.0f, 0.28f, 0.60f },
}};

void setupSectionLabel(juce::Label& label, const juce::String& text)
{
    label.setText(text, juce::dontSendNotification);
    label.setFont(UiFonts::heading());
    label.setJustificationType(juce::Justification::centredLeft);
    label.setColour(juce::Label::textColourId, juce::Colour(0xfff0f0f5));
}

void setupRowLabel(juce::Label& label, const juce::String& text, const juce::String& tooltip)
{
    label.setText(text, juce::dontSendNotification);
    label.setFont(UiFonts::body(11.5f));
    label.setJustificationType(juce::Justification::centredLeft);
    label.setMinimumHorizontalScale(0.72f);
    label.setHelpText(tooltip);
    label.setTooltip(tooltip);
}

void setupSlider(juce::Slider& slider, const juce::String& name, const juce::String& tooltip)
{
    slider.setName(name);
    slider.setSliderStyle(juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 84, 18);
    slider.setHelpText(tooltip);
    slider.setTooltip(tooltip);
}

void setPlainParameter(juce::AudioProcessorValueTreeState& apvts,
                       const juce::String& parameterID,
                       float value)
{
    if (auto* p = apvts.getParameter(parameterID))
        p->setValueNotifyingHost(apvts.getParameterRange(parameterID).convertTo0to1(value));
}

float getPlainParameter(juce::AudioProcessorValueTreeState& apvts,
                        const juce::String& parameterID,
                        float fallback)
{
    if (auto* value = apvts.getRawParameterValue(parameterID))
        return value->load();
    return fallback;
}

bool closeEnough(float a, float b)
{
    return std::abs(a - b) <= 0.011f;
}

bool currentMatchesProfile(juce::AudioProcessorValueTreeState& apvts, const AnalysisProfile& profile)
{
    return closeEnough(getPlainParameter(apvts, ats_param::kAnalysisType, 3.0f), profile.type)
           && closeEnough(getPlainParameter(apvts, ats_param::kAnalysisStart, 0.0f), profile.start)
           && closeEnough(getPlainParameter(apvts, ats_param::kAnalysisDuration, 0.0f), profile.duration)
           && closeEnough(getPlainParameter(apvts, ats_param::kAnalysisLowestFreq, 20.0f), profile.lowestFreq)
           && closeEnough(getPlainParameter(apvts, ats_param::kAnalysisHighestFreq, 18000.0f), profile.highestFreq)
           && closeEnough(getPlainParameter(apvts, ats_param::kAnalysisFreqDev, 0.10f), profile.freqDev)
           && closeEnough(getPlainParameter(apvts, ats_param::kAnalysisWinCycles, 4.0f), profile.winCycles)
           && closeEnough(getPlainParameter(apvts, ats_param::kAnalysisWinType, 1.0f), profile.winType)
           && closeEnough(getPlainParameter(apvts, ats_param::kAnalysisHopSize, 0.25f), profile.hopSize)
           && closeEnough(getPlainParameter(apvts, ats_param::kAnalysisLowestMag, -60.0f), profile.lowestMag)
           && closeEnough(getPlainParameter(apvts, ats_param::kAnalysisTrackLen, 3.0f), profile.trackLen)
           && closeEnough(getPlainParameter(apvts, ats_param::kAnalysisMinSegLen, 3.0f), profile.minSegLen)
           && closeEnough(getPlainParameter(apvts, ats_param::kAnalysisMinGapLen, 3.0f), profile.minGapLen)
           && closeEnough(getPlainParameter(apvts, ats_param::kAnalysisSmrThreshold, 30.0f), profile.smrThreshold)
           && closeEnough(getPlainParameter(apvts, ats_param::kAnalysisMinSegSmr, 60.0f), profile.minSegSmr)
           && closeEnough(getPlainParameter(apvts,
                                            ats_param::kAnalysisLastPeakContribution,
                                            0.20f),
                          profile.lastPeakContribution)
           && closeEnough(getPlainParameter(apvts, ats_param::kAnalysisSmrContribution, 0.50f),
                          profile.smrContribution);
}

void applyProfile(juce::AudioProcessorValueTreeState& apvts, const AnalysisProfile& profile)
{
    setPlainParameter(apvts, ats_param::kAnalysisType, profile.type);
    setPlainParameter(apvts, ats_param::kAnalysisStart, profile.start);
    setPlainParameter(apvts, ats_param::kAnalysisDuration, profile.duration);
    setPlainParameter(apvts, ats_param::kAnalysisLowestFreq, profile.lowestFreq);
    setPlainParameter(apvts, ats_param::kAnalysisHighestFreq, profile.highestFreq);
    setPlainParameter(apvts, ats_param::kAnalysisFreqDev, profile.freqDev);
    setPlainParameter(apvts, ats_param::kAnalysisWinCycles, profile.winCycles);
    setPlainParameter(apvts, ats_param::kAnalysisWinType, profile.winType);
    setPlainParameter(apvts, ats_param::kAnalysisHopSize, profile.hopSize);
    setPlainParameter(apvts, ats_param::kAnalysisLowestMag, profile.lowestMag);
    setPlainParameter(apvts, ats_param::kAnalysisTrackLen, profile.trackLen);
    setPlainParameter(apvts, ats_param::kAnalysisMinSegLen, profile.minSegLen);
    setPlainParameter(apvts, ats_param::kAnalysisMinGapLen, profile.minGapLen);
    setPlainParameter(apvts, ats_param::kAnalysisSmrThreshold, profile.smrThreshold);
    setPlainParameter(apvts, ats_param::kAnalysisMinSegSmr, profile.minSegSmr);
    setPlainParameter(apvts,
                      ats_param::kAnalysisLastPeakContribution,
                      profile.lastPeakContribution);
    setPlainParameter(apvts, ats_param::kAnalysisSmrContribution, profile.smrContribution);
}

void placeEvenly(juce::Rectangle<int> row, std::initializer_list<juce::Component*> components)
{
    int remaining = (int) components.size();
    if (remaining <= 0)
        return;

    for (auto* component : components)
    {
        auto cell = row.removeFromLeft(row.getWidth() / remaining);
        component->setBounds(cell.reduced(3, 1));
        --remaining;
    }
}

juce::String compactSourceName(juce::String name)
{
    name = name.trim();
    if (name.isEmpty())
        return "No source assigned";
    if (name.length() <= 34)
        return name;
    return name.substring(0, 31) + "...";
}

juce::String formatSourceMeta(const AtsMorphSourceSlotInfo& info)
{
    if (!info.hasModel)
        return "No analyzed model yet";

    const auto name = info.sourceName.isNotEmpty() ? info.sourceName : info.slotName;
    juce::String text = compactSourceName(name);
    text << " | " << juce::String(info.durationSeconds, 2) << "s"
         << " | " << juce::String(info.partialCount) << " partials";
    return text;
}

juce::String formatSourceOverview(const AtsMorphSourceSlotInfo& info)
{
    if (!info.hasModel)
        return "Analyze a WAV here, then audition it from this card.";

    juce::String text = "Frames " + juce::String(info.frameCount)
                        + " | " + juce::String(info.sampleRate, 0) + " Hz"
                        + " | residual " + juce::String(info.hasResidual ? "yes" : "no");
    if (info.hasResidual && info.residualDiagnostics.activeBandFrameCount > 0)
    {
        text << " | bands " << info.residualDiagnostics.activeBandCount << "/" << ATSA_CRITICAL_BANDS
             << " | cells " << info.residualDiagnostics.activeBandFrameCount;
    }
    else if (info.hasResidual)
    {
        text << " | diagnostics unavailable";
    }
    return text;
}

bool isSupportedDropFile(const juce::File& file)
{
    return file.existsAsFile() && file.getFileExtension().equalsIgnoreCase(".wav");
}

juce::File getFirstSupportedDropFile(const juce::StringArray& files)
{
    for (const auto& path : files)
    {
        juce::File file(path);
        if (isSupportedDropFile(file))
            return file;
    }

    return {};
}
} // namespace

AnalysisTabPanel::AnalysisTabPanel(AtsAudioProcessor& p)
    : audioProcessor(p)
{
    workflowLabel.setText("Analysis workflow", juce::dontSendNotification);
    workflowLabel.setJustificationType(juce::Justification::centredLeft);
    workflowLabel.setFont(UiFonts::heading());
    addAndMakeVisible(workflowLabel);

    statusLabel.setJustificationType(juce::Justification::centredLeft);
    statusLabel.setFont(UiFonts::body(12.0f));
    statusLabel.setMinimumHorizontalScale(0.72f);
    addAndMakeVisible(statusLabel);

    errorLabel.setJustificationType(juce::Justification::centredLeft);
    errorLabel.setFont(UiFonts::body());
    errorLabel.setMinimumHorizontalScale(0.7f);
    errorLabel.setColour(juce::Label::textColourId, juce::Colour(0xffff8a80));
    addAndMakeVisible(errorLabel);

    importWavButton.setHelpText("Choose a WAV file as the current analysis source.");
    importWavButton.setTooltip("Choose a WAV file as the current analysis source.");
    importWavButton.onClick = [this] { pickFile(); };
    addAndMakeVisible(importWavButton);

    analyzeCurrentButton.setHelpText("Run analysis on the currently imported WAV with the settings below.");
    analyzeCurrentButton.setTooltip("Run analysis on the currently imported WAV with the settings below.");
    analyzeCurrentButton.onClick = [this] {
        audioProcessor.startAnalysis();
        refreshFromProcessor();
    };
    addAndMakeVisible(analyzeCurrentButton);

    analyzeSourceAButton.setHelpText("Choose a WAV file, analyze it with these settings, and assign it to morph source A.");
    analyzeSourceAButton.setTooltip("Choose a WAV file, analyze it with these settings, and assign it to morph source A.");
    analyzeSourceAButton.onClick = [this] { pickMorphSourceFile(0); };
    addAndMakeVisible(analyzeSourceAButton);

    analyzeSourceBButton.setHelpText("Choose a WAV file, analyze it with these settings, and assign it to morph source B.");
    analyzeSourceBButton.setTooltip("Choose a WAV file, analyze it with these settings, and assign it to morph source B.");
    analyzeSourceBButton.onClick = [this] { pickMorphSourceFile(1); };
    addAndMakeVisible(analyzeSourceBButton);

    cancelButton.setHelpText("Stop the current analysis or render job.");
    cancelButton.setTooltip("Stop the current analysis or render job.");
    cancelButton.onClick = [this] { cancelCurrentJob(); };
    addAndMakeVisible(cancelButton);

    stopPreviewButton.setHelpText("Stop source or morph preview playback.");
    stopPreviewButton.setTooltip("Stop source or morph preview playback.");
    stopPreviewButton.onClick = [this] {
        audioProcessor.stopPreviewPlayback();
        refreshFromProcessor();
    };
    addAndMakeVisible(stopPreviewButton);
    addAndMakeVisible(progressBar);

    auto setupSourceCard = [this](juce::GroupComponent& group,
                                  const juce::String& title,
                                  juce::Label& meta,
                                  juce::Label& overview,
                                  juce::TextButton& chooseButton,
                                  juce::TextButton& reanalyzeButton,
                                  juce::TextButton& playButton,
                                  juce::Label& auditionLabel,
                                  juce::ToggleButton& bothButton,
                                  juce::ToggleButton& partialsButton,
                                  juce::ToggleButton& residualButton,
                                  int slotIndex) {
        group.setText(title);
        group.setTextLabelPosition(juce::Justification::centredLeft);
        addAndMakeVisible(group);

        meta.setJustificationType(juce::Justification::centredLeft);
        meta.setFont(UiFonts::body());
        meta.setMinimumHorizontalScale(0.64f);
        meta.setColour(juce::Label::textColourId, juce::Colour(0xfff0f0f5));
        addAndMakeVisible(meta);

        overview.setJustificationType(juce::Justification::centredLeft);
        overview.setFont(UiFonts::small_());
        overview.setMinimumHorizontalScale(0.64f);
        overview.setColour(juce::Label::textColourId, juce::Colour(0xffb8c7d9));
        addAndMakeVisible(overview);

        chooseButton.setButtonText(slotIndex == 0 ? "Choose A" : "Choose B");
        chooseButton.setHelpText("Pick a WAV or ATS file for " + title + ".");
        chooseButton.setTooltip("Pick a WAV or ATS file for " + title + ".");
        chooseButton.onClick = [this, slotIndex] { pickMorphSourceFile(slotIndex); };
        addAndMakeVisible(chooseButton);

        reanalyzeButton.setHelpText("Re-run analysis for this source using the already chosen WAV and current settings.");
        reanalyzeButton.setTooltip("Re-run analysis for this source using the already chosen WAV and current settings.");
        reanalyzeButton.onClick = [this, slotIndex] { reanalyzeMorphSourceFile(slotIndex); };
        addAndMakeVisible(reanalyzeButton);

        playButton.setHelpText("Play this analyzed source from the beginning.");
        playButton.setTooltip("Play this analyzed source from the beginning.");
        playButton.onClick = [this, slotIndex] { playMorphSourceFile(slotIndex); };
        addAndMakeVisible(playButton);

        auditionLabel.setText("Audition", juce::dontSendNotification);
        auditionLabel.setJustificationType(juce::Justification::centredLeft);
        auditionLabel.setFont(UiFonts::small_());
        auditionLabel.setMinimumHorizontalScale(0.7f);
        auditionLabel.setHelpText("Choose which layer to audition when playing this source.");
        auditionLabel.setTooltip("Choose which layer to audition when playing this source.");
        addAndMakeVisible(auditionLabel);

        const int radioGroup = slotIndex == 0 ? 4101 : 4102;
        for (auto* button : { &bothButton, &partialsButton, &residualButton })
        {
            button->setRadioGroupId(radioGroup);
            button->setClickingTogglesState(true);
            button->setHelpText("Choose which layer to audition when playing this source.");
            button->setTooltip("Choose which layer to audition when playing this source.");
            addAndMakeVisible(button);
        }
        bothButton.setToggleState(true, juce::dontSendNotification);
        partialsButton.setHelpText("Play only the sinusoidal partial reconstruction for this source.");
        partialsButton.setTooltip("Play only the sinusoidal partial reconstruction for this source.");
        residualButton.setHelpText("Play only the residual/noise reconstruction for this source.");
        residualButton.setTooltip("Play only the residual/noise reconstruction for this source.");
    };
    setupSourceCard(sourceAGroup,
                    "Source A",
                    sourceAMetaLabel,
                    sourceAOverviewLabel,
                    analyzeSourceAButton,
                    reanalyzeSourceAButton,
                    playSourceAButton,
                    sourceAAuditionLabel,
                    sourceABothButton,
                    sourceAPartialsButton,
                    sourceAResidualButton,
                    0);
    setupSourceCard(sourceBGroup,
                    "Source B",
                    sourceBMetaLabel,
                    sourceBOverviewLabel,
                    analyzeSourceBButton,
                    reanalyzeSourceBButton,
                    playSourceBButton,
                    sourceBAuditionLabel,
                    sourceBBothButton,
                    sourceBPartialsButton,
                    sourceBResidualButton,
                    1);
    exportSourceAButton.setHelpText("Export Source A as ATS.");
    exportSourceAButton.onClick = [this] { exportMorphSourceFileAts(0); };
    exportSourceBButton.setHelpText("Export Source B as ATS.");
    exportSourceBButton.onClick = [this] { exportMorphSourceFileAts(1); };
    addAndMakeVisible(exportSourceAButton);
    addAndMakeVisible(exportSourceBButton);

    profileLabel.setText("Analysis Profile", juce::dontSendNotification);
    profileLabel.setJustificationType(juce::Justification::centredLeft);
    profileLabel.setFont(UiFonts::body(11.5f));
    profileLabel.setMinimumHorizontalScale(0.7f);
    profileLabel.setHelpText("Factory starting points for common source types. Apply changes parameters only; it does not analyze.");
    profileLabel.setTooltip("Factory starting points for common source types. Apply changes parameters only; it does not analyze.");
    addAndMakeVisible(profileLabel);

    analysisProfileBox.addItem("Custom", kCustomProfileId);
    for (size_t i = 0; i < kAnalysisProfiles.size(); ++i)
        analysisProfileBox.addItem(kAnalysisProfiles[i].name, kFirstProfileId + (int) i);
    analysisProfileBox.setHelpText("Choose a factory analysis profile, then Apply Profile before re-analyzing A or B.");
    analysisProfileBox.setTooltip("Choose a factory analysis profile, then Apply Profile before re-analyzing A or B.");
    analysisProfileBox.onChange = [this] {
        profileChoicePending = analysisProfileBox.getSelectedId() >= kFirstProfileId;
        applyProfileButton.setEnabled(profileChoicePending);
    };
    addAndMakeVisible(analysisProfileBox);

    applyProfileButton.setHelpText("Write the selected profile values into the analysis parameters below.");
    applyProfileButton.setTooltip("Write the selected profile values into the analysis parameters below.");
    applyProfileButton.onClick = [this] { applySelectedProfile(); };
    addAndMakeVisible(applyProfileButton);

    hintLabel.setText("Analysis settings are used by the next Session Analyze, Analyze Source A, or Analyze Source B action. "
                      "For cleaner resynthesis, lower the peak threshold to keep more quiet partials; for cleaner/noisier separation, use analysis type 4 full.",
                      juce::dontSendNotification);
    hintLabel.setJustificationType(juce::Justification::topLeft);
    hintLabel.setFont(UiFonts::heading(12.5f));
    hintLabel.setMinimumHorizontalScale(0.72f);
    content.addAndMakeVisible(hintLabel);

    resetButton.setHelpText("Restore Qatsh/ATSA defaults. Use this when analysis gets unstable or over-tuned.");
    resetButton.setTooltip("Restore Qatsh/ATSA defaults. Use this when analysis gets unstable or over-tuned.");
    resetButton.onClick = [this] { resetToClassicDefaults(); };
    content.addAndMakeVisible(resetButton);

    setupSectionLabel(sourceSectionLabel, "Source range");
    setupSectionLabel(spectralSectionLabel, "Spectral resolution and peak picking");
    setupSectionLabel(trackingSectionLabel, "Partial tracking and cleanup");
    content.addAndMakeVisible(sourceSectionLabel);
    content.addAndMakeVisible(spectralSectionLabel);
    content.addAndMakeVisible(trackingSectionLabel);

    const juce::String analysisTypeTooltip =
        "What data to save in the ATS model. Use 4 full for best resynthesis and morphing.";
    setupRowLabel(analysisTypeLabel, "Analysis type", analysisTypeTooltip);
    analysisTypeBox.addItemList(juce::StringArray { "1 amp+freq", "2 +phase", "3 +noise", "4 full" }, 1);
    analysisTypeBox.setHelpText(analysisTypeTooltip);
    analysisTypeBox.setTooltip(analysisTypeTooltip);
    content.addAndMakeVisible(analysisTypeLabel);
    content.addAndMakeVisible(analysisTypeBox);

    const juce::String windowTypeTooltip =
        "FFT window shape. Blackman-Harris is a safe default; try Hanning for sharper transient response.";
    setupRowLabel(windowTypeLabel, "Window type", windowTypeTooltip);
    windowTypeBox.addItemList(juce::StringArray { "Blackman", "Blackman-Harris", "Hanning", "Von Hann" }, 1);
    windowTypeBox.setHelpText(windowTypeTooltip);
    windowTypeBox.setTooltip(windowTypeTooltip);
    content.addAndMakeVisible(windowTypeLabel);
    content.addAndMakeVisible(windowTypeBox);

    const juce::String multiResTooltip =
        "Run a second, longer FFT window for the low band (below 500 Hz). Better bass partial "
        "separation; slower analysis. Off by default for bit-identical legacy behavior.";
    setupRowLabel(multiResLabel, "Multi-resolution", multiResTooltip);
    multiResButton.setHelpText(multiResTooltip);
    multiResButton.setTooltip(multiResTooltip);
    content.addAndMakeVisible(multiResLabel);
    content.addAndMakeVisible(multiResButton);

    for (size_t i = 0; i < kSliderSpecs.size(); ++i)
    {
        setupRowLabel(sliderLabels[i], kSliderSpecs[i].label, kSliderSpecs[i].tooltip);
        setupSlider(sliders[i], kSliderSpecs[i].label, kSliderSpecs[i].tooltip);
        content.addAndMakeVisible(sliderLabels[i]);
        content.addAndMakeVisible(sliders[i]);
        sliderAttachments[i] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            audioProcessor.apvts, kSliderSpecs[i].parameterID, sliders[i]);
    }

    analysisTypeAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.apvts, ats_param::kAnalysisType, analysisTypeBox);
    windowTypeAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.apvts, ats_param::kAnalysisWinType, windowTypeBox);
    multiResAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.apvts, ats_param::kAnalysisMultiRes, multiResButton);

    viewport.setViewedComponent(&content, false);
    viewport.setScrollBarsShown(true, false);
    addAndMakeVisible(viewport);

    refreshFromProcessor();
}

void AnalysisTabPanel::paintOverChildren(juce::Graphics& g)
{
    if (dropHoverSlot != 0 && dropHoverSlot != 1)
        return;

    const auto card = dropHoverSlot == 0 ? sourceAGroup.getBounds() : sourceBGroup.getBounds();
    if (card.isEmpty())
        return;

    auto highlight = card.toFloat().reduced(2.0f);
    g.saveState();
    g.reduceClipRegion(card);
    g.setColour(juce::Colour(0xff6f9eff).withAlpha(0.16f));
    g.fillRoundedRectangle(highlight, 7.0f);
    g.setColour(juce::Colour(0xff8fb6ff).withAlpha(0.95f));
    g.drawRoundedRectangle(highlight, 7.0f, 2.0f);

    auto messageArea = card.reduced(14, 12).removeFromTop(24);
    g.setColour(juce::Colour(0xfff5f8ff));
    g.setFont(UiFonts::body(12.0f));
    g.drawFittedText(juce::String("Drop WAV to analyze Source ") + (dropHoverSlot == 0 ? "A" : "B"),
                     messageArea,
                     juce::Justification::centred,
                     1);
    g.restoreState();
}

void AnalysisTabPanel::resized()
{
    auto outer = getLocalBounds().reduced(12);
    workflowLabel.setBounds({});
    statusLabel.setBounds({});
    importWavButton.setBounds({});
    analyzeCurrentButton.setBounds({});
    cancelButton.setBounds({});
    stopPreviewButton.setBounds({});

    const bool narrow = outer.getWidth() < 680;
    progressBar.setBounds(outer.removeFromTop(12));
    outer.removeFromTop(6);
    errorLabel.setBounds(outer.removeFromTop(18));
    outer.removeFromTop(8);

    auto sourceCards = outer.removeFromTop(narrow ? 232 : 112);
    auto placeSourceCard = [&](juce::Rectangle<int> card,
                               juce::GroupComponent& group,
                               juce::Label& meta,
                               juce::Label& overview,
                               juce::TextButton& chooseButton,
                               juce::TextButton& reanalyzeButton,
                               juce::TextButton& playButton,
                               juce::TextButton& exportButton,
                               juce::Label& auditionLabel,
                               juce::ToggleButton& bothButton,
                               juce::ToggleButton& partialsButton,
                               juce::ToggleButton& residualButton) {
        group.setBounds(card);
        auto inner = card.reduced(10, 14);
        auto buttonRow = inner.removeFromRight(narrow ? 132 : 192);
        chooseButton.setBounds(buttonRow.removeFromTop(24).reduced(2));
        buttonRow.removeFromTop(4);
        reanalyzeButton.setBounds(buttonRow.removeFromTop(24).reduced(2));
        buttonRow.removeFromTop(4);
        playButton.setBounds(buttonRow.removeFromTop(24).reduced(2));
        buttonRow.removeFromTop(4);
        exportButton.setBounds(buttonRow.removeFromTop(18).reduced(2));
        inner.removeFromRight(6);
        meta.setBounds(inner.removeFromTop(24));
        overview.setBounds(inner.removeFromTop(24));
        inner.removeFromTop(4);
        auto auditionRow = inner.removeFromTop(24);
        auditionLabel.setBounds(auditionRow.removeFromLeft(narrow ? 54 : 62));
        bothButton.setBounds(auditionRow.removeFromLeft(narrow ? 54 : 62));
        partialsButton.setBounds(auditionRow.removeFromLeft(narrow ? 72 : 82));
        residualButton.setBounds(auditionRow.removeFromLeft(narrow ? 78 : 88));
    };

    if (narrow)
    {
        placeSourceCard(sourceCards.removeFromTop(112).reduced(0, 1),
                        sourceAGroup,
                        sourceAMetaLabel,
                        sourceAOverviewLabel,
                        analyzeSourceAButton,
                        reanalyzeSourceAButton,
                        playSourceAButton,
                        exportSourceAButton,
                        sourceAAuditionLabel,
                        sourceABothButton,
                        sourceAPartialsButton,
                        sourceAResidualButton);
        sourceCards.removeFromTop(4);
        placeSourceCard(sourceCards.removeFromTop(112).reduced(0, 1),
                        sourceBGroup,
                        sourceBMetaLabel,
                        sourceBOverviewLabel,
                        analyzeSourceBButton,
                        reanalyzeSourceBButton,
                        playSourceBButton,
                        exportSourceBButton,
                        sourceBAuditionLabel,
                        sourceBBothButton,
                        sourceBPartialsButton,
                        sourceBResidualButton);
    }
    else
    {
        auto sourceA = sourceCards.removeFromLeft(sourceCards.getWidth() / 2).reduced(0, 1);
        sourceCards.removeFromLeft(8);
        auto sourceB = sourceCards.reduced(0, 1);
        placeSourceCard(sourceA,
                        sourceAGroup,
                        sourceAMetaLabel,
                        sourceAOverviewLabel,
                        analyzeSourceAButton,
                        reanalyzeSourceAButton,
                        playSourceAButton,
                        exportSourceAButton,
                        sourceAAuditionLabel,
                        sourceABothButton,
                        sourceAPartialsButton,
                        sourceAResidualButton);
        placeSourceCard(sourceB,
                        sourceBGroup,
                        sourceBMetaLabel,
                        sourceBOverviewLabel,
                        analyzeSourceBButton,
                        reanalyzeSourceBButton,
                        playSourceBButton,
                        exportSourceBButton,
                        sourceBAuditionLabel,
                        sourceBBothButton,
                        sourceBPartialsButton,
                        sourceBResidualButton);
    }

    outer.removeFromTop(8);
    auto profileRow = outer.removeFromTop(30);
    profileLabel.setBounds(profileRow.removeFromLeft(narrow ? 98 : 124));
    applyProfileButton.setBounds(profileRow.removeFromRight(narrow ? 112 : 132).reduced(2, 1));
    analysisProfileBox.setBounds(profileRow.reduced(2, 1));
    outer.removeFromTop(8);
    viewport.setBounds(outer);

    const int rowH = 38;
    const int sectionH = 28;
    const int gap = 8;
    const int margin = 12;
    const int contentWidth = juce::jmax(540, viewport.getWidth() - 20);
    const int contentHeight = margin * 2 + 66 + rowH * 18 + sectionH * 3 + gap * 10;
    content.setSize(contentWidth, contentHeight);

    auto r = content.getLocalBounds().reduced(margin);
    hintLabel.setBounds(r.removeFromTop(66));
    resetButton.setBounds(r.removeFromTop(28).removeFromLeft(180));
    r.removeFromTop(gap);

    auto placeRow = [&](juce::Label& label, juce::Component& control) {
        auto row = r.removeFromTop(rowH);
        label.setBounds(row.removeFromLeft(190));
        row.removeFromLeft(10);
        control.setBounds(row);
        r.removeFromTop(4);
    };
    auto placeSection = [&](juce::Label& label) {
        label.setBounds(r.removeFromTop(sectionH));
        r.removeFromTop(2);
    };

    placeSection(sourceSectionLabel);
    placeRow(analysisTypeLabel, analysisTypeBox);
    placeRow(sliderLabels[0], sliders[0]);
    placeRow(sliderLabels[1], sliders[1]);

    r.removeFromTop(gap);
    placeSection(spectralSectionLabel);
    placeRow(sliderLabels[2], sliders[2]);
    placeRow(sliderLabels[3], sliders[3]);
    placeRow(sliderLabels[4], sliders[4]);
    placeRow(sliderLabels[5], sliders[5]);
    placeRow(windowTypeLabel, windowTypeBox);
    placeRow(multiResLabel, multiResButton);
    placeRow(sliderLabels[6], sliders[6]);
    placeRow(sliderLabels[7], sliders[7]);

    r.removeFromTop(gap);
    placeSection(trackingSectionLabel);
    for (size_t i = 8; i < sliderLabels.size(); ++i)
        placeRow(sliderLabels[i], sliders[i]);
}

void AnalysisTabPanel::refreshFromProcessor()
{
    displayProgress = (double) audioProcessor.getAnalysisProgress();

    const bool workerBusy = audioProcessor.isWorkerBusy();
    const auto uiState = audioProcessor.getUiState();
    const bool hasCurrentSource = uiState != AtsUiState::NoSource;
    importWavButton.setEnabled(!workerBusy);
    analyzeCurrentButton.setEnabled(hasCurrentSource && !workerBusy);
    analyzeSourceAButton.setEnabled(!workerBusy);
    analyzeSourceBButton.setEnabled(!workerBusy);
    cancelButton.setEnabled(workerBusy);
    stopPreviewButton.setEnabled(audioProcessor.isPreviewPlaying());
    applyProfileButton.setEnabled(analysisProfileBox.getSelectedId() >= kFirstProfileId);
    refreshProfileSelection();

    AtsMorphSourceSlotInfo sourceA;
    AtsMorphSourceSlotInfo sourceB;
    audioProcessor.getMorphSourceSlotInfo(0, sourceA);
    audioProcessor.getMorphSourceSlotInfo(1, sourceB);
    juce::File sourceAFile;
    juce::File sourceBFile;
    const bool hasSourceAPath = audioProcessor.getMorphSourceFile(0, sourceAFile);
    const bool hasSourceBPath = audioProcessor.getMorphSourceFile(1, sourceBFile);
    const bool hasSourceAFile = hasSourceAPath && sourceAFile.existsAsFile();
    const bool hasSourceBFile = hasSourceBPath && sourceBFile.existsAsFile();
    sourceAMetaLabel.setText(sourceA.hasModel || !hasSourceAPath
                                 ? formatSourceMeta(sourceA)
                                 : compactSourceName(sourceAFile.getFileName()) + " | not analyzed",
                             juce::dontSendNotification);
    sourceBMetaLabel.setText(sourceB.hasModel || !hasSourceBPath
                                 ? formatSourceMeta(sourceB)
                                 : compactSourceName(sourceBFile.getFileName()) + " | not analyzed",
                             juce::dontSendNotification);
    sourceAOverviewLabel.setText(hasSourceAPath && !hasSourceAFile
                                     ? "Chosen Source A file is missing. Choose A again."
                                     : formatSourceOverview(sourceA),
                                 juce::dontSendNotification);
    sourceBOverviewLabel.setText(hasSourceBPath && !hasSourceBFile
                                     ? "Chosen Source B file is missing. Choose B again."
                                     : formatSourceOverview(sourceB),
                                 juce::dontSendNotification);
    const int pendingSlot = audioProcessor.getPendingMorphSourceSlot();
    if (pendingSlot == 0)
        sourceAOverviewLabel.setText("Analyzing into Source A...", juce::dontSendNotification);
    else if (pendingSlot == 1)
        sourceBOverviewLabel.setText("Analyzing into Source B...", juce::dontSendNotification);
    if (sourceA.hasModel && !sourceA.hasResidual && sourceAResidualButton.getToggleState())
        sourceABothButton.setToggleState(true, juce::dontSendNotification);
    if (sourceB.hasModel && !sourceB.hasResidual && sourceBResidualButton.getToggleState())
        sourceBBothButton.setToggleState(true, juce::dontSendNotification);
    reanalyzeSourceAButton.setEnabled(hasSourceAFile && !workerBusy);
    reanalyzeSourceBButton.setEnabled(hasSourceBFile && !workerBusy);
    playSourceAButton.setEnabled(sourceA.hasModel && !workerBusy);
    playSourceBButton.setEnabled(sourceB.hasModel && !workerBusy);
    exportSourceAButton.setEnabled(sourceA.hasModel && !workerBusy);
    exportSourceBButton.setEnabled(sourceB.hasModel && !workerBusy);
    sourceAResidualButton.setEnabled(!sourceA.hasModel || sourceA.hasResidual);
    sourceBResidualButton.setEnabled(!sourceB.hasModel || sourceB.hasResidual);

    switch (uiState)
    {
    case AtsUiState::NoSource:
        statusLabel.setText("No source loaded", juce::dontSendNotification);
        break;
    case AtsUiState::ReadyToAnalyze:
        statusLabel.setText("Ready to analyze current WAV", juce::dontSendNotification);
        break;
    case AtsUiState::Analyzing:
    {
        if (pendingSlot == 0 || pendingSlot == 1)
            statusLabel.setText("Analyzing Source " + juce::String(pendingSlot == 0 ? "A" : "B"),
                                juce::dontSendNotification);
        else
            statusLabel.setText("Analyzing current WAV", juce::dontSendNotification);
        break;
    }
    case AtsUiState::Analyzed:
        statusLabel.setText("Analysis ready", juce::dontSendNotification);
        break;
    case AtsUiState::Resynthesizing:
        statusLabel.setText("Rendering preview", juce::dontSendNotification);
        break;
    case AtsUiState::ApplyingMath:
        statusLabel.setText("Applying math transform", juce::dontSendNotification);
        break;
    case AtsUiState::Error:
        statusLabel.setText("Analysis error", juce::dontSendNotification);
        break;
    }

    const auto err = audioProcessor.getLastError();
    errorLabel.setText(err.isEmpty() ? juce::String() : ("Last error: " + err),
                       juce::dontSendNotification);
}

void AnalysisTabPanel::pickFile()
{
    fileChooser = std::make_unique<juce::FileChooser>("Select mono/stereo WAV", juce::File{}, "*.wav");
    auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;

    fileChooser->launchAsync(flags, [this](const juce::FileChooser& fc) {
        const auto f = fc.getResult();
        if (f.existsAsFile())
            audioProcessor.loadSourceFile(f);
        refreshFromProcessor();
    });
}

void AnalysisTabPanel::pickMorphSourceFile(int slotIndex)
{
    const auto slotName = slotIndex == 0 ? juce::String("Source A") : juce::String("Source B");
    fileChooser = std::make_unique<juce::FileChooser>("Choose WAV or ATS for " + slotName,
                                                      juce::File{},
                                                      "*.wav;*.ats");
    auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;

    fileChooser->launchAsync(flags, [this, slotIndex](const juce::FileChooser& fc) {
        const auto f = fc.getResult();
        if (!f.existsAsFile())
            return;

        if (f.hasFileExtension(".ats"))
        {
            juce::String err;
            if (!audioProcessor.importAtsFile(f, err)
                || !audioProcessor.setCurrentModelAsMorphSourceSlot(slotIndex, err))
            {
                juce::NativeMessageBox::showMessageBoxAsync(
                    juce::AlertWindow::WarningIcon,
                    slotIndex == 0 ? "Source A" : "Source B",
                    err.isEmpty() ? juce::String("Could not load ATS source.") : err);
            }
            refreshFromProcessor();
            return;
        }

        analyzeMorphSourceFile(slotIndex, f);
    });
}

void AnalysisTabPanel::analyzeMorphSourceFile(int slotIndex, const juce::File& file)
{
    const auto slotName = slotIndex == 0 ? juce::String("Source A") : juce::String("Source B");
    juce::String err;
    if (!audioProcessor.loadAndAnalyzeMorphSourceSlot(slotIndex, file, err))
        juce::NativeMessageBox::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                    slotName,
                                                    err.isEmpty() ? juce::String("Could not analyze morph source.") : err);
    refreshFromProcessor();
}

void AnalysisTabPanel::reanalyzeMorphSourceFile(int slotIndex)
{
    juce::String err;
    if (!audioProcessor.reanalyzeMorphSourceSlot(slotIndex, err))
        juce::NativeMessageBox::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                    slotIndex == 0 ? "Re-analyze Source A" : "Re-analyze Source B",
                                                    err.isEmpty() ? juce::String("Could not re-analyze source.") : err);
    refreshFromProcessor();
}

void AnalysisTabPanel::playMorphSourceFile(int slotIndex)
{
    juce::String err;
    const auto mode = (AtsSourceAuditionMode) getSourceAuditionModeIndex(slotIndex);
    if (!audioProcessor.listenMorphSourceSlot(slotIndex, mode, err))
        juce::NativeMessageBox::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                    slotIndex == 0 ? "Play Source A" : "Play Source B",
                                                    err.isEmpty() ? juce::String("Could not play source.") : err);
    refreshFromProcessor();
}

void AnalysisTabPanel::exportMorphSourceFileAts(int slotIndex)
{
    sourceAtsExportChooser = std::make_unique<juce::FileChooser>(slotIndex == 0 ? "Export Source A ATS"
                                                                                 : "Export Source B ATS",
                                                                  juce::File{},
                                                                  "*.ats");
    constexpr auto flags = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles;
    sourceAtsExportChooser->launchAsync(flags, [this, slotIndex](const juce::FileChooser& fc) {
        auto file = fc.getResult();
        if (file.getFullPathName().isEmpty())
            return;
        if (!file.hasFileExtension(".ats"))
            file = file.withFileExtension("ats");

        juce::String err;
        if (!audioProcessor.exportMorphSourceSlotAts(slotIndex, file, err))
        {
            juce::NativeMessageBox::showMessageBoxAsync(
                juce::AlertWindow::WarningIcon,
                slotIndex == 0 ? "Export Source A" : "Export Source B",
                err.isEmpty() ? juce::String("Could not export source ATS.") : err);
        }
    });
}

int AnalysisTabPanel::getSourceAuditionModeIndex(int slotIndex) const
{
    if (slotIndex == 0)
    {
        if (sourceAPartialsButton.getToggleState())
            return (int) AtsSourceAuditionMode::PartialsOnly;
        if (sourceAResidualButton.getToggleState())
            return (int) AtsSourceAuditionMode::ResidualOnly;
        return (int) AtsSourceAuditionMode::Both;
    }

    if (sourceBPartialsButton.getToggleState())
        return (int) AtsSourceAuditionMode::PartialsOnly;
    if (sourceBResidualButton.getToggleState())
        return (int) AtsSourceAuditionMode::ResidualOnly;
    return (int) AtsSourceAuditionMode::Both;
}

bool AnalysisTabPanel::isInterestedInFileDrag(const juce::StringArray& files)
{
    return getFirstSupportedDropFile(files).existsAsFile();
}

void AnalysisTabPanel::fileDragEnter(const juce::StringArray& files, int x, int y)
{
    if (isInterestedInFileDrag(files))
        setDropHoverSlot(getDropSlotAt(juce::Point<int>(x, y)));
}

void AnalysisTabPanel::fileDragMove(const juce::StringArray& files, int x, int y)
{
    setDropHoverSlot(isInterestedInFileDrag(files) ? getDropSlotAt(juce::Point<int>(x, y)) : -1);
}

void AnalysisTabPanel::fileDragExit(const juce::StringArray&)
{
    setDropHoverSlot(-1);
}

void AnalysisTabPanel::filesDropped(const juce::StringArray& files, int x, int y)
{
    const int slotIndex = getDropSlotAt(juce::Point<int>(x, y));
    setDropHoverSlot(-1);

    if (slotIndex != 0 && slotIndex != 1)
        return;

    auto file = getFirstSupportedDropFile(files);
    if (!file.existsAsFile())
    {
        juce::NativeMessageBox::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                    "Analyze Source",
                                                    "Drop a WAV file onto Source A or Source B.");
        return;
    }

    analyzeMorphSourceFile(slotIndex, file);
}

int AnalysisTabPanel::getDropSlotAt(juce::Point<int> point) const
{
    if (sourceAGroup.getBounds().contains(point))
        return 0;
    if (sourceBGroup.getBounds().contains(point))
        return 1;
    return -1;
}

void AnalysisTabPanel::setDropHoverSlot(int slotIndex)
{
    slotIndex = (slotIndex == 0 || slotIndex == 1) ? slotIndex : -1;
    if (dropHoverSlot == slotIndex)
        return;

    dropHoverSlot = slotIndex;
    repaint();
}

void AnalysisTabPanel::cancelCurrentJob()
{
    if (!audioProcessor.isWorkerBusy())
    {
        audioProcessor.cancelJobs();
        refreshFromProcessor();
        return;
    }

    const bool yes = juce::NativeMessageBox::showYesNoBox(
        juce::AlertWindow::QuestionIcon,
        "Cancel background work?",
        "Analysis or resynthesis is running. Stop the current job?");
    if (yes)
        audioProcessor.cancelJobs();
    refreshFromProcessor();
}

void AnalysisTabPanel::applySelectedProfile()
{
    const int selectedId = analysisProfileBox.getSelectedId();
    const int profileIndex = selectedId - kFirstProfileId;
    if (!juce::isPositiveAndBelow(profileIndex, (int) kAnalysisProfiles.size()))
        return;

    applyProfile(audioProcessor.apvts, kAnalysisProfiles[(size_t) profileIndex]);
    profileChoicePending = false;
    refreshProfileSelection();
}

void AnalysisTabPanel::refreshProfileSelection()
{
    if (profileChoicePending)
    {
        applyProfileButton.setEnabled(true);
        return;
    }

    int matchingId = kCustomProfileId;
    for (size_t i = 0; i < kAnalysisProfiles.size(); ++i)
    {
        if (currentMatchesProfile(audioProcessor.apvts, kAnalysisProfiles[i]))
        {
            matchingId = kFirstProfileId + (int) i;
            break;
        }
    }

    if (analysisProfileBox.getSelectedId() != matchingId)
        analysisProfileBox.setSelectedId(matchingId, juce::dontSendNotification);

    applyProfileButton.setEnabled(false);
}

void AnalysisTabPanel::resetToClassicDefaults()
{
    setPlainParameter(audioProcessor.apvts, ats_param::kAnalysisType, (float) (ATSA_TYPE - 1));
    setPlainParameter(audioProcessor.apvts, ats_param::kAnalysisStart, (float) ATSA_START);
    setPlainParameter(audioProcessor.apvts, ats_param::kAnalysisDuration, (float) ATSA_DUR);
    setPlainParameter(audioProcessor.apvts, ats_param::kAnalysisLowestFreq, (float) ATSA_LFREQ);
    setPlainParameter(audioProcessor.apvts, ats_param::kAnalysisHighestFreq, (float) ATSA_HFREQ);
    setPlainParameter(audioProcessor.apvts, ats_param::kAnalysisFreqDev, (float) ATSA_FREQDEV);
    setPlainParameter(audioProcessor.apvts, ats_param::kAnalysisWinCycles, (float) ATSA_WCYCLES);
    setPlainParameter(audioProcessor.apvts, ats_param::kAnalysisWinType, (float) ATSA_WTYPE);
    setPlainParameter(audioProcessor.apvts, ats_param::kAnalysisHopSize, (float) ATSA_HSIZE);
    setPlainParameter(audioProcessor.apvts, ats_param::kAnalysisLowestMag, (float) ATSA_LMAG);
    setPlainParameter(audioProcessor.apvts, ats_param::kAnalysisTrackLen, (float) ATSA_TRKLEN);
    setPlainParameter(audioProcessor.apvts, ats_param::kAnalysisMinSegLen, (float) ATSA_MSEGLEN);
    setPlainParameter(audioProcessor.apvts, ats_param::kAnalysisMinGapLen, (float) ATSA_MGAPLEN);
    setPlainParameter(audioProcessor.apvts, ats_param::kAnalysisSmrThreshold, (float) ATSA_SMRTHRES);
    setPlainParameter(audioProcessor.apvts, ats_param::kAnalysisMinSegSmr, (float) ATSA_MSEGSMR);
    setPlainParameter(audioProcessor.apvts, ats_param::kAnalysisLastPeakContribution, (float) ATSA_LPKCONT);
    setPlainParameter(audioProcessor.apvts, ats_param::kAnalysisSmrContribution, (float) ATSA_SMRCONT);
    setPlainParameter(audioProcessor.apvts, ats_param::kAnalysisMultiRes, 0.0f);
    profileChoicePending = false;
    refreshProfileSelection();
}
