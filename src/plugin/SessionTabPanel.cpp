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
} // namespace

SessionTabPanel::SessionTabPanel(AtsAudioProcessor& p)
    : audioProcessor(p)
    , spectroPanel(p)
{
    loadButton.setExplicitFocusOrder(1);
    loadButton.setHelpText("Choose a mono or stereo WAV file to analyze.");
    analyzeButton.setExplicitFocusOrder(2);
    analyzeButton.setHelpText("Run ATS analysis on the imported audio (CPU heavy, background thread).");
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
    if (id <= 0)
        return;

    const int nFactory = ats_preset::factoryPresetNames().size();
    if (id <= nFactory)
    {
        ats_preset::applyFactoryPreset(audioProcessor, id - 1);
        return;
    }

    if (id >= kUserPresetComboIdBase)
    {
        const int idx = id - kUserPresetComboIdBase;
        if (juce::isPositiveAndBelow(idx, userPresetFiles.size()))
            audioProcessor.loadFullStatePreset(userPresetFiles.getReference(idx));
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

    r.removeFromTop(sc(10));
    statusLabel.setBounds(r.removeFromTop(sc(22)));
    r.removeFromTop(sc(6));
    errorLabel.setBounds(r.removeFromTop(sc(40)));
    r.removeFromTop(sc(6));
    cpuWarningLabel.setBounds(r.removeFromTop(sc(20)));
    r.removeFromTop(sc(8));
    progressBar.setBounds(r.removeFromTop(sc(14)));
    r.removeFromTop(sc(12));

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

    const auto err = audioProcessor.getLastError();
    errorLabel.setText(err.isEmpty() ? juce::String() : ("Last error: " + err),
                       juce::dontSendNotification);

    int partials = 0;
    int frames = 0;
    bool hasRes = false;
    if (audioProcessor.getModelFootprint(partials, frames, hasRes))
    {
        const int64_t prod = (int64_t) partials * (int64_t) frames;
        if (prod > ats_editor::kModelSizeSoftCap)
        {
            cpuWarningLabel.setText(
                "Large model: " + juce::String(partials) + " partials x " + juce::String(frames)
                    + " frames (product " + juce::String(prod) + " exceeds soft limit "
                    + juce::String(ats_editor::kModelSizeSoftCap)
                    + "; analysis and resynth may be slow or memory-heavy).",
                juce::dontSendNotification);
        }
        else
        {
            cpuWarningLabel.setText(juce::String(), juce::dontSendNotification);
        }
    }
    else
    {
        cpuWarningLabel.setText(juce::String(), juce::dontSendNotification);
    }
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
