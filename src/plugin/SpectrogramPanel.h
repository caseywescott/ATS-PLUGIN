#pragma once

#include <JuceHeader.h>

#include "PluginProcessor.h"

/** Fairlight-inspired partial/residual model view with selectable spectral traces. */
class SpectrogramPanel : public juce::Component,
                         private juce::Timer,
                         private juce::ScrollBar::Listener
{
public:
    explicit SpectrogramPanel(AtsAudioProcessor&);
    ~SpectrogramPanel() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMagnify(const juce::MouseEvent&, float scaleFactor) override;
    bool keyPressed(const juce::KeyPress&) override;

    enum class Mode
    {
        Combined,
        Partials,
        Residual,
        Isometric,
        IsometricResidual,
        IsometricEnvelope,
        IsometricEnvelopeRDC
    };

    void setMode(Mode m);

    [[nodiscard]] AtsTraceSource getViewedSource() const { return traceSource; }

    // Currently selected frame range in the trace, or {-1,-1} if no selection.
    [[nodiscard]] bool getSelectedFrameRange(int& firstFrame, int& lastFrame) const;

private:
    class TraceZoomBar;
    class PlayheadOverlay;

    void timerCallback() override;
    void scrollBarMoved(juce::ScrollBar* scrollBarThatHasMoved, double newRangeStart) override;
    bool updatePlayheadAnimation();
    void resetPlayheadAnimation();
    void invalidateDisplayCache();
    void repaintDisplay();
    void rebuildTraceSnapshot();
    void rebuildTraceSnapshot(bool preserveDisplayReference);
    void resetDisplayReference();
    void showViewMenu();
    void setView(AtsTraceSource source, Mode mode);
    void updateViewMenuButton();
    void paintScreen(juce::Graphics&, juce::Rectangle<float> screen);
    void paintResidualBackdrop(juce::Graphics&, juce::Rectangle<float> plot, bool foreground);
    void paintPartialTraces(juce::Graphics&, juce::Rectangle<float> plot);
    void paintIsometricGrid(juce::Graphics&, juce::Rectangle<float> plot);
    void paintIsometricPartials(juce::Graphics&, juce::Rectangle<float> plot);
    void paintIsometricResidual(juce::Graphics&, juce::Rectangle<float> plot);
    void paintIsometricEnvelope(juce::Graphics&, juce::Rectangle<float> plot);
    void paintIsometricEnvelopeRDC(juce::Graphics&, juce::Rectangle<float> plot);
    void paintEnvelopeSurfaceRDC(juce::Graphics& g,
                                 juce::Rectangle<float> plot,
                                 const AtsSpectralTraceSnapshot& snap,
                                 juce::Colour colour,
                                 float alphaScale) const;
    void paintEnvelopeSurface(juce::Graphics& g,
                              juce::Rectangle<float> plot,
                              const AtsSpectralTraceSnapshot& snap,
                              juce::Colour colour,
                              float alphaScale) const;
    void paintPlaybackPlayhead(juce::Graphics&, juce::Rectangle<float> plot);
    void paintPlanarPlaybackPlayhead(juce::Graphics&, juce::Rectangle<float> plot, float playheadNorm);
    void paintIsometricPlaybackPlayhead(juce::Graphics&, juce::Rectangle<float> plot, float playheadNorm);
    [[nodiscard]] juce::Rectangle<int> playheadPaintBounds(juce::Rectangle<float> plot, float playheadNorm) const;
    void paintSmearnovOverlay(juce::Graphics&, juce::Rectangle<float> plot) const;
    void paintSmearnovDetectionCurves(juce::Graphics&, juce::Rectangle<float> plot) const;

    enum class OnsetHandle { None, Marker, LeftEdge, RightEdge };
    [[nodiscard]] bool smearnovOverlayInteractive() const;
    [[nodiscard]] bool smearnovOverlayPresent() const;
    void updateOnsetEditButtonVisibility();
    [[nodiscard]] int onsetHandleAtPoint(juce::Point<float> pos,
                                         juce::Rectangle<float> plot,
                                         OnsetHandle& handleOut) const;
    void updateDraggedOnset(juce::Point<float> pos);
    void commitEditedOnsets();
    void paintSelectionOverlay(juce::Graphics&, juce::Rectangle<float> screen, juce::Rectangle<float> plot);
    void paintBandLabelGutter(juce::Graphics&, juce::Rectangle<float> gutter, juce::Rectangle<float> plot);
    void updateSelectionLabel();
    void updateResidualToolbarVisibility();
    void refreshHoveredDbReadout();
    void applyResidualDbFromEditor();
    void commitEnvelopeDraw();
    void runSmoothEnvelope();
    [[nodiscard]] bool isEnvelopeDisplayMode() const
    {
        return mode == Mode::IsometricEnvelope || mode == Mode::IsometricEnvelopeRDC;
    }
    [[nodiscard]] float timeNormFromIsoPoint(juce::Point<float> pos, juce::Rectangle<float> plot) const;
    void isoTimeFreqFromPoint(juce::Point<float> pos,
                              juce::Rectangle<float> plot,
                              float& timeNormAbs,
                              float& freqNormAbs) const;
    [[nodiscard]] bool pointInEnvBox(juce::Point<float> pos, juce::Rectangle<float> plot) const;
    [[nodiscard]] bool envBoxFrameRange(int& firstFrame, int& lastFrame) const;
    [[nodiscard]] bool envBoxFreqRangeHz(double& loHz, double& hiHz) const;
    void scaleEnvBoxGain(float deltaY);
    void paintEnvelopeRangeSelection(juce::Graphics&, juce::Rectangle<float> plot) const;
    void runSmoothOnSelection();
    void runTiltOnSelection();
    void runCopyBand();
    void runPasteBand();
    void runSwapBands();
    void showResidualEditError(const juce::String& error);
    [[nodiscard]] double dbFromPlotYInBand(int bandIndex, float y, juce::Rectangle<float> plot) const;
    [[nodiscard]] juce::Rectangle<float> getBandLabelGutterBounds() const;
    [[nodiscard]] bool showsBandLabelGutter() const;
    [[nodiscard]] int getSelectionFirstFrame() const;
    [[nodiscard]] int getSelectionLastFrame() const;
    void deleteSelection();
    void deleteSelectedPartials();
    void deleteSelectedResidual();
    void scaleSelectedData(float deltaY);
    void applyZoomAtScreenPoint(juce::Point<float> anchor, float timeFactor, float freqFactor);
    void panView(float deltaTimeNorm, float deltaFreqNorm);
    void clampViewport();
    void syncZoomControlsFromViewport();
    void syncViewportFromZoomControls();
    void resetViewport();
    void layoutTraceViewChrome();
    void updateScrollBarsFromViewport();
    void styleTraceScrollBars();

    [[nodiscard]] juce::Rectangle<float> getScreenBounds() const;
    [[nodiscard]] juce::Rectangle<float> getTraceContentBounds() const;
    [[nodiscard]] juce::Rectangle<float> getPlotBounds() const;
    [[nodiscard]] juce::Range<float> visibleTimeRange() const;
    [[nodiscard]] juce::Range<float> visibleFreqRange() const;
    [[nodiscard]] juce::Point<float> positionForPoint(const AtsPartialTracePoint&, juce::Rectangle<float> plot) const;
    [[nodiscard]] juce::Point<float> positionForIsoPoint(const AtsPartialTracePoint&, juce::Rectangle<float> plot) const;
    [[nodiscard]] juce::Point<float> positionForIsoCoords(float timeNorm,
                                                          float freqNorm,
                                                          float ampNorm,
                                                          juce::Rectangle<float> plot) const;
    [[nodiscard]] float xForFrame(int frame, juce::Rectangle<float> plot) const;
    [[nodiscard]] float timeNormFromScreenX(float x, juce::Rectangle<float> plot) const;
    [[nodiscard]] float freqNormFromScreenY(float y, juce::Rectangle<float> plot) const;
    [[nodiscard]] juce::Range<float> yRangeForBand(const AtsResidualBandTrace&, juce::Rectangle<float> plot) const;
    [[nodiscard]] juce::Rectangle<float> residualSelectionRect(juce::Rectangle<float> plot) const;
    [[nodiscard]] int frameForX(float x, juce::Rectangle<float> plot) const;
    [[nodiscard]] int residualBandForY(float y, juce::Rectangle<float> plot) const;
    [[nodiscard]] int residualBandForGutterPoint(juce::Point<float> pos,
                                                 juce::Rectangle<float> gutter,
                                                 juce::Rectangle<float> plot) const;
    [[nodiscard]] int residualBandForPoint(juce::Point<float> pos, juce::Rectangle<float> plot) const;
    [[nodiscard]] std::vector<int> residualBandsForRect(juce::Rectangle<float> rect,
                                                        juce::Rectangle<float> plot) const;
    [[nodiscard]] int findNearestPartial(juce::Point<float> pos, float maxDistancePx) const;
    [[nodiscard]] bool isPartialSelected(int partialIndex) const;
    [[nodiscard]] bool hasResidualSelection() const;
    [[nodiscard]] bool traceIntersectsLasso(const AtsPartialTrace&, juce::Rectangle<float> lasso) const;
    static float distanceToSegment(juce::Point<float> p, juce::Point<float> a, juce::Point<float> b);

    AtsAudioProcessor& processor;
    AtsTraceSource traceSource = AtsTraceSource::Current;
    Mode mode = Mode::Combined;
    AtsSpectralTraceSnapshot traceSnapshot;
    AtsSpectralTraceSnapshot secondarySnapshot;  // second source for envelope overlay
    juce::ToggleButton envSourceABtn { "Source A" };
    juce::ToggleButton envSourceBBtn { "Source B" };
    juce::Label envSmoothLabel;
    juce::Slider envSmoothSlider;
    juce::TextButton envSmoothButton { "Smooth Env" };
    int lastModelRevision = -1;
    uint64_t lastDisplayShapeRevision = 0;
    uint64_t lastSmearnovOverlayRevision = 0;
    double ampDisplayReferencePeak = 0.0;
    double residualDisplayReferencePeak = 0.0;

    juce::TextButton viewMenuButton { "Current / Trace + Residual" };
    juce::TextButton onsetEditButton { "Edit onsets" };
    juce::TextButton deleteButton { "Delete" };
    juce::Label selectionLabel;

    juce::ToggleButton residualDrawButton { "Draw" };
    juce::TextButton residualSmoothButton { "Smooth" };
    juce::Slider residualTiltSlider;
    juce::TextButton residualTiltButton { "Tilt" };
    juce::TextButton residualCopyButton { "Copy" };
    juce::TextButton residualPasteButton { "Paste" };
    juce::TextButton residualSwapButton { "Swap" };
    juce::Label residualDbLabel;
    juce::TextEditor residualDbEditor;

    std::unique_ptr<TraceZoomBar> zoomBar;
    std::unique_ptr<PlayheadOverlay> playheadOverlay;
    juce::ScrollBar timeScrollBar { false };
    juce::ScrollBar freqScrollBar { true };

    static constexpr int traceScrollBarThickness = 13;
    static constexpr int kBandLabelGutterWidth = 54;
    static constexpr int kResidualToolbarHeight = 24;

    float timeZoom = 1.0f;
    float freqZoom = 1.0f;
    float timeCenter = 0.5f;
    float freqCenter = 0.5f;
    bool panningView = false;
    juce::Point<float> panDragStart;
    float panTimeCenterStart = 0.5f;
    float panFreqCenterStart = 0.5f;
    bool syncingZoomControls = false;
    bool syncingScrollBars = false;

    std::vector<int> selectedPartials;
    std::vector<int> selectedResidualBands;
    int selectedResidualFirstFrame = -1;
    int selectedResidualLastFrame = -1;
    int hoveredPartial = -1;
    int hoveredResidualBand = -1;
    int hoveredResidualFrame = -1;
    int draggingOnsetIndex = -1;
    OnsetHandle draggingOnsetHandle = OnsetHandle::None;
    int hoveredOnsetIndex = -1;
    OnsetHandle hoveredOnsetHandle = OnsetHandle::None;
    float animatedPlayheadNorm = -1.0f;
    double lastPlayheadAnimationMs = 0.0;
    juce::Image displayCache;
    bool displayCacheDirty = true;
    juce::Point<float> dragStart;
    float lastScaleDragY = 0.0f;
    juce::Rectangle<float> lassoRect;
    bool draggingLasso = false;
    bool scalingSelection = false;
    bool selectingEnvRange = false;
    bool scalingEnvGain = false;
    float envRangeStartNorm = -1.0f;  // absolute time-norm [0,1] of the editable envelope slab; -1 = none
    float envRangeEndNorm = -1.0f;
    float envFreqStartNorm = -1.0f;   // absolute freq-norm [0,1] sub-range (y); -1 = full depth
    float envFreqEndNorm = -1.0f;
    bool drawingResidualEnvelope = false;
    int envelopeDrawBand = -1;
    std::vector<std::pair<int, double>> envelopeDrawPoints;
    bool residualDbEditorActive = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectrogramPanel)
};
