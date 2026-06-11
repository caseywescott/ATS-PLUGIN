#pragma once

#include <array>
#include <memory>

#include <JuceHeader.h>

class AtsAudioProcessor;

/** Scrollable expert controls for ATSA/ANARGS analysis settings used by the next Analyze run. */
class AnalysisTabPanel : public juce::Component,
                         public juce::FileDragAndDropTarget
{
public:
    static constexpr size_t kSliderCount = 15;

    explicit AnalysisTabPanel(AtsAudioProcessor&);
    void paintOverChildren(juce::Graphics&) override;
    void resized() override;
    void refreshFromProcessor();

private:
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void fileDragEnter(const juce::StringArray& files, int x, int y) override;
    void fileDragMove(const juce::StringArray& files, int x, int y) override;
    void fileDragExit(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

    void pickFile();
    void pickMorphSourceFile(int slotIndex);
    void analyzeMorphSourceFile(int slotIndex, const juce::File& file);
    void reanalyzeMorphSourceFile(int slotIndex);
    void playMorphSourceFile(int slotIndex);
    void exportMorphSourceFileAts(int slotIndex);
    int getSourceAuditionModeIndex(int slotIndex) const;
    int getDropSlotAt(juce::Point<int> point) const;
    void setDropHoverSlot(int slotIndex);
    void cancelCurrentJob();
    void applySelectedProfile();
    void refreshProfileSelection();
    void resetToClassicDefaults();

    AtsAudioProcessor& audioProcessor;

    juce::Label workflowLabel;
    juce::Label statusLabel;
    juce::Label errorLabel;
    juce::TextButton importWavButton { "Import WAV" };
    juce::TextButton analyzeCurrentButton { "Analyze Current" };
    juce::TextButton analyzeSourceAButton { "Analyze Source A" };
    juce::TextButton analyzeSourceBButton { "Analyze Source B" };
    juce::TextButton cancelButton { "Cancel" };
    juce::TextButton stopPreviewButton { "Stop Preview" };
    double displayProgress = 0.0;
    juce::ProgressBar progressBar { displayProgress };
    int dropHoverSlot = -1;

    juce::GroupComponent sourceAGroup;
    juce::GroupComponent sourceBGroup;
    juce::Label sourceAMetaLabel;
    juce::Label sourceBMetaLabel;
    juce::Label sourceAOverviewLabel;
    juce::Label sourceBOverviewLabel;
    juce::TextButton reanalyzeSourceAButton { "Re-analyze A" };
    juce::TextButton reanalyzeSourceBButton { "Re-analyze B" };
    juce::TextButton playSourceAButton { "Play A" };
    juce::TextButton playSourceBButton { "Play B" };
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
    juce::Label profileLabel;
    juce::ComboBox analysisProfileBox;
    juce::TextButton applyProfileButton { "Apply Profile" };
    bool profileChoicePending = false;

    juce::Viewport viewport;
    juce::Component content;

    juce::Label hintLabel;
    juce::TextButton resetButton { "Reset classic defaults" };

    juce::Label sourceSectionLabel;
    juce::Label spectralSectionLabel;
    juce::Label trackingSectionLabel;

    juce::Label analysisTypeLabel;
    juce::ComboBox analysisTypeBox;
    juce::Label windowTypeLabel;
    juce::ComboBox windowTypeBox;
    juce::Label multiResLabel;
    juce::ToggleButton multiResButton { "Multi-resolution low band" };

    std::array<juce::Label, kSliderCount> sliderLabels;
    std::array<juce::Slider, kSliderCount> sliders;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> analysisTypeAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> windowTypeAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> multiResAttach;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, kSliderCount> sliderAttachments;
    std::unique_ptr<juce::FileChooser> fileChooser;
    std::unique_ptr<juce::FileChooser> sourceAtsExportChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnalysisTabPanel)
};
