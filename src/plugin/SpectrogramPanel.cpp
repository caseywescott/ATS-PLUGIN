#include "SpectrogramPanel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>

#include "AtsFrequencyShape.h"

namespace
{
constexpr int kMaxVisiblePartials = 900;
constexpr int kMaxPointsPerPartial = 720;
constexpr int kResidualColumns = 220;
constexpr float kMinZoom = 1.0f;
constexpr float kMaxZoom = 32.0f;
constexpr float kZoomBarWidth = 204.0f;

float zoomFromSlider(double sliderValue)
{
    return kMinZoom * std::pow(kMaxZoom / kMinZoom, (float) juce::jlimit(0.0, 1.0, sliderValue));
}

double sliderFromZoom(float zoom)
{
    const float clamped = juce::jlimit(kMinZoom, kMaxZoom, zoom);
    return std::log(clamped / kMinZoom) / std::log(kMaxZoom / kMinZoom);
}

bool isResidualDisplayMode(SpectrogramPanel::Mode mode)
{
    return mode == SpectrogramPanel::Mode::Residual || mode == SpectrogramPanel::Mode::IsometricResidual;
}

bool isIsometricDisplayMode(SpectrogramPanel::Mode mode)
{
    return mode == SpectrogramPanel::Mode::Isometric
        || mode == SpectrogramPanel::Mode::IsometricResidual
        || mode == SpectrogramPanel::Mode::IsometricEnvelope
        || mode == SpectrogramPanel::Mode::IsometricEnvelopeRDC;
}
} // namespace

class SpectrogramPanel::TraceZoomBar : public juce::Component
{
public:
    std::function<void()> onChange;

    TraceZoomBar()
    {
        for (auto* slider : { &verticalSlider, &horizontalSlider })
        {
            slider->setSliderStyle(juce::Slider::LinearHorizontal);
            slider->setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
            slider->setRange(0.0, 1.0, 0.001);
            slider->setDoubleClickReturnValue(true, 0.0);
            slider->setScrollWheelEnabled(false);
            slider->onValueChange = [this] {
                if (!suppressCallbacks && onChange)
                    onChange();
            };
            addAndMakeVisible(*slider);
        }

        verticalSlider.setTooltip("Vertical zoom (frequency axis)");
        horizontalSlider.setTooltip("Horizontal zoom (time axis)");
    }

    void setVerticalZoom(float zoom, juce::NotificationType notification = juce::dontSendNotification)
    {
        verticalSlider.setValue(sliderFromZoom(zoom), notification);
    }

    void setHorizontalZoom(float zoom, juce::NotificationType notification = juce::dontSendNotification)
    {
        horizontalSlider.setValue(sliderFromZoom(zoom), notification);
    }

    [[nodiscard]] float getVerticalZoom() const { return zoomFromSlider(verticalSlider.getValue()); }
    [[nodiscard]] float getHorizontalZoom() const { return zoomFromSlider(horizontalSlider.getValue()); }

    void setCallbacksSuppressed(bool suppressed) { suppressCallbacks = suppressed; }

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        g.setColour(juce::Colour(0xff1a1f1c));
        g.fillRoundedRectangle(bounds, 8.0f);
        g.setColour(juce::Colour(0xff303833));
        g.drawRoundedRectangle(bounds.reduced(0.5f), 8.0f, 1.0f);

        const float midX = bounds.getCentreX();
        g.setColour(juce::Colour(0xff2a302c));
        g.fillRect(midX - 0.5f, bounds.getY() + 4.0f, 1.0f, bounds.getHeight() - 8.0f);

        g.setColour(juce::Colour(0xffb8c4bc));
        paintVerticalAxisIcon(g, verticalIconArea);
        paintHorizontalAxisIcon(g, horizontalIconArea);
    }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced(4, 5);
        const int halfW = bounds.getWidth() / 2;
        auto leftSection = bounds.removeFromLeft(halfW);
        bounds.removeFromLeft(1);
        auto rightSection = bounds;

        verticalIconArea = leftSection.removeFromLeft(16).toFloat().reduced(0.0f, 1.0f);
        leftSection.removeFromLeft(2);
        verticalSlider.setBounds(leftSection);

        horizontalIconArea = rightSection.removeFromLeft(16).toFloat().reduced(0.0f, 1.0f);
        rightSection.removeFromLeft(2);
        horizontalSlider.setBounds(rightSection);
    }

    void lookAndFeelChanged() override
    {
        for (auto* slider : { &verticalSlider, &horizontalSlider })
        {
            slider->setColour(juce::Slider::backgroundColourId, juce::Colour(0xff0f1310));
            slider->setColour(juce::Slider::trackColourId, juce::Colour(0xff0f1310));
            slider->setColour(juce::Slider::thumbColourId, juce::Colour(0xffb8c4bc));
        }
    }

private:
    static void paintBidirectionalArrow(juce::Graphics& g,
                                          juce::Rectangle<float> area,
                                          bool horizontal)
    {
        if (area.getWidth() < 4.0f || area.getHeight() < 4.0f)
            return;

        const float pad = 1.0f;
        const float strokeW = 1.2f;
        const juce::PathStrokeType stroke(strokeW,
                                          juce::PathStrokeType::curved,
                                          juce::PathStrokeType::butt);

        juce::Path icon;

        if (horizontal)
        {
            const float cy = std::floor(area.getCentreY()) + 0.5f;
            const float x0 = std::floor(area.getX() + pad) + 0.5f;
            const float x1 = std::floor(area.getRight() - pad) - 0.5f;
            const float span = x1 - x0;
            const float headLen = juce::jlimit(2.2f, 3.2f, span * 0.26f);
            const float headHalfH = juce::jlimit(1.6f, 2.4f, area.getHeight() * 0.28f);

            icon.startNewSubPath(x0 + headLen, cy);
            icon.lineTo(x1 - headLen, cy);

            icon.startNewSubPath(x0 + headLen, cy - headHalfH);
            icon.lineTo(x0, cy);
            icon.lineTo(x0 + headLen, cy + headHalfH);

            icon.startNewSubPath(x1 - headLen, cy - headHalfH);
            icon.lineTo(x1, cy);
            icon.lineTo(x1 - headLen, cy + headHalfH);
        }
        else
        {
            const float cx = std::floor(area.getCentreX()) + 0.5f;
            const float y0 = std::floor(area.getY() + pad) + 0.5f;
            const float y1 = std::floor(area.getBottom() - pad) - 0.5f;
            const float span = y1 - y0;
            const float headLen = juce::jlimit(2.2f, 3.2f, span * 0.26f);
            const float headHalfW = juce::jlimit(1.6f, 2.4f, area.getWidth() * 0.28f);

            icon.startNewSubPath(cx, y0 + headLen);
            icon.lineTo(cx, y1 - headLen);

            icon.startNewSubPath(cx - headHalfW, y0 + headLen);
            icon.lineTo(cx, y0);
            icon.lineTo(cx + headHalfW, y0 + headLen);

            icon.startNewSubPath(cx - headHalfW, y1 - headLen);
            icon.lineTo(cx, y1);
            icon.lineTo(cx + headHalfW, y1 - headLen);
        }

        g.strokePath(icon, stroke);
    }

    static void paintVerticalAxisIcon(juce::Graphics& g, juce::Rectangle<float> area)
    {
        paintBidirectionalArrow(g, area, false);
    }

    static void paintHorizontalAxisIcon(juce::Graphics& g, juce::Rectangle<float> area)
    {
        paintBidirectionalArrow(g, area, true);
    }

    juce::Slider verticalSlider;
    juce::Slider horizontalSlider;
    juce::Rectangle<float> verticalIconArea;
    juce::Rectangle<float> horizontalIconArea;
    bool suppressCallbacks = false;
};

class SpectrogramPanel::PlayheadOverlay : public juce::Component,
                                          private juce::Timer
{
public:
    explicit PlayheadOverlay(SpectrogramPanel& panel) : owner(panel)
    {
        setInterceptsMouseClicks(false, false);
        setPaintingIsUnclipped(true);
        startTimerHz(60);
    }

    ~PlayheadOverlay() override
    {
        stopTimer();
    }

    void paint(juce::Graphics& g) override
    {
        const float playheadNorm = owner.animatedPlayheadNorm;
        if (playheadNorm < 0.0f)
            return;

        g.saveState();
        g.reduceClipRegion(getLocalBounds());
        owner.paintPlaybackPlayhead(g, getLocalBounds().toFloat());
        g.restoreState();
    }

private:
    void timerCallback() override
    {
        const auto oldBounds = lastPaintBounds;
        const bool wasActive = owner.animatedPlayheadNorm >= 0.0f;
        const bool active = owner.updatePlayheadAnimation();

        juce::Rectangle<int> nextBounds;
        if (active)
            nextBounds = owner.playheadPaintBounds(getLocalBounds().toFloat(), owner.animatedPlayheadNorm);

        lastPaintBounds = nextBounds;

        auto dirty = oldBounds.getUnion(nextBounds).expanded(10).getIntersection(getLocalBounds());
        if (dirty.isEmpty() && wasActive)
            dirty = getLocalBounds();

        if (!dirty.isEmpty())
            repaint(dirty);
    }

    SpectrogramPanel& owner;
    juce::Rectangle<int> lastPaintBounds;
};

namespace
{

juce::Rectangle<float> normalisedRect(juce::Point<float> a, juce::Point<float> b)
{
    return { std::min(a.x, b.x),
             std::min(a.y, b.y),
             std::abs(a.x - b.x),
             std::abs(a.y - b.y) };
}

juce::Colour traceColour(float ampNorm)
{
    const auto green = juce::Colour(0xff10ff77);
    const auto cyan = juce::Colour(0xff6ffff0);
    return green.interpolatedWith(cyan, juce::jlimit(0.0f, 1.0f, ampNorm * 0.85f));
}
} // namespace

SpectrogramPanel::SpectrogramPanel(AtsAudioProcessor& p)
    : processor(p)
{
    setTitle("Spectral trace display");
    setName("Spectral trace display");
    setHelpText("Selectable ATS partial/residual traces. Scroll wheel zooms; bottom/right "
                "scrollbars pan time/frequency when zoomed; Alt-drag pans; 0 or Home fits full source.");
    setWantsKeyboardFocus(true);

    styleTraceScrollBars();
    timeScrollBar.addListener(this);
    freqScrollBar.addListener(this);
    timeScrollBar.setAutoHide(false);
    freqScrollBar.setAutoHide(false);
    addAndMakeVisible(timeScrollBar);
    addAndMakeVisible(freqScrollBar);

    viewMenuButton.setName("Spectral view source and mode");
    viewMenuButton.setHelpText("Choose Current, Source A, or Source B, then choose trace/residual display mode.");
    viewMenuButton.onClick = [this] { showViewMenu(); };
    addAndMakeVisible(viewMenuButton);
    updateViewMenuButton();

    onsetEditButton.setClickingTogglesState(true);
    onsetEditButton.setName("Edit Smearnov onsets");
    onsetEditButton.setTooltip("Edit Smearnov onset markers. When off, markers are click-through "
                               "so you can select partials and edit residual underneath them.");
    onsetEditButton.setHelpText("Toggle onset editing. On: drag the vertical onset markers and their "
                                "left/right edges. Off: the overlay is dimmed and click-through so "
                                "partial/residual editing works under the markers.");
    onsetEditButton.onClick = [this] {
        // Leaving onset-edit mode must not strand a half-finished drag or stale highlight.
        draggingOnsetIndex = -1;
        draggingOnsetHandle = OnsetHandle::None;
        hoveredOnsetIndex = -1;
        hoveredOnsetHandle = OnsetHandle::None;
        repaint();
    };
    addChildComponent(onsetEditButton);

    deleteButton.setTooltip("Mute selected partial traces or residual-band energy.");
    deleteButton.onClick = [this] { deleteSelection(); };
    addAndMakeVisible(deleteButton);

    residualDrawButton.setClickingTogglesState(true);
    residualDrawButton.setTooltip("Draw a per-band noise envelope by dragging on a band row.");
    residualDrawButton.setHelpText("Paint residual energy (dB) along time on one Bark band.");
    addChildComponent(residualDrawButton);

    residualTiltSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    residualTiltSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 44, 18);
    residualTiltSlider.setRange(-12.0, 12.0, 0.1);
    residualTiltSlider.setValue(0.0, juce::dontSendNotification);
    residualTiltSlider.setTooltip("Tilt amount in dB per band step across the selection.");
    addChildComponent(residualTiltSlider);

    residualTiltButton.setTooltip("Apply spectral tilt across selected bands using the tilt slider.");
    residualTiltButton.onClick = [this] { runTiltOnSelection(); };
    addChildComponent(residualTiltButton);

    residualSmoothButton.setTooltip("Box-smooth residual energy over the selected bands and frame range.");
    residualSmoothButton.onClick = [this] { runSmoothOnSelection(); };
    addChildComponent(residualSmoothButton);

    residualCopyButton.setTooltip("Copy selected band/frame residual energy to clipboard (Cmd+C).");
    residualCopyButton.onClick = [this] { runCopyBand(); };
    addChildComponent(residualCopyButton);

    residualPasteButton.setTooltip("Paste clipboard residual energy into the selected band/frame range.");
    residualPasteButton.onClick = [this] { runPasteBand(); };
    addChildComponent(residualPasteButton);

    residualSwapButton.setTooltip("Swap residual energy between two selected bands over the frame range.");
    residualSwapButton.onClick = [this] { runSwapBands(); };
    addChildComponent(residualSwapButton);

    residualRedistButton.setTooltip("Equalize energy across selected bands per frame (preserves total energy).");
    residualRedistButton.onClick = [this] { runRedistBands(); };
    addChildComponent(residualRedistButton);

    residualDbLabel.setText("Cell dB", juce::dontSendNotification);
    residualDbLabel.setJustificationType(juce::Justification::centredRight);
    residualDbLabel.setFont(juce::Font(11.0f));
    addChildComponent(residualDbLabel);

    residualDbEditor.setInputRestrictions(0, "0123456789.-");
    residualDbEditor.setTooltip("Hovered band/frame level in dB. Double-click the plot or press Enter to apply.");
    residualDbEditor.setFont(juce::Font(11.0f));
    residualDbEditor.onReturnKey = [this] { applyResidualDbFromEditor(); };
    residualDbEditor.onFocusLost = [this] {
        if (residualDbEditorActive)
            applyResidualDbFromEditor();
    };
    addChildComponent(residualDbEditor);

    selectionLabel.setJustificationType(juce::Justification::centredLeft);
    selectionLabel.setFont(juce::Font(12.0f, juce::Font::plain));
    selectionLabel.setColour(juce::Label::textColourId, juce::Colour(0xffc7ffe6));
    addAndMakeVisible(selectionLabel);
    updateSelectionLabel();

    zoomBar = std::make_unique<TraceZoomBar>();
    zoomBar->onChange = [this] { syncViewportFromZoomControls(); };
    zoomBar->lookAndFeelChanged();
    zoomBar->setVerticalZoom(freqZoom);
    zoomBar->setHorizontalZoom(timeZoom);
    addAndMakeVisible(*zoomBar);

    playheadOverlay = std::make_unique<PlayheadOverlay>(*this);
    addAndMakeVisible(*playheadOverlay);

    // Envelope overlay toggles
    envSourceABtn.setToggleState(true, juce::dontSendNotification);
    envSourceBBtn.setToggleState(false, juce::dontSendNotification);
    envSourceABtn.setColour(juce::ToggleButton::textColourId, juce::Colour(0xff4090d0));
    envSourceBBtn.setColour(juce::ToggleButton::textColourId, juce::Colour(0xffd08030));
    envSourceABtn.onStateChange = [this] { if (mode == Mode::IsometricEnvelope || mode == Mode::IsometricEnvelopeRDC) repaint(); };
    envSourceBBtn.onStateChange = [this] { if (mode == Mode::IsometricEnvelope || mode == Mode::IsometricEnvelopeRDC) repaint(); };
    addChildComponent(envSourceABtn);
    addChildComponent(envSourceBBtn);

    envSmoothLabel.setText("Smoothness", juce::dontSendNotification);
    envSmoothLabel.setJustificationType(juce::Justification::centredRight);
    envSmoothLabel.setFont(juce::Font(11.0f));
    addChildComponent(envSmoothLabel);

    envSmoothSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    envSmoothSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 40, 18);
    envSmoothSlider.setRange(0.0, 1.0, 0.01);
    envSmoothSlider.setValue(0.5, juce::dontSendNotification);
    envSmoothSlider.setTooltip("Envelope smoothness: higher flattens partials onto a simpler "
                               "spectral envelope (lower polynomial order).");
    addChildComponent(envSmoothSlider);

    envSmoothButton.setTooltip("Flatten the viewed source's partial amplitudes onto the fitted "
                               "RDC spectral envelope over the visible time range.");
    envSmoothButton.onClick = [this] { runSmoothEnvelope(); };
    addChildComponent(envSmoothButton);

    partialFreqDrawButton.setClickingTogglesState(true);
    partialFreqDrawButton.setTooltip("Draw partial frequency: drag near a partial trace to reshape its pitch contour.");
    partialFreqDrawButton.setHelpText("Paint a new frequency contour onto a partial by dragging.");
    addChildComponent(partialFreqDrawButton);

    partialSoloButton.setTooltip("Audition selected partials in isolation (silences all others).");
    partialSoloButton.onClick = [this] { runPartialSolo(); };
    addChildComponent(partialSoloButton);

    partialFreezeButton.setTooltip("Freeze: copy the center-of-view frame's spectrum to all frames, creating a sustained static tone.");
    partialFreezeButton.onClick = [this] { runPartialFreeze(); };
    addChildComponent(partialFreezeButton);

    partialPruneButton.setTooltip("Select all partials below an amplitude threshold for deletion. Press H to select harmonics of the hovered partial.");
    partialPruneButton.onClick = [this] { runPartialPrune(); };
    addChildComponent(partialPruneButton);

    partialTiltSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    partialTiltSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 44, 18);
    partialTiltSlider.setRange(-12.0, 12.0, 0.1);
    partialTiltSlider.setValue(0.0, juce::dontSendNotification);
    partialTiltSlider.setTooltip("Spectral tilt in dB/octave applied to partial amplitudes.");
    addChildComponent(partialTiltSlider);

    partialTiltButton.setTooltip("Apply spectral tilt to partial amplitudes across the visible frame range.");
    partialTiltButton.onClick = [this] { runPartialTilt(); };
    addChildComponent(partialTiltButton);

    updateResidualToolbarVisibility();
    startTimerHz(10);
}

SpectrogramPanel::~SpectrogramPanel()
{
    stopTimer();
    timeScrollBar.removeListener(this);
    freqScrollBar.removeListener(this);
}

void SpectrogramPanel::setMode(Mode m)
{
    if (mode == m)
        return;
    setView(traceSource, m);
}

void SpectrogramPanel::setView(AtsTraceSource source, Mode nextMode)
{
    const bool sourceChanged = traceSource != source;
    const bool changed = sourceChanged || mode != nextMode;
    traceSource = source;
    mode = nextMode;
    if (isResidualDisplayMode(mode))
    {
        selectedPartials.clear();
        hoveredPartial = -1;
    }
    else
    {
        selectedResidualBands.clear();
        selectedResidualFirstFrame = -1;
        selectedResidualLastFrame = -1;
        hoveredResidualBand = -1;
        hoveredResidualFrame = -1;
    }
    scalingSelection = false;
    draggingLasso = false;
    if (sourceChanged)
    {
        resetViewport();
        resetDisplayReference();
        resetPlayheadAnimation();
    }
    updateViewMenuButton();
    updateSelectionLabel();
    updateResidualToolbarVisibility();
    if (changed)
        rebuildTraceSnapshot();
    resized();
    repaintDisplay();
}

void SpectrogramPanel::showViewMenu()
{
    auto modeName = [](Mode m) {
        switch (m)
        {
            case Mode::Combined: return juce::String("Trace + Residual");
            case Mode::Partials: return juce::String("Partials");
            case Mode::Residual: return juce::String("Residual");
            case Mode::Isometric: return juce::String("Iso 3D");
            case Mode::IsometricResidual: return juce::String("Iso 3D Residual");
            case Mode::IsometricEnvelope:    return juce::String("Iso 3D Envelope");
            case Mode::IsometricEnvelopeRDC: return juce::String("Iso 3D Env (RDC)");
        }
        return juce::String("Trace + Residual");
    };

    auto addSourceMenu = [&](juce::PopupMenu& root,
                             AtsTraceSource source,
                             const juce::String& label,
                             bool enabled,
                             int baseId) {
        juce::PopupMenu sub;
        sub.addItem(baseId + 1, modeName(Mode::Combined), enabled,
                    traceSource == source && mode == Mode::Combined);
        sub.addItem(baseId + 2, modeName(Mode::Partials), enabled,
                    traceSource == source && mode == Mode::Partials);
        sub.addItem(baseId + 3, modeName(Mode::Residual), enabled,
                    traceSource == source && mode == Mode::Residual);
        sub.addItem(baseId + 4, modeName(Mode::Isometric), enabled,
                    traceSource == source && mode == Mode::Isometric);
        sub.addItem(baseId + 5, modeName(Mode::IsometricResidual), enabled,
                    traceSource == source && mode == Mode::IsometricResidual);
        sub.addItem(baseId + 6, modeName(Mode::IsometricEnvelope), enabled,
                    traceSource == source && mode == Mode::IsometricEnvelope);
        sub.addItem(baseId + 7, modeName(Mode::IsometricEnvelopeRDC), enabled,
                    traceSource == source && mode == Mode::IsometricEnvelopeRDC);
        root.addSubMenu(label, sub, enabled);
    };

    int partials = 0;
    int frames = 0;
    bool hasResidual = false;
    const bool hasCurrent = processor.getModelFootprint(partials, frames, hasResidual);
    AtsMorphSourceSlotInfo sourceA;
    AtsMorphSourceSlotInfo sourceB;
    processor.getMorphSourceSlotInfo(0, sourceA);
    processor.getMorphSourceSlotInfo(1, sourceB);

    juce::PopupMenu menu;
    addSourceMenu(menu, AtsTraceSource::Current, "Current", hasCurrent, 100);
    addSourceMenu(menu, AtsTraceSource::SourceA, "Source A", sourceA.hasModel, 200);
    addSourceMenu(menu, AtsTraceSource::SourceB, "Source B", sourceB.hasModel, 300);

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&viewMenuButton),
                       [this](int result) {
                           if (result == 0)
                               return;

                           AtsTraceSource nextSource = AtsTraceSource::Current;
                           int modeOffset = result - 100;
                           if (result >= 200 && result < 300)
                           {
                               nextSource = AtsTraceSource::SourceA;
                               modeOffset = result - 200;
                           }
                           else if (result >= 300)
                           {
                               nextSource = AtsTraceSource::SourceB;
                               modeOffset = result - 300;
                           }

                           Mode nextMode = Mode::Combined;
                           if (modeOffset == 2)
                               nextMode = Mode::Partials;
                           else if (modeOffset == 3)
                               nextMode = Mode::Residual;
                           else if (modeOffset == 4)
                               nextMode = Mode::Isometric;
                           else if (modeOffset == 5)
                               nextMode = Mode::IsometricResidual;
                           else if (modeOffset == 6)
                               nextMode = Mode::IsometricEnvelope;
                           else if (modeOffset == 7)
                               nextMode = Mode::IsometricEnvelopeRDC;
                           setView(nextSource, nextMode);
                       });
}

void SpectrogramPanel::updateViewMenuButton()
{
    juce::String sourceName = "Current";
    if (traceSource == AtsTraceSource::SourceA)
        sourceName = "Source A";
    else if (traceSource == AtsTraceSource::SourceB)
        sourceName = "Source B";

    juce::String modeName = "Trace + Residual";
    if (mode == Mode::Partials)
        modeName = "Partials";
    else if (mode == Mode::Residual)
        modeName = "Residual";
    else if (mode == Mode::Isometric)
        modeName = "Iso 3D";
    else if (mode == Mode::IsometricResidual)
        modeName = "Iso 3D Residual";
    else if (mode == Mode::IsometricEnvelope)
        modeName = "Iso 3D Envelope";
    else if (mode == Mode::IsometricEnvelopeRDC)
        modeName = "Iso 3D Env (RDC)";

    viewMenuButton.setButtonText(sourceName + " / " + modeName);
}

void SpectrogramPanel::timerCallback()
{
    // Don't compete with the resynth worker — the snapshot would be stale anyway.
    if (processor.isWorkerBusy())
        return;

    // Never clobber an in-progress onset edit with a rebuilt snapshot.
    if (draggingOnsetIndex >= 0)
        return;

    const int rev = processor.getModelRevision();
    const uint64_t morphSourcesRev = processor.getMorphSourcesRevision();
    const uint64_t shapeRev = processor.getDisplayShapeRevision();
    const uint64_t overlayRev = processor.getSmearnovOverlayRevision();
    if (rev != lastModelRevision || morphSourcesRev != lastMorphSourcesRevision)
    {
        lastModelRevision = rev;
        lastMorphSourcesRevision = morphSourcesRev;
        lastDisplayShapeRevision = shapeRev;
        lastSmearnovOverlayRevision = overlayRev;
        rebuildTraceSnapshot();
        repaintDisplay();
    }
    else if (shapeRev != lastDisplayShapeRevision)
    {
        lastDisplayShapeRevision = shapeRev;
        lastSmearnovOverlayRevision = overlayRev;
        rebuildTraceSnapshot(true);
        repaintDisplay();
    }
    else if (overlayRev != lastSmearnovOverlayRevision)
    {
        lastSmearnovOverlayRevision = overlayRev;
        rebuildTraceSnapshot(true);
        repaintDisplay();
    }
}

bool SpectrogramPanel::updatePlayheadAnimation()
{
    const float target = processor.getPreviewPlayheadNormForTraceSource(traceSource);
    if (target < 0.0f)
    {
        resetPlayheadAnimation();
        return false;
    }

    const double nowMs = juce::Time::getMillisecondCounterHiRes();
    if (animatedPlayheadNorm < 0.0f || lastPlayheadAnimationMs <= 0.0
        || target < animatedPlayheadNorm - 0.02f
        || std::abs(target - animatedPlayheadNorm) > 0.25f)
    {
        animatedPlayheadNorm = target;
        lastPlayheadAnimationMs = nowMs;
        return true;
    }

    const double deltaSeconds = juce::jlimit(0.0, 0.08, (nowMs - lastPlayheadAnimationMs) * 0.001);
    lastPlayheadAnimationMs = nowMs;

    constexpr double followRateHz = 34.0;
    const float alpha = (float) (1.0 - std::exp(-followRateHz * deltaSeconds));
    animatedPlayheadNorm += (target - animatedPlayheadNorm) * juce::jlimit(0.0f, 1.0f, alpha);
    animatedPlayheadNorm = juce::jlimit(0.0f, 1.0f, animatedPlayheadNorm);
    return true;
}

void SpectrogramPanel::resetPlayheadAnimation()
{
    animatedPlayheadNorm = -1.0f;
    lastPlayheadAnimationMs = 0.0;
}

void SpectrogramPanel::invalidateDisplayCache()
{
    displayCacheDirty = true;
}

void SpectrogramPanel::repaintDisplay()
{
    invalidateDisplayCache();
    repaint();
    if (playheadOverlay != nullptr)
        playheadOverlay->repaint();
}

void SpectrogramPanel::resetDisplayReference()
{
    ampDisplayReferencePeak = 0.0;
    residualDisplayReferencePeak = 0.0;
}

void SpectrogramPanel::rebuildTraceSnapshot()
{
    rebuildTraceSnapshot(false);
}

void SpectrogramPanel::rebuildTraceSnapshot(bool preserveDisplayReference)
{
    if (!preserveDisplayReference)
        resetDisplayReference();

    AtsSpectralTraceDisplayReference displayReference;
    if (preserveDisplayReference)
    {
        displayReference.ampPeak = ampDisplayReferencePeak;
        displayReference.residualPeak = residualDisplayReferencePeak;
    }

    AtsSpectralTraceSnapshot next;
    processor.buildSpectralTraceSnapshot(traceSource,
                                         next,
                                         kMaxVisiblePartials,
                                         kMaxPointsPerPartial,
                                         kResidualColumns,
                                         displayReference);
    traceSnapshot = std::move(next);
    lastDisplayShapeRevision = traceSnapshot.displayShapeRevision;
    lastSmearnovOverlayRevision = processor.getSmearnovOverlayRevision();

    if (ampDisplayReferencePeak <= 0.0)
        ampDisplayReferencePeak = traceSnapshot.ampDisplayReferencePeak;
    if (residualDisplayReferencePeak <= 0.0)
        residualDisplayReferencePeak = traceSnapshot.residualDisplayReferencePeak;

    selectedPartials.erase(std::remove_if(selectedPartials.begin(),
                                          selectedPartials.end(),
                                          [this](int partialIndex) {
                                              return std::none_of(traceSnapshot.partials.begin(),
                                                                  traceSnapshot.partials.end(),
                                                                  [partialIndex](const auto& trace) {
                                                                      return trace.partialIndex == partialIndex;
                                                                  });
                                          }),
                           selectedPartials.end());

    selectedResidualBands.erase(std::remove_if(selectedResidualBands.begin(),
                                               selectedResidualBands.end(),
                                               [this](int bandIndex) {
                                                   return std::none_of(traceSnapshot.residualBands.begin(),
                                                                       traceSnapshot.residualBands.end(),
                                                                       [bandIndex](const auto& band) {
                                                                           return band.bandIndex == bandIndex;
                                                                       });
                                               }),
                                selectedResidualBands.end());
    if (traceSnapshot.frameCount <= 0 || selectedResidualBands.empty())
    {
        selectedResidualBands.clear();
        selectedResidualFirstFrame = -1;
        selectedResidualLastFrame = -1;
    }
    else
    {
        selectedResidualFirstFrame = juce::jlimit(0, traceSnapshot.frameCount - 1, selectedResidualFirstFrame);
        selectedResidualLastFrame = juce::jlimit(0, traceSnapshot.frameCount - 1, selectedResidualLastFrame);
    }

    hoveredPartial = -1;
    hoveredResidualBand = -1;
    hoveredResidualFrame = -1;
    updateSelectionLabel();

    // Build the complementary source snapshot for the envelope overlay.
    if (mode == Mode::IsometricEnvelope || mode == Mode::IsometricEnvelopeRDC)
    {
        AtsTraceSource altSource = AtsTraceSource::SourceA;
        if (traceSource == AtsTraceSource::SourceA)
            altSource = AtsTraceSource::SourceB;
        else if (traceSource == AtsTraceSource::Current)
            altSource = AtsTraceSource::SourceA;
        AtsSpectralTraceSnapshot alt;
        processor.buildSpectralTraceSnapshot(altSource, alt,
                                             kMaxVisiblePartials, kMaxPointsPerPartial,
                                             kResidualColumns, {});
        secondarySnapshot = std::move(alt);
    }
    else
    {
        secondarySnapshot = {};
    }

    const bool wasOnsetButtonVisible = onsetEditButton.isVisible();
    updateOnsetEditButtonVisibility();
    if (onsetEditButton.isVisible() != wasOnsetButtonVisible)
        resized();

    repaintDisplay();
}

void SpectrogramPanel::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds();
    if (bounds.isEmpty())
        return;

    if (displayCache.isNull()
        || displayCache.getWidth() != bounds.getWidth()
        || displayCache.getHeight() != bounds.getHeight()
        || displayCacheDirty)
    {
        displayCache = juce::Image(juce::Image::ARGB, bounds.getWidth(), bounds.getHeight(), true);
        juce::Graphics cacheGraphics(displayCache);
        cacheGraphics.fillAll(juce::Colour(0xff070a08));
        paintScreen(cacheGraphics, getScreenBounds());
        displayCacheDirty = false;
    }

    g.drawImageAt(displayCache, 0, 0);
}

void SpectrogramPanel::paintScreen(juce::Graphics& g, juce::Rectangle<float> screen)
{
    g.setColour(juce::Colour(0xff050705));
    g.fillRoundedRectangle(screen, 5.0f);
    g.setColour(juce::Colour(0xff0d3f32));
    g.drawRoundedRectangle(screen.reduced(0.5f), 5.0f, 1.0f);
    g.setColour(juce::Colour(0x443fffc0));
    g.drawRoundedRectangle(screen.reduced(3.0f), 4.0f, 1.0f);

    auto plot = getPlotBounds();
    g.saveState();
    g.reduceClipRegion(plot.toNearestInt());

    g.setColour(juce::Colour(0xff020302));
    g.fillRect(plot);

    if (isIsometricDisplayMode(mode))
    {
        paintIsometricGrid(g, plot);
    }
    else
    {
        g.setColour(juce::Colour(0x183cffb3));
        for (int i = 0; i <= 8; ++i)
        {
            const float x = plot.getX() + plot.getWidth() * (float) i / 8.0f;
            g.drawVerticalLine(juce::roundToInt(x), plot.getY(), plot.getBottom());
        }
        for (int i = 0; i <= 6; ++i)
        {
            const float y = plot.getY() + plot.getHeight() * (float) i / 6.0f;
            g.drawHorizontalLine(juce::roundToInt(y), plot.getX(), plot.getRight());
        }
    }

    g.setFont(juce::Font(12.0f, juce::Font::plain));
    g.setColour(juce::Colour(0xff8dffd8));
    g.drawText("ATS TRACE DISPLAY",
               screen.withTrimmedLeft(14.0f).withTrimmedTop(6.0f).withHeight(15.0f),
               juce::Justification::centredLeft,
               false);

    if (traceSnapshot.frameCount <= 0)
    {
        g.setFont(juce::Font(15.0f, juce::Font::plain));
        g.setColour(juce::Colour(0xff5bffbb));
        g.drawText("NO ATS MODEL", plot.toNearestInt(), juce::Justification::centred);
    }
    else
    {
        if (mode == Mode::Combined)
            paintResidualBackdrop(g, plot, false);
        else if (mode == Mode::Residual)
            paintResidualBackdrop(g, plot, true);

        if (mode == Mode::Isometric)
            paintIsometricPartials(g, plot);
        else if (mode == Mode::IsometricResidual)
            paintIsometricResidual(g, plot);
        else if (mode == Mode::IsometricEnvelope)
            paintIsometricEnvelope(g, plot);
        else if (mode == Mode::IsometricEnvelopeRDC)
            paintIsometricEnvelopeRDC(g, plot);
        else if (mode != Mode::Residual)
            paintPartialTraces(g, plot);

        paintSmearnovOverlay(g, plot);
        paintSelectionOverlay(g, screen, plot);
    }

    g.restoreState();

    if (showsBandLabelGutter())
        paintBandLabelGutter(g, getBandLabelGutterBounds(), plot);

    g.setColour(juce::Colour(0x25000000));
    for (int y = juce::roundToInt(screen.getY()) + 2; y < juce::roundToInt(screen.getBottom()); y += 3)
        g.drawHorizontalLine(y, screen.getX() + 2.0f, screen.getRight() - 2.0f);
}

void SpectrogramPanel::paintResidualBackdrop(juce::Graphics& g, juce::Rectangle<float> plot, bool foreground)
{
    if (traceSnapshot.residualBands.empty())
        return;

    if (foreground)
    {
        g.setColour(juce::Colour(0x2210ff77));
        for (const auto& band : traceSnapshot.residualBands)
        {
            const auto yRange = yRangeForBand(band, plot);
            const float centreY = yRange.getStart() + yRange.getLength() * 0.5f;
            g.drawHorizontalLine(juce::roundToInt(centreY), plot.getX(), plot.getRight());
        }
    }

    for (const auto& band : traceSnapshot.residualBands)
    {
        const auto yRange = yRangeForBand(band, plot);
        const float y = yRange.getStart();
        const float h = juce::jmax(1.0f, yRange.getLength());
        const int columns = (int) band.energyNorm.size();
        if (columns <= 0)
            continue;

        const float columnW = plot.getWidth() / (float) columns;
        if (foreground)
        {
            const float centreY = y + h * 0.5f;
            juce::Path ridge;
            juce::Path fill;
            fill.startNewSubPath(plot.getX(), centreY);
            bool started = false;
            for (int column = 0; column < columns; ++column)
            {
                const float e = band.energyNorm[(size_t) column];
                const float columnTimeNorm = ((float) column + 0.5f) / (float) columns;
                const float x = plot.getX() + plot.getWidth()
                                * ((columnTimeNorm - visibleTimeRange().getStart()) / visibleTimeRange().getLength());
                const float ridgeY = centreY - e * h * 0.46f;
                if (!started)
                {
                    ridge.startNewSubPath(x, ridgeY);
                    started = true;
                }
                else
                {
                    ridge.lineTo(x, ridgeY);
                }
                fill.lineTo(x, ridgeY);
            }
            fill.lineTo(plot.getRight(), centreY);
            fill.closeSubPath();

            g.setColour(juce::Colour(0xff12ff88).withAlpha(0.055f));
            g.fillPath(fill);
            g.setColour(juce::Colour(0xff77fff1).withAlpha(0.28f));
            g.strokePath(ridge, juce::PathStrokeType(3.2f));
            g.setColour(juce::Colour(0xff16ff80).withAlpha(0.82f));
            g.strokePath(ridge, juce::PathStrokeType(1.15f));
        }

        for (int column = 0; column < columns; ++column)
        {
            const float e = band.energyNorm[(size_t) column];
            if (e <= 0.01f)
                continue;

            const float columnTimeNorm = ((float) column + 0.5f) / (float) columns;
            const float x = plot.getX() + plot.getWidth()
                          * ((columnTimeNorm - visibleTimeRange().getStart()) / visibleTimeRange().getLength());
            if (x + columnW < plot.getX() || x > plot.getRight())
                continue;

            const float alpha = foreground ? (0.035f + e * 0.18f) : (0.025f + e * 0.19f);
            const auto colour = juce::Colour(0xff36ffbf).interpolatedWith(juce::Colour(0xffbaff6a), e * 0.55f)
                                    .withAlpha(alpha);
            g.setColour(colour);
            if (foreground)
            {
                const float centreY = y + h * 0.5f;
                const float barH = juce::jmax(1.0f, e * h * 0.88f);
                g.fillRect(juce::Rectangle<float>(x - columnW * 0.5f,
                                                  centreY - barH * 0.5f,
                                                  juce::jmax(1.0f, columnW + 0.5f),
                                                  barH));
            }
            else
            {
                g.fillRect(juce::Rectangle<float>(x - columnW * 0.5f,
                                                  y,
                                                  juce::jmax(1.0f, columnW + 0.5f),
                                                  h));
            }
        }
    }
}

void SpectrogramPanel::paintPartialTraces(juce::Graphics& g, juce::Rectangle<float> plot)
{
    auto drawPass = [&](bool selectedPass) {
        for (const auto& trace : traceSnapshot.partials)
        {
            const bool selected = isPartialSelected(trace.partialIndex);
            const bool hovered = trace.partialIndex == hoveredPartial;
            if (selected != selectedPass)
                continue;

            for (size_t i = 1; i < trace.points.size(); ++i)
            {
                const auto& prev = trace.points[i - 1];
                const auto& cur = trace.points[i];
                if (!prev.active || !cur.active)
                    continue;

                const auto a = positionForPoint(prev, plot);
                const auto b = positionForPoint(cur, plot);
                if (std::max(a.x, b.x) < plot.getX() || std::min(a.x, b.x) > plot.getRight()
                    || std::max(a.y, b.y) < plot.getY() || std::min(a.y, b.y) > plot.getBottom())
                    continue;

                const float amp = juce::jlimit(0.0f, 2.0f, (prev.ampNorm + cur.ampNorm) * 0.5f);
                const float ampVis = juce::jmin(1.0f, amp);
                const auto line = juce::Line<float>(a, b);

                if (selected || hovered)
                {
                    g.setColour((selected ? juce::Colour(0xfffff6a2) : juce::Colour(0xffcffff6)).withAlpha(0.18f));
                    g.drawLine(line, selected ? 6.0f : 4.0f);
                }

                auto colour = selected ? juce::Colour(0xfffff3a0) : traceColour(ampVis);
                if (hovered && !selected)
                    colour = juce::Colour(0xffe9fff7);
                g.setColour(colour.withAlpha(selected ? juce::jmin(1.0f, 0.72f + amp * 0.2f)
                                                     : (0.15f + ampVis * 0.62f)));
                g.drawLine(line, (selected ? 1.8f : 0.55f) + amp * (selected ? 1.6f : 1.35f));
            }
        }
    };

    drawPass(false);
    drawPass(true);
}

void SpectrogramPanel::paintIsometricGrid(juce::Graphics& g, juce::Rectangle<float> plot)
{
    auto makePoint = [&](float timeNorm, float freqNorm, float ampNorm = 0.0f) {
        AtsPartialTracePoint p;
        p.timeNorm = visibleTimeRange().getStart() + visibleTimeRange().getLength() * timeNorm;
        p.freqNorm = visibleFreqRange().getStart() + visibleFreqRange().getLength() * freqNorm;
        p.ampNorm = ampNorm;
        p.active = true;
        return positionForIsoPoint(p, plot);
    };

    auto drawIsoLine = [&](juce::Point<float> a,
                           juce::Point<float> b,
                           juce::Colour colour,
                           float thickness) {
        g.setColour(colour.withAlpha(colour.getFloatAlpha() * 0.45f));
        g.drawLine({ a, b }, thickness + 2.2f);
        g.setColour(colour);
        g.drawLine({ a, b }, thickness);
    };

    const auto frontLeft = makePoint(0.0f, 0.0f);
    const auto frontRight = makePoint(0.0f, 1.0f);
    const auto backLeft = makePoint(1.0f, 0.0f);
    const auto backRight = makePoint(1.0f, 1.0f);

    g.setColour(juce::Colour(0x1217ff9e));
    for (int i = 1; i < 8; ++i)
    {
        const float f = (float) i / 8.0f;
        g.drawLine({ makePoint(0.0f, f), makePoint(1.0f, f) }, 0.7f);
    }
    for (int i = 1; i < 10; ++i)
    {
        const float t = (float) i / 10.0f;
        g.drawLine({ makePoint(t, 0.0f), makePoint(t, 1.0f) }, 0.65f);
    }

    drawIsoLine(frontLeft, frontRight, juce::Colour(0xff25f7c4).withAlpha(0.48f), 1.15f);
    drawIsoLine(frontLeft, backLeft, juce::Colour(0xff16dba8).withAlpha(0.40f), 1.0f);
    drawIsoLine(frontRight, backRight, juce::Colour(0xff16dba8).withAlpha(0.25f), 0.8f);
    drawIsoLine(backLeft, backRight, juce::Colour(0xff16dba8).withAlpha(0.23f), 0.8f);

    for (const auto base : { frontLeft, backLeft, frontRight })
    {
        const auto top = juce::Point<float>(base.x, base.y - plot.getHeight() * 0.28f);
        drawIsoLine(base, top, juce::Colour(0xff25f7c4).withAlpha(0.18f), 0.65f);
    }
}

void SpectrogramPanel::paintIsometricPartials(juce::Graphics& g, juce::Rectangle<float> plot)
{
    if (traceSnapshot.partials.empty())
        return;

    const auto timeRange = visibleTimeRange();
    const auto freqRange = visibleFreqRange();
    const float timePad = timeRange.getLength() * 0.025f;
    const float freqPad = freqRange.getLength() * 0.04f;
    auto pointIsVisible = [&](const AtsPartialTracePoint& point) {
        return point.active
               && point.timeNorm >= timeRange.getStart() - timePad
               && point.timeNorm <= timeRange.getEnd() + timePad
               && point.freqNorm >= freqRange.getStart() - freqPad
               && point.freqNorm <= freqRange.getEnd() + freqPad;
    };

    auto drawTracePass = [&](bool selectedPass) {
        for (const auto& trace : traceSnapshot.partials)
        {
            const bool selected = isPartialSelected(trace.partialIndex);
            const bool hovered = trace.partialIndex == hoveredPartial;
            if (selected != selectedPass)
                continue;

            for (size_t i = 1; i < trace.points.size(); ++i)
            {
                const auto& prev = trace.points[i - 1];
                const auto& cur = trace.points[i];
                if (!pointIsVisible(prev) || !pointIsVisible(cur))
                    continue;

                const auto a = positionForIsoPoint(prev, plot);
                const auto b = positionForIsoPoint(cur, plot);
                if (std::max(a.x, b.x) < plot.getX() || std::min(a.x, b.x) > plot.getRight()
                    || std::max(a.y, b.y) < plot.getY() || std::min(a.y, b.y) > plot.getBottom())
                    continue;

                const float amp = juce::jlimit(0.0f, 2.0f, (prev.ampNorm + cur.ampNorm) * 0.5f);
                const float ampVis = juce::jmin(1.0f, amp);
                const auto line = juce::Line<float>(a, b);

                if (selected || hovered)
                {
                    g.setColour((selected ? juce::Colour(0xfffff6a2) : juce::Colour(0xffd9fff8)).withAlpha(0.22f));
                    g.drawLine(line, selected ? 6.4f : 4.5f);
                }

                auto colour = selected ? juce::Colour(0xfffff3a0)
                                       : juce::Colour(0xff2ffff0).interpolatedWith(juce::Colour(0xff6aff8d), ampVis * 0.55f);
                if (hovered && !selected)
                    colour = juce::Colour(0xffeffffb);

                g.setColour(colour.withAlpha(selected ? juce::jmin(1.0f, 0.78f + amp * 0.16f)
                                                     : (0.18f + ampVis * 0.58f)));
                g.drawLine(line, (selected ? 1.75f : 0.65f) + amp * (selected ? 1.45f : 1.1f));
            }
        }
    };

    drawTracePass(false);
    drawTracePass(true);
}

void SpectrogramPanel::paintIsometricResidual(juce::Graphics& g, juce::Rectangle<float> plot)
{
    if (traceSnapshot.residualBands.empty())
        return;

    const auto timeRange = visibleTimeRange();
    const auto freqRange = visibleFreqRange();
    const double minHz = juce::jmax(1.0, traceSnapshot.minFrequencyHz);
    const double maxHz = juce::jmax(minHz * 1.2, traceSnapshot.maxFrequencyHz);
    const double logRange = std::log(maxHz / minHz);
    auto normForHz = [&](double hz) {
        const double clamped = juce::jlimit(minHz, maxHz, juce::jmax(1.0, hz));
        return (float) juce::jlimit(0.0, 1.0, std::log(clamped / minHz) / logRange);
    };

    for (const auto& band : traceSnapshot.residualBands)
    {
        const float lowNorm = normForHz(band.lowHz);
        const float highNorm = normForHz(band.highHz);
        const float freqNorm = juce::jlimit(0.0f, 1.0f, 0.5f * (lowNorm + highNorm));
        if (freqNorm < freqRange.getStart() - freqRange.getLength() * 0.05f
            || freqNorm > freqRange.getEnd() + freqRange.getLength() * 0.05f)
            continue;

        const bool selected = std::find(selectedResidualBands.begin(),
                                        selectedResidualBands.end(),
                                        band.bandIndex) != selectedResidualBands.end();
        const bool hovered = band.bandIndex == hoveredResidualBand;
        const int columns = (int) band.energyNorm.size();
        if (columns <= 1)
            continue;

        juce::Path baseLine;
        juce::Path ridgeLine;
        bool started = false;

        for (int column = 0; column < columns; ++column)
        {
            const float timeNorm = (float) column / (float) (columns - 1);
            if (timeNorm < timeRange.getStart() - timeRange.getLength() * 0.03f
                || timeNorm > timeRange.getEnd() + timeRange.getLength() * 0.03f)
                continue;

            AtsPartialTracePoint basePoint;
            basePoint.timeNorm = timeNorm;
            basePoint.freqNorm = freqNorm;
            basePoint.ampNorm = 0.0f;
            basePoint.active = true;

            AtsPartialTracePoint ridgePoint = basePoint;
            ridgePoint.ampNorm = juce::jlimit(0.0f, 1.15f, band.energyNorm[(size_t) column] * 1.15f);

            const auto p0 = positionForIsoPoint(basePoint, plot);
            const auto p1 = positionForIsoPoint(ridgePoint, plot);

            if (!started)
            {
                baseLine.startNewSubPath(p0);
                ridgeLine.startNewSubPath(p1);
                started = true;
            }
            else
            {
                baseLine.lineTo(p0);
                ridgeLine.lineTo(p1);
            }
        }

        if (!started)
            continue;

        if (hovered || selected)
        {
            g.setColour((selected ? juce::Colour(0xfffff6a2) : juce::Colour(0xffd9fff8)).withAlpha(0.24f));
            g.strokePath(ridgeLine, juce::PathStrokeType(selected ? 7.0f : 5.0f));
        }

        g.setColour(juce::Colour(0xff22dba7).withAlpha(hovered ? 0.58f : (selected ? 0.44f : 0.20f)));
        g.strokePath(baseLine, juce::PathStrokeType(hovered ? 2.2f : (selected ? 1.7f : 1.0f)));

        g.setColour((selected ? juce::Colour(0xfffff3a0) : juce::Colour(0xff74fff0))
                        .withAlpha(hovered ? 1.0f : (selected ? 0.82f : 0.62f)));
        g.strokePath(ridgeLine, juce::PathStrokeType(hovered ? 3.2f : (selected ? 2.6f : 1.75f)));
    }
}

// ---------------------------------------------------------------------------
// Envelope surface helper — draws a smoothed spectral envelope waterfall from
// the partial amplitudes in `snap`.  Each time slice interpolates both
// freqNorm and ampNorm at the exact timeNorm, then builds a smooth envelope
// via Gaussian kernel regression — matching the Python Bark-scale RDC output.
void SpectrogramPanel::paintEnvelopeSurface(juce::Graphics& g,
                                            juce::Rectangle<float> plot,
                                            const AtsSpectralTraceSnapshot& snap,
                                            juce::Colour colour,
                                            float alphaScale) const
{
    if (snap.partials.empty())
        return;

    const auto timeRange = visibleTimeRange();
    const auto freqRange = visibleFreqRange();
    constexpr int   kSlices   = 40;    // time columns
    constexpr int   kFreqBins = 128;   // freq resolution per slice
    constexpr float kSigma    = 6.5f;  // wider Gaussian → smoother envelope
    constexpr int   kTaperBins = 14;   // wider fade-in/out → no steep box edges


    // Pre-build all slice paths so we can do clearance + colour in two passes.
    struct SliceData { juce::Path path; float depthAlpha; };
    std::vector<SliceData> slices;
    slices.reserve(static_cast<std::size_t>(kSlices));

    for (int si = 0; si < kSlices; ++si)
    {
        const float timeNorm = timeRange.getStart()
                               + timeRange.getLength() * (float) si / (float) (kSlices - 1);

        // Collect (visFreqNorm, ampNorm) for every active partial at this time.
        std::vector<std::pair<float, float>> samples;
        samples.reserve(snap.partials.size());

        for (const auto& trace : snap.partials)
        {
            float ampAtTime  = 0.0f;
            float freqAtTime = -1.0f;

            for (std::size_t pi = 1; pi < trace.points.size(); ++pi)
            {
                const auto& prev = trace.points[pi - 1];
                const auto& cur  = trace.points[pi];
                if (cur.timeNorm < timeNorm)
                    continue;
                if (prev.timeNorm > timeNorm)
                    break;
                if (!prev.active || !cur.active)
                    continue;
                const float span = cur.timeNorm - prev.timeNorm;
                const float t = span > 1.0e-6f ? (timeNorm - prev.timeNorm) / span : 0.0f;
                ampAtTime  = prev.ampNorm  + t * (cur.ampNorm  - prev.ampNorm);
                freqAtTime = prev.freqNorm + t * (cur.freqNorm - prev.freqNorm);
                break;
            }

            if (ampAtTime <= 0.0f || freqAtTime < 0.0f)
                continue;

            const float fVis = (freqAtTime - freqRange.getStart())
                               / juce::jmax(1.0e-6f, freqRange.getLength());
            if (fVis >= -0.1f && fVis <= 1.1f)
                samples.push_back({ fVis, ampAtTime });
        }

        if (samples.empty())
            continue;

        // Gaussian kernel regression — wider sigma fills harmonic gaps smoothly.
        std::vector<float> sumW (static_cast<std::size_t>(kFreqBins), 0.0f);
        std::vector<float> sumWA(static_cast<std::size_t>(kFreqBins), 0.0f);

        for (const auto& [fVis, amp] : samples)
        {
            const float binF = juce::jlimit(-0.5f, kFreqBins - 0.5f, fVis * kFreqBins);
            const int lo = juce::jmax(0, (int) (binF - 4.0f * kSigma));
            const int hi = juce::jmin(kFreqBins - 1, (int) (binF + 4.0f * kSigma) + 1);
            for (int b = lo; b <= hi; ++b)
            {
                const float dist = (float) b - binF;
                const float w = std::exp(-0.5f * dist * dist / (kSigma * kSigma));
                sumW [static_cast<std::size_t>(b)] += w;
                sumWA[static_cast<std::size_t>(b)] += w * amp;
            }
        }

        // Cosine taper over wider edge region → gentler slopes, no box silhouette.
        auto getAmpN = [&](int b) -> float {
            if (sumW[static_cast<std::size_t>(b)] <= 1.0e-6f)
                return 0.0f;
            float val = sumWA[static_cast<std::size_t>(b)] / sumW[static_cast<std::size_t>(b)];
            const int edgeDist = std::min(b, kFreqBins - 1 - b);
            if (edgeDist < kTaperBins)
            {
                const float t = (float) edgeDist / (float) kTaperBins;
                val *= 0.5f - 0.5f * std::cos(juce::MathConstants<float>::pi * t);
            }
            return val;
        };

        // Draw the full frequency span — the cosine taper already decays the
        // edges to exactly 0, so there is no box-outline silhouette.
        juce::Path topPath;
        for (int b = 0; b < kFreqBins; ++b)
        {
            const float ampN  = getAmpN(b);
            const float freqN = freqRange.getStart()
                                + freqRange.getLength() * (float) b / (float) (kFreqBins - 1);
            const auto pt = positionForIsoCoords(timeNorm, freqN, ampN, plot);
            if (b == 0)
                topPath.startNewSubPath(pt);
            else
                topPath.lineTo(pt);
        }

        if (topPath.isEmpty())
            continue;

        const float depthAlpha = 0.35f + 0.65f * (float) si / (float) (kSlices - 1);
        slices.push_back({ std::move(topPath), depthAlpha });
    }

    // Painter's algorithm: two passes back-to-front.
    // Pass 1 — thick near-black clearance stroke so front slices occlude back slices.
    const juce::Colour clearCol = juce::Colour(0xff050a0e);
    for (const auto& s : slices)
        g.setColour(clearCol.withAlpha(s.depthAlpha * 0.92f)),
        g.strokePath(s.path, juce::PathStrokeType(4.5f));

    // Pass 2 — glow + crisp coloured line.
    for (const auto& s : slices)
    {
        const juce::Colour lineCol = colour.withAlpha(s.depthAlpha * alphaScale);
        g.setColour(lineCol.withAlpha(lineCol.getFloatAlpha() * 0.25f));
        g.strokePath(s.path, juce::PathStrokeType(3.5f));
        g.setColour(lineCol);
        g.strokePath(s.path, juce::PathStrokeType(1.5f));
    }
}

void SpectrogramPanel::paintEnvelopeRangeSelection(juce::Graphics& g, juce::Rectangle<float> plot) const
{
    if (envRangeStartNorm < 0.0f || envRangeEndNorm < 0.0f)
        return;

    const auto timeRange = visibleTimeRange();
    const auto freqRange = visibleFreqRange();
    float t0 = juce::jmin(envRangeStartNorm, envRangeEndNorm);
    float t1 = juce::jmax(envRangeStartNorm, envRangeEndNorm);
    // Clip the slab to the visible time window so it never paints outside the plot.
    t0 = juce::jlimit(timeRange.getStart(), timeRange.getEnd(), t0);
    t1 = juce::jlimit(timeRange.getStart(), timeRange.getEnd(), t1);
    if (t1 <= t0)
        return;

    float frontFreq = freqRange.getStart();
    float backFreq = freqRange.getEnd();
    if (envFreqStartNorm >= 0.0f && envFreqEndNorm >= 0.0f)
    {
        frontFreq = juce::jlimit(freqRange.getStart(), freqRange.getEnd(),
                                 juce::jmin(envFreqStartNorm, envFreqEndNorm));
        backFreq = juce::jlimit(freqRange.getStart(), freqRange.getEnd(),
                                juce::jmax(envFreqStartNorm, envFreqEndNorm));
    }
    const float topAmp = 1.1f;

    const auto fb0 = positionForIsoCoords(t0, frontFreq, 0.0f, plot);
    const auto fb1 = positionForIsoCoords(t1, frontFreq, 0.0f, plot);
    const auto bb0 = positionForIsoCoords(t0, backFreq, 0.0f, plot);
    const auto bb1 = positionForIsoCoords(t1, backFreq, 0.0f, plot);
    const auto ft0 = positionForIsoCoords(t0, frontFreq, topAmp, plot);
    const auto ft1 = positionForIsoCoords(t1, frontFreq, topAmp, plot);
    const auto bt0 = positionForIsoCoords(t0, backFreq, topAmp, plot);
    const auto bt1 = positionForIsoCoords(t1, backFreq, topAmp, plot);

    const juce::Colour accent(0xff5ed0ff);

    // Floor quad across the full frequency depth — the primary "this time region" cue.
    {
        juce::Path floor;
        floor.startNewSubPath(fb0);
        floor.lineTo(fb1);
        floor.lineTo(bb1);
        floor.lineTo(bb0);
        floor.closeSubPath();
        g.setColour(accent.withAlpha(0.15f));
        g.fillPath(floor);
        g.setColour(accent.withAlpha(0.5f));
        g.strokePath(floor, juce::PathStrokeType(1.2f));
    }

    // End-walls at the start/end times (front edges bold, back edges faint for depth).
    g.setColour(accent.withAlpha(0.8f));
    g.drawLine(juce::Line<float>(fb0, ft0), 1.8f);
    g.drawLine(juce::Line<float>(fb1, ft1), 1.8f);
    g.setColour(accent.withAlpha(0.32f));
    g.drawLine(juce::Line<float>(bb0, bt0), 1.0f);
    g.drawLine(juce::Line<float>(bb1, bt1), 1.0f);

    // Top outline of the slab.
    g.setColour(accent.withAlpha(0.42f));
    g.drawLine(juce::Line<float>(ft0, ft1), 1.0f);
    g.drawLine(juce::Line<float>(bt0, bt1), 1.0f);
    g.drawLine(juce::Line<float>(ft0, bt0), 1.0f);
    g.drawLine(juce::Line<float>(ft1, bt1), 1.0f);

    // Time-range label above the leading wall.
    const double dur = traceSnapshot.durationSeconds;
    juce::String label;
    if (dur > 0.0)
        label = juce::String(t0 * (float) dur, 3) + " - " + juce::String(t1 * (float) dur, 3) + " s";
    else
        label = juce::String(juce::roundToInt(t0 * 100.0f)) + " - "
                + juce::String(juce::roundToInt(t1 * 100.0f)) + " %";

    g.setFont(juce::Font(11.0f, juce::Font::bold));
    const float textW = g.getCurrentFont().getStringWidthFloat(label) + 10.0f;
    juce::Rectangle<float> labelBounds(ft0.x - 2.0f, ft0.y - 18.0f, textW, 15.0f);
    g.setColour(juce::Colours::black.withAlpha(0.55f));
    g.fillRoundedRectangle(labelBounds, 3.0f);
    g.setColour(accent.brighter(0.25f));
    g.drawText(label, labelBounds, juce::Justification::centred);
}

void SpectrogramPanel::paintIsometricEnvelope(juce::Graphics& g, juce::Rectangle<float> plot)
{
    // Draw secondary source first (behind primary).
    if (envSourceBBtn.getToggleState() && !secondarySnapshot.partials.empty())
        paintEnvelopeSurface(g, plot, secondarySnapshot, juce::Colour(0xffd08030), 0.85f);

    if (envSourceABtn.getToggleState() && !traceSnapshot.partials.empty())
        paintEnvelopeSurface(g, plot, traceSnapshot, juce::Colour(0xff4090d0), 1.0f);

    paintEnvelopeRangeSelection(g, plot);
}

// ---------------------------------------------------------------------------
// Self-contained Bark-scale Regularized Discrete Cepstrum (RDC) envelope math.
// Mirrors the Python visualize_envelopes.py rdc_fit_and_eval implementation.
namespace
{
namespace rdcDisplay
{
// Bark-scale normalization to [0,1].
static float barkNorm(float hz, float minHz, float maxHz)
{
    auto bark = [](float f) { return 6.0f * std::asinh(juce::jmax(1.0f, f) / 600.0f); };
    const float bMin = bark(juce::jmax(1.0f, minHz));
    const float bMax = bark(juce::jmax(1.0f, maxHz));
    return juce::jlimit(0.0f, 1.0f, (bark(juce::jmax(1.0f, hz)) - bMin)
                                     / juce::jmax(1.0e-6f, bMax - bMin));
}

// Cosine basis row: [1, 2cos(π·pos), 2cos(2π·pos), ..., 2cos(order·π·pos)].
static void buildBasisRow(float pos, int order, float* row)
{
    row[0] = 1.0f;
    const float theta = juce::MathConstants<float>::pi * juce::jlimit(0.0f, 1.0f, pos);
    for (int k = 1; k <= order; ++k)
        row[k] = 2.0f * std::cos((float) k * theta);
}

// Gaussian elimination with partial pivoting for a small n×n system.
// A (n*n, row-major) and b (n) are modified in-place; solution written to x.
static bool solveGaussian(int n, float* A, float* b, float* x)
{
    for (int col = 0; col < n; ++col)
    {
        int pivotRow = col;
        float pivotAbs = std::abs(A[col * n + col]);
        for (int row = col + 1; row < n; ++row)
        {
            const float v = std::abs(A[row * n + col]);
            if (v > pivotAbs) { pivotAbs = v; pivotRow = row; }
        }
        if (pivotAbs < 1.0e-9f) return false;
        if (pivotRow != col)
        {
            for (int k = 0; k < n; ++k) std::swap(A[col * n + k], A[pivotRow * n + k]);
            std::swap(b[col], b[pivotRow]);
        }
        const float diag = A[col * n + col];
        for (int row = col + 1; row < n; ++row)
        {
            const float f = A[row * n + col] / diag;
            for (int k = col; k < n; ++k) A[row * n + k] -= f * A[col * n + k];
            b[row] -= f * b[col];
        }
    }
    for (int row = n - 1; row >= 0; --row)
    {
        x[row] = b[row];
        for (int k = row + 1; k < n; ++k) x[row] -= A[row * n + k] * x[k];
        x[row] /= A[row * n + row];
    }
    return true;
}

// Fit RDC envelope to weighted (barkPos, dB) points.  Returns false on failure.
// order+1 ≤ 16. coeffs[] receives order+1 values.
static bool fitRDC(const std::vector<std::pair<float, float>>& pts,
                   const std::vector<float>& weights,
                   int order, float reg, float* coeffs)
{
    const int n = order + 1;
    if (n > 16 || (int) pts.size() < 2) return false;

    constexpr int kMaxN = 16;
    float A[kMaxN * kMaxN] = {};
    float rhs[kMaxN]       = {};
    float row[kMaxN];

    for (std::size_t i = 0; i < pts.size(); ++i)
    {
        buildBasisRow(pts[i].first, order, row);
        const float w = weights[i];
        const float y = pts[i].second;
        for (int r = 0; r < n; ++r)
        {
            rhs[r] += w * row[r] * y;
            for (int c = 0; c < n; ++c)
                A[r * n + c] += w * row[r] * row[c];
        }
    }
    // Second-derivative Tikhonov penalty: diag [0, 1^4, 2^4, 3^4, ...]
    for (int k = 1; k < n; ++k)
        A[k * n + k] += reg * (float) (k * k * k * k);

    return solveGaussian(n, A, rhs, coeffs);
}

// Evaluate fitted RDC envelope at barkPos.
static float evalRDC(float barkPos, const float* coeffs, int order)
{
    constexpr int kMaxN = 16;
    float row[kMaxN];
    buildBasisRow(barkPos, order, row);
    float val = 0.0f;
    for (int k = 0; k <= order; ++k)
        val += coeffs[k] * row[k];
    return val;
}
} // namespace rdcDisplay
} // anonymous namespace

// ---------------------------------------------------------------------------
void SpectrogramPanel::paintIsometricEnvelopeRDC(juce::Graphics& g, juce::Rectangle<float> plot)
{
    if (envSourceBBtn.getToggleState() && !secondarySnapshot.partials.empty())
        paintEnvelopeSurfaceRDC(g, plot, secondarySnapshot, juce::Colour(0xffd08030), 0.85f);
    if (envSourceABtn.getToggleState() && !traceSnapshot.partials.empty())
        paintEnvelopeSurfaceRDC(g, plot, traceSnapshot, juce::Colour(0xff4090d0), 1.0f);

    paintEnvelopeRangeSelection(g, plot);
}

// ---------------------------------------------------------------------------
// RDC envelope waterfall.  For each time slice, fits a Bark-scale regularised
// cepstrum to the partial amplitudes (matching the Python rdc_fit_and_eval
// implementation), then evaluates on a dense log-Hz grid for display.
void SpectrogramPanel::paintEnvelopeSurfaceRDC(juce::Graphics& g,
                                               juce::Rectangle<float> plot,
                                               const AtsSpectralTraceSnapshot& snap,
                                               juce::Colour colour,
                                               float alphaScale) const
{
    if (snap.partials.empty())
        return;

    const auto timeRange = visibleTimeRange();
    const auto freqRange = visibleFreqRange();
    constexpr int   kSlices    = 40;
    constexpr int   kGridSize  = 128;
    constexpr int   kOrder     = 10;       // cosine series order
    constexpr float kReg       = 8.0e-3f;  // second-derivative Tikhonov lambda
    constexpr float kMinDb     = -120.0f;
    constexpr float kDbRange   = 120.0f;
    constexpr int   kTaperBins = 14;       // wider taper → gentler edges, no box silhouette


    // Model frequency range — used for log↔Hz and Bark normalisation.
    const float minHz    = (float) juce::jmax(1.0, snap.minFrequencyHz);
    const float maxHz    = (float) juce::jmax((double) minHz * 1.1, snap.maxFrequencyHz);
    const float logRange = std::log(maxHz / minHz);

    // freqNorm (log-scaled 0-1) → Hz.
    auto freqNormToHz = [&](float fn) -> float {
        return minHz * std::exp(fn * logRange);
    };

    // Pre-build all slice paths for painter's algorithm (back-to-front two-pass draw).
    struct SliceData { juce::Path path; float depthAlpha; };
    std::vector<SliceData> slices;
    slices.reserve(static_cast<std::size_t>(kSlices));

    for (int si = 0; si < kSlices; ++si)
    {
        const float timeNorm = timeRange.getStart()
                               + timeRange.getLength() * (float) si / (float) (kSlices - 1);

        // Collect (barkPos, dB, weight) from all active partials at this time.
        std::vector<std::pair<float, float>> fitPts;
        std::vector<float> fitWeights;
        fitPts.reserve(snap.partials.size());
        fitWeights.reserve(snap.partials.size());

        for (const auto& trace : snap.partials)
        {
            float ampAtTime  = 0.0f;
            float freqAtTime = -1.0f;

            for (std::size_t pi = 1; pi < trace.points.size(); ++pi)
            {
                const auto& prev = trace.points[pi - 1];
                const auto& cur  = trace.points[pi];
                if (cur.timeNorm < timeNorm)  continue;
                if (prev.timeNorm > timeNorm) break;
                if (!prev.active || !cur.active) continue;
                const float span = cur.timeNorm - prev.timeNorm;
                const float t = span > 1.0e-6f ? (timeNorm - prev.timeNorm) / span : 0.0f;
                ampAtTime  = prev.ampNorm  + t * (cur.ampNorm  - prev.ampNorm);
                freqAtTime = prev.freqNorm + t * (cur.freqNorm - prev.freqNorm);
                break;
            }

            if (ampAtTime <= 0.0f || freqAtTime < 0.0f)
                continue;

            const float hz      = freqNormToHz(freqAtTime);
            const float barkPos = rdcDisplay::barkNorm(hz, minHz, maxHz);
            const float dB      = kMinDb + ampAtTime * kDbRange;
            fitPts.push_back({ barkPos, dB });
            fitWeights.push_back(juce::jmax(1.0e-4f, ampAtTime));
        }

        if ((int) fitPts.size() < 3)
            continue;

        // Fit RDC — reduce order for sparse frames.
        const int effOrder = std::min(kOrder, std::max(1, (int) fitPts.size() - 1));
        float coeffs[16] = {};
        if (!rdcDisplay::fitRDC(fitPts, fitWeights, effOrder, kReg, coeffs))
            continue;

        // Evaluate RDC on a dense grid spanning the visible frequency range.
        // Draw the full span — the cosine taper decays edges to 0, avoiding box silhouettes.
        juce::Path topPath;
        for (int b = 0; b < kGridSize; ++b)
        {
            const float freqN   = freqRange.getStart()
                                  + freqRange.getLength() * (float) b / (float) (kGridSize - 1);
            const float hz      = freqNormToHz(freqN);
            const float barkPos = rdcDisplay::barkNorm(hz, minHz, maxHz);
            const float rawDb   = rdcDisplay::evalRDC(barkPos, coeffs, effOrder);
            float ampN = juce::jlimit(0.0f, 1.0f, (rawDb - kMinDb) / kDbRange);

            // Wider cosine edge taper → smooth fade at frequency boundaries.
            const int edgeDist = std::min(b, kGridSize - 1 - b);
            if (edgeDist < kTaperBins)
            {
                const float t = (float) edgeDist / (float) kTaperBins;
                ampN *= 0.5f - 0.5f * std::cos(juce::MathConstants<float>::pi * t);
            }

            const auto pt = positionForIsoCoords(timeNorm, freqN, ampN, plot);
            if (b == 0)
                topPath.startNewSubPath(pt);
            else
                topPath.lineTo(pt);
        }

        if (topPath.isEmpty())
            continue;

        const float depthAlpha = 0.35f + 0.65f * (float) si / (float) (kSlices - 1);
        slices.push_back({ std::move(topPath), depthAlpha });
    }

    // Painter's algorithm: two passes back-to-front.
    // Pass 1 — thick near-black clearance stroke so front slices occlude back slices.
    const juce::Colour clearCol = juce::Colour(0xff050a0e);
    for (const auto& s : slices)
        g.setColour(clearCol.withAlpha(s.depthAlpha * 0.92f)),
        g.strokePath(s.path, juce::PathStrokeType(4.5f));

    // Pass 2 — glow + crisp coloured line.
    for (const auto& s : slices)
    {
        const juce::Colour lineCol = colour.withAlpha(s.depthAlpha * alphaScale);
        g.setColour(lineCol.withAlpha(lineCol.getFloatAlpha() * 0.25f));
        g.strokePath(s.path, juce::PathStrokeType(3.5f));
        g.setColour(lineCol);
        g.strokePath(s.path, juce::PathStrokeType(1.5f));
    }
}

// ---------------------------------------------------------------------------
void SpectrogramPanel::paintSelectionOverlay(juce::Graphics& g,
                                             juce::Rectangle<float> screen,
                                             juce::Rectangle<float> plot)
{
    g.setFont(juce::Font(11.0f, juce::Font::plain));
    g.setColour(juce::Colour(0xff70ffd4));
    const auto leftFooter = isResidualDisplayMode(mode)
                                ? (juce::String((int) traceSnapshot.residualBands.size()) + " residual bands")
                                : (juce::String(traceSnapshot.visiblePartialCount) + "/"
                                   + juce::String(traceSnapshot.partialCount) + " partials");
    g.drawText(leftFooter,
               juce::Rectangle<float>(screen.getX() + 14.0f,
                                      screen.getBottom() - 18.0f,
                                      160.0f,
                                      14.0f)
                   .toNearestInt(),
               juce::Justification::centredLeft);
    g.drawText(juce::String(traceSnapshot.minFrequencyHz, 0) + " - "
                   + juce::String(traceSnapshot.maxFrequencyHz, 0) + " Hz",
               juce::Rectangle<float>(screen.getRight() - 170.0f,
                                      screen.getBottom() - 18.0f,
                                      156.0f,
                                      14.0f)
                   .toNearestInt(),
               juce::Justification::centredRight);

    ats::FrequencyShapeSettings displayShape;
    displayShape.mode = traceSnapshot.frequencyShapeMode;
    displayShape.amount = traceSnapshot.frequencyShapeAmount;
    displayShape.pivotHz = traceSnapshot.frequencyShapePivotHz;
    displayShape.resolvedPivotHz = traceSnapshot.resolvedFrequencyShapePivotHz;
    const bool showShapeOverlay = !isResidualDisplayMode(mode)
                                  && ats::isFrequencyShapeModeEnabled(displayShape);
    if (showShapeOverlay && traceSnapshot.resolvedFrequencyShapePivotHz > 0.0
        && traceSnapshot.maxFrequencyHz > traceSnapshot.minFrequencyHz)
    {
        const double minHz = juce::jmax(1.0, traceSnapshot.minFrequencyHz);
        const double maxHz = juce::jmax(minHz * 1.2, traceSnapshot.maxFrequencyHz);
        const double logRange = std::log(maxHz / minHz);
        const double clampedPivot = juce::jlimit(minHz, maxHz, traceSnapshot.resolvedFrequencyShapePivotHz);
        const float pivotNorm = (float) juce::jlimit(0.0,
                                                     1.0,
                                                     std::log(clampedPivot / minHz) / logRange);
        const auto accent = juce::Colour(0xffbaffea);
        g.setColour(accent.withAlpha(0.72f));

        auto drawDashedLine = [&](juce::Line<float> line) {
            constexpr int dashCount = 36;
            for (int i = 0; i < dashCount; i += 2)
            {
                const float t0 = (float) i / (float) dashCount;
                const float t1 = (float) (i + 1) / (float) dashCount;
                g.drawLine({ line.getPointAlongLineProportionally(t0),
                             line.getPointAlongLineProportionally(t1) },
                           1.1f);
            }
        };

        if (isIsometricDisplayMode(mode))
        {
            const auto t = visibleTimeRange();
            drawDashedLine({ positionForIsoCoords(t.getStart(), pivotNorm, 0.0f, plot),
                             positionForIsoCoords(t.getEnd(), pivotNorm, 0.0f, plot) });
        }
        else
        {
            const auto f = visibleFreqRange();
            const float yNorm = (pivotNorm - f.getStart()) / juce::jmax(1.0e-6f, f.getLength());
            if (yNorm >= -0.05f && yNorm <= 1.05f)
            {
                const float y = plot.getBottom() - plot.getHeight() * yNorm;
                drawDashedLine({ { plot.getX(), y }, { plot.getRight(), y } });
            }
        }

        juce::String pivotText = traceSnapshot.frequencyShapePivotHz <= 0.0
                                     ? ("Auto -> " + juce::String(traceSnapshot.resolvedFrequencyShapePivotHz, 0) + " Hz")
                                     : (juce::String(traceSnapshot.resolvedFrequencyShapePivotHz, 0) + " Hz");
        juce::String badge = juce::String(ats::frequencyShapeModeName(traceSnapshot.frequencyShapeMode))
                             + " " + juce::String(traceSnapshot.frequencyShapeAmount, 2)
                             + " @ " + pivotText;
        auto badgeBounds = juce::Rectangle<float>(plot.getX() + 7.0f,
                                                  plot.getY() + 6.0f,
                                                  juce::jmin(280.0f, plot.getWidth() - 14.0f),
                                                  18.0f);
        g.setColour(juce::Colour(0xcc07110f));
        g.fillRoundedRectangle(badgeBounds, 4.0f);
        g.setColour(accent.withAlpha(0.9f));
        g.drawRoundedRectangle(badgeBounds.reduced(0.5f), 4.0f, 0.8f);
        g.setFont(juce::Font(11.0f, juce::Font::bold));
        g.drawText(badge,
                   badgeBounds.reduced(6.0f, 1.0f).toNearestInt(),
                   juce::Justification::centredLeft,
                   true);
        g.setFont(juce::Font(11.0f, juce::Font::plain));
    }

    const float hoverReadoutY = plot.getY() + (showShapeOverlay ? 29.0f : 7.0f);

    if (isResidualDisplayMode(mode) && hoveredResidualBand >= 0)
    {
        const auto band = std::find_if(traceSnapshot.residualBands.begin(),
                                       traceSnapshot.residualBands.end(),
                                       [this](const auto& b) { return b.bandIndex == hoveredResidualBand; });
        if (band != traceSnapshot.residualBands.end())
        {
            juce::String hoverText = "B" + juce::String(hoveredResidualBand) + "  "
                                     + juce::String(band->lowHz, 0) + "-"
                                     + juce::String(band->highHz, 0) + " Hz";
            if (hoveredResidualFrame >= 0)
            {
                double db = -120.0;
                if (processor.readResidualBandEnergyDb(traceSource, hoveredResidualBand, hoveredResidualFrame, db))
                    hoverText << " | f" << hoveredResidualFrame << " " << juce::String(db, 1) << " dB";
            }

            g.setColour(juce::Colour(0xffbaffea));
            g.drawText(hoverText,
                       juce::Rectangle<float>(plot.getX() + 8.0f,
                                              hoverReadoutY,
                                              260.0f,
                                              14.0f)
                           .toNearestInt(),
                       juce::Justification::centredLeft);
        }
    }
    else if (hoveredPartial >= 0)
    {
        const auto trace = std::find_if(traceSnapshot.partials.begin(),
                                        traceSnapshot.partials.end(),
                                        [this](const auto& t) { return t.partialIndex == hoveredPartial; });
        if (trace != traceSnapshot.partials.end())
        {
            g.setColour(juce::Colour(0xffbaffea));
            juce::String hoverText = "P" + juce::String(hoveredPartial) + "  ";
            if (showShapeOverlay && std::abs(trace->meanFrequencyHz - trace->sourceMeanFrequencyHz) > 0.05)
            {
                hoverText << juce::String(trace->sourceMeanFrequencyHz, 1)
                          << " -> " << juce::String(trace->meanFrequencyHz, 1)
                          << " Hz (" << ats::frequencyShapeModeName(traceSnapshot.frequencyShapeMode)
                          << " " << juce::String(traceSnapshot.frequencyShapeAmount, 2) << ")";
            }
            else
            {
                hoverText << juce::String(trace->meanFrequencyHz, 1) << " Hz";
            }
            g.drawText(hoverText,
                       juce::Rectangle<float>(plot.getX() + 8.0f,
                                              hoverReadoutY,
                                              330.0f,
                                              14.0f)
                           .toNearestInt(),
                       juce::Justification::centredLeft);
        }
    }

    if (isResidualDisplayMode(mode) && !isIsometricDisplayMode(mode) && hasResidualSelection())
    {
        const auto selection = residualSelectionRect(plot);
        g.setColour(juce::Colour(0x283ffff0));
        g.fillRect(selection);
        g.setColour(juce::Colour(0xfffff3a0));
        g.drawRect(selection, 1.4f);
    }

    if (draggingLasso && lassoRect.getWidth() > 2.0f && lassoRect.getHeight() > 2.0f)
    {
        g.setColour(juce::Colour(0x223ffff0));
        g.fillRect(lassoRect);
        g.setColour(juce::Colour(0xffbaffea));
        g.drawRect(lassoRect, 1.0f);
    }

    if (drawingResidualEnvelope && envelopeDrawBand >= 0 && envelopeDrawPoints.size() >= 2)
    {
        const auto band = std::find_if(traceSnapshot.residualBands.begin(),
                                       traceSnapshot.residualBands.end(),
                                       [this](const auto& b) { return b.bandIndex == envelopeDrawBand; });
        if (band != traceSnapshot.residualBands.end())
        {
            const auto yRange = yRangeForBand(*band, plot);
            juce::Path stroke;
            bool started = false;
            for (const auto& point : envelopeDrawPoints)
            {
                const float timeNorm = traceSnapshot.frameCount <= 1
                                           ? 0.0f
                                           : (float) point.first / (float) (traceSnapshot.frameCount - 1);
                const float x = plot.getX()
                                + plot.getWidth()
                                      * ((timeNorm - visibleTimeRange().getStart()) / visibleTimeRange().getLength());
                const float dbNorm = (float) juce::jlimit(-72.0, 0.0, point.second) / -72.0f;
                const float y = yRange.getEnd() - yRange.getLength() * dbNorm;
                if (!started)
                {
                    stroke.startNewSubPath(x, y);
                    started = true;
                }
                else
                {
                    stroke.lineTo(x, y);
                }
            }
            g.setColour(juce::Colour(0xfffff3a0).withAlpha(0.85f));
            g.strokePath(stroke, juce::PathStrokeType(2.0f));
        }
    }

    if (drawingPartialFreq && partialFreqDrawPoints.size() >= 2 && traceSnapshot.frameCount > 1)
    {
        const double minHz = juce::jmax(1.0, traceSnapshot.minFrequencyHz);
        const double maxHz = juce::jmax(minHz * 1.2, traceSnapshot.maxFrequencyHz);
        const double logRange = std::log(maxHz / minHz);
        juce::Path stroke;
        bool started = false;
        for (const auto& pt : partialFreqDrawPoints)
        {
            const float timeNorm = (float) pt.first / (float) (traceSnapshot.frameCount - 1);
            const float freqNorm = (float) juce::jlimit(0.0, 1.0,
                                                        std::log(juce::jmax(1.0, pt.second) / minHz) / logRange);
            const auto pos = positionForPoint(AtsPartialTracePoint { timeNorm, freqNorm }, plot);
            if (!started)
            {
                stroke.startNewSubPath(pos.x, pos.y);
                started = true;
            }
            else
            {
                stroke.lineTo(pos.x, pos.y);
            }
        }
        g.setColour(juce::Colour(0xffaaff99).withAlpha(0.9f));
        g.strokePath(stroke, juce::PathStrokeType(2.0f));
    }
}

void SpectrogramPanel::paintPlaybackPlayhead(juce::Graphics& g, juce::Rectangle<float> plot)
{
    const float playheadNorm = animatedPlayheadNorm;
    if (playheadNorm < 0.0f)
        return;

    if (isIsometricDisplayMode(mode))
        paintIsometricPlaybackPlayhead(g, plot, playheadNorm);
    else
        paintPlanarPlaybackPlayhead(g, plot, playheadNorm);
}

void SpectrogramPanel::paintPlanarPlaybackPlayhead(juce::Graphics& g,
                                                   juce::Rectangle<float> plot,
                                                   float playheadNorm)
{
    const auto timeRange = visibleTimeRange();
    const float timeSpan = juce::jmax(1.0e-6f, timeRange.getLength());
    const float xNorm = (playheadNorm - timeRange.getStart()) / timeSpan;
    if (xNorm < -0.01f || xNorm > 1.01f)
        return;

    const float x = plot.getX() + plot.getWidth() * xNorm;
    const float y0 = plot.getY();
    const float y1 = plot.getBottom();

    const auto line = juce::Line<float>({ x, y0 }, { x, y1 });
    g.setColour(juce::Colour(0x28cffff6));
    g.drawLine(line, 6.0f);
    g.setColour(juce::Colour(0x74cffff6));
    g.drawLine(line, 2.6f);
    g.setColour(juce::Colour(0xfff3ffb2));
    g.drawLine(line, 1.15f);

    const float headW = 7.0f;
    juce::Path head;
    head.startNewSubPath(x - headW * 0.5f, y0 + 1.0f);
    head.lineTo(x + headW * 0.5f, y0 + 1.0f);
    head.lineTo(x, y0 + 8.5f);
    head.closeSubPath();
    g.setColour(juce::Colour(0xfff3ffb2).withAlpha(0.88f));
    g.fillPath(head);
}

void SpectrogramPanel::paintIsometricPlaybackPlayhead(juce::Graphics& g,
                                                      juce::Rectangle<float> plot,
                                                      float playheadNorm)
{
    const auto timeRange = visibleTimeRange();
    const auto freqRange = visibleFreqRange();
    if (playheadNorm < timeRange.getStart() - timeRange.getLength() * 0.025f
        || playheadNorm > timeRange.getEnd() + timeRange.getLength() * 0.025f)
    {
        return;
    }

    constexpr float topAmp = 1.12f;
    const float frontFreq = freqRange.getStart();
    const float backFreq = freqRange.getEnd();
    const float halfDepth = timeRange.getLength() * 0.04f;
    const float t0 = juce::jlimit(0.0f, 1.0f, playheadNorm - halfDepth);
    const float t1 = juce::jlimit(0.0f, 1.0f, playheadNorm + halfDepth);

    const auto frontCentreBase = positionForIsoCoords(playheadNorm, frontFreq, 0.0f, plot);
    const auto frontCentreTop = positionForIsoCoords(playheadNorm, frontFreq, topAmp, plot);
    const auto backCentreBase = positionForIsoCoords(playheadNorm, backFreq, 0.0f, plot);
    const auto backCentreTop = positionForIsoCoords(playheadNorm, backFreq, topAmp, plot);
    const auto frontCentreMid = positionForIsoCoords(playheadNorm, frontFreq, topAmp * 0.52f, plot);
    const auto backCentreMid = positionForIsoCoords(playheadNorm, backFreq, topAmp * 0.52f, plot);

    juce::Path scanPlane;
    scanPlane.startNewSubPath(frontCentreBase);
    scanPlane.lineTo(backCentreBase);
    scanPlane.lineTo(backCentreTop);
    scanPlane.lineTo(frontCentreTop);
    scanPlane.closeSubPath();

    g.setColour(juce::Colour(0x12f3ffb2));
    g.fillPath(scanPlane);

    auto drawGlow = [&g](juce::Point<float> a, juce::Point<float> b, float coreAlpha) {
        const auto line = juce::Line<float>(a, b);
        g.setColour(juce::Colour(0x36cffff6));
        g.drawLine(line, 7.0f);
        g.setColour(juce::Colour(0x88cffff6));
        g.drawLine(line, 3.0f);
        g.setColour(juce::Colour(0xfff3ffb2).withAlpha(coreAlpha));
        g.drawLine(line, 1.25f);
    };

    auto drawDepthMarker = [&](float freqNorm, float alphaScale) {
        const auto baseStart = positionForIsoCoords(t0, freqNorm, 0.0f, plot);
        const auto baseEnd = positionForIsoCoords(t1, freqNorm, 0.0f, plot);
        const auto topStart = positionForIsoCoords(t0, freqNorm, topAmp, plot);
        const auto topEnd = positionForIsoCoords(t1, freqNorm, topAmp, plot);
        const auto centreBase = positionForIsoCoords(playheadNorm, freqNorm, 0.0f, plot);
        const auto centreTop = positionForIsoCoords(playheadNorm, freqNorm, topAmp, plot);

        juce::Path depthRibbon;
        depthRibbon.startNewSubPath(baseStart);
        depthRibbon.lineTo(baseEnd);
        depthRibbon.lineTo(topEnd);
        depthRibbon.lineTo(topStart);
        depthRibbon.closeSubPath();

        g.setColour(juce::Colour(0x18f3ffb2).withMultipliedAlpha(alphaScale));
        g.fillPath(depthRibbon);

        drawGlow(baseStart, baseEnd, 0.95f * alphaScale);
        drawGlow(topStart, topEnd, 0.68f * alphaScale);
        drawGlow(centreBase, centreTop, 0.94f * alphaScale);

        juce::Path head;
        const auto headBack = positionForIsoCoords(juce::jlimit(0.0f, 1.0f, playheadNorm - halfDepth * 0.42f),
                                                   freqNorm,
                                                   topAmp,
                                                   plot);
        const auto headFront = positionForIsoCoords(juce::jlimit(0.0f, 1.0f, playheadNorm + halfDepth * 0.42f),
                                                    freqNorm,
                                                    topAmp,
                                                    plot);
        const auto headTip = positionForIsoCoords(playheadNorm, freqNorm, topAmp + 0.16f, plot);
        head.startNewSubPath(headBack);
        head.lineTo(headFront);
        head.lineTo(headTip);
        head.closeSubPath();
        g.setColour(juce::Colour(0xfff3ffb2).withAlpha(0.78f * alphaScale));
        g.fillPath(head);
    };

    drawDepthMarker(backFreq, 0.82f);
    drawDepthMarker(frontFreq, 1.0f);

    drawGlow(frontCentreBase, backCentreBase, 0.82f);
    drawGlow(frontCentreTop, backCentreTop, 0.56f);

    const auto throughLine = juce::Line<float>(frontCentreMid, backCentreMid);
    g.setColour(juce::Colour(0x99f3ffb2));
    g.drawLine(throughLine, 6.0f);
    g.setColour(juce::Colour(0xfffff3b2));
    g.drawLine(throughLine, 2.2f);
}

void SpectrogramPanel::paintSmearnovOverlay(juce::Graphics& g, juce::Rectangle<float> plot) const
{
    if (traceSnapshot.frameCount <= 1 || isIsometricDisplayMode(mode))
        return;

    if (traceSnapshot.onsets.empty() && traceSnapshot.smearnovDetectionCurve.empty())
        return;

    g.saveState();
    g.reduceClipRegion(plot.toNearestInt());

    // When onset editing is off the markers are click-through; dim them so they read as a
    // passive reference and let the partial/residual data underneath stay legible.
    const bool onsetEditActive = onsetEditButton.getToggleState();
    if (!onsetEditActive)
        g.beginTransparencyLayer(0.4f);

    const auto spanColour = isResidualDisplayMode(mode) ? juce::Colour(0xff74fff0)
                                                        : juce::Colour(0xfffff3a0);
    const auto markerColour = isResidualDisplayMode(mode) ? juce::Colour(0xffbaffea)
                                                          : juce::Colour(0xfffff3a0);

    double maxStrength = 0.0;
    for (const auto& onset : traceSnapshot.onsets)
    {
        if (std::isfinite(onset.strength) && onset.strength > 0.0)
            maxStrength = std::max(maxStrength, onset.strength);
    }

    auto strengthNormFor = [maxStrength](const ats::AtsOnset& onset) {
        if (maxStrength <= 0.0 || !std::isfinite(onset.strength))
            return 0.65f;
        return juce::jlimit(0.15f, 1.0f, (float) (onset.strength / maxStrength));
    };

    const int activeOnset = draggingOnsetIndex >= 0 ? draggingOnsetIndex : hoveredOnsetIndex;
    const OnsetHandle activeHandle = draggingOnsetIndex >= 0 ? draggingOnsetHandle : hoveredOnsetHandle;

    for (int onsetIdx = 0; onsetIdx < (int) traceSnapshot.onsets.size(); ++onsetIdx)
    {
        const auto& onset = traceSnapshot.onsets[(size_t) onsetIdx];
        const int firstFrame = juce::jlimit(0,
                                            traceSnapshot.frameCount - 1,
                                            std::min(onset.preFrame, onset.peakFrame));
        const int lastFrame = juce::jlimit(0,
                                           traceSnapshot.frameCount - 1,
                                           std::max(onset.preFrame, onset.peakFrame));
        const float x0 = xForFrame(firstFrame, plot);
        const float x1 = xForFrame(lastFrame, plot);
        const float left = std::min(x0, x1);
        const float right = std::max(x0, x1) + 1.0f;
        if (right < plot.getX() || left > plot.getRight())
            continue;

        const float strength = strengthNormFor(onset);
        const bool onsetActive = onsetIdx == activeOnset;
        const auto span = juce::Rectangle<float>::leftTopRightBottom(juce::jmax(plot.getX(), left),
                                                                     plot.getY(),
                                                                     juce::jmin(plot.getRight(), right),
                                                                     plot.getBottom());
        g.setColour(spanColour.withAlpha((onsetActive ? 0.090f : 0.035f) + 0.070f * strength));
        g.fillRect(span);

        const bool leftActive = onsetActive && activeHandle == OnsetHandle::LeftEdge;
        const bool rightActive = onsetActive && activeHandle == OnsetHandle::RightEdge;
        g.setColour(spanColour.withAlpha((leftActive ? 0.85f : 0.13f) + 0.10f * strength));
        g.drawVerticalLine(juce::roundToInt(left), plot.getY(), plot.getBottom());
        g.setColour(spanColour.withAlpha((rightActive ? 0.85f : 0.13f) + 0.10f * strength));
        g.drawVerticalLine(juce::roundToInt(right), plot.getY(), plot.getBottom());
    }

    paintSmearnovDetectionCurves(g, plot);

    for (int onsetIdx = 0; onsetIdx < (int) traceSnapshot.onsets.size(); ++onsetIdx)
    {
        const auto& onset = traceSnapshot.onsets[(size_t) onsetIdx];
        const int frame = juce::jlimit(0, traceSnapshot.frameCount - 1, onset.frame);
        const float x = xForFrame(frame, plot);
        if (x < plot.getX() - 2.0f || x > plot.getRight() + 2.0f)
            continue;

        const float strength = strengthNormFor(onset);
        const bool markerActive = onsetIdx == activeOnset && activeHandle == OnsetHandle::Marker;
        const float glow = markerActive ? 0.30f : 0.0f;
        const auto line = juce::Line<float>({ x, plot.getY() }, { x, plot.getBottom() });
        g.setColour(markerColour.withAlpha(0.05f + 0.06f * strength + glow));
        g.drawLine(line, markerActive ? 4.5f : 3.0f);
        g.setColour(markerColour.withAlpha(0.14f + 0.12f * strength + glow));
        g.drawLine(line, markerActive ? 1.6f : 1.2f);
        g.setColour(markerColour.withAlpha(juce::jmin(1.0f, 0.58f + 0.22f * strength + glow)));
        g.drawLine(line, markerActive ? 1.1f : 0.75f);

        constexpr float headW = 5.0f;
        constexpr float headH = 5.5f;
        juce::Path topHead;
        topHead.startNewSubPath(x - headW * 0.5f, plot.getY() + 1.0f);
        topHead.lineTo(x + headW * 0.5f, plot.getY() + 1.0f);
        topHead.lineTo(x, plot.getY() + headH);
        topHead.closeSubPath();

        juce::Path bottomHead;
        bottomHead.startNewSubPath(x - headW * 0.5f, plot.getBottom() - 1.0f);
        bottomHead.lineTo(x + headW * 0.5f, plot.getBottom() - 1.0f);
        bottomHead.lineTo(x, plot.getBottom() - headH);
        bottomHead.closeSubPath();

        g.setColour(markerColour.withAlpha(0.42f + 0.22f * strength));
        g.fillPath(topHead);
        g.fillPath(bottomHead);
    }

    if (!onsetEditActive)
        g.endTransparencyLayer();

    g.restoreState();
}

void SpectrogramPanel::paintSmearnovDetectionCurves(juce::Graphics& g, juce::Rectangle<float> plot) const
{
    const auto& combined = traceSnapshot.smearnovDetectionCurve;
    const auto& threshold = traceSnapshot.smearnovThresholdCurve;
    if (combined.size() < 2 && threshold.size() < 2)
        return;

    const float curveHeight = juce::jlimit(22.0f, 54.0f, plot.getHeight() * 0.18f);
    const auto curveArea = juce::Rectangle<float>(plot.getX() + 2.0f,
                                                  plot.getBottom() - curveHeight - 3.0f,
                                                  juce::jmax(1.0f, plot.getWidth() - 4.0f),
                                                  curveHeight);
    if (curveArea.getWidth() < 2.0f || curveArea.getHeight() < 2.0f)
        return;

    double maxValue = 0.0;
    auto inspect = [&maxValue](const std::vector<double>& values) {
        for (const double value : values)
        {
            if (std::isfinite(value) && value > maxValue)
                maxValue = value;
        }
    };
    inspect(combined);
    inspect(threshold);
    if (maxValue <= 0.0)
        return;

    g.setColour(juce::Colour(0x26000000));
    g.fillRoundedRectangle(curveArea.expanded(2.0f, 1.0f), 3.0f);

    const auto timeRange = visibleTimeRange();
    const float timeSpan = juce::jmax(1.0e-6f, timeRange.getLength());
    auto pointsFor = [&](const std::vector<double>& values) {
        std::vector<juce::Point<float>> points;
        const int count = (int) values.size();
        if (count < 2)
            return points;

        const int step = juce::jmax(1, count / 900);
        points.reserve((size_t) (count / step + 2));
        auto append = [&](int i) {
            const double value = values[(size_t) i];
            if (!std::isfinite(value))
                return;

            const float timeNorm = (float) i / (float) (count - 1);
            const float xNorm = (timeNorm - timeRange.getStart()) / timeSpan;
            if (xNorm < -0.01f)
                return;
            if (xNorm > 1.01f)
                return;

            const float yNorm = juce::jlimit(0.0f, 1.0f, (float) (value / maxValue));
            points.push_back({ plot.getX() + plot.getWidth() * xNorm,
                               curveArea.getBottom() - curveArea.getHeight() * yNorm });
        };

        for (int i = 0; i < count; i += step)
            append(i);
        if ((count - 1) % step != 0)
            append(count - 1);
        return points;
    };

    auto drawPoints = [&g](const std::vector<juce::Point<float>>& points,
                           juce::Colour colour,
                           float thickness,
                           bool dashed) {
        if (points.size() < 2)
            return;

        if (!dashed)
        {
            juce::Path path;
            path.startNewSubPath(points.front());
            for (size_t i = 1; i < points.size(); ++i)
                path.lineTo(points[i]);
            g.setColour(colour);
            g.strokePath(path, juce::PathStrokeType(thickness));
            return;
        }

        g.setColour(colour);
        for (size_t i = 1; i < points.size(); ++i)
        {
            if (((int) i / 3) % 2 == 0)
                g.drawLine({ points[i - 1], points[i] }, thickness);
        }
    };

    drawPoints(pointsFor(threshold), juce::Colour(0xfffff3a0).withAlpha(0.42f), 1.05f, true);
    drawPoints(pointsFor(combined), juce::Colour(0xff78fff1).withAlpha(0.58f), 1.25f, false);
}

juce::Rectangle<int> SpectrogramPanel::playheadPaintBounds(juce::Rectangle<float> plot,
                                                           float playheadNorm) const
{
    if (playheadNorm < 0.0f)
        return {};

    if (!isIsometricDisplayMode(mode))
    {
        const auto timeRange = visibleTimeRange();
        const float timeSpan = juce::jmax(1.0e-6f, timeRange.getLength());
        const float xNorm = (playheadNorm - timeRange.getStart()) / timeSpan;
        if (xNorm < -0.05f || xNorm > 1.05f)
            return {};

        const float x = plot.getX() + plot.getWidth() * xNorm;
        return juce::Rectangle<float>(x - 14.0f,
                                      plot.getY() - 14.0f,
                                      28.0f,
                                      plot.getHeight() + 28.0f)
            .getSmallestIntegerContainer();
    }

    const auto timeRange = visibleTimeRange();
    if (playheadNorm < timeRange.getStart() - timeRange.getLength() * 0.05f
        || playheadNorm > timeRange.getEnd() + timeRange.getLength() * 0.05f)
    {
        return {};
    }

    const auto freqRange = visibleFreqRange();
    constexpr float topAmp = 1.30f;
    const float frontFreq = freqRange.getStart();
    const float backFreq = freqRange.getEnd();
    const float halfDepth = timeRange.getLength() * 0.055f;
    const float t0 = juce::jlimit(0.0f, 1.0f, playheadNorm - halfDepth);
    const float t1 = juce::jlimit(0.0f, 1.0f, playheadNorm + halfDepth);

    std::array<juce::Point<float>, 12> points {
        positionForIsoCoords(t0, frontFreq, 0.0f, plot),
        positionForIsoCoords(t1, frontFreq, 0.0f, plot),
        positionForIsoCoords(t0, frontFreq, topAmp, plot),
        positionForIsoCoords(t1, frontFreq, topAmp, plot),
        positionForIsoCoords(playheadNorm, frontFreq, topAmp + 0.16f, plot),
        positionForIsoCoords(playheadNorm, frontFreq, topAmp * 0.52f, plot),
        positionForIsoCoords(t0, backFreq, 0.0f, plot),
        positionForIsoCoords(t1, backFreq, 0.0f, plot),
        positionForIsoCoords(t0, backFreq, topAmp, plot),
        positionForIsoCoords(t1, backFreq, topAmp, plot),
        positionForIsoCoords(playheadNorm, backFreq, topAmp + 0.16f, plot),
        positionForIsoCoords(playheadNorm, backFreq, topAmp * 0.52f, plot),
    };

    juce::Rectangle<float> bounds(points.front().x, points.front().y, 1.0f, 1.0f);
    for (const auto& point : points)
        bounds = bounds.getUnion(juce::Rectangle<float>(point.x, point.y, 1.0f, 1.0f));

    return bounds.expanded(18.0f).getSmallestIntegerContainer();
}

void SpectrogramPanel::resized()
{
    auto r = getLocalBounds().reduced(5);
    const bool residualUi = isResidualDisplayMode(mode);
    const bool envelopeUi = (mode == Mode::IsometricEnvelope || mode == Mode::IsometricEnvelopeRDC);
    const bool partialUi = (mode == Mode::Partials || mode == Mode::Combined);
    auto header = r.removeFromTop((residualUi || envelopeUi || partialUi) ? 50 : 26);
    auto headerRow1 = header.removeFromTop(26);

    if (zoomBar != nullptr)
    {
        zoomBar->setBounds(headerRow1.removeFromRight((int) kZoomBarWidth));
        headerRow1.removeFromRight(6);
    }
    viewMenuButton.setBounds(headerRow1.removeFromLeft(182));
    headerRow1.removeFromLeft(6);
    deleteButton.setBounds(headerRow1.removeFromLeft(72));
    headerRow1.removeFromLeft(8);
    updateOnsetEditButtonVisibility();
    if (onsetEditButton.isVisible())
    {
        onsetEditButton.setBounds(headerRow1.removeFromLeft(90));
        headerRow1.removeFromLeft(8);
    }
    if (envelopeUi)
    {
        envSourceABtn.setBounds(headerRow1.removeFromLeft(76));
        headerRow1.removeFromLeft(4);
        envSourceBBtn.setBounds(headerRow1.removeFromLeft(76));
        headerRow1.removeFromLeft(4);
    }
    selectionLabel.setBounds(headerRow1);

    if (residualUi)
    {
        auto toolRow = header;
        residualDrawButton.setBounds(toolRow.removeFromLeft(52));
        toolRow.removeFromLeft(4);
        residualSmoothButton.setBounds(toolRow.removeFromLeft(62));
        toolRow.removeFromLeft(4);
        residualCopyButton.setBounds(toolRow.removeFromLeft(52));
        toolRow.removeFromLeft(4);
        residualPasteButton.setBounds(toolRow.removeFromLeft(56));
        toolRow.removeFromLeft(4);
        residualSwapButton.setBounds(toolRow.removeFromLeft(56));
        toolRow.removeFromLeft(4);
        residualRedistButton.setBounds(toolRow.removeFromLeft(56));
        toolRow.removeFromLeft(8);
        residualTiltSlider.setBounds(toolRow.removeFromLeft(juce::jmax(72, toolRow.getWidth() / 4)));
        toolRow.removeFromLeft(4);
        residualTiltButton.setBounds(toolRow.removeFromLeft(44));
        toolRow.removeFromLeft(8);
        residualDbLabel.setBounds(toolRow.removeFromLeft(44));
        residualDbEditor.setBounds(toolRow.removeFromLeft(juce::jmax(72, toolRow.getWidth())));
    }
    else if (envelopeUi)
    {
        auto toolRow = header;
        envSmoothLabel.setBounds(toolRow.removeFromLeft(76));
        toolRow.removeFromLeft(4);
        envSmoothSlider.setBounds(toolRow.removeFromLeft(juce::jmax(120, toolRow.getWidth() / 3)));
        toolRow.removeFromLeft(8);
        envSmoothButton.setBounds(toolRow.removeFromLeft(88));
    }
    else if (partialUi)
    {
        auto toolRow = header;
        partialFreqDrawButton.setBounds(toolRow.removeFromLeft(52));
        toolRow.removeFromLeft(4);
        partialSoloButton.setBounds(toolRow.removeFromLeft(48));
        toolRow.removeFromLeft(4);
        partialFreezeButton.setBounds(toolRow.removeFromLeft(56));
        toolRow.removeFromLeft(4);
        partialPruneButton.setBounds(toolRow.removeFromLeft(52));
        toolRow.removeFromLeft(8);
        partialTiltSlider.setBounds(toolRow.removeFromLeft(juce::jmax(72, toolRow.getWidth() / 3)));
        toolRow.removeFromLeft(4);
        partialTiltButton.setBounds(toolRow.removeFromLeft(44));
    }

    layoutTraceViewChrome();
    if (playheadOverlay != nullptr)
        playheadOverlay->setBounds(getPlotBounds().toNearestInt());
    repaintDisplay();
}

void SpectrogramPanel::styleTraceScrollBars()
{
    for (auto* bar : { &timeScrollBar, &freqScrollBar })
    {
        bar->setColour(juce::ScrollBar::backgroundColourId, juce::Colour(0xff0a0d0b));
        bar->setColour(juce::ScrollBar::trackColourId, juce::Colour(0xff151a17));
        bar->setColour(juce::ScrollBar::thumbColourId, juce::Colour(0xff8a9a92));
    }
}

void SpectrogramPanel::layoutTraceViewChrome()
{
    auto area = getScreenBounds().toNearestInt().reduced(14, 12);
    area.removeFromTop(18);
    area.removeFromBottom(18);

    const auto vBarArea = area.removeFromRight(traceScrollBarThickness);
    const auto hBarArea = area.removeFromBottom(traceScrollBarThickness);

    freqScrollBar.setBounds(vBarArea.reduced(0, 1));
    timeScrollBar.setBounds(hBarArea.reduced(1, 0));
    updateScrollBarsFromViewport();
}

void SpectrogramPanel::updateScrollBarsFromViewport()
{
    if (syncingScrollBars)
        return;

    syncingScrollBars = true;

    const float timeSpan = 1.0f / juce::jmax(1.0f, timeZoom);
    const float freqSpan = 1.0f / juce::jmax(1.0f, freqZoom);
    const auto timeRange = visibleTimeRange();
    const auto freqRange = visibleFreqRange();

    bool canPanTime = timeZoom > 1.001f;
    bool canPanFreq = freqZoom > 1.001f;
    float hStart = timeRange.getStart();
    float hSpan = timeSpan;
    float vStart = 1.0f - freqRange.getEnd();
    float vSpan = freqSpan;

    if (isIsometricDisplayMode(mode))
    {
        // In iso mode, map horizontal scrollbar to frequency (dominant screen X axis),
        // and vertical scrollbar to time (dominant screen Y axis).
        hStart = freqRange.getStart();
        hSpan = freqSpan;
        canPanTime = freqZoom > 1.001f;

        vStart = 1.0f - timeRange.getEnd();
        vSpan = timeSpan;
        canPanFreq = timeZoom > 1.001f;
    }

    timeScrollBar.setEnabled(canPanTime);
    freqScrollBar.setEnabled(canPanFreq);
    timeScrollBar.setAlpha(canPanTime ? 1.0f : 0.35f);
    freqScrollBar.setAlpha(canPanFreq ? 1.0f : 0.35f);

    // Range limits = full normalized source extent; current range size = visible window.
    timeScrollBar.setRangeLimits(0.0, 1.0, juce::dontSendNotification);
    freqScrollBar.setRangeLimits(0.0, 1.0, juce::dontSendNotification);

    if (canPanTime)
    {
        timeScrollBar.setCurrentRange((double) hStart,
                                      (double) hSpan,
                                      juce::dontSendNotification);
    }
    else
    {
        timeScrollBar.setCurrentRange(0.0, 1.0, juce::dontSendNotification);
    }

    if (canPanFreq)
    {
        freqScrollBar.setCurrentRange((double) vStart, (double) vSpan, juce::dontSendNotification);
    }
    else
    {
        freqScrollBar.setCurrentRange(0.0, 1.0, juce::dontSendNotification);
    }

    syncingScrollBars = false;
}

void SpectrogramPanel::scrollBarMoved(juce::ScrollBar* scrollBarThatHasMoved, double newRangeStart)
{
    if (syncingScrollBars || scrollBarThatHasMoved == nullptr)
        return;

    if (scrollBarThatHasMoved == &timeScrollBar)
    {
        if (isIsometricDisplayMode(mode))
        {
            // Iso mode: horizontal scrollbar drives frequency viewport.
            const float span = 1.0f / juce::jmax(1.0f, freqZoom);
            const float visibleStart = juce::jlimit(0.0f, 1.0f - span, (float) newRangeStart);
            freqCenter = visibleStart + span * 0.5f;
        }
        else
        {
            const float span = 1.0f / juce::jmax(1.0f, timeZoom);
            const float visibleStart = juce::jlimit(0.0f, 1.0f - span, (float) newRangeStart);
            timeCenter = visibleStart + span * 0.5f;
        }
    }
    else if (scrollBarThatHasMoved == &freqScrollBar)
    {
        if (isIsometricDisplayMode(mode))
        {
            // Iso mode: vertical scrollbar drives time viewport (inverted top->later).
            const float span = 1.0f / juce::jmax(1.0f, timeZoom);
            const float visibleStart = juce::jlimit(0.0f, 1.0f - span, 1.0f - (float) newRangeStart - span);
            timeCenter = visibleStart + span * 0.5f;
        }
        else
        {
            const float span = 1.0f / juce::jmax(1.0f, freqZoom);
            const float visibleStart = juce::jlimit(0.0f, 1.0f - span, 1.0f - (float) newRangeStart - span);
            freqCenter = visibleStart + span * 0.5f;
        }
    }

    clampViewport();
    repaintDisplay();
}

bool SpectrogramPanel::smearnovOverlayInteractive() const
{
    return onsetEditButton.getToggleState()
        && !isIsometricDisplayMode(mode)
        && traceSnapshot.frameCount > 1
        && !traceSnapshot.onsets.empty();
}

bool SpectrogramPanel::smearnovOverlayPresent() const
{
    return !isIsometricDisplayMode(mode)
        && traceSnapshot.frameCount > 1
        && !traceSnapshot.onsets.empty();
}

void SpectrogramPanel::updateOnsetEditButtonVisibility()
{
    const bool present = smearnovOverlayPresent();
    if (onsetEditButton.isVisible() != present)
        onsetEditButton.setVisible(present);
    if (!present && onsetEditButton.getToggleState())
        onsetEditButton.setToggleState(false, juce::dontSendNotification);
}

int SpectrogramPanel::onsetHandleAtPoint(juce::Point<float> pos,
                                         juce::Rectangle<float> plot,
                                         OnsetHandle& handleOut) const
{
    handleOut = OnsetHandle::None;
    if (!smearnovOverlayInteractive() || !plot.expanded(2.0f).contains(pos))
        return -1;

    constexpr float edgeTol = 4.0f;
    constexpr float markerTol = 5.0f;

    int bestEdgeIndex = -1;
    OnsetHandle bestEdgeHandle = OnsetHandle::None;
    float bestEdgeDist = edgeTol;
    int bestMarkerIndex = -1;
    float bestMarkerDist = markerTol;

    const int frameCount = traceSnapshot.frameCount;
    for (int i = 0; i < (int) traceSnapshot.onsets.size(); ++i)
    {
        const auto& onset = traceSnapshot.onsets[(size_t) i];
        const int firstFrame = juce::jlimit(0, frameCount - 1, std::min(onset.preFrame, onset.peakFrame));
        const int lastFrame = juce::jlimit(0, frameCount - 1, std::max(onset.preFrame, onset.peakFrame));
        const float leftX = xForFrame(firstFrame, plot);
        const float rightX = xForFrame(lastFrame, plot) + 1.0f;
        const float markerX = xForFrame(juce::jlimit(0, frameCount - 1, onset.frame), plot);

        const float dl = std::abs(pos.x - leftX);
        if (dl < bestEdgeDist)
        {
            bestEdgeDist = dl;
            bestEdgeIndex = i;
            bestEdgeHandle = OnsetHandle::LeftEdge;
        }
        const float dr = std::abs(pos.x - rightX);
        if (dr < bestEdgeDist)
        {
            bestEdgeDist = dr;
            bestEdgeIndex = i;
            bestEdgeHandle = OnsetHandle::RightEdge;
        }
        const float dm = std::abs(pos.x - markerX);
        if (dm < bestMarkerDist)
        {
            bestMarkerDist = dm;
            bestMarkerIndex = i;
        }
    }

    // Edges win over the marker so a window can be narrowed even when the
    // marker sits close by.
    if (bestEdgeIndex >= 0)
    {
        handleOut = bestEdgeHandle;
        return bestEdgeIndex;
    }
    if (bestMarkerIndex >= 0)
    {
        handleOut = OnsetHandle::Marker;
        return bestMarkerIndex;
    }
    return -1;
}

void SpectrogramPanel::updateDraggedOnset(juce::Point<float> pos)
{
    if (draggingOnsetIndex < 0 || draggingOnsetIndex >= (int) traceSnapshot.onsets.size())
        return;

    const auto plot = getPlotBounds();
    const int frameCount = traceSnapshot.frameCount;
    const int frame = juce::jlimit(0, frameCount - 1, frameForX(pos.x, plot));
    auto& onset = traceSnapshot.onsets[(size_t) draggingOnsetIndex];

    switch (draggingOnsetHandle)
    {
        case OnsetHandle::Marker:
        {
            // Translate the whole attack window with the onset marker.
            const int delta = frame - onset.frame;
            const int lo = std::min(onset.preFrame, std::min(onset.peakFrame, onset.frame));
            const int hi = std::max(onset.preFrame, std::max(onset.peakFrame, onset.frame));
            const int clampedDelta = juce::jlimit(-lo, frameCount - 1 - hi, delta);
            onset.frame = juce::jlimit(0, frameCount - 1, onset.frame + clampedDelta);
            onset.preFrame = juce::jlimit(0, frameCount - 1, onset.preFrame + clampedDelta);
            onset.peakFrame = juce::jlimit(0, frameCount - 1, onset.peakFrame + clampedDelta);
            break;
        }
        case OnsetHandle::LeftEdge:
            onset.preFrame = juce::jlimit(0, onset.peakFrame, frame);
            break;
        case OnsetHandle::RightEdge:
            onset.peakFrame = juce::jlimit(onset.preFrame, frameCount - 1, frame);
            break;
        case OnsetHandle::None:
        default:
            return;
    }

    repaintDisplay();
}

void SpectrogramPanel::commitEditedOnsets()
{
    processor.setSmearnovOnsetOverrides(traceSource, traceSnapshot.onsets);
    lastSmearnovOverlayRevision = processor.getSmearnovOverlayRevision();
    repaintDisplay();
}

void SpectrogramPanel::mouseDown(const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    dragStart = e.position;
    draggingLasso = false;
    lassoRect = {};

    const auto plot = getPlotBounds();
    const bool panGesture = e.mods.isMiddleButtonDown() || e.mods.isAltDown();
    if (panGesture && plot.contains(e.position))
    {
        panningView = true;
        panDragStart = e.position;
        panTimeCenterStart = timeCenter;
        panFreqCenterStart = freqCenter;
        return;
    }

    // Click-to-seek: when preview is playing, Ctrl+click anywhere in the plot seeks playback.
    if (e.mods.isCommandDown() && !e.mods.isAltDown() && plot.contains(e.position)
        && processor.isPreviewPlaying() && traceSnapshot.frameCount > 0)
    {
        const float timeNorm = timeNormFromScreenX(e.position.x, plot);
        processor.seekPreviewPlaybackNorm(juce::jlimit(0.0f, 1.0f, timeNorm));
        return;
    }

    // Smearnov onset/attack-window editing takes precedence over trace/residual
    // selection when a handle is grabbed.
    if (!e.mods.isAltDown() && !e.mods.isMiddleButtonDown())
    {
        OnsetHandle handle = OnsetHandle::None;
        const int idx = onsetHandleAtPoint(e.position, plot, handle);
        if (idx >= 0)
        {
            draggingOnsetIndex = idx;
            draggingOnsetHandle = handle;
            hoveredOnsetIndex = idx;
            hoveredOnsetHandle = handle;
            return;
        }
    }

    // Isometric envelope views: drag a time x frequency box, then vertical-drag inside it to
    // raise/lower amplitude (z).  Shift+drag also constrains the frequency (y) extent.
    if (isEnvelopeDisplayMode())
    {
        selectedPartials.clear();
        const bool haveBox = (envRangeStartNorm >= 0.0f && envRangeEndNorm >= 0.0f);
        if (haveBox && !e.mods.isShiftDown() && pointInEnvBox(e.position, plot))
        {
            scalingEnvGain = true;
            lastScaleDragY = e.position.y;
            return;
        }

        selectingEnvRange = true;
        float t = 0.0f, f = 0.0f;
        isoTimeFreqFromPoint(e.position, plot, t, f);
        envRangeStartNorm = t;
        envRangeEndNorm = t;
        if (e.mods.isShiftDown())
        {
            envFreqStartNorm = f;
            envFreqEndNorm = f;
        }
        else
        {
            envFreqStartNorm = -1.0f;
            envFreqEndNorm = -1.0f;
        }
        updateSelectionLabel();
        repaintDisplay();
        return;
    }

    if (isResidualDisplayMode(mode))
    {
        selectedPartials.clear();
        envelopeDrawPoints.clear();
        drawingResidualEnvelope = false;

        if (residualDrawButton.getToggleState())
        {
            const int band = residualBandForPoint(e.position, plot);
            if (band >= 0)
            {
                envelopeDrawBand = band;
                drawingResidualEnvelope = true;
                selectedResidualBands = { band };
                const int frame = frameForX(e.position.x, plot);
                selectedResidualFirstFrame = frame;
                selectedResidualLastFrame = frame;
                envelopeDrawPoints.clear();
                envelopeDrawPoints.push_back({ frame, dbFromPlotYInBand(band, e.position.y, plot) });
                updateSelectionLabel();
                refreshHoveredDbReadout();
                repaintDisplay();
                return;
            }
        }

        if (hasResidualSelection() && residualSelectionRect(plot).expanded(3.0f).contains(e.position)
            && !e.mods.isShiftDown() && !e.mods.isCommandDown() && !residualDrawButton.getToggleState())
        {
            scalingSelection = true;
            lastScaleDragY = e.position.y;
            return;
        }

        const int band = residualBandForPoint(e.position, plot);
        if (band >= 0)
        {
            if (!e.mods.isShiftDown() && !e.mods.isCommandDown())
                selectedResidualBands.clear();

            if (std::find(selectedResidualBands.begin(), selectedResidualBands.end(), band)
                == selectedResidualBands.end())
            {
                selectedResidualBands.push_back(band);
            }
            const int frame = frameForX(e.position.x, plot);
            selectedResidualFirstFrame = frame;
            selectedResidualLastFrame = frame;
        }
        else if (!e.mods.isShiftDown() && !e.mods.isCommandDown())
        {
            selectedResidualBands.clear();
            selectedResidualFirstFrame = -1;
            selectedResidualLastFrame = -1;
        }

        updateSelectionLabel();
        refreshHoveredDbReadout();
        repaintDisplay();
        return;
    }

    selectedResidualBands.clear();
    selectedResidualFirstFrame = -1;
    selectedResidualLastFrame = -1;

    if (partialFreqDrawButton.getToggleState())
    {
        const auto plot = getPlotBounds();
        const int nearest = findNearestPartial(e.position, 12.0f);
        if (nearest >= 0)
        {
            partialFreqDrawPartial = nearest;
            drawingPartialFreq = true;
            const int frame = frameForX(e.position.x, plot);
            const float fn = freqNormFromScreenY(e.position.y, plot);
            const double minHz = juce::jmax(1.0, traceSnapshot.minFrequencyHz);
            const double maxHz = juce::jmax(minHz * 1.2, traceSnapshot.maxFrequencyHz);
            const double hz = minHz * std::exp((double) fn * std::log(maxHz / minHz));
            partialFreqDrawPoints.clear();
            partialFreqDrawPoints.push_back({ frame, hz });
            selectedPartials = { nearest };
            updateSelectionLabel();
            repaintDisplay();
            return;
        }
    }

    const int nearest = findNearestPartial(e.position, 8.0f);
    if (nearest >= 0)
    {
        const bool additive = e.mods.isShiftDown() || e.mods.isCommandDown();
        if (!additive && isPartialSelected(nearest))
        {
            scalingSelection = true;
            lastScaleDragY = e.position.y;
            return;
        }

        if (!additive)
            selectedPartials.clear();

        auto it = std::find(selectedPartials.begin(), selectedPartials.end(), nearest);
        if (additive && it != selectedPartials.end())
            selectedPartials.erase(it);
        else if (it == selectedPartials.end())
            selectedPartials.push_back(nearest);
    }
    else if (!e.mods.isShiftDown() && !e.mods.isCommandDown())
    {
        selectedPartials.clear();
    }

    updateSelectionLabel();
    repaintDisplay();
}

void SpectrogramPanel::mouseDrag(const juce::MouseEvent& e)
{
    if (draggingOnsetIndex >= 0)
    {
        updateDraggedOnset(e.position);
        return;
    }

    // Ctrl+drag scrubs preview playback position.
    if (e.mods.isCommandDown() && !e.mods.isAltDown() && processor.isPreviewPlaying()
        && traceSnapshot.frameCount > 0)
    {
        const auto plot = getPlotBounds();
        const float timeNorm = timeNormFromScreenX(e.position.x, plot);
        processor.seekPreviewPlaybackNorm(juce::jlimit(0.0f, 1.0f, timeNorm));
        return;
    }

    if (panningView)
    {
        const auto plot = getPlotBounds();
        if (plot.getWidth() > 1.0f && plot.getHeight() > 1.0f)
        {
            const float deltaX = e.position.x - panDragStart.x;
            const float deltaY = e.position.y - panDragStart.y;
            const auto timeRange = visibleTimeRange();
            const auto freqRange = visibleFreqRange();
            timeCenter = panTimeCenterStart - deltaX / plot.getWidth() * timeRange.getLength();
            freqCenter = panFreqCenterStart + deltaY / plot.getHeight() * freqRange.getLength();
            clampViewport();
            syncZoomControlsFromViewport();
            repaintDisplay();
        }
        return;
    }

    if (drawingPartialFreq && partialFreqDrawPartial >= 0)
    {
        const auto plot = getPlotBounds();
        const int frame = frameForX(e.position.x, plot);
        const float fn = freqNormFromScreenY(e.position.y, plot);
        const double minHz = juce::jmax(1.0, traceSnapshot.minFrequencyHz);
        const double maxHz = juce::jmax(minHz * 1.2, traceSnapshot.maxFrequencyHz);
        const double hz = minHz * std::exp((double) fn * std::log(maxHz / minHz));
        if (partialFreqDrawPoints.empty() || partialFreqDrawPoints.back().first != frame)
            partialFreqDrawPoints.push_back({ frame, hz });
        else
            partialFreqDrawPoints.back().second = hz;
        repaintDisplay();
        return;
    }

    if (drawingResidualEnvelope && envelopeDrawBand >= 0)
    {
        const auto plot = getPlotBounds();
        const int frame = frameForX(e.position.x, plot);
        const double db = dbFromPlotYInBand(envelopeDrawBand, e.position.y, plot);
        if (envelopeDrawPoints.empty() || envelopeDrawPoints.back().first != frame)
            envelopeDrawPoints.push_back({ frame, db });
        else
            envelopeDrawPoints.back().second = db;

        selectedResidualFirstFrame = envelopeDrawPoints.front().first;
        selectedResidualLastFrame = envelopeDrawPoints.back().first;
        updateSelectionLabel();
        refreshHoveredDbReadout();
        repaintDisplay();
        return;
    }

    if (scalingSelection)
    {
        scaleSelectedData(e.position.y - lastScaleDragY);
        lastScaleDragY = e.position.y;
        return;
    }

    if (scalingEnvGain)
    {
        scaleEnvBoxGain(e.position.y - lastScaleDragY);
        lastScaleDragY = e.position.y;
        return;
    }

    if (selectingEnvRange)
    {
        float t = 0.0f, f = 0.0f;
        isoTimeFreqFromPoint(e.position, getPlotBounds(), t, f);
        envRangeEndNorm = t;
        if (envFreqStartNorm >= 0.0f)
            envFreqEndNorm = f;
        updateSelectionLabel();
        repaintDisplay();
        return;
    }

    if (dragStart.getDistanceFrom(e.position) < 5.0f)
        return;

    draggingLasso = true;
    lassoRect = normalisedRect(dragStart, e.position).getIntersection(getPlotBounds());
    repaintDisplay();
}

void SpectrogramPanel::mouseUp(const juce::MouseEvent& e)
{
    if (draggingOnsetIndex >= 0)
    {
        commitEditedOnsets();
        draggingOnsetIndex = -1;
        draggingOnsetHandle = OnsetHandle::None;
        panningView = false;
        draggingLasso = false;
        lassoRect = {};
        return;
    }

    if (drawingPartialFreq)
    {
        commitPartialFreqDraw();
        drawingPartialFreq = false;
        partialFreqDrawPartial = -1;
        partialFreqDrawPoints.clear();
        scalingSelection = false;
        panningView = false;
        draggingLasso = false;
        lassoRect = {};
        updateSelectionLabel();
        repaintDisplay();
        return;
    }

    if (drawingResidualEnvelope)
    {
        commitEnvelopeDraw();
        drawingResidualEnvelope = false;
        envelopeDrawBand = -1;
        envelopeDrawPoints.clear();
        scalingSelection = false;
        panningView = false;
        draggingLasso = false;
        lassoRect = {};
        updateSelectionLabel();
        refreshHoveredDbReadout();
        repaintDisplay();
        return;
    }

    if (scalingEnvGain)
    {
        scalingEnvGain = false;
        panningView = false;
        draggingLasso = false;
        lassoRect = {};
        updateSelectionLabel();
        repaintDisplay();
        return;
    }

    if (selectingEnvRange)
    {
        selectingEnvRange = false;
        // A click (no meaningful drag) clears the slab and reverts to the full visible window.
        if (std::abs(envRangeEndNorm - envRangeStartNorm) < 0.01f)
        {
            envRangeStartNorm = -1.0f;
            envRangeEndNorm = -1.0f;
            envFreqStartNorm = -1.0f;
            envFreqEndNorm = -1.0f;
        }
        else
        {
            if (envRangeEndNorm < envRangeStartNorm)
                std::swap(envRangeStartNorm, envRangeEndNorm);
            if (envFreqStartNorm >= 0.0f && envFreqEndNorm >= 0.0f)
            {
                if (std::abs(envFreqEndNorm - envFreqStartNorm) < 0.01f)
                {
                    envFreqStartNorm = -1.0f;  // negligible y extent -> full depth
                    envFreqEndNorm = -1.0f;
                }
                else if (envFreqEndNorm < envFreqStartNorm)
                {
                    std::swap(envFreqStartNorm, envFreqEndNorm);
                }
            }
        }
        panningView = false;
        draggingLasso = false;
        lassoRect = {};
        updateSelectionLabel();
        repaintDisplay();
        return;
    }

    if (draggingLasso)
    {
        if (isResidualDisplayMode(mode))
        {
            const auto plot = getPlotBounds();
            auto residualSelection = residualBandsForRect(lassoRect, plot);
            const int firstFrame = frameForX(lassoRect.getX(), plot);
            const int lastFrame = frameForX(lassoRect.getRight(), plot);
            if (e.mods.isShiftDown() || e.mods.isCommandDown())
            {
                for (const int bandIndex : residualSelection)
                {
                    if (std::find(selectedResidualBands.begin(), selectedResidualBands.end(), bandIndex)
                        == selectedResidualBands.end())
                    {
                        selectedResidualBands.push_back(bandIndex);
                    }
                }
                if (selectedResidualFirstFrame < 0)
                {
                    selectedResidualFirstFrame = std::min(firstFrame, lastFrame);
                    selectedResidualLastFrame = std::max(firstFrame, lastFrame);
                }
                else
                {
                    selectedResidualFirstFrame = std::min(selectedResidualFirstFrame, std::min(firstFrame, lastFrame));
                    selectedResidualLastFrame = std::max(selectedResidualLastFrame, std::max(firstFrame, lastFrame));
                }
            }
            else
            {
                selectedResidualBands = std::move(residualSelection);
                selectedResidualFirstFrame = std::min(firstFrame, lastFrame);
                selectedResidualLastFrame = std::max(firstFrame, lastFrame);
            }
        }
        else
        {
            std::vector<int> lassoSelection;
            for (const auto& trace : traceSnapshot.partials)
            {
                if (traceIntersectsLasso(trace, lassoRect))
                    lassoSelection.push_back(trace.partialIndex);
            }

            if (e.mods.isShiftDown() || e.mods.isCommandDown())
            {
                for (const int partialIndex : lassoSelection)
                {
                    if (!isPartialSelected(partialIndex))
                        selectedPartials.push_back(partialIndex);
                }
            }
            else
            {
                selectedPartials = std::move(lassoSelection);
            }
        }
    }

    draggingLasso = false;
    scalingSelection = false;
    panningView = false;
    lassoRect = {};
    updateSelectionLabel();
    repaintDisplay();
}

void SpectrogramPanel::mouseMove(const juce::MouseEvent& e)
{
    if (smearnovOverlayInteractive())
    {
        const auto plot = getPlotBounds();
        OnsetHandle handle = OnsetHandle::None;
        const int idx = onsetHandleAtPoint(e.position, plot, handle);
        if (idx >= 0)
        {
            if (idx != hoveredOnsetIndex || handle != hoveredOnsetHandle)
            {
                hoveredOnsetIndex = idx;
                hoveredOnsetHandle = handle;
                hoveredPartial = -1;
                hoveredResidualBand = -1;
                setMouseCursor(handle == OnsetHandle::Marker
                                   ? juce::MouseCursor::DraggingHandCursor
                                   : juce::MouseCursor::LeftRightResizeCursor);
                repaintDisplay();
            }
            return;
        }
        if (hoveredOnsetIndex >= 0)
        {
            hoveredOnsetIndex = -1;
            hoveredOnsetHandle = OnsetHandle::None;
            setMouseCursor(juce::MouseCursor::NormalCursor);
            repaintDisplay();
        }
    }

    if (isResidualDisplayMode(mode))
    {
        const auto plot = getPlotBounds();
        const int nextBand = residualBandForPoint(e.position, plot);
        const int nextFrame = plot.contains(e.position) ? frameForX(e.position.x, plot) : -1;
        if (nextBand != hoveredResidualBand || nextFrame != hoveredResidualFrame)
        {
            hoveredResidualBand = nextBand;
            hoveredResidualFrame = nextFrame;
            hoveredPartial = -1;
            setMouseCursor(hoveredResidualBand >= 0 ? juce::MouseCursor::PointingHandCursor
                                                     : juce::MouseCursor::NormalCursor);
            refreshHoveredDbReadout();
            repaintDisplay();
        }
        return;
    }

    const int nextHover = findNearestPartial(e.position, 7.0f);
    if (nextHover != hoveredPartial)
    {
        hoveredPartial = nextHover;
        setMouseCursor(hoveredPartial >= 0 ? juce::MouseCursor::PointingHandCursor
                                           : juce::MouseCursor::NormalCursor);
        repaintDisplay();
    }
}

void SpectrogramPanel::mouseDoubleClick(const juce::MouseEvent& e)
{
    if (smearnovOverlayInteractive())
    {
        const auto plot = getPlotBounds();
        OnsetHandle handle = OnsetHandle::None;
        const int idx = onsetHandleAtPoint(e.position, plot, handle);
        if (idx >= 0 && idx < (int) traceSnapshot.onsets.size())
        {
            traceSnapshot.onsets.erase(traceSnapshot.onsets.begin() + idx);
            hoveredOnsetIndex = -1;
            hoveredOnsetHandle = OnsetHandle::None;
            draggingOnsetIndex = -1;
            draggingOnsetHandle = OnsetHandle::None;
            commitEditedOnsets();
            return;
        }
    }

    if (!isResidualDisplayMode(mode))
        return;

    const auto plot = getPlotBounds();
    if (!plot.contains(e.position))
        return;

    const int band = residualBandForPoint(e.position, plot);
    const int frame = frameForX(e.position.x, plot);
    if (band < 0 || frame < 0)
        return;

    hoveredResidualBand = band;
    hoveredResidualFrame = frame;
    if (std::find(selectedResidualBands.begin(), selectedResidualBands.end(), band) == selectedResidualBands.end())
        selectedResidualBands = { band };
    selectedResidualFirstFrame = frame;
    selectedResidualLastFrame = frame;

    residualDbEditorActive = true;
    refreshHoveredDbReadout();
    residualDbEditor.setEnabled(true);
    residualDbEditor.grabKeyboardFocus();
    residualDbEditor.selectAll();
    updateSelectionLabel();
    repaintDisplay();
}

void SpectrogramPanel::mouseExit(const juce::MouseEvent&)
{
    hoveredResidualBand = -1;
    hoveredResidualFrame = -1;
    bool needsRepaint = hoveredPartial >= 0 || hoveredOnsetIndex >= 0;
    hoveredPartial = -1;
    hoveredOnsetIndex = -1;
    hoveredOnsetHandle = OnsetHandle::None;
    if (needsRepaint)
        repaintDisplay();
}

void SpectrogramPanel::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (traceSnapshot.frameCount <= 0)
        return;

    const auto plot = getPlotBounds();
    if (! plot.contains(e.position))
        return;

    if (wheel.deltaY != 0.0f)
    {
        const float factor = std::pow(1.12f, wheel.deltaY);
        if (e.mods.isShiftDown())
            applyZoomAtScreenPoint(e.position, factor, 1.0f);
        else if (e.mods.isAltDown())
            applyZoomAtScreenPoint(e.position, 1.0f, factor);
        else
            applyZoomAtScreenPoint(e.position, factor, factor);
        return;
    }

    if (wheel.deltaX != 0.0f && timeZoom > 1.0f)
        panView(-wheel.deltaX * 0.04f / timeZoom, 0.0f);
}

void SpectrogramPanel::mouseMagnify(const juce::MouseEvent& e, float scaleFactor)
{
    if (traceSnapshot.frameCount <= 0 || scaleFactor <= 0.0f)
        return;

    const auto plot = getPlotBounds();
    if (! plot.contains(e.position))
        return;

    applyZoomAtScreenPoint(e.position, scaleFactor, scaleFactor);
}

bool SpectrogramPanel::keyPressed(const juce::KeyPress& key)
{
    if (key.getKeyCode() == juce::KeyPress::deleteKey || key.getKeyCode() == juce::KeyPress::backspaceKey)
    {
        deleteSelection();
        return true;
    }

    if (key.getKeyCode() == juce::KeyPress::escapeKey)
    {
        selectedPartials.clear();
        selectedResidualBands.clear();
        selectedResidualFirstFrame = -1;
        selectedResidualLastFrame = -1;
        updateSelectionLabel();
        repaintDisplay();
        return true;
    }

    if (key.getKeyCode() == juce::KeyPress::homeKey
        || key.getTextCharacter() == '0')
    {
        resetViewport();
        repaintDisplay();
        return true;
    }

    if ((key.getModifiers().isCommandDown() || key.getModifiers().isCtrlDown())
        && key.getTextCharacter() == '0')
    {
        resetViewport();
        repaintDisplay();
        return true;
    }

    if ((key.getModifiers().isCommandDown() || key.getModifiers().isCtrlDown())
        && (key.getTextCharacter() == 'a' || key.getTextCharacter() == 'A'))
    {
        if (isResidualDisplayMode(mode))
        {
            selectedResidualBands.clear();
            selectedResidualBands.reserve(traceSnapshot.residualBands.size());
            for (const auto& band : traceSnapshot.residualBands)
                selectedResidualBands.push_back(band.bandIndex);
            selectedResidualFirstFrame = 0;
            selectedResidualLastFrame = juce::jmax(0, traceSnapshot.frameCount - 1);
        }
        else
        {
            selectedPartials.clear();
            selectedPartials.reserve(traceSnapshot.partials.size());
            for (const auto& trace : traceSnapshot.partials)
                selectedPartials.push_back(trace.partialIndex);
        }
        updateSelectionLabel();
        repaintDisplay();
        return true;
    }

    if ((key.getModifiers().isCommandDown() || key.getModifiers().isCtrlDown())
        && isResidualDisplayMode(mode))
    {
        if (key.getTextCharacter() == 'c' || key.getTextCharacter() == 'C')
        {
            runCopyBand();
            return true;
        }
        if (key.getTextCharacter() == 'v' || key.getTextCharacter() == 'V')
        {
            runPasteBand();
            return true;
        }
        if (key.getTextCharacter() == 'x' || key.getTextCharacter() == 'X')
        {
            runSwapBands();
            return true;
        }
    }

    if (isResidualDisplayMode(mode)
        && residualDbEditor.hasKeyboardFocus(true)
        && key.getKeyCode() == juce::KeyPress::returnKey)
    {
        applyResidualDbFromEditor();
        return true;
    }

    // 'H' — select all harmonics of the hovered partial (within ±50 cents).
    if ((key.getTextCharacter() == 'h' || key.getTextCharacter() == 'H')
        && !isResidualDisplayMode(mode)
        && hoveredPartial >= 0)
    {
        const auto it = std::find_if(traceSnapshot.partials.begin(), traceSnapshot.partials.end(),
                                     [this](const AtsPartialTrace& t) { return t.partialIndex == hoveredPartial; });
        if (it != traceSnapshot.partials.end() && it->meanFrequencyHz > 0.0)
        {
            const double baseHz = it->meanFrequencyHz;
            constexpr double kCentsTolerance = 50.0; // ±50 cents (~2.9%)
            const double ratio = std::pow(2.0, kCentsTolerance / 1200.0);

            const bool additive = key.getModifiers().isShiftDown();
            if (!additive)
                selectedPartials.clear();

            for (const auto& trace : traceSnapshot.partials)
            {
                if (trace.meanFrequencyHz <= 0.0)
                    continue;
                const double f = trace.meanFrequencyHz;
                const double k = f / baseHz;
                const long kRound = std::lround(k);
                if (kRound >= 1 && std::abs(k - (double) kRound) * baseHz / f < (ratio - 1.0))
                {
                    if (std::find(selectedPartials.begin(), selectedPartials.end(),
                                  trace.partialIndex) == selectedPartials.end())
                        selectedPartials.push_back(trace.partialIndex);
                }
            }
            updateSelectionLabel();
            repaintDisplay();
        }
        return true;
    }

    return false;
}

void SpectrogramPanel::updateSelectionLabel()
{
    deleteButton.setEnabled(!selectedPartials.empty() || hasResidualSelection());

    if (traceSnapshot.frameCount <= 0)
    {
        selectionLabel.setText("No model", juce::dontSendNotification);
        return;
    }

    if (isEnvelopeDisplayMode())
    {
        if (envRangeStartNorm >= 0.0f && envRangeEndNorm >= 0.0f)
        {
            const float t0 = juce::jmin(envRangeStartNorm, envRangeEndNorm);
            const float t1 = juce::jmax(envRangeStartNorm, envRangeEndNorm);
            const double dur = traceSnapshot.durationSeconds;
            juce::String text = "Box ";
            if (dur > 0.0)
                text << juce::String(t0 * (float) dur, 3) << "-" << juce::String(t1 * (float) dur, 3) << " s";
            else
                text << juce::String(juce::roundToInt(t0 * 100.0f)) << "-"
                     << juce::String(juce::roundToInt(t1 * 100.0f)) << " %";

            double loHz = 0.0, hiHz = 0.0;
            if (envBoxFreqRangeHz(loHz, hiHz))
                text << " | " << juce::String(loHz, 0) << "-" << juce::String(hiHz, 0) << " Hz";
            else
                text << " | full band";
            text << " · drag ↕ to gain";
            selectionLabel.setText(text, juce::dontSendNotification);
        }
        else
        {
            selectionLabel.setText("Drag box (Shift = + freq) · whole visible range",
                                   juce::dontSendNotification);
        }
        return;
    }

    if (hasResidualSelection())
    {
        std::sort(selectedResidualBands.begin(), selectedResidualBands.end());
        const int firstBand = selectedResidualBands.front();
        const int lastBand = selectedResidualBands.back();
        juce::String text = "Residual B" + juce::String(firstBand)
                            + (firstBand == lastBand ? juce::String()
                                                     : ("-B" + juce::String(lastBand)))
                            + " | frames "
                            + juce::String(getSelectionFirstFrame())
                            + "-"
                            + juce::String(getSelectionLastFrame());

        if (firstBand == lastBand)
        {
            const auto band = std::find_if(traceSnapshot.residualBands.begin(),
                                           traceSnapshot.residualBands.end(),
                                           [firstBand](const auto& b) { return b.bandIndex == firstBand; });
            if (band != traceSnapshot.residualBands.end())
                text << " | " << juce::String(band->lowHz, 0) << "-" << juce::String(band->highHz, 0) << " Hz";
        }

        selectionLabel.setText(text, juce::dontSendNotification);
        return;
    }

    if (selectedPartials.empty())
    {
        selectionLabel.setText("No selection", juce::dontSendNotification);
        return;
    }

    if (selectedPartials.size() == 1)
    {
        const int partialIndex = selectedPartials.front();
        const auto trace = std::find_if(traceSnapshot.partials.begin(),
                                        traceSnapshot.partials.end(),
                                        [partialIndex](const auto& t) { return t.partialIndex == partialIndex; });
        if (trace != traceSnapshot.partials.end())
        {
            selectionLabel.setText("P" + juce::String(partialIndex) + " | "
                                       + juce::String(trace->meanFrequencyHz, 1) + " Hz",
                                   juce::dontSendNotification);
            return;
        }
    }

    selectionLabel.setText(juce::String((int) selectedPartials.size()) + " partials selected",
                           juce::dontSendNotification);
}

void SpectrogramPanel::deleteSelection()
{
    if (isResidualDisplayMode(mode) && hasResidualSelection())
    {
        deleteSelectedResidual();
        return;
    }

    deleteSelectedPartials();
}

void SpectrogramPanel::deleteSelectedPartials()
{
    if (selectedPartials.empty())
        return;

    juce::String error;
    if (!processor.mutePartials(traceSource, selectedPartials, error))
    {
        if (error.isNotEmpty())
            juce::NativeMessageBox::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Delete partials", error);
        return;
    }

    selectedPartials.clear();
    rebuildTraceSnapshot();
}

void SpectrogramPanel::deleteSelectedResidual()
{
    if (!hasResidualSelection())
        return;

    juce::String error;
    if (!processor.muteResidualBands(traceSource,
                                     selectedResidualBands,
                                     selectedResidualFirstFrame,
                                     selectedResidualLastFrame,
                                     error))
    {
        if (error.isNotEmpty())
            juce::NativeMessageBox::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Delete residual", error);
        return;
    }

    selectedResidualBands.clear();
    selectedResidualFirstFrame = -1;
    selectedResidualLastFrame = -1;
    rebuildTraceSnapshot();
}

void SpectrogramPanel::scaleSelectedData(float deltaY)
{
    if (std::abs(deltaY) < 0.25f)
        return;

    const double scale = std::pow(2.0, (double) -deltaY / 30.0);
    juce::String error;
    bool ok = false;
    if (isResidualDisplayMode(mode) && hasResidualSelection())
    {
        ok = processor.scaleResidualBands(traceSource,
                                          selectedResidualBands,
                                          selectedResidualFirstFrame,
                                          selectedResidualLastFrame,
                                          scale,
                                          error);
    }
    else if (!selectedPartials.empty())
    {
        ok = processor.scalePartials(traceSource, selectedPartials, scale, error);
    }

    if (ok)
    {
        rebuildTraceSnapshot(true);
        lastModelRevision = processor.getModelRevision();
        updateSelectionLabel();
        repaintDisplay();
    }
}

void SpectrogramPanel::applyZoomAtScreenPoint(juce::Point<float> anchor,
                                              float timeFactor,
                                              float freqFactor)
{
    const auto plot = getPlotBounds();
    if (plot.getWidth() <= 1.0f || plot.getHeight() <= 1.0f)
        return;

    const float anchorTime = timeNormFromScreenX(anchor.x, plot);
    const float anchorFreq = freqNormFromScreenY(anchor.y, plot);
    const float anchorScreenX = (anchor.x - plot.getX()) / plot.getWidth();
    const float anchorScreenY = (plot.getBottom() - anchor.y) / plot.getHeight();

    timeZoom = juce::jlimit(kMinZoom, kMaxZoom, timeZoom * timeFactor);
    freqZoom = juce::jlimit(kMinZoom, kMaxZoom, freqZoom * freqFactor);

    const float timeHalfSpan = 0.5f / timeZoom;
    const float freqHalfSpan = 0.5f / freqZoom;
    timeCenter = anchorTime + timeHalfSpan * (1.0f - 2.0f * anchorScreenX);
    freqCenter = anchorFreq + freqHalfSpan * (1.0f - 2.0f * anchorScreenY);
    clampViewport();
    syncZoomControlsFromViewport();
    updateScrollBarsFromViewport();
    repaintDisplay();
}

void SpectrogramPanel::panView(float deltaTimeNorm, float deltaFreqNorm)
{
    if (deltaTimeNorm == 0.0f && deltaFreqNorm == 0.0f)
        return;

    timeCenter += deltaTimeNorm;
    freqCenter += deltaFreqNorm;
    clampViewport();
    repaintDisplay();
}

void SpectrogramPanel::clampViewport()
{
    const float timeHalfSpan = 0.5f / timeZoom;
    const float freqHalfSpan = 0.5f / freqZoom;

    if (timeHalfSpan >= 0.5f)
        timeCenter = 0.5f;
    else
        timeCenter = juce::jlimit(timeHalfSpan, 1.0f - timeHalfSpan, timeCenter);

    if (freqHalfSpan >= 0.5f)
        freqCenter = 0.5f;
    else
        freqCenter = juce::jlimit(freqHalfSpan, 1.0f - freqHalfSpan, freqCenter);

    updateScrollBarsFromViewport();
}

void SpectrogramPanel::syncZoomControlsFromViewport()
{
    if (zoomBar == nullptr || syncingZoomControls)
        return;

    syncingZoomControls = true;
    zoomBar->setCallbacksSuppressed(true);
    zoomBar->setVerticalZoom(freqZoom);
    zoomBar->setHorizontalZoom(timeZoom);
    zoomBar->setCallbacksSuppressed(false);
    syncingZoomControls = false;
}

void SpectrogramPanel::syncViewportFromZoomControls()
{
    if (zoomBar == nullptr || syncingZoomControls)
        return;

    const auto plot = getPlotBounds();
    const float anchorTime = timeCenter;
    const float anchorFreq = freqCenter;

    timeZoom = zoomBar->getHorizontalZoom();
    freqZoom = zoomBar->getVerticalZoom();
    timeCenter = anchorTime;
    freqCenter = anchorFreq;
    clampViewport();
    syncZoomControlsFromViewport();
    updateScrollBarsFromViewport();
    repaintDisplay();
}

void SpectrogramPanel::resetViewport()
{
    timeZoom = 1.0f;
    freqZoom = 1.0f;
    timeCenter = 0.5f;
    freqCenter = 0.5f;
    panningView = false;
    syncZoomControlsFromViewport();
    updateScrollBarsFromViewport();
}

juce::Rectangle<float> SpectrogramPanel::getScreenBounds() const
{
    auto r = getLocalBounds().toFloat().reduced(5.0f);
    r.removeFromTop(isResidualDisplayMode(mode) ? 50.0f : 30.0f);
    return r;
}

bool SpectrogramPanel::showsBandLabelGutter() const
{
    return isResidualDisplayMode(mode) && traceSnapshot.frameCount > 0 && !traceSnapshot.residualBands.empty();
}

juce::Rectangle<float> SpectrogramPanel::getBandLabelGutterBounds() const
{
    auto content = getTraceContentBounds();
    if (!showsBandLabelGutter())
        return {};

    auto gutter = content.removeFromLeft((float) kBandLabelGutterWidth);
    gutter.removeFromTop(18.0f);
    gutter.removeFromBottom(18.0f);
    return gutter;
}

juce::Rectangle<float> SpectrogramPanel::getTraceContentBounds() const
{
    auto content = getScreenBounds().reduced(14.0f, 12.0f);
    content.removeFromTop(18.0f);
    content.removeFromBottom(18.0f + (float) traceScrollBarThickness);
    content.removeFromRight((float) traceScrollBarThickness);
    return content;
}

juce::Rectangle<float> SpectrogramPanel::getPlotBounds() const
{
    auto plot = getTraceContentBounds();
    if (showsBandLabelGutter())
        plot.removeFromLeft((float) kBandLabelGutterWidth);
    plot.removeFromRight(10.0f);
    return plot;
}

juce::Range<float> SpectrogramPanel::visibleTimeRange() const
{
    const float halfSpan = 0.5f / juce::jmax(1.0f, timeZoom);
    return { timeCenter - halfSpan, timeCenter + halfSpan };
}

juce::Range<float> SpectrogramPanel::visibleFreqRange() const
{
    const float halfSpan = 0.5f / juce::jmax(1.0f, freqZoom);
    return { freqCenter - halfSpan, freqCenter + halfSpan };
}

juce::Point<float> SpectrogramPanel::positionForPoint(const AtsPartialTracePoint& point,
                                                      juce::Rectangle<float> plot) const
{
    const auto timeRange = visibleTimeRange();
    const auto freqRange = visibleFreqRange();
    const float timeSpan = juce::jmax(1.0e-6f, timeRange.getLength());
    const float freqSpan = juce::jmax(1.0e-6f, freqRange.getLength());
    const float xNorm = (point.timeNorm - timeRange.getStart()) / timeSpan;
    const float yNorm = (point.freqNorm - freqRange.getStart()) / freqSpan;
    return { plot.getX() + plot.getWidth() * xNorm,
             plot.getBottom() - plot.getHeight() * yNorm };
}

juce::Point<float> SpectrogramPanel::positionForIsoPoint(const AtsPartialTracePoint& point,
                                                         juce::Rectangle<float> plot) const
{
    return positionForIsoCoords(point.timeNorm, point.freqNorm, point.ampNorm, plot);
}

juce::Point<float> SpectrogramPanel::positionForIsoCoords(float pointTimeNorm,
                                                          float pointFreqNorm,
                                                          float pointAmpNorm,
                                                          juce::Rectangle<float> plot) const
{
    const auto timeRange = visibleTimeRange();
    const auto freqRange = visibleFreqRange();
    const float timeSpan = juce::jmax(1.0e-6f, timeRange.getLength());
    const float freqSpan = juce::jmax(1.0e-6f, freqRange.getLength());
    const float timeNorm = (pointTimeNorm - timeRange.getStart()) / timeSpan;
    const float freqNorm = (pointFreqNorm - freqRange.getStart()) / freqSpan;
    const float ampNorm = juce::jlimit(0.0f, 1.25f, pointAmpNorm);

    const auto origin = juce::Point<float>(plot.getX() + plot.getWidth() * 0.075f,
                                           plot.getBottom() - plot.getHeight() * 0.085f);
    const auto freqAxis = juce::Point<float>(plot.getWidth() * 0.66f,
                                             -plot.getHeight() * 0.055f);
    const auto timeAxis = juce::Point<float>(plot.getWidth() * 0.255f,
                                             -plot.getHeight() * 0.49f);
    const auto ampAxis = juce::Point<float>(0.0f,
                                            -plot.getHeight() * 0.28f);

    return { origin.x + freqAxis.x * freqNorm + timeAxis.x * timeNorm + ampAxis.x * ampNorm,
             origin.y + freqAxis.y * freqNorm + timeAxis.y * timeNorm + ampAxis.y * ampNorm };
}

float SpectrogramPanel::timeNormFromIsoPoint(juce::Point<float> pos, juce::Rectangle<float> plot) const
{
    // Invert the isometric projection for time only by projecting the cursor onto the
    // front-bottom edge (freq=front, amp=0), where screen position is linear in time.
    const auto timeRange = visibleTimeRange();
    const auto freqRange = visibleFreqRange();
    const auto origin = positionForIsoCoords(timeRange.getStart(), freqRange.getStart(), 0.0f, plot);
    const auto axisEnd = positionForIsoCoords(timeRange.getEnd(), freqRange.getStart(), 0.0f, plot);
    const auto axis = axisEnd - origin;
    const float denom = axis.x * axis.x + axis.y * axis.y;
    if (denom < 1.0e-6f)
        return timeRange.getStart();
    const auto rel = pos - origin;
    const float u = juce::jlimit(0.0f, 1.0f, (rel.x * axis.x + rel.y * axis.y) / denom);
    return juce::jlimit(0.0f, 1.0f, timeRange.getStart() + u * timeRange.getLength());
}

void SpectrogramPanel::isoTimeFreqFromPoint(juce::Point<float> pos,
                                            juce::Rectangle<float> plot,
                                            float& timeNormAbs,
                                            float& freqNormAbs) const
{
    // Solve the iso projection for (time, freq) on the amp=0 floor plane.  The amp axis is
    // purely vertical, so a 2x2 solve over the time and freq screen vectors recovers both.
    const auto timeRange = visibleTimeRange();
    const auto freqRange = visibleFreqRange();
    const auto origin = positionForIsoCoords(timeRange.getStart(), freqRange.getStart(), 0.0f, plot);
    const auto timeAxis = positionForIsoCoords(timeRange.getEnd(), freqRange.getStart(), 0.0f, plot) - origin;
    const auto freqAxis = positionForIsoCoords(timeRange.getStart(), freqRange.getEnd(), 0.0f, plot) - origin;
    const auto rel = pos - origin;

    const float det = freqAxis.x * timeAxis.y - timeAxis.x * freqAxis.y;
    float timeRel = 0.0f;
    float freqRel = 0.0f;
    if (std::abs(det) > 1.0e-6f)
    {
        freqRel = (rel.x * timeAxis.y - timeAxis.x * rel.y) / det;
        timeRel = (freqAxis.x * rel.y - rel.x * freqAxis.y) / det;
    }
    timeRel = juce::jlimit(0.0f, 1.0f, timeRel);
    freqRel = juce::jlimit(0.0f, 1.0f, freqRel);
    timeNormAbs = juce::jlimit(0.0f, 1.0f, timeRange.getStart() + timeRel * timeRange.getLength());
    freqNormAbs = juce::jlimit(0.0f, 1.0f, freqRange.getStart() + freqRel * freqRange.getLength());
}

bool SpectrogramPanel::pointInEnvBox(juce::Point<float> pos, juce::Rectangle<float> plot) const
{
    if (envRangeStartNorm < 0.0f || envRangeEndNorm < 0.0f)
        return false;
    float t = 0.0f, f = 0.0f;
    isoTimeFreqFromPoint(pos, plot, t, f);
    const float t0 = juce::jmin(envRangeStartNorm, envRangeEndNorm);
    const float t1 = juce::jmax(envRangeStartNorm, envRangeEndNorm);
    if (t < t0 || t > t1)
        return false;
    if (envFreqStartNorm >= 0.0f && envFreqEndNorm >= 0.0f)
    {
        const float f0 = juce::jmin(envFreqStartNorm, envFreqEndNorm);
        const float f1 = juce::jmax(envFreqStartNorm, envFreqEndNorm);
        if (f < f0 || f > f1)
            return false;
    }
    return true;
}

bool SpectrogramPanel::envBoxFrameRange(int& firstFrame, int& lastFrame) const
{
    if (traceSnapshot.frameCount <= 0)
        return false;
    const int last = traceSnapshot.frameCount - 1;
    juce::Range<float> tr = visibleTimeRange();
    if (envRangeStartNorm >= 0.0f && envRangeEndNorm >= 0.0f
        && std::abs(envRangeEndNorm - envRangeStartNorm) > 0.001f)
    {
        tr = { juce::jmin(envRangeStartNorm, envRangeEndNorm),
               juce::jmax(envRangeStartNorm, envRangeEndNorm) };
    }
    firstFrame = juce::jlimit(0, last, (int) std::floor(juce::jlimit(0.0f, 1.0f, tr.getStart()) * (float) last));
    lastFrame = juce::jlimit(0, last, (int) std::ceil(juce::jlimit(0.0f, 1.0f, tr.getEnd()) * (float) last));
    if (lastFrame < firstFrame)
        std::swap(firstFrame, lastFrame);
    return true;
}

bool SpectrogramPanel::envBoxFreqRangeHz(double& loHz, double& hiHz) const
{
    // Returns false (full range) when no frequency sub-band is selected.
    if (envFreqStartNorm < 0.0f || envFreqEndNorm < 0.0f)
    {
        loHz = 0.0;
        hiHz = 0.0;
        return false;
    }
    const double minHz = juce::jmax(1.0, traceSnapshot.minFrequencyHz);
    const double maxHz = juce::jmax(minHz * 1.2, traceSnapshot.maxFrequencyHz);
    const double logRange = std::log(maxHz / minHz);
    const float f0 = juce::jmin(envFreqStartNorm, envFreqEndNorm);
    const float f1 = juce::jmax(envFreqStartNorm, envFreqEndNorm);
    loHz = minHz * std::exp((double) f0 * logRange);
    hiHz = minHz * std::exp((double) f1 * logRange);
    return true;
}

void SpectrogramPanel::scaleEnvBoxGain(float deltaY)
{
    if (std::abs(deltaY) < 0.25f)
        return;

    int f0 = 0, f1 = 0;
    if (!envBoxFrameRange(f0, f1))
        return;
    double loHz = 0.0, hiHz = 0.0;
    (void) envBoxFreqRangeHz(loHz, hiHz);  // false -> {0,0} = full range, handled downstream

    const double scale = std::pow(2.0, (double) -deltaY / 30.0);
    juce::String error;
    if (processor.gainPartialEnvelope(traceSource, f0, f1, loHz, hiHz, scale, error))
    {
        rebuildTraceSnapshot(true);
        lastModelRevision = processor.getModelRevision();
        updateSelectionLabel();
        repaintDisplay();
    }
}

float SpectrogramPanel::xForFrame(int frame, juce::Rectangle<float> plot) const
{
    if (traceSnapshot.frameCount <= 1)
        return plot.getX();

    const int clampedFrame = juce::jlimit(0, traceSnapshot.frameCount - 1, frame);
    const float timeNorm = (float) clampedFrame / (float) (traceSnapshot.frameCount - 1);
    const auto timeRange = visibleTimeRange();
    const float xNorm = (timeNorm - timeRange.getStart()) / juce::jmax(1.0e-6f, timeRange.getLength());
    return plot.getX() + plot.getWidth() * xNorm;
}

float SpectrogramPanel::timeNormFromScreenX(float x, juce::Rectangle<float> plot) const
{
    const auto timeRange = visibleTimeRange();
    const float t = juce::jlimit(0.0f, 1.0f, (x - plot.getX()) / juce::jmax(1.0f, plot.getWidth()));
    return timeRange.getStart() + t * timeRange.getLength();
}

float SpectrogramPanel::freqNormFromScreenY(float y, juce::Rectangle<float> plot) const
{
    const auto freqRange = visibleFreqRange();
    const float t = juce::jlimit(0.0f, 1.0f, (plot.getBottom() - y) / juce::jmax(1.0f, plot.getHeight()));
    return freqRange.getStart() + t * freqRange.getLength();
}

juce::Range<float> SpectrogramPanel::yRangeForBand(const AtsResidualBandTrace& band,
                                                   juce::Rectangle<float> plot) const
{
    const double minHz = juce::jmax(1.0, traceSnapshot.minFrequencyHz);
    const double maxHz = juce::jmax(minHz * 1.2, traceSnapshot.maxFrequencyHz);
    const double logRange = std::log(maxHz / minHz);
    auto normForHz = [&](double hz) {
        const double clamped = juce::jlimit(minHz, maxHz, juce::jmax(1.0, hz));
        return (float) juce::jlimit(0.0, 1.0, std::log(clamped / minHz) / logRange);
    };

    const auto freqRange = visibleFreqRange();
    const float freqSpan = juce::jmax(1.0e-6f, freqRange.getLength());
    auto screenYForNorm = [&](float norm) {
        const float yNorm = (norm - freqRange.getStart()) / freqSpan;
        return plot.getBottom() - plot.getHeight() * yNorm;
    };

    const float yHigh = screenYForNorm(normForHz(band.highHz));
    const float yLow = screenYForNorm(normForHz(band.lowHz));
    return { std::min(yHigh, yLow), std::max(yHigh, yLow) };
}

juce::Rectangle<float> SpectrogramPanel::residualSelectionRect(juce::Rectangle<float> plot) const
{
    if (!hasResidualSelection() || traceSnapshot.frameCount <= 0)
        return {};

    float top = plot.getBottom();
    float bottom = plot.getY();
    for (const int bandIndex : selectedResidualBands)
    {
        const auto band = std::find_if(traceSnapshot.residualBands.begin(),
                                       traceSnapshot.residualBands.end(),
                                       [bandIndex](const auto& b) { return b.bandIndex == bandIndex; });
        if (band == traceSnapshot.residualBands.end())
            continue;

        const auto yRange = yRangeForBand(*band, plot);
        top = std::min(top, yRange.getStart());
        bottom = std::max(bottom, yRange.getEnd());
    }

    const int firstFrame = std::min(selectedResidualFirstFrame, selectedResidualLastFrame);
    const int lastFrame = std::max(selectedResidualFirstFrame, selectedResidualLastFrame);
    const float firstTimeNorm = traceSnapshot.frameCount <= 1
                                    ? 0.0f
                                    : (float) firstFrame / (float) (traceSnapshot.frameCount - 1);
    const float lastTimeNorm = traceSnapshot.frameCount <= 1
                                   ? 0.0f
                                   : (float) lastFrame / (float) (traceSnapshot.frameCount - 1);
    const float x1 = plot.getX()
                     + plot.getWidth()
                           * ((firstTimeNorm - visibleTimeRange().getStart()) / visibleTimeRange().getLength());
    const float x2 = plot.getX()
                     + plot.getWidth()
                           * ((lastTimeNorm - visibleTimeRange().getStart()) / visibleTimeRange().getLength());
    return juce::Rectangle<float>::leftTopRightBottom(std::min(x1, x2),
                                                      top,
                                                      std::max(x1, x2) + 1.0f,
                                                      bottom);
}

int SpectrogramPanel::frameForX(float x, juce::Rectangle<float> plot) const
{
    if (traceSnapshot.frameCount <= 1 || plot.getWidth() <= 1.0f)
        return 0;

    const float timeNorm = timeNormFromScreenX(x, plot);
    return juce::jlimit(0,
                        traceSnapshot.frameCount - 1,
                        juce::roundToInt(timeNorm * (float) (traceSnapshot.frameCount - 1)));
}

int SpectrogramPanel::residualBandForY(float y, juce::Rectangle<float> plot) const
{
    int bestBand = -1;
    float bestDistance = 8.0f;
    for (const auto& band : traceSnapshot.residualBands)
    {
        const auto yRange = yRangeForBand(band, plot).expanded(1.5f);
        if (yRange.contains(y))
            return band.bandIndex;

        const float centreY = (yRange.getStart() + yRange.getEnd()) * 0.5f;
        const float distance = std::abs(y - centreY);
        if (distance < bestDistance)
        {
            bestDistance = distance;
            bestBand = band.bandIndex;
        }
    }
    return bestBand;
}

int SpectrogramPanel::residualBandForGutterPoint(juce::Point<float> pos,
                                                 juce::Rectangle<float> gutter,
                                                 juce::Rectangle<float> plot) const
{
    if (gutter.isEmpty() || traceSnapshot.residualBands.empty())
        return -1;

    int bestBand = -1;
    float bestDistance = 14.0f;
    for (const auto& band : traceSnapshot.residualBands)
    {
        const auto yRange = yRangeForBand(band, plot);
        if (yRange.getEnd() < gutter.getY() - 2.0f || yRange.getStart() > gutter.getBottom() + 2.0f)
            continue;

        const float centreY = (yRange.getStart() + yRange.getEnd()) * 0.5f;
        const auto labelRect = juce::Rectangle<float>(gutter.getX() + 1.0f,
                                                      centreY - 9.0f,
                                                      gutter.getWidth() - 2.0f,
                                                      20.0f);
        if (labelRect.contains(pos))
            return band.bandIndex;

        const float distance = std::abs(pos.y - centreY);
        if (distance < bestDistance)
        {
            bestDistance = distance;
            bestBand = band.bandIndex;
        }
    }
    return bestBand;
}

int SpectrogramPanel::residualBandForPoint(juce::Point<float> pos, juce::Rectangle<float> plot) const
{
    const auto gutter = getBandLabelGutterBounds();
    if (!gutter.isEmpty() && gutter.contains(pos))
    {
        const int gutterBand = residualBandForGutterPoint(pos, gutter, plot);
        if (gutterBand >= 0)
            return gutterBand;
    }

    if (!isIsometricDisplayMode(mode))
        return residualBandForY(pos.y, plot);

    if (!plot.expanded(2.0f).contains(pos))
        return -1;

    const auto timeRange = visibleTimeRange();
    const auto freqRange = visibleFreqRange();
    const double minHz = juce::jmax(1.0, traceSnapshot.minFrequencyHz);
    const double maxHz = juce::jmax(minHz * 1.2, traceSnapshot.maxFrequencyHz);
    const double logRange = std::log(maxHz / minHz);
    auto normForHz = [&](double hz) {
        const double clamped = juce::jlimit(minHz, maxHz, juce::jmax(1.0, hz));
        return (float) juce::jlimit(0.0, 1.0, std::log(clamped / minHz) / logRange);
    };

    const float maxHitDistance = mode == Mode::IsometricResidual ? 16.0f : 12.0f;
    int bestBand = -1;
    float bestDistance = maxHitDistance;

    for (const auto& band : traceSnapshot.residualBands)
    {
        const float lowNorm = normForHz(band.lowHz);
        const float highNorm = normForHz(band.highHz);
        const float freqNorm = juce::jlimit(0.0f, 1.0f, 0.5f * (lowNorm + highNorm));
        if (freqNorm < freqRange.getStart() - freqRange.getLength() * 0.05f
            || freqNorm > freqRange.getEnd() + freqRange.getLength() * 0.05f)
            continue;

        const int columns = (int) band.energyNorm.size();
        if (columns <= 1)
            continue;

        juce::Point<float> prevBase;
        juce::Point<float> prevRidge;
        bool hasPrev = false;

        for (int column = 0; column < columns; ++column)
        {
            const float timeNorm = (float) column / (float) (columns - 1);
            if (timeNorm < timeRange.getStart() - timeRange.getLength() * 0.03f
                || timeNorm > timeRange.getEnd() + timeRange.getLength() * 0.03f)
                continue;

            AtsPartialTracePoint basePoint;
            basePoint.timeNorm = timeNorm;
            basePoint.freqNorm = freqNorm;
            basePoint.ampNorm = 0.0f;
            basePoint.active = true;

            AtsPartialTracePoint ridgePoint = basePoint;
            ridgePoint.ampNorm = juce::jlimit(0.0f, 1.15f, band.energyNorm[(size_t) column] * 1.15f);

            const auto basePos = positionForIsoPoint(basePoint, plot);
            const auto ridgePos = positionForIsoPoint(ridgePoint, plot);

            if (hasPrev)
            {
                const float ridgeDist = distanceToSegment(pos, prevRidge, ridgePos);
                const float baseDist = distanceToSegment(pos, prevBase, basePos);
                const float distance = juce::jmin(ridgeDist, baseDist);
                if (distance < bestDistance)
                {
                    bestDistance = distance;
                    bestBand = band.bandIndex;
                }
            }

            prevBase = basePos;
            prevRidge = ridgePos;
            hasPrev = true;
        }
    }

    return bestBand;
}

std::vector<int> SpectrogramPanel::residualBandsForRect(juce::Rectangle<float> rect,
                                                        juce::Rectangle<float> plot) const
{
    std::vector<int> bands;
    if (rect.isEmpty())
        return bands;

    for (const auto& band : traceSnapshot.residualBands)
    {
        const auto yRange = yRangeForBand(band, plot);
        const auto bandRect = juce::Rectangle<float>::leftTopRightBottom(plot.getX(),
                                                                         yRange.getStart(),
                                                                         plot.getRight(),
                                                                         yRange.getEnd());
        if (rect.intersects(bandRect))
            bands.push_back(band.bandIndex);
    }
    return bands;
}

int SpectrogramPanel::findNearestPartial(juce::Point<float> pos, float maxDistancePx) const
{
    if (!getPlotBounds().expanded(maxDistancePx).contains(pos))
        return -1;

    float bestDistance = maxDistancePx;
    int bestPartial = -1;
    const auto plot = getPlotBounds();
    for (const auto& trace : traceSnapshot.partials)
    {
        for (size_t i = 1; i < trace.points.size(); ++i)
        {
            const auto& prev = trace.points[i - 1];
            const auto& cur = trace.points[i];
            if (!prev.active || !cur.active)
                continue;

            const auto prevPos = isIsometricDisplayMode(mode) ? positionForIsoPoint(prev, plot) : positionForPoint(prev, plot);
            const auto curPos = isIsometricDisplayMode(mode) ? positionForIsoPoint(cur, plot) : positionForPoint(cur, plot);
            const float distance = distanceToSegment(pos, prevPos, curPos);
            if (distance < bestDistance)
            {
                bestDistance = distance;
                bestPartial = trace.partialIndex;
            }
        }
    }
    return bestPartial;
}

bool SpectrogramPanel::isPartialSelected(int partialIndex) const
{
    return std::find(selectedPartials.begin(), selectedPartials.end(), partialIndex) != selectedPartials.end();
}

bool SpectrogramPanel::hasResidualSelection() const
{
    return !selectedResidualBands.empty()
           && selectedResidualFirstFrame >= 0
           && selectedResidualLastFrame >= 0
           && traceSnapshot.frameCount > 0;
}

bool SpectrogramPanel::traceIntersectsLasso(const AtsPartialTrace& trace, juce::Rectangle<float> lasso) const
{
    if (lasso.isEmpty())
        return false;

    const auto plot = getPlotBounds();
    for (const auto& point : trace.points)
    {
        if (point.active && lasso.contains(isIsometricDisplayMode(mode) ? positionForIsoPoint(point, plot)
                                                                        : positionForPoint(point, plot)))
            return true;
    }
    return false;
}

float SpectrogramPanel::distanceToSegment(juce::Point<float> p,
                                          juce::Point<float> a,
                                          juce::Point<float> b)
{
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;
    const float len2 = dx * dx + dy * dy;
    if (len2 <= 1.0e-6f)
        return p.getDistanceFrom(a);

    const float t = juce::jlimit(0.0f, 1.0f, ((p.x - a.x) * dx + (p.y - a.y) * dy) / len2);
    return p.getDistanceFrom({ a.x + dx * t, a.y + dy * t });
}

void SpectrogramPanel::updateResidualToolbarVisibility()
{
    const bool show = isResidualDisplayMode(mode);
    for (auto* c : { static_cast<juce::Component*>(&residualDrawButton),
                     static_cast<juce::Component*>(&residualSmoothButton),
                     static_cast<juce::Component*>(&residualTiltSlider),
                     static_cast<juce::Component*>(&residualTiltButton),
                     static_cast<juce::Component*>(&residualCopyButton),
                     static_cast<juce::Component*>(&residualPasteButton),
                     static_cast<juce::Component*>(&residualSwapButton),
                     static_cast<juce::Component*>(&residualRedistButton),
                     static_cast<juce::Component*>(&residualDbLabel),
                     static_cast<juce::Component*>(&residualDbEditor) })
    {
        c->setVisible(show);
    }

    if (show)
    {
        residualPasteButton.setEnabled(processor.hasResidualBandClipboard());
        residualDbEditor.setReadOnly(!residualDbEditorActive);
        refreshHoveredDbReadout();
    }
    else
    {
        residualDrawButton.setToggleState(false, juce::dontSendNotification);
        residualDbEditorActive = false;
        drawingResidualEnvelope = false;
        envelopeDrawPoints.clear();
    }

    const bool partialUi = (mode == Mode::Partials || mode == Mode::Combined);
    for (auto* c : { static_cast<juce::Component*>(&partialFreqDrawButton),
                     static_cast<juce::Component*>(&partialSoloButton),
                     static_cast<juce::Component*>(&partialFreezeButton),
                     static_cast<juce::Component*>(&partialPruneButton),
                     static_cast<juce::Component*>(&partialTiltSlider),
                     static_cast<juce::Component*>(&partialTiltButton) })
        c->setVisible(partialUi);
    if (!partialUi)
    {
        partialFreqDrawButton.setToggleState(false, juce::dontSendNotification);
        drawingPartialFreq = false;
        partialFreqDrawPoints.clear();
    }

    const bool envMode = (mode == Mode::IsometricEnvelope || mode == Mode::IsometricEnvelopeRDC);
    envSourceABtn.setVisible(envMode);
    envSourceBBtn.setVisible(envMode);
    envSmoothLabel.setVisible(envMode);
    envSmoothSlider.setVisible(envMode);
    envSmoothButton.setVisible(envMode);
}

bool SpectrogramPanel::getSelectedFrameRange(int& firstFrame, int& lastFrame) const
{
    if (selectedResidualFirstFrame < 0 || selectedResidualLastFrame < 0
        || traceSnapshot.frameCount <= 0)
    {
        firstFrame = -1;
        lastFrame = -1;
        return false;
    }

    firstFrame = getSelectionFirstFrame();
    lastFrame = getSelectionLastFrame();
    return true;
}

int SpectrogramPanel::getSelectionFirstFrame() const
{
    return juce::jlimit(0, juce::jmax(0, traceSnapshot.frameCount - 1),
                        std::min(selectedResidualFirstFrame, selectedResidualLastFrame));
}

int SpectrogramPanel::getSelectionLastFrame() const
{
    return juce::jlimit(0, juce::jmax(0, traceSnapshot.frameCount - 1),
                        std::max(selectedResidualFirstFrame, selectedResidualLastFrame));
}

void SpectrogramPanel::showResidualEditError(const juce::String& error)
{
    if (error.isNotEmpty())
        juce::NativeMessageBox::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Residual band edit", error);
}

double SpectrogramPanel::dbFromPlotYInBand(int bandIndex, float y, juce::Rectangle<float> plot) const
{
    const auto band = std::find_if(traceSnapshot.residualBands.begin(),
                                   traceSnapshot.residualBands.end(),
                                   [bandIndex](const auto& b) { return b.bandIndex == bandIndex; });
    if (band == traceSnapshot.residualBands.end())
        return -72.0;

    const auto yRange = yRangeForBand(*band, plot);
    const float t = juce::jlimit(0.0f, 1.0f, (yRange.getEnd() - y) / juce::jmax(1.0f, yRange.getLength()));
    return juce::jlimit(-120.0, 0.0, -72.0 + (double) t * 72.0);
}

void SpectrogramPanel::refreshHoveredDbReadout()
{
    if (!isResidualDisplayMode(mode))
        return;

    if (hoveredResidualBand < 0 || hoveredResidualFrame < 0)
    {
        if (!residualDbEditorActive)
            residualDbEditor.setText({}, false);
        return;
    }

    double db = -120.0;
    if (processor.readResidualBandEnergyDb(traceSource, hoveredResidualBand, hoveredResidualFrame, db))
    {
        if (!residualDbEditorActive || !residualDbEditor.hasKeyboardFocus(true))
            residualDbEditor.setText(juce::String(db, 1), false);
    }
}

void SpectrogramPanel::applyResidualDbFromEditor()
{
    if (!isResidualDisplayMode(mode) || hoveredResidualBand < 0 || hoveredResidualFrame < 0)
        return;

    const double db = residualDbEditor.getText().getDoubleValue();
    juce::String error;
    if (!processor.writeResidualBandEnergyDb(traceSource, hoveredResidualBand, hoveredResidualFrame, db, error))
    {
        showResidualEditError(error);
        return;
    }

    residualDbEditorActive = false;
    residualDbEditor.setReadOnly(true);
    residualDbEditor.giveAwayKeyboardFocus();
    rebuildTraceSnapshot(true);
    refreshHoveredDbReadout();
    updateSelectionLabel();
    repaintDisplay();
}

void SpectrogramPanel::commitEnvelopeDraw()
{
    if (envelopeDrawBand < 0 || envelopeDrawPoints.empty())
        return;

    std::sort(envelopeDrawPoints.begin(),
              envelopeDrawPoints.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    const int firstFrame = envelopeDrawPoints.front().first;
    const int lastFrame = envelopeDrawPoints.back().first;
    std::vector<std::pair<int, double>> painted;
    painted.reserve((size_t) (lastFrame - firstFrame + 1));

    size_t segment = 0;
    for (int frame = firstFrame; frame <= lastFrame; ++frame)
    {
        while (segment + 1 < envelopeDrawPoints.size() && envelopeDrawPoints[segment + 1].first <= frame)
            ++segment;

        const auto& a = envelopeDrawPoints[segment];
        if (segment + 1 >= envelopeDrawPoints.size())
        {
            painted.push_back({ frame, a.second });
            continue;
        }

        const auto& b = envelopeDrawPoints[segment + 1];
        if (b.first == a.first)
        {
            painted.push_back({ frame, a.second });
            continue;
        }

        const double t = (double) (frame - a.first) / (double) (b.first - a.first);
        painted.push_back({ frame, a.second + t * (b.second - a.second) });
    }

    juce::String error;
    if (!processor.paintResidualBandEnvelope(traceSource,
                                             envelopeDrawBand,
                                             firstFrame,
                                             lastFrame,
                                             painted,
                                             error))
    {
        showResidualEditError(error);
        return;
    }

    rebuildTraceSnapshot(true);
}

void SpectrogramPanel::commitPartialFreqDraw()
{
    if (partialFreqDrawPartial < 0 || partialFreqDrawPoints.empty())
        return;

    std::sort(partialFreqDrawPoints.begin(),
              partialFreqDrawPoints.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    const int firstFrame = partialFreqDrawPoints.front().first;
    const int lastFrame = partialFreqDrawPoints.back().first;
    std::vector<std::pair<int, double>> painted;
    painted.reserve((size_t) (lastFrame - firstFrame + 1));

    size_t segment = 0;
    for (int frame = firstFrame; frame <= lastFrame; ++frame)
    {
        while (segment + 1 < partialFreqDrawPoints.size()
               && partialFreqDrawPoints[segment + 1].first <= frame)
            ++segment;

        const auto& a = partialFreqDrawPoints[segment];
        if (segment + 1 >= partialFreqDrawPoints.size())
        {
            painted.push_back({ frame, a.second });
            continue;
        }

        const auto& b = partialFreqDrawPoints[segment + 1];
        if (b.first == a.first)
        {
            painted.push_back({ frame, a.second });
            continue;
        }

        const double t = (double) (frame - a.first) / (double) (b.first - a.first);
        painted.push_back({ frame, a.second + t * (b.second - a.second) });
    }

    juce::String error;
    if (!processor.paintPartialFrequency(traceSource,
                                         partialFreqDrawPartial,
                                         firstFrame,
                                         lastFrame,
                                         painted,
                                         error))
    {
        juce::NativeMessageBox::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                    "Paint Partial Frequency", error);
        return;
    }

    rebuildTraceSnapshot(true);
}

void SpectrogramPanel::runSmoothEnvelope()
{
    if (traceSnapshot.partials.empty() || traceSnapshot.frameCount <= 0)
    {
        juce::NativeMessageBox::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Smooth envelope",
                                                    "Analyze a source before smoothing its envelope.");
        return;
    }

    // Operate on the dragged time slab if one is set, otherwise the whole visible window.
    juce::Range<float> tr = visibleTimeRange();
    if (envRangeStartNorm >= 0.0f && envRangeEndNorm >= 0.0f
        && std::abs(envRangeEndNorm - envRangeStartNorm) > 0.001f)
    {
        tr = { juce::jmin(envRangeStartNorm, envRangeEndNorm),
               juce::jmax(envRangeStartNorm, envRangeEndNorm) };
    }
    const int last = juce::jmax(0, traceSnapshot.frameCount - 1);
    int f0 = juce::jlimit(0, last, (int) std::floor(juce::jlimit(0.0f, 1.0f, tr.getStart()) * (float) last));
    int f1 = juce::jlimit(0, last, (int) std::ceil(juce::jlimit(0.0f, 1.0f, tr.getEnd()) * (float) last));
    if (f1 < f0)
        std::swap(f0, f1);

    juce::String error;
    if (!processor.smoothPartialEnvelope(traceSource, f0, f1, envSmoothSlider.getValue(), error))
    {
        juce::NativeMessageBox::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Smooth envelope",
                                                    error.isEmpty() ? juce::String("Could not smooth envelope.") : error);
        return;
    }

    rebuildTraceSnapshot(true);
    repaintDisplay();
}

void SpectrogramPanel::runSmoothOnSelection()
{
    if (!hasResidualSelection())
    {
        showResidualEditError("Select one or more residual bands and a frame range.");
        return;
    }

    juce::String error;
    if (!processor.smoothResidualBands(traceSource,
                                       selectedResidualBands,
                                       getSelectionFirstFrame(),
                                       getSelectionLastFrame(),
                                       3,
                                       error))
    {
        showResidualEditError(error);
        return;
    }

    rebuildTraceSnapshot(true);
    repaintDisplay();
}

void SpectrogramPanel::runTiltOnSelection()
{
    if (!hasResidualSelection())
    {
        showResidualEditError("Select two or more bands for tilt, or one band with zero tilt.");
        return;
    }

    const double tilt = residualTiltSlider.getValue();
    if (std::abs(tilt) < 1.0e-6)
        return;

    juce::String error;
    if (!processor.tiltResidualBands(traceSource,
                                     selectedResidualBands,
                                     getSelectionFirstFrame(),
                                     getSelectionLastFrame(),
                                     tilt,
                                     error))
    {
        showResidualEditError(error);
        return;
    }

    rebuildTraceSnapshot(true);
    repaintDisplay();
}

void SpectrogramPanel::runPartialTilt()
{
    const double tilt = partialTiltSlider.getValue();
    if (std::abs(tilt) < 1.0e-6)
        return;

    int f0 = 0, f1 = 0;
    if (!envBoxFrameRange(f0, f1))
        return;

    double loHz = 0.0, hiHz = 0.0;
    (void) envBoxFreqRangeHz(loHz, hiHz);

    juce::String error;
    if (!processor.tiltPartialEnvelope(traceSource, f0, f1, loHz, hiHz, tilt, error))
    {
        juce::NativeMessageBox::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                    "Tilt Partial Envelope",
                                                    error.isEmpty() ? "Could not apply tilt." : error);
        return;
    }

    rebuildTraceSnapshot(true);
    repaintDisplay();
}

void SpectrogramPanel::runPartialSolo()
{
    if (selectedPartials.empty())
    {
        juce::NativeMessageBox::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Solo Partials",
                                                    "Select one or more partials before soloing.");
        return;
    }

    juce::String error;
    if (!processor.playSelectedPartialsPreview(traceSource, selectedPartials, error))
    {
        if (error.isNotEmpty())
            juce::NativeMessageBox::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Solo Partials", error);
    }
}

void SpectrogramPanel::runPartialFreeze()
{
    if (traceSnapshot.frameCount <= 0)
    {
        juce::NativeMessageBox::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Freeze Partials",
                                                    "No model loaded.");
        return;
    }

    // Use the playhead position if preview is active; otherwise use view center.
    float centerNorm = (visibleTimeRange().getStart() + visibleTimeRange().getEnd()) * 0.5f;
    const float playheadNorm = processor.getPreviewPlayheadNormForTraceSource(traceSource);
    if (playheadNorm >= 0.0f)
        centerNorm = playheadNorm;

    const int frame = juce::jlimit(0,
                                   traceSnapshot.frameCount - 1,
                                   juce::roundToInt(centerNorm * (float) (traceSnapshot.frameCount - 1)));

    juce::String error;
    if (!processor.freezePartialAtFrame(traceSource, frame, error))
    {
        juce::NativeMessageBox::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Freeze Partials",
                                                    error.isEmpty() ? "Could not freeze." : error);
        return;
    }

    rebuildTraceSnapshot(true);
    repaintDisplay();
}

void SpectrogramPanel::runCopyBand()
{
    if (!hasResidualSelection() || selectedResidualBands.size() != 1)
    {
        showResidualEditError("Select exactly one band and frame range to copy.");
        return;
    }

    juce::String error;
    if (!processor.copyResidualBandToClipboard(traceSource,
                                               selectedResidualBands.front(),
                                               getSelectionFirstFrame(),
                                               getSelectionLastFrame(),
                                               error))
    {
        showResidualEditError(error);
        return;
    }

    residualPasteButton.setEnabled(true);
    updateSelectionLabel();
}

void SpectrogramPanel::runPasteBand()
{
    if (!hasResidualSelection() || selectedResidualBands.size() != 1)
    {
        showResidualEditError("Select exactly one target band and frame range to paste.");
        return;
    }

    juce::String error;
    if (!processor.pasteResidualBandFromClipboard(traceSource,
                                                  selectedResidualBands.front(),
                                                  getSelectionFirstFrame(),
                                                  getSelectionLastFrame(),
                                                  error))
    {
        showResidualEditError(error);
        return;
    }

    rebuildTraceSnapshot(true);
    repaintDisplay();
}

void SpectrogramPanel::runSwapBands()
{
    if (!hasResidualSelection() || selectedResidualBands.size() != 2)
    {
        showResidualEditError("Select exactly two bands (Shift/Cmd-click) over a frame range to swap.");
        return;
    }

    std::vector<int> bands = selectedResidualBands;
    std::sort(bands.begin(), bands.end());
    juce::String error;
    if (!processor.swapResidualBands(traceSource,
                                     bands[0],
                                     bands[1],
                                     getSelectionFirstFrame(),
                                     getSelectionLastFrame(),
                                     error))
    {
        showResidualEditError(error);
        return;
    }

    rebuildTraceSnapshot(true);
    repaintDisplay();
}

void SpectrogramPanel::runPartialPrune()
{
    if (traceSnapshot.partials.empty())
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                               "Prune Partials", "No partials to prune.");
        return;
    }

    juce::AlertWindow dialog("Prune Weak Partials",
                             "Select all partials whose peak amplitude is below this threshold "
                             "(as % of the loudest partial). Press Delete to remove them after.",
                             juce::AlertWindow::QuestionIcon);
    dialog.addTextEditor("threshold", "10", "Threshold (% of peak):");
    dialog.addButton("Select", 1);
    dialog.addButton("Cancel", 0);

    if (dialog.runModalLoop() != 1)
        return;

    const double pct = dialog.getTextEditorContents("threshold").getDoubleValue();
    if (pct <= 0.0 || pct > 100.0)
        return;

    const float threshold = (float) (pct / 100.0);
    selectedPartials.clear();
    for (const auto& trace : traceSnapshot.partials)
    {
        if (trace.peakAmpNorm < threshold)
            selectedPartials.push_back(trace.partialIndex);
    }

    updateSelectionLabel();
    repaintDisplay();
}

void SpectrogramPanel::runRedistBands()
{
    if (!hasResidualSelection() || selectedResidualBands.size() < 2)
    {
        showResidualEditError("Select at least two bands (Shift/Cmd-click) over a frame range to redistribute.");
        return;
    }

    juce::String error;
    if (!processor.autoRedistributeResidualBands(traceSource,
                                                  selectedResidualBands,
                                                  getSelectionFirstFrame(),
                                                  getSelectionLastFrame(),
                                                  error))
    {
        showResidualEditError(error);
        return;
    }

    rebuildTraceSnapshot(true);
    repaintDisplay();
}

void SpectrogramPanel::paintBandLabelGutter(juce::Graphics& g,
                                            juce::Rectangle<float> gutter,
                                            juce::Rectangle<float> plot)
{
    if (gutter.isEmpty() || traceSnapshot.residualBands.empty())
        return;

    g.setColour(juce::Colour(0xff0a1210));
    g.fillRect(gutter);
    g.setColour(juce::Colour(0xff2b4a3d));
    g.drawVerticalLine(juce::roundToInt(gutter.getRight()), gutter.getY(), gutter.getBottom());

    g.setFont(juce::Font(9.0f));
    for (const auto& band : traceSnapshot.residualBands)
    {
        const auto yRange = yRangeForBand(band, plot);
        if (yRange.getEnd() < plot.getY() - 2.0f || yRange.getStart() > plot.getBottom() + 2.0f)
            continue;

        const float centreY = (yRange.getStart() + yRange.getEnd()) * 0.5f;
        const bool hot = band.bandIndex == hoveredResidualBand
                         || std::find(selectedResidualBands.begin(),
                                      selectedResidualBands.end(),
                                      band.bandIndex) != selectedResidualBands.end();
        g.setColour(hot ? juce::Colour(0xffd8fff0) : juce::Colour(0xff7aa896));
        g.drawText("B" + juce::String(band.bandIndex),
                   juce::Rectangle<float>(gutter.getX() + 2.0f, centreY - 8.0f, 18.0f, 10.0f).toNearestInt(),
                   juce::Justification::centredLeft);
        g.drawText(juce::String(band.lowHz, 0),
                   juce::Rectangle<float>(gutter.getX() + 2.0f, centreY + 1.0f, gutter.getWidth() - 4.0f, 10.0f)
                       .toNearestInt(),
                   juce::Justification::centredLeft);
    }
}
