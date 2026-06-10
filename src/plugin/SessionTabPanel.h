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

    int findNearestPoint(juce::Point<float> pos, bool selectedLaneOnly) const;
    AtsMorphTimelinePoint pointFromPosition(juce::Point<float> pos) const;
    juce::Point<float> positionForPoint(const AtsMorphTimelinePoint& point) const;
    void paintLane(juce::Graphics& g, MorphTimelinePointKind kind, bool selectedLane) const;
    void updatePointFromMouse(const juce::MouseEvent&);
    void notifyChange();

    std::vector<AtsMorphTimelinePoint> points;
    int selectedPoint = -1;
    bool editing = false;
    MorphTimelinePointKind newPointKind = MorphTimelinePointKind::Morph;

    juce::ToggleButton morphModeBtn  { "Morph" };
    juce::ToggleButton hybridModeBtn { "Hybrid" };
    juce::ToggleButton envMorphModeBtn { "Env" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MorphTimelineEditor)
};

/** Main session tab: import, analyze, presets, status, core knobs, spectrogram. */
class SessionTabPanel : public juce::Component,
                        public juce::FileDragAndDropTarget
{
public:
    explicit SessionTabPanel(AtsAudioProcessor&);
    void resized() override;
    void paintOverChildren(juce::Graphics&) override;

    void refreshFromProcessor();

    // FileDragAndDropTarget
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void fileDragEnter(const juce::StringArray& files, int x, int y) override;
    void fileDragMove(const juce::StringArray& files, int x, int y) override;
    void fileDragExit(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

private:
    static constexpr size_t kMorphLaneUiCount = 6;

    void pickFile();
    void pickMorphSourceFile(int slotIndex);
    void loadDroppedFileToSlot(int slotIndex, const juce::File& f);
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
    void bounceMorphTimeline();
    void transportPlay();
    int getAuditionModeIndex() const;
    void exportMorphSourceSlotAts(int slotIndex);

    void setActiveControlTab(int tab);
    void refreshSmearnovControls();
    void runSmearnovDetect();
    void runSmearnovApply();
    void hideMorphCluster();
    void hideSmearnovPanel();
    void layoutSmearnovPanel(juce::Rectangle<int> area);

    AtsAudioProcessor& audioProcessor;

    juce::TextButton loadButton { "Import WAV" };
    juce::TextButton analyzeButton { "Analyze" };
    juce::TextButton cancelButton { "Cancel" };

    // Unified transport bar: one Play/Stop, a source selector, a single audition-mode
    // radio group, and a global non-destructive Smear preview toggle.
    juce::Label transportSourceLabel;
    juce::ComboBox transportSourceCombo;
    juce::TextButton transportPlayButton { "Play" };
    juce::Label transportAuditionLabel;
    juce::ToggleButton auditionBothButton { "Both" };
    juce::ToggleButton auditionPartialsButton { "Partials" };
    juce::ToggleButton auditionResidualButton { "Residual" };
    juce::ToggleButton auditionSmearButton { "Smear only" };
    juce::ToggleButton smearEffectToggle { "Smear" };
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

    // Scrollable left-column controls
    juce::Viewport controlsViewport;
    juce::Component controlsContent;

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
    juce::TextButton exportSourceAButton { "Export ATS" };
    juce::TextButton exportSourceBButton { "Export ATS" };
    juce::Label morphSnapshotLabel;
    juce::Label morphSnapshotStatusLabel;
    std::array<juce::TextButton, ats_param::kMorphVariationSlotCount> morphVariationRecallButtons;
    std::array<juce::TextButton, ats_param::kMorphVariationSlotCount> morphVariationSaveButtons;
    juce::Label morphTimelineLabel;
    juce::Label morphTimelineHintLabel;
    MorphTimelineEditor morphTimelineEditor;
    juce::TextButton morphTimelineBounceButton { "Bounce Timeline" };
    juce::TextButton morphTimelineResetButton { "Reset" };
    juce::Label morphSmoothingLabel;
    juce::Slider morphSmoothingSlider;
    juce::TextButton resetMorphLanesButton { "Link lanes" };
    juce::Label morphPolicyLabel;
    juce::ComboBox morphGridTypeCombo;
    juce::ComboBox morphCorrespondenceCombo;
    juce::ComboBox morphQualityCombo;
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

    // --- Control-cluster tab + Smearnov panel (SMEARNOV_SPEC §4) ---
    int activeControlTab = 0; // 0 = Morph, 1 = Smearnov
    bool smearKnobsExpanded = false;
    bool smearAdvancedExpanded = false;
    bool smearShowMathStatus = true; // false freezes a Detect result until next Apply
    bool smearSyncingSensitivity = false; // guards the Sensitivity knob during programmatic updates

    juce::TextButton morphTabButton { "Morph" };
    juce::TextButton smearnovTabButton { "Smearnov" };

    juce::Label smearMacroLabel;
    juce::Slider smearBlurSlider;
    juce::Label smearMixLabel;
    juce::Slider smearMixSlider;
    juce::Label smearSensitivityLabel;
    juce::Slider smearSensitivitySlider;
    juce::Label smearSharpenLabel;
    juce::Slider smearSharpenSlider;
    juce::Label smearExaggerateLabel;
    juce::Slider smearExaggerateSlider;
    juce::TextButton smearKnobsDisclosure { "+ knobs" };

    juce::Label smearTargetsLabel;
    juce::ComboBox smearTargetsCombo;
    juce::Label smearScopeLabel;
    juce::ComboBox smearScopeCombo;

    juce::TextButton smearDetectButton { "Detect" };
    juce::TextButton smearApplyButton { "Apply Smear" };
    juce::Label smearStatusLabel;

    juce::TextButton smearAdvancedDisclosure { "Advanced" };
    juce::Label smearMaxDbLabel;        juce::Slider smearMaxDbSlider;
    juce::Label smearWeightDetLabel;    juce::Slider smearWeightDetSlider;
    juce::Label smearWeightResLabel;    juce::Slider smearWeightResSlider;
    juce::Label smearWeightHfcLabel;    juce::Slider smearWeightHfcSlider;
    juce::Label smearWeightBirthLabel;  juce::Slider smearWeightBirthSlider;
    juce::Label smearWeightPhaseLabel;  juce::Slider smearWeightPhaseSlider;
    juce::Label smearThreshLambdaLabel; juce::Slider smearThreshLambdaSlider;
    juce::Label smearPreMarginLabel;    juce::Slider smearPreMarginSlider;
    juce::Label smearPeakWindowLabel;   juce::Slider smearPeakWindowSlider;
    juce::Label smearMinGapLabel;       juce::Slider smearMinGapSlider;
    juce::Label smearMaxAttackLabel;    juce::Slider smearMaxAttackSlider;
    juce::ToggleButton smearUsePhaseDevButton { "Phase dev" };
    juce::ToggleButton smearRpcaFallbackButton { "RPCA fallback" };

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

    // Timbre render controls (§8.1–8.3): brightness, tilt, tonal/noise
    juce::Label timbreSectionLabel;
    juce::Label timbreBrightnessLabel;
    juce::Slider timbreBrightnessSlider;
    juce::Label timbreTiltLabel;
    juce::Slider timbreTiltSlider;
    juce::Label timbreNoiseLabel;
    juce::Slider timbreNoiseSlider;

    std::array<juce::Label, kMorphLaneUiCount> morphLaneLabels;
    std::array<juce::Slider, kMorphLaneUiCount> morphLaneSliders;
    std::array<juce::ToggleButton, kMorphLaneUiCount> morphLaneLinkedButtons;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> partGainAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> resGainAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outputTrimAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> morphSmoothingAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> morphGridTypeAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> morphCorrespondenceAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> morphQualityAttach;
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

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> timbreBrightnessAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> timbreTiltAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> timbreNoiseAttach;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> smearBlurAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> smearMixAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> smearSharpenAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> smearExaggerateAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> smearMaxDbAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> smearWeightDetAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> smearWeightResAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> smearWeightHfcAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> smearWeightBirthAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> smearWeightPhaseAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> smearThreshLambdaAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> smearPreMarginAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> smearPeakWindowAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> smearMinGapAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> smearMaxAttackAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> smearTargetsAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> smearScopeAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> smearUsePhaseDevAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> smearRpcaFallbackAttach;

    int dragHoverSlot = -1; // 0=A, 1=B, -1=none

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
