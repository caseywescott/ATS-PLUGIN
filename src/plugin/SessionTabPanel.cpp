#include "SessionTabPanel.h"

#include "PluginProcessor.h"
#include "PresetLibrary.h"

namespace
{
int scaledUi(int px)
{
    if (auto* disp = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        return juce::roundToInt((float) px * disp->scale);
    return px;
}

juce::String wrapTextToWidth(const juce::String& text, const juce::Font& font, int maxWidth)
{
    if (text.isEmpty() || maxWidth <= 16)
        return text;

    juce::StringArray words;
    words.addTokens(text, " ", {});
    words.removeEmptyStrings();
    if (words.isEmpty())
        return text;

    juce::String wrapped;
    juce::String line;
    for (const auto& word : words)
    {
        const juce::String candidate = line.isEmpty() ? word : (line + " " + word);
        if (!line.isEmpty() && font.getStringWidth(candidate) > maxWidth)
        {
            if (wrapped.isNotEmpty())
                wrapped << "\n";
            wrapped << line;
            line = word;
        }
        else
        {
            line = candidate;
        }
    }

    if (line.isNotEmpty())
    {
        if (wrapped.isNotEmpty())
            wrapped << "\n";
        wrapped << line;
    }
    return wrapped;
}

int countWrappedLines(const juce::String& text)
{
    if (text.isEmpty())
        return 1;
    int lines = 1;
    for (int i = 0; i < text.length(); ++i)
    {
        if (text[i] == '\n')
            ++lines;
    }
    return lines;
}

bool setRawTextIfChanged(juce::String& current, const juce::String& next)
{
    if (current == next)
        return false;

    current = next;
    return true;
}
} // namespace

SessionTabPanel::SessionTabPanel(AtsAudioProcessor& p)
    : audioProcessor(p)
    , spectroPanel(p)
{
    loadButton.setExplicitFocusOrder(1);
    loadButton.setHelpText("Choose a mono or stereo WAV file to analyze.");
    analyzeButton.setExplicitFocusOrder(2);
    analyzeButton.setHelpText("Run ATS analysis on the imported audio (CPU heavy, background thread). Synthesis and diagnostics follow the selected Classic, Modern, or Experimental analysis path.");
    cancelButton.setExplicitFocusOrder(3);
    cancelButton.setHelpText("Stop the current analysis or resynthesis job.");
    presetCombo.setExplicitFocusOrder(4);
    presetCombo.setHelpText("Factory parameter curves or full-state user presets from disk.");
    savePresetButton.setExplicitFocusOrder(5);
    savePresetButton.setHelpText("Save parameters and embedded ATS model to a .atspreset file.");

    presetLabel.setText("Preset", juce::dontSendNotification);
    presetLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(presetLabel);
    addAndMakeVisible(presetCombo);
    presetCombo.onChange = [this] { onPresetComboChanged(); };

    savePresetButton.onClick = [this] { saveUserPreset(); };
    addAndMakeVisible(savePresetButton);

    importAtsButton.setHelpText("Load a native .ats file (Qatsh / ATSA interchange).");
    importAtsButton.onClick = [this] { pickImportAts(); };
    addAndMakeVisible(importAtsButton);

    exportAtsButton.setHelpText("Save the current analyzed model as a native .ats file.");
    exportAtsButton.onClick = [this] { pickExportAts(); };
    addAndMakeVisible(exportAtsButton);

    previewPlayButton.setHelpText("Start preview playback from the beginning of the current rendered model.");
    previewPlayButton.setExplicitFocusOrder(9);
    previewPlayButton.onClick = [this] { audioProcessor.startPreviewPlayback(); };
    addAndMakeVisible(previewPlayButton);

    previewStopButton.setHelpText("Stop preview playback and reset to the start.");
    previewStopButton.setExplicitFocusOrder(10);
    previewStopButton.onClick = [this] { audioProcessor.stopPreviewPlayback(); };
    addAndMakeVisible(previewStopButton);

    statusLabel.setJustificationType(juce::Justification::centredLeft);
    statusLabel.setFont(juce::Font(15.0f, juce::Font::bold));
    statusLabel.setExplicitFocusOrder(6);
    statusLabel.setHelpText("Current high-level plugin state.");
    addAndMakeVisible(statusLabel);

    errorLabel.setColour(juce::Label::textColourId, juce::Colour(0xffff8a80));
    errorLabel.setJustificationType(juce::Justification::topLeft);
    errorLabel.setExplicitFocusOrder(7);
    errorLabel.setHelpText("Most recent error from analysis, resynthesis, or file I/O.");
    addAndMakeVisible(errorLabel);
    modeWarningLabel.setJustificationType(juce::Justification::topLeft);
    modeWarningLabel.setColour(juce::Label::textColourId, juce::Colour(0xffffd180));
    addAndMakeVisible(modeWarningLabel);
    presetMetadataLabel.setJustificationType(juce::Justification::topLeft);
    presetMetadataLabel.setFont(juce::Font(12.0f));
    presetMetadataLabel.setColour(juce::Label::textColourId, juce::Colour(0xffc5e1ff));
    addAndMakeVisible(presetMetadataLabel);
    diagnosticsLabel.setJustificationType(juce::Justification::topLeft);
    diagnosticsLabel.setColour(juce::Label::textColourId, juce::Colour(0xff9dd7ff));
    addAndMakeVisible(diagnosticsLabel);

    cpuWarningLabel.setJustificationType(juce::Justification::centredLeft);
    cpuWarningLabel.setFont(juce::Font(12.0f, juce::Font::italic));
    cpuWarningLabel.setColour(juce::Label::textColourId, juce::Colour(0xffffcc88));
    cpuWarningLabel.setExplicitFocusOrder(8);
    cpuWarningLabel.setHelpText("Shown when partial and frame counts exceed the documented soft limit.");
    addAndMakeVisible(cpuWarningLabel);

    addAndMakeVisible(progressBar);

    loadButton.onClick = [this] { pickFile(); };
    addAndMakeVisible(loadButton);

    analyzeButton.onClick = [this] { audioProcessor.startAnalysis(); };
    addAndMakeVisible(analyzeButton);

    cancelButton.onClick = [this] {
        if (!audioProcessor.isWorkerBusy())
        {
            audioProcessor.cancelJobs();
            return;
        }

        const bool yes = juce::NativeMessageBox::showYesNoBox(
            juce::AlertWindow::QuestionIcon,
            "Cancel background work?",
            "Analysis or resynthesis is running. Stop the current job?");
        if (yes)
            audioProcessor.cancelJobs();
    };
    addAndMakeVisible(cancelButton);

    auto setupSlider = [](juce::Slider& s, const juce::String& name, int order) {
        s.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 18);
        s.setName(name);
        s.setExplicitFocusOrder(order);
        s.setHelpText(name);
    };
    auto setupKnobLabel = [](juce::Label& l, const juce::String& text) {
        l.setText(text, juce::dontSendNotification);
        l.setJustificationType(juce::Justification::centred);
        l.setFont(juce::Font(12.0f, juce::Font::bold));
    };
    setupKnobLabel(partGainLabel, "partGain - Partials (amp)");
    setupKnobLabel(resGainLabel, "resGain - Residual (ramp)");
    setupKnobLabel(outputTrimLabel, "outputTrim - Master level");
    addAndMakeVisible(partGainLabel);
    addAndMakeVisible(resGainLabel);
    addAndMakeVisible(outputTrimLabel);

    const juce::Colour knobLabCol(0xfff0f0f5);
    partGainLabel.setColour(juce::Label::textColourId, knobLabCol);
    resGainLabel.setColour(juce::Label::textColourId, knobLabCol);
    outputTrimLabel.setColour(juce::Label::textColourId, knobLabCol);

    setupSlider(partGainSlider, "partGain", 11);
    setupSlider(resGainSlider, "resGain", 12);
    setupSlider(outputTrimSlider, "outputTrim", 13);
    partGainSlider.setHelpText("Partials path gain (SPARAMS amp). Applied in real time to the preview.");
    resGainSlider.setHelpText("Residual / noise-band gain (SPARAMS ramp). Applied in real time to the preview.");
    outputTrimSlider.setHelpText("Master output trim after partial + residual mix.");
    addAndMakeVisible(partGainSlider);
    addAndMakeVisible(resGainSlider);
    addAndMakeVisible(outputTrimSlider);

    partGainAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "partGain", partGainSlider);
    resGainAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "resGain", resGainSlider);
    outputTrimAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "outputTrim", outputTrimSlider);
    analysisModeLabel.setText("Analysis", juce::dontSendNotification);
    trackerModeLabel.setText("Tracker", juce::dontSendNotification);
    analysisModeCombo.addItemList(
        juce::StringArray { "Classic ATS", "Modern ATS", "Experimental reassigned/DDM" }, 1);
    analysisModeCombo.setHelpText("Classic ATS uses the original ATSA/Qatsh analysis and do_synthesis. Modern ATS renders through the modern snapshot path. Experimental reassigned/DDM also renders through the modern snapshot path but tags the result experimental.");
    trackerModeCombo.addItemList(juce::StringArray { "Classic recursive freq/SMR", "Modern assignment" }, 1);
    addAndMakeVisible(analysisModeLabel);
    addAndMakeVisible(analysisModeCombo);
    addAndMakeVisible(trackerModeLabel);
    addAndMakeVisible(trackerModeCombo);
    analysisModeAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.apvts, "analysisMode", analysisModeCombo);
    trackerModeAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.apvts, "trackerMode", trackerModeCombo);

    spectroPanel.setExplicitFocusOrder(14);
    addAndMakeVisible(spectroPanel);

    rebuildPresetList();
}

void SessionTabPanel::rebuildPresetList()
{
    const int prevId = presetCombo.getSelectedId();
    presetCombo.clear(juce::dontSendNotification);
    userPresetFiles.clear();

    const auto factories = ats_preset::factoryPresetNames();
    for (int i = 0; i < factories.size(); ++i)
        presetCombo.addItem("Factory: " + factories[i], i + 1);

    const auto dir = ats_preset::getUserPresetDirectory();
    juce::Array<juce::File> found;
    dir.findChildFiles(found, juce::File::findFiles, false, "*.atspreset");
    found.sort();

    int nextId = kUserPresetComboIdBase;
    for (const auto& f : found)
    {
        userPresetFiles.add(f);
        presetCombo.addItem("User: " + f.getFileNameWithoutExtension(), nextId);
        ++nextId;
    }

    if (prevId != 0 && presetCombo.indexOfItemId(prevId) >= 0)
        presetCombo.setSelectedId(prevId, juce::dontSendNotification);
    else
        presetCombo.setSelectedId(0, juce::dontSendNotification);
}

void SessionTabPanel::onPresetComboChanged()
{
    const int id = presetCombo.getSelectedId();
    setPresetMetadataText({});
    if (id <= 0)
        return;

    const int nFactory = ats_preset::factoryPresetNames().size();
    if (id <= nFactory)
    {
        setPresetMetadataText(ats_preset::factoryPresetMetadata(id - 1));
        audioProcessor.setCurrentProgram(id - 1);
        return;
    }

    if (id >= kUserPresetComboIdBase)
    {
        const int idx = id - kUserPresetComboIdBase;
        if (juce::isPositiveAndBelow(idx, userPresetFiles.size()))
        {
            setPresetMetadataText("User full-state preset (.atspreset): restores analysis/tracker mode and model snapshot.");
            audioProcessor.loadFullStatePreset(userPresetFiles.getReference(idx));
        }
    }
}

void SessionTabPanel::saveUserPreset()
{
    const auto startDir = ats_preset::getUserPresetDirectory();
    presetSaveChooser = std::make_unique<juce::FileChooser>("Save full-state preset", startDir, "*.atspreset");

    constexpr auto flags = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles;

    presetSaveChooser->launchAsync(flags, [this](const juce::FileChooser& fc) {
        auto f = fc.getResult();
        if (f.getFullPathName().isEmpty())
            return;

        if (!f.getFileName().endsWithIgnoreCase(".atspreset"))
            f = f.withFileExtension("atspreset");

        const auto savedPath = f.getFullPathName();
        if (audioProcessor.saveFullStatePreset(f))
        {
            rebuildPresetList();
            for (int i = 0; i < userPresetFiles.size(); ++i)
            {
                if (userPresetFiles.getReference(i).getFullPathName() == savedPath)
                {
                    presetCombo.setSelectedId(kUserPresetComboIdBase + i, juce::dontSendNotification);
                    break;
                }
            }
        }
    });
}

void SessionTabPanel::resized()
{
    const auto sc = [](int px) { return scaledUi(px); };

    auto r = getLocalBounds().reduced(sc(12));

    auto top = r.removeFromTop(sc(28));
    loadButton.setBounds(top.removeFromLeft(sc(118)));
    top.removeFromLeft(sc(8));
    analyzeButton.setBounds(top.removeFromLeft(sc(92)));
    top.removeFromLeft(sc(8));
    cancelButton.setBounds(top.removeFromLeft(sc(92)));
    top.removeFromLeft(sc(16));

    auto presetLabelArea = top.removeFromLeft(sc(52));
    presetLabel.setBounds(presetLabelArea);
    presetCombo.setBounds(top.removeFromLeft(juce::jmax(sc(220), top.getWidth() / 3)));
    top.removeFromLeft(sc(8));
    savePresetButton.setBounds(top.removeFromLeft(sc(110)));

    r.removeFromTop(sc(6));
    auto ioRow = r.removeFromTop(sc(28));
    importAtsButton.setBounds(ioRow.removeFromLeft(sc(130)));
    ioRow.removeFromLeft(sc(8));
    exportAtsButton.setBounds(ioRow.removeFromLeft(sc(130)));
    ioRow.removeFromLeft(sc(16));
    previewPlayButton.setBounds(ioRow.removeFromLeft(sc(90)));
    ioRow.removeFromLeft(sc(8));
    previewStopButton.setBounds(ioRow.removeFromLeft(sc(90)));

    r.removeFromTop(sc(6));
    statusLabel.setBounds(r.removeFromTop(sc(22)));

    auto placeDynamicLabel = [&](juce::Label& label,
                                 const juce::String& rawText,
                                 int minHeight,
                                 int maxHeight,
                                 int lineHeight)
    {
        if (rawText.isEmpty())
        {
            label.setText({}, juce::dontSendNotification);
            label.setBounds({});
            return;
        }

        r.removeFromTop(sc(3));
        const auto wrapped = wrapTextToWidth(rawText, label.getFont(), juce::jmax(sc(100), r.getWidth()));
        const int lines = countWrappedLines(wrapped);
        const int height = juce::jlimit(sc(minHeight), sc(maxHeight), lines * sc(lineHeight) + sc(4));
        label.setText(wrapped, juce::dontSendNotification);
        label.setBounds(r.removeFromTop(height));
    };

    placeDynamicLabel(errorLabel, rawErrorText, 20, 54, 14);
    placeDynamicLabel(modeWarningLabel, rawModeWarningText, 18, 56, 14);
    placeDynamicLabel(presetMetadataLabel, rawPresetMetadataText, 14, 60, 14);
    placeDynamicLabel(diagnosticsLabel, rawDiagnosticsText, 20, 66, 14);
    placeDynamicLabel(cpuWarningLabel, rawCpuWarningText, 18, 56, 14);

    r.removeFromTop(sc(4));
    progressBar.setBounds(r.removeFromTop(sc(14)));
    r.removeFromTop(sc(6));

    auto body = r;
    const int specW = juce::jmax(sc(280), body.getWidth() / 3);
    auto specCol = body.removeFromRight(specW);
    spectroPanel.setBounds(specCol);

    const int labelH = sc(32);
    const int knobH = sc(104);
    auto knobRow = body.removeFromTop(labelH + knobH + sc(6));
    const int w = juce::jmax(1, knobRow.getWidth() / 3);

    auto placeColumn = [&](juce::Label& lab, juce::Slider& sld) {
        auto col = knobRow.removeFromLeft(w).reduced(sc(6));
        lab.setBounds(col.removeFromTop(labelH));
        sld.setBounds(col);
    };
    placeColumn(partGainLabel, partGainSlider);
    placeColumn(resGainLabel, resGainSlider);
    placeColumn(outputTrimLabel, outputTrimSlider);
    body.removeFromTop(sc(8));
    auto modeRow = body.removeFromTop(sc(24));
    analysisModeLabel.setBounds(modeRow.removeFromLeft(sc(96)));
    analysisModeCombo.setBounds(modeRow.removeFromLeft(sc(248)));
    modeRow.removeFromLeft(sc(8));
    trackerModeLabel.setBounds(modeRow.removeFromLeft(sc(56)));
    trackerModeCombo.setBounds(modeRow.removeFromLeft(sc(220)));
}

void SessionTabPanel::refreshFromProcessor()
{
    displayProgress = (double) audioProcessor.getAnalysisProgress();
    const bool hasPreview = audioProcessor.hasPreviewAudio();
    const bool playing = audioProcessor.isPreviewPlaying();
    previewPlayButton.setEnabled(hasPreview && !playing);
    previewStopButton.setEnabled(hasPreview && playing);

    switch (audioProcessor.getUiState())
    {
    case AtsUiState::NoSource:
        statusLabel.setText("State: No source", juce::dontSendNotification);
        break;
    case AtsUiState::ReadyToAnalyze:
        statusLabel.setText("State: Ready to analyze", juce::dontSendNotification);
        break;
    case AtsUiState::Analyzing:
        statusLabel.setText("State: Analyzing (CPU heavy)", juce::dontSendNotification);
        break;
    case AtsUiState::Analyzed:
        statusLabel.setText(playing ? "State: Analyzed / preview playing"
                                    : "State: Analyzed / preview ready",
                            juce::dontSendNotification);
        break;
    case AtsUiState::Resynthesizing:
        statusLabel.setText("State: Resynthesizing", juce::dontSendNotification);
        break;
    case AtsUiState::ApplyingMath:
        statusLabel.setText("State: Applying math transform", juce::dontSendNotification);
        break;
    case AtsUiState::Error:
        statusLabel.setText("State: Error", juce::dontSendNotification);
        break;
    }

    bool layoutNeedsRefresh = false;

    const auto err = audioProcessor.getLastError();
    const juce::String nextError = err.isEmpty() ? juce::String() : ("Last error: " + err);
    layoutNeedsRefresh |= setRawTextIfChanged(rawErrorText, nextError);

    juce::String nextDiagnostics;
    juce::String nextWarnings;
    if (!audioProcessor.getModernDiagnosticsSummary(nextDiagnostics, nextWarnings))
    {
        // Classic ATS mode: show basic model info when a model is available.
        int classicPartials = 0, classicFrames = 0;
        bool classicHasRes = false;
        if (audioProcessor.getModelFootprint(classicPartials, classicFrames, classicHasRes))
        {
            nextDiagnostics = "Classic ATS - partials: " + juce::String(classicPartials)
                              + ", frames: " + juce::String(classicFrames)
                              + ", residual bands: "
                              + (classicHasRes ? juce::String("yes") : juce::String("no"));
        }
    }

    const int selectedAnalysisId = analysisModeCombo.getSelectedId();
    const bool trackerSelectable = selectedAnalysisId == 2 || selectedAnalysisId == 3;
    trackerModeLabel.setEnabled(trackerSelectable);
    trackerModeCombo.setEnabled(trackerSelectable);
    if (!trackerSelectable && nextWarnings.isEmpty())
    {
        nextWarnings =
            "Tracker selector is enabled only when Analysis mode is Modern ATS or Experimental reassigned/DDM.";
    }

    layoutNeedsRefresh |= setRawTextIfChanged(rawDiagnosticsText, nextDiagnostics);
    layoutNeedsRefresh |= setRawTextIfChanged(rawModeWarningText, nextWarnings);

    int partials = 0;
    int frames = 0;
    bool hasRes = false;
    juce::String nextCpuWarning;
    if (audioProcessor.getModelFootprint(partials, frames, hasRes))
    {
        const int64_t prod = (int64_t) partials * (int64_t) frames;
        if (prod > ats_editor::kModelSizeSoftCap)
        {
            nextCpuWarning = "Large model: " + juce::String(partials) + " partials x "
                             + juce::String(frames) + " frames (product " + juce::String(prod)
                             + " exceeds soft limit " + juce::String(ats_editor::kModelSizeSoftCap)
                             + "; analysis and resynth may be slow or memory-heavy).";
        }
    }

    layoutNeedsRefresh |= setRawTextIfChanged(rawCpuWarningText, nextCpuWarning);

    if (layoutNeedsRefresh)
        resized();
}

void SessionTabPanel::pickImportAts()
{
    atsImportChooser = std::make_unique<juce::FileChooser>("Import ATS analysis", juce::File{}, "*.ats");
    constexpr auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
    atsImportChooser->launchAsync(flags, [this](const juce::FileChooser& fc) {
        const auto f = fc.getResult();
        if (!f.existsAsFile())
            return;
        juce::String err;
        if (!audioProcessor.importAtsFile(f, err))
            juce::NativeMessageBox::showMessageBoxAsync(
                juce::AlertWindow::WarningIcon, "Import ATS", err.isEmpty() ? juce::String("Import failed.") : err);
    });
}

void SessionTabPanel::pickExportAts()
{
    atsExportChooser = std::make_unique<juce::FileChooser>("Export ATS analysis", juce::File{}, "*.ats");
    constexpr auto flags = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles;
    atsExportChooser->launchAsync(flags, [this](const juce::FileChooser& fc) {
        auto f = fc.getResult();
        if (f.getFullPathName().isEmpty())
            return;
        juce::String err;
        if (!audioProcessor.exportAtsFile(f, err))
            juce::NativeMessageBox::showMessageBoxAsync(
                juce::AlertWindow::WarningIcon, "Export ATS", err.isEmpty() ? juce::String("Export failed.") : err);
    });
}

void SessionTabPanel::pickFile()
{
    fileChooser = std::make_unique<juce::FileChooser>("Select mono/stereo WAV", juce::File{}, "*.wav");
    auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;

    fileChooser->launchAsync(flags, [this](const juce::FileChooser& fc) {
        const auto f = fc.getResult();
        if (f.existsAsFile())
            audioProcessor.loadSourceFile(f);
    });
}

void SessionTabPanel::setPresetMetadataText(const juce::String& text)
{
    if (rawPresetMetadataText == text)
        return;

    rawPresetMetadataText = text;
    resized();
}
