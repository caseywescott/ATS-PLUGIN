#pragma once

#include <array>
#include <functional>
#include <vector>

#include <JuceHeader.h>

#include "PluginProcessor.h"
#include "SpectrogramPanel.h"
#include "UiConstants.h"

class MorphBandStrip : public juce::Component
{
public:
    MorphBandStrip() = default;

    struct BandView
    {
        int bandIndex = -1;
        double sourcePower = 0.0;
        double targetPower = 0.0;
        double outputPower = 0.0;
        int matchedCount = 0;
        int unmatchedCount = 0;
        bool warning = false;
    };

    void setBands(std::vector<BandView> nextBands, int selectedBand);
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;

    std::function<void(int)> onBandSelected;

private:
    std::vector<BandView> bands;
    int selectedBandIndex = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MorphBandStrip)
};

class MorphTimelineEditor : public juce::Component
{
public:
    MorphTimelineEditor();

    void setPoints(std::vector<AtsMorphTimelinePoint> nextPoints);
    [[nodiscard]] std::vector<AtsMorphTimelinePoint> getPoints() const { return points; }
    [[nodiscard]] bool isEditing() const { return editing; }

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;

    std::function<void(std::vector<AtsMorphTimelinePoint>)> onPointsChanged;

private:
    static constexpr int kHeaderH = 18; // px reserved at top for mode buttons

    [[nodiscard]] juce::Rectangle<float> getPlotArea() const;

    int findNearestPoint(juce::Point<float> pos) const;
    AtsMorphTimelinePoint pointFromPosition(juce::Point<float> pos) const;
    juce::Point<float> positionForPoint(const AtsMorphTimelinePoint& point) const;
    void updatePointFromMouse(const juce::MouseEvent&);
    void notifyChange();

    std::vector<AtsMorphTimelinePoint> points;
    int selectedPoint = -1;
    bool editing = false;
    MorphTimelinePointKind newPointKind = MorphTimelinePointKind::Morph;

    juce::ToggleButton morphModeBtn  { "Morph" };
    juce::ToggleButton hybridModeBtn { "Hybrid" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MorphTimelineEditor)
};

/** Main session tab: import, analyze, presets, status, core knobs, spectrogram. */
class SessionTabPanel : public juce::Component
{
public:
    explicit SessionTabPanel(AtsAudioProcessor&);
    void resized() override;

    void refreshFromProcessor();

private:
    static constexpr size_t kMorphLaneUiCount = 6;

    void pickFile();
    void pickMorphSourceFile(int slotIndex);
    void pickImportAts();
    void pickExportAts();
    void rebuildPresetList();
    void onPresetComboChanged();
    void saveUserPreset();
    void resetMorphLanesToLinked();
    void saveMorphVariation(size_t index);
    void recallMorphVariation(size_t index);
    void refreshMorphSnapshotControls();
    void refreshMorphBandDiagnostics();
    void refreshMorphTimeline();
    void setMorphTimelinePreset(std::vector<AtsMorphTimelinePoint> points);
    void playMorphTimeline();
    void bounceMorphTimeline();
    int getMorphTimelineAuditionModeIndex() const;
    int getSourceAuditionModeIndex(int slotIndex) const;
    void exportMorphSourceSlotAts(int slotIndex);

    AtsAudioProcessor& audioProcessor;

    juce::TextButton loadButton { "Import WAV" };
    juce::TextButton analyzeButton { "Analyze" };
    juce::TextButton currentPlayButton { "Play" };
    juce::TextButton cancelButton { "Cancel" };
    juce::ComboBox presetCombo;
    juce::TextButton savePresetButton { "Save preset" };
    juce::TextButton importAtsButton { "Import ATS..." };
    juce::TextButton exportAtsButton { "Export ATS..." };
    juce::Label presetLabel;
    juce::Label statusLabel;
    juce::Label errorLabel;
    juce::Label cpuWarningLabel;
    double displayProgress = 0.0;
    juce::ProgressBar progressBar { displayProgress };
    juce::Label analysisSummaryLabel;

    SpectrogramPanel spectroPanel;
    MorphBandStrip morphBandStrip;

    juce::GroupComponent sourceAGroup;
    juce::GroupComponent sourceBGroup;
    juce::Label sourceALabel;
    juce::Label sourceBLabel;
    juce::Label sourceAMetaLabel;
    juce::Label sourceBMetaLabel;
    juce::Label sourceAOverviewLabel;
    juce::Label sourceBOverviewLabel;
    juce::TextButton sourceALoadAnalyzeButton { "Analyze Source A" };
    juce::TextButton sourceBLoadAnalyzeButton { "Analyze Source B" };
    juce::TextButton sourceAUseCurrentButton { "Use Current" };
    juce::TextButton sourceBUseCurrentButton { "Use Current" };
    juce::TextButton sourceAListenButton { "Listen A" };
    juce::TextButton sourceBListenButton { "Listen B" };
    juce::TextButton exportSourceAButton { "Export ATS" };
    juce::TextButton exportSourceBButton { "Export ATS" };
    juce::Label sourceAAuditionLabel;
    juce::Label sourceBAuditionLabel;
    juce::ToggleButton sourceABothButton { "Both" };
    juce::ToggleButton sourceAPartialsButton { "Partials" };
    juce::ToggleButton sourceAResidualButton { "Residual" };
    juce::ToggleButton sourceBBothButton { "Both" };
    juce::ToggleButton sourceBPartialsButton { "Partials" };
    juce::ToggleButton sourceBResidualButton { "Residual" };
    juce::Label morphSnapshotLabel;
    juce::Label morphSnapshotStatusLabel;
    std::array<juce::TextButton, ats_param::kMorphVariationSlotCount> morphVariationRecallButtons;
    std::array<juce::TextButton, ats_param::kMorphVariationSlotCount> morphVariationSaveButtons;
    juce::Label morphTimelineLabel;
    juce::Label morphTimelineHintLabel;
    MorphTimelineEditor morphTimelineEditor;
    juce::TextButton morphTimelinePlayButton { "Play Timeline" };
    juce::Label morphTimelineAuditionLabel;
    juce::ToggleButton morphTimelineBothButton { "Both" };
    juce::ToggleButton morphTimelinePartialsButton { "Partials" };
    juce::ToggleButton morphTimelineResidualButton { "Residual" };
    juce::TextButton morphTimelineBounceButton { "Bounce Timeline" };
    juce::TextButton morphTimelineResetButton { "Reset" };
    juce::Label morphSmoothingLabel;
    juce::Slider morphSmoothingSlider;
    juce::TextButton resetMorphLanesButton { "Link lanes" };
    juce::Label morphPolicyLabel;
    juce::ComboBox morphGridTypeCombo;
    juce::ComboBox morphCorrespondenceCombo;
    juce::ComboBox morphSourcePolicyCombo;
    juce::ComboBox morphTargetPolicyCombo;
    juce::ComboBox morphFrequencyDomainCombo;
    juce::ComboBox morphAmplitudeDomainCombo;
    juce::ComboBox morphPhaseStrategyCombo;
    juce::ComboBox morphTransientPolicyCombo;
    juce::ToggleButton morphDiscardOptInButton;
    juce::Label morphBandLabel;
    juce::Label morphBandInspectorLabel;
    std::vector<AtsMorphBandDiagnostic> morphBandDiagnostics;
    int selectedMorphBandIndex = 0;

    std::unique_ptr<juce::FileChooser> fileChooser;
    std::unique_ptr<juce::FileChooser> presetSaveChooser;
    std::unique_ptr<juce::FileChooser> atsImportChooser;
    std::unique_ptr<juce::FileChooser> atsExportChooser;
    std::unique_ptr<juce::FileChooser> timelineBounceChooser;

    juce::Array<juce::File> userPresetFiles;

    juce::Label partGainLabel;
    juce::Label resGainLabel;
    juce::Label outputTrimLabel;

    juce::Slider partGainSlider;
    juce::Slider resGainSlider;
    juce::Slider outputTrimSlider;

    std::array<juce::Label, kMorphLaneUiCount> morphLaneLabels;
    std::array<juce::Slider, kMorphLaneUiCount> morphLaneSliders;
    std::array<juce::ToggleButton, kMorphLaneUiCount> morphLaneLinkedButtons;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> partGainAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> resGainAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outputTrimAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> morphSmoothingAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> morphGridTypeAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> morphCorrespondenceAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> morphSourcePolicyAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> morphTargetPolicyAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> morphFrequencyDomainAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> morphAmplitudeDomainAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> morphPhaseStrategyAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> morphTransientPolicyAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> morphDiscardOptInAttach;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>,
               kMorphLaneUiCount>
        morphLaneAmountAttach;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>,
               kMorphLaneUiCount>
        morphLaneLinkedAttach;

    // Cached values for refreshFromProcessor — only re-queried when revisions change.
    int cachedModelRevision   = -1;
    int cachedMorphRevision   = -1;
    int cachedModelPartials   = 0;
    int cachedModelFrames     = 0;
    bool cachedModelHasResidual = false;
    AtsMorphSourceSlotInfo cachedSourceA;
    AtsMorphSourceSlotInfo cachedSourceB;

    static constexpr int kUserPresetComboIdBase = 1000;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SessionTabPanel)
};
