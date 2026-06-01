#include "SessionTabPanel.h"

#include "PluginProcessor.h"
#include "PresetLibrary.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace
{
int scaledUi(int px)
{
    if (auto* disp = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        return juce::roundToInt((float) px * disp->scale);
    return px;
}

float getPlainParameter(AtsAudioProcessor& processor, const juce::String& parameterID, float fallback)
{
    if (auto* value = processor.apvts.getRawParameterValue(parameterID))
        return value->load();
    return fallback;
}

void setPlainParameter(AtsAudioProcessor& processor, const juce::String& parameterID, float value)
{
    if (auto* p = processor.apvts.getParameter(parameterID))
        p->setValueNotifyingHost(processor.apvts.getParameterRange(parameterID).convertTo0to1(value));
}

void setBoolParameter(AtsAudioProcessor& processor, const juce::String& parameterID, bool value)
{
    if (auto* p = processor.apvts.getParameter(parameterID))
        p->setValueNotifyingHost(value ? 1.0f : 0.0f);
}

juce::String compactHash(juce::String hash)
{
    hash = hash.trim();
    if (hash.length() <= 38)
        return hash;
    return hash.substring(0, 35) + "...";
}

juce::String formatSourceMeta(const AtsMorphSourceSlotInfo& info)
{
    if (!info.hasModel)
        return "Empty";

    const auto name = info.sourceName.isNotEmpty() ? info.sourceName : info.slotName;
    juce::String text = name;
    text << "\n" << juce::String(info.durationSeconds, 2) << "s | "
         << juce::String(info.sampleRate, 0) << " Hz | "
         << juce::String(info.partialCount) << " partials";
    text << "\n" << (info.hasResidual ? "Residual yes" : "Residual no")
         << " | gain " << juce::String(info.sourceGain, 2)
         << " | offset " << juce::String(info.timeOffsetSeconds, 2) << "s";
    if (info.sourceHash.isNotEmpty())
        text << "\n" << compactHash(info.sourceHash);
    return text;
}

juce::String formatSourceOverview(const AtsMorphSourceSlotInfo& info)
{
    if (!info.hasModel)
        return "Overview empty";

    return "Tracks " + juce::String(info.partialCount)
           + " | frames " + juce::String(info.frameCount)
           + " | markers " + juce::String(info.hasTransientMarkers ? "yes" : "none");
}

juce::Colour snapshotStateColour(AtsMorphSnapshotUiState state)
{
    switch (state)
    {
        case AtsMorphSnapshotUiState::Ready: return juce::Colour(0xff8fd6a4);
        case AtsMorphSnapshotUiState::Stale: return juce::Colour(0xfff0c06a);
        case AtsMorphSnapshotUiState::Rebuilding: return juce::Colour(0xff9fd0ff);
        case AtsMorphSnapshotUiState::Warning:
        default: return juce::Colour(0xffff8a7a);
    }
}

juce::String analysisTypeName(int index)
{
    switch (juce::jlimit(0, 3, index))
    {
        case 0: return "1 amp+freq";
        case 1: return "2 +phase";
        case 2: return "3 +noise";
        case 3:
        default: return "4 full";
    }
}

juce::String formatAnalysisSummary(AtsAudioProcessor& processor)
{
    const int typeIndex = juce::roundToInt(getPlainParameter(processor,
                                                            ats_param::kAnalysisType,
                                                            (float) (ATSA_TYPE - 1)));
    const auto peak = getPlainParameter(processor, ats_param::kAnalysisLowestMag, (float) ATSA_LMAG);
    const auto hop = getPlainParameter(processor, ats_param::kAnalysisHopSize, (float) ATSA_HSIZE);
    const auto cycles = getPlainParameter(processor, ats_param::kAnalysisWinCycles, (float) ATSA_WCYCLES);

    return "Analysis: " + analysisTypeName(typeIndex)
           + " | Peak " + juce::String(peak, 0) + " dB"
           + " | Hop " + juce::String(hop, 2)
           + " | Win " + juce::String(cycles, 1) + " cyc";
}
} // namespace

void MorphBandStrip::setBands(std::vector<BandView> nextBands, int selectedBand)
{
    bands = std::move(nextBands);
    selectedBandIndex = selectedBand;
    repaint();
}

void MorphBandStrip::paint(juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat().reduced(1.0f);
    g.setColour(juce::Colour(0xff111923));
    g.fillRoundedRectangle(area, 4.0f);
    g.setColour(juce::Colour(0xff2b3b4d));
    g.drawRoundedRectangle(area, 4.0f, 1.0f);

    if (bands.empty() || area.getWidth() <= 2.0f)
    {
        g.setFont(10.0f);
        g.setColour(juce::Colour(0xffb8c7d9));
        g.drawText("No bands", getLocalBounds(), juce::Justification::centred);
        return;
    }

    double maxPower = 1.0e-12;
    for (const auto& band : bands)
        maxPower = std::max(maxPower, std::max(band.sourcePower, std::max(band.targetPower, band.outputPower)));

    const float bandW = area.getWidth() / (float) bands.size();
    for (size_t i = 0; i < bands.size(); ++i)
    {
        const auto& band = bands[i];
        auto cell = juce::Rectangle<float>(area.getX() + bandW * (float) i,
                                           area.getY(),
                                           juce::jmax(1.0f, bandW - 1.0f),
                                           area.getHeight());
        const float sourceH = (float) juce::jlimit(0.0, 1.0, band.sourcePower / maxPower);
        const float targetH = (float) juce::jlimit(0.0, 1.0, band.targetPower / maxPower);
        const float outputH = (float) juce::jlimit(0.0, 1.0, band.outputPower / maxPower);

        g.setColour(band.warning ? juce::Colour(0xff5a3028) : juce::Colour(0xff1b2634));
        g.fillRect(cell);
        g.setColour(juce::Colour(0xff6a9cff));
        g.fillRect(cell.removeFromBottom(area.getHeight() * sourceH * 0.33f));
        g.setColour(juce::Colour(0xffa8d66d));
        g.fillRect(juce::Rectangle<float>(cell.getX(), area.getBottom() - area.getHeight() * targetH * 0.66f,
                                          cell.getWidth(), area.getHeight() * targetH * 0.33f));
        g.setColour(juce::Colour(0xffffcc66));
        g.fillRect(juce::Rectangle<float>(cell.getX(), area.getBottom() - area.getHeight() * outputH,
                                          cell.getWidth(), area.getHeight() * outputH * 0.25f));

        if (band.unmatchedCount > 0)
        {
            g.setColour(juce::Colour(0xffff8a80));
            g.fillRect(juce::Rectangle<float>(area.getX() + bandW * (float) i,
                                              area.getY(),
                                              juce::jmax(1.0f, bandW - 1.0f),
                                              2.0f));
        }

        if (band.bandIndex == selectedBandIndex)
        {
            g.setColour(juce::Colour(0xfff0f0f5));
            g.drawRect(juce::Rectangle<float>(area.getX() + bandW * (float) i,
                                              area.getY(),
                                              juce::jmax(1.0f, bandW - 1.0f),
                                              area.getHeight()),
                       1.5f);
        }
    }
}

void MorphBandStrip::mouseDown(const juce::MouseEvent& e)
{
    if (bands.empty())
        return;

    const auto area = getLocalBounds().toFloat().reduced(1.0f);
    const int index = juce::jlimit(0,
                                   (int) bands.size() - 1,
                                   (int) ((e.position.x - area.getX()) * (float) bands.size()
                                          / juce::jmax(1.0f, area.getWidth())));
    selectedBandIndex = bands[(size_t) index].bandIndex;
    if (onBandSelected)
        onBandSelected(selectedBandIndex);
    repaint();
}

MorphTimelineEditor::MorphTimelineEditor()
{
    morphModeBtn.setRadioGroupId(101);
    hybridModeBtn.setRadioGroupId(101);
    morphModeBtn.setToggleState(true, juce::dontSendNotification);

    morphModeBtn.setColour(juce::ToggleButton::textColourId, juce::Colour(0xffb8d4ff));
    hybridModeBtn.setColour(juce::ToggleButton::textColourId, juce::Colour(0xffd08030));

    morphModeBtn.onStateChange = [this] {
        if (morphModeBtn.getToggleState())
        {
            newPointKind = MorphTimelinePointKind::Morph;
            selectedPoint = -1;
            repaint();
        }
    };
    hybridModeBtn.onStateChange = [this] {
        if (hybridModeBtn.getToggleState())
        {
            newPointKind = MorphTimelinePointKind::Hybrid;
            selectedPoint = -1;
            repaint();
        }
    };

    addAndMakeVisible(morphModeBtn);
    addAndMakeVisible(hybridModeBtn);
}

void MorphTimelineEditor::resized()
{
    auto header = getLocalBounds().removeFromTop(kHeaderH).reduced(4, 2);
    // "Add:" label occupies the left ~28px implicitly in paint; buttons go right of it.
    header.removeFromLeft(28);
    morphModeBtn.setBounds(header.removeFromLeft(54));
    hybridModeBtn.setBounds(header.removeFromLeft(58));
}

juce::Rectangle<float> MorphTimelineEditor::getPlotArea() const
{
    auto area = getLocalBounds().toFloat().reduced(1.5f);
    area.removeFromTop((float) kHeaderH);
    return area.reduced(8.0f, 7.0f);
}

void MorphTimelineEditor::setPoints(std::vector<AtsMorphTimelinePoint> nextPoints)
{
    points = std::move(nextPoints);
    if (points.empty())
        points = { { 0.0, 0.0 }, { 1.0, 1.0 } };

    std::sort(points.begin(), points.end(), [](const auto& a, const auto& b) {
        if (a.timeNorm != b.timeNorm)
            return a.timeNorm < b.timeNorm;
        return static_cast<int>(a.kind) < static_cast<int>(b.kind);
    });
    for (auto& point : points)
    {
        point.timeNorm = juce::jlimit(0.0, 1.0, point.timeNorm);
        point.morphAmount = juce::jlimit(0.0, 1.0, point.morphAmount);
    }
    selectedPoint = juce::jlimit(-1, (int) points.size() - 1, selectedPoint);
    repaint();
}

void MorphTimelineEditor::paint(juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat().reduced(1.5f);
    g.setColour(juce::Colour(0xff111923));
    g.fillRoundedRectangle(area, 5.0f);
    g.setColour(juce::Colour(0xff2b3b4d));
    g.drawRoundedRectangle(area, 5.0f, 1.0f);

    // Header strip label
    g.setFont(juce::Font(10.0f, juce::Font::bold));
    g.setColour(juce::Colour(0xff8899aa));
    g.drawText("Add:", getLocalBounds().removeFromTop(kHeaderH).withTrimmedLeft(6),
               juce::Justification::centredLeft);

    auto plot = getPlotArea();
    if (plot.getWidth() <= 2.0f || plot.getHeight() <= 2.0f)
        return;

    g.setColour(juce::Colour(0x445f7895));
    for (int i = 1; i < 4; ++i)
    {
        const float x = plot.getX() + plot.getWidth() * (float) i / 4.0f;
        g.drawVerticalLine(juce::roundToInt(x), plot.getY(), plot.getBottom());
    }
    g.drawHorizontalLine(juce::roundToInt(plot.getCentreY()), plot.getX(), plot.getRight());

    g.setFont(10.0f);
    g.setColour(juce::Colour(0xffb8c7d9));
    g.drawText("A",
               juce::roundToInt(plot.getX()),
               juce::roundToInt(plot.getBottom() - 13.0f),
               16,
               13,
               juce::Justification::centredLeft);
    g.drawText("B",
               juce::roundToInt(plot.getX()),
               juce::roundToInt(plot.getY()),
               16,
               13,
               juce::Justification::centredLeft);

    if (points.empty())
        return;

    const bool editingHybrid = newPointKind == MorphTimelinePointKind::Hybrid;
    paintLane(g, editingHybrid ? MorphTimelinePointKind::Morph : MorphTimelinePointKind::Hybrid, false);
    paintLane(g, newPointKind, true);
}

void MorphTimelineEditor::mouseDown(const juce::MouseEvent& e)
{
    // Ignore clicks in the header strip; those are handled by the child buttons.
    if (e.position.y < (float) kHeaderH)
        return;

    if (points.empty())
        setPoints({});

    const int nearest = findNearestPoint(e.position, e.mods.isPopupMenu() ? false : true);
    if (e.mods.isPopupMenu() && nearest >= 0)
    {
        const bool isHybrid = points[(size_t) nearest].kind == MorphTimelinePointKind::Hybrid;
        const auto& nearestPoint = points[(size_t) nearest];
        const bool canRemove = nearestPoint.kind == MorphTimelinePointKind::Hybrid
                               || (nearestPoint.timeNorm > 1.0e-6
                                   && nearestPoint.timeNorm < 1.0 - 1.0e-6);

        juce::PopupMenu menu;
        menu.addItem(1, isHybrid ? "Make Morph"  : "Make Hybrid",  true, false);
        if (canRemove)
            menu.addItem(2, "Remove point", true, false);

        menu.showMenuAsync(juce::PopupMenu::Options{}.withTargetComponent(this),
            [this, nearest, isHybrid](int result)
            {
                if (result == 1 && juce::isPositiveAndBelow(nearest, (int) points.size()))
                {
                    points[(size_t) nearest].kind = isHybrid ? MorphTimelinePointKind::Morph
                                                             : MorphTimelinePointKind::Hybrid;
                    notifyChange();
                    repaint();
                }
                else if (result == 2 && juce::isPositiveAndBelow(nearest, (int) points.size()))
                {
                    const auto& point = points[(size_t) nearest];
                    const bool canRemovePoint = point.kind == MorphTimelinePointKind::Hybrid
                                                || (point.timeNorm > 1.0e-6
                                                    && point.timeNorm < 1.0 - 1.0e-6);
                    if (canRemovePoint)
                    {
                        points.erase(points.begin() + nearest);
                        selectedPoint = -1;
                        notifyChange();
                        repaint();
                    }
                }
            });
        return;
    }

    selectedPoint = nearest;
    if (selectedPoint < 0)
    {
        points.push_back(pointFromPosition(e.position));
        std::sort(points.begin(), points.end(), [](const auto& a, const auto& b) {
            if (a.timeNorm != b.timeNorm)
                return a.timeNorm < b.timeNorm;
            return static_cast<int>(a.kind) < static_cast<int>(b.kind);
        });
        selectedPoint = findNearestPoint(e.position, true);
    }

    editing = true;
    updatePointFromMouse(e);
}

void MorphTimelineEditor::mouseDrag(const juce::MouseEvent& e)
{
    updatePointFromMouse(e);
}

void MorphTimelineEditor::mouseUp(const juce::MouseEvent&)
{
    editing = false;
}

void MorphTimelineEditor::mouseDoubleClick(const juce::MouseEvent& e)
{
    const int nearest = findNearestPoint(e.position, true);
    if (nearest >= 0)
    {
        const auto& point = points[(size_t) nearest];
        const bool canRemovePoint = point.kind == MorphTimelinePointKind::Hybrid
                                    || (point.timeNorm > 1.0e-6 && point.timeNorm < 1.0 - 1.0e-6);
        if (canRemovePoint)
        {
            points.erase(points.begin() + nearest);
            selectedPoint = -1;
            notifyChange();
            repaint();
        }
    }
}

int MorphTimelineEditor::findNearestPoint(juce::Point<float> pos, bool selectedLaneOnly) const
{
    int nearest = -1;
    float nearestDistance = 10.0f;
    for (size_t i = 0; i < points.size(); ++i)
    {
        if (selectedLaneOnly && points[i].kind != newPointKind)
            continue;

        const auto p = positionForPoint(points[i]);
        const float distance = p.getDistanceFrom(pos);
        if (distance <= nearestDistance)
        {
            nearest = (int) i;
            nearestDistance = distance;
        }
    }
    return nearest;
}

AtsMorphTimelinePoint MorphTimelineEditor::pointFromPosition(juce::Point<float> pos) const
{
    const auto plot = getPlotArea();
    AtsMorphTimelinePoint point;
    point.timeNorm   = juce::jlimit(0.0, 1.0, (double) ((pos.x - plot.getX()) / juce::jmax(1.0f, plot.getWidth())));
    point.morphAmount = juce::jlimit(0.0, 1.0, (double) ((plot.getBottom() - pos.y) / juce::jmax(1.0f, plot.getHeight())));
    point.kind = newPointKind;
    return point;
}

juce::Point<float> MorphTimelineEditor::positionForPoint(const AtsMorphTimelinePoint& point) const
{
    const auto plot = getPlotArea();
    return { plot.getX() + plot.getWidth()  * (float) juce::jlimit(0.0, 1.0, point.timeNorm),
             plot.getBottom() - plot.getHeight() * (float) juce::jlimit(0.0, 1.0, point.morphAmount) };
}

void MorphTimelineEditor::paintLane(juce::Graphics& g, MorphTimelinePointKind kind, bool selectedLane) const
{
    std::vector<std::pair<size_t, AtsMorphTimelinePoint>> lane;
    lane.reserve(points.size());
    for (size_t i = 0; i < points.size(); ++i)
    {
        if (points[i].kind == kind)
            lane.push_back({ i, points[i] });
    }

    if (lane.empty())
        return;

    std::sort(lane.begin(), lane.end(), [](const auto& a, const auto& b) {
        return a.second.timeNorm < b.second.timeNorm;
    });

    const bool isHybrid = kind == MorphTimelinePointKind::Hybrid;
    const auto baseColour = isHybrid ? juce::Colour(0xffd08030) : juce::Colour(0xff75a7ff);
    const float pathAlpha = selectedLane ? 0.95f : 0.22f;
    const float pointAlpha = selectedLane ? 1.0f : 0.30f;

    juce::Path path;
    auto start = lane.front().second;
    start.timeNorm = 0.0;
    path.startNewSubPath(positionForPoint(start));
    for (const auto& item : lane)
        path.lineTo(positionForPoint(item.second));
    auto end = lane.back().second;
    end.timeNorm = 1.0;
    path.lineTo(positionForPoint(end));

    g.setColour(baseColour.withAlpha(pathAlpha));
    g.strokePath(path, juce::PathStrokeType(selectedLane ? 2.2f : 1.2f));

    for (const auto& item : lane)
    {
        const auto p = positionForPoint(item.second);
        const bool selected = selectedLane && (int) item.first == selectedPoint;

        if (isHybrid)
        {
            constexpr float r = 5.5f;
            juce::Path diamond;
            diamond.startNewSubPath(p.x,     p.y - r);
            diamond.lineTo(p.x + r, p.y);
            diamond.lineTo(p.x,     p.y + r);
            diamond.lineTo(p.x - r, p.y);
            diamond.closeSubPath();
            g.setColour((selected ? juce::Colour(0xfffffb70) : juce::Colour(0xffd08030)).withAlpha(pointAlpha));
            g.fillPath(diamond);
            g.setColour(juce::Colour(0xff12121a).withAlpha(selectedLane ? 1.0f : 0.35f));
            g.strokePath(diamond, juce::PathStrokeType(selectedLane ? 1.0f : 0.7f));
        }
        else
        {
            g.setColour((selected ? juce::Colour(0xffffd27a) : juce::Colour(0xffb8d4ff)).withAlpha(pointAlpha));
            g.fillEllipse(p.x - 4.5f, p.y - 4.5f, 9.0f, 9.0f);
            g.setColour(juce::Colour(0xff12121a).withAlpha(selectedLane ? 1.0f : 0.35f));
            g.drawEllipse(p.x - 4.5f, p.y - 4.5f, 9.0f, 9.0f, selectedLane ? 1.0f : 0.7f);
        }
    }
}

void MorphTimelineEditor::updatePointFromMouse(const juce::MouseEvent& e)
{
    if (!juce::isPositiveAndBelow(selectedPoint, (int) points.size()))
        return;

    auto point = pointFromPosition(e.position);
    const auto selectedKind = points[(size_t) selectedPoint].kind;
    const double currentTime = points[(size_t) selectedPoint].timeNorm;
    if (currentTime <= 1.0e-6)
        point.timeNorm = 0.0;
    else if (currentTime >= 1.0 - 1.0e-6)
        point.timeNorm = 1.0;
    else
    {
        double minTime = 0.0;
        double maxTime = 1.0;
        for (int i = selectedPoint - 1; i >= 0; --i)
        {
            if (points[(size_t) i].kind == selectedKind)
            {
                minTime = points[(size_t) i].timeNorm + 0.005;
                break;
            }
        }
        for (int i = selectedPoint + 1; i < (int) points.size(); ++i)
        {
            if (points[(size_t) i].kind == selectedKind)
            {
                maxTime = points[(size_t) i].timeNorm - 0.005;
                break;
            }
        }
        point.timeNorm = juce::jlimit(minTime, maxTime, point.timeNorm);
    }

    point.kind = selectedKind; // preserve kind through drag
    points[(size_t) selectedPoint] = point;
    notifyChange();
    repaint();
}

void MorphTimelineEditor::notifyChange()
{
    if (onPointsChanged)
        onPointsChanged(points);
}

SessionTabPanel::SessionTabPanel(AtsAudioProcessor& p)
    : audioProcessor(p)
    , spectroPanel(p)
{
    loadButton.setExplicitFocusOrder(1);
    loadButton.setHelpText("Choose a mono or stereo WAV file to analyze.");
    analyzeButton.setExplicitFocusOrder(2);
    analyzeButton.setHelpText("Run ATS analysis on the imported audio (CPU heavy, background thread).");
    currentPlayButton.setExplicitFocusOrder(3);
    currentPlayButton.setHelpText("Render and play the current analyzed ATS model, including partial edits and deletes.");
    cancelButton.setExplicitFocusOrder(4);
    cancelButton.setHelpText("Stop the current analysis or resynthesis job.");
    presetCombo.setExplicitFocusOrder(5);
    presetCombo.setHelpText("Factory parameter curves or full-state user presets from disk.");
    savePresetButton.setExplicitFocusOrder(6);
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

    statusLabel.setJustificationType(juce::Justification::centredLeft);
    statusLabel.setFont(juce::Font(13.5f, juce::Font::bold));
    statusLabel.setExplicitFocusOrder(6);
    statusLabel.setHelpText("Current high-level plugin state.");
    addAndMakeVisible(statusLabel);

    errorLabel.setColour(juce::Label::textColourId, juce::Colour(0xffff8a80));
    errorLabel.setJustificationType(juce::Justification::topLeft);
    errorLabel.setExplicitFocusOrder(7);
    errorLabel.setHelpText("Most recent error from analysis, resynthesis, or file I/O.");
    addAndMakeVisible(errorLabel);

    cpuWarningLabel.setJustificationType(juce::Justification::centredLeft);
    cpuWarningLabel.setFont(juce::Font(11.0f, juce::Font::italic));
    cpuWarningLabel.setColour(juce::Label::textColourId, juce::Colour(0xffffcc88));
    cpuWarningLabel.setExplicitFocusOrder(8);
    cpuWarningLabel.setHelpText("Shown when partial and frame counts exceed the documented soft limit.");
    addAndMakeVisible(cpuWarningLabel);

    addAndMakeVisible(progressBar);

    analysisSummaryLabel.setJustificationType(juce::Justification::centredLeft);
    analysisSummaryLabel.setFont(juce::Font(11.0f, juce::Font::bold));
    analysisSummaryLabel.setMinimumHorizontalScale(0.64f);
    analysisSummaryLabel.setColour(juce::Label::textColourId, juce::Colour(0xffd8e4f2));
    analysisSummaryLabel.setHelpText("Current analysis settings used by Analyze, Analyze Source A, and Analyze Source B.");
    addAndMakeVisible(analysisSummaryLabel);

    loadButton.onClick = [this] { pickFile(); };
    addAndMakeVisible(loadButton);

    analyzeButton.onClick = [this] { audioProcessor.startAnalysis(); };
    addAndMakeVisible(analyzeButton);

    currentPlayButton.onClick = [this] {
        if (audioProcessor.isPreviewPlaying()
            && audioProcessor.getPreviewPlaybackKind() == AtsPreviewPlaybackKind::MorphPreview)
        {
            audioProcessor.stopPreviewPlayback();
            refreshFromProcessor();
            return;
        }

        juce::String err;
        if (!audioProcessor.playCurrentModelPreview(err))
            juce::NativeMessageBox::showMessageBoxAsync(
                juce::AlertWindow::WarningIcon,
                "Play current model",
                err.isEmpty() ? juce::String("Could not play the current model.") : err);
        refreshFromProcessor();
    };
    addAndMakeVisible(currentPlayButton);

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
    const juce::String layerGainHelp =
        "Applied in real time to layer-mix, partial-only, and residual-only previews. "
        "Classic combined Both playback bypasses separate layer gains.";
    const juce::String partialGainHelp = "Partials path gain (SPARAMS amp). " + layerGainHelp;
    const juce::String residualGainHelp = "Residual / noise-band gain (SPARAMS ramp). " + layerGainHelp;
    partGainSlider.setHelpText(partialGainHelp);
    partGainSlider.setTooltip(partialGainHelp);
    resGainSlider.setHelpText(residualGainHelp);
    resGainSlider.setTooltip(residualGainHelp);
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

    auto setupSlotLabel = [](juce::Label& l, const juce::String& text, bool heading) {
        l.setText(text, juce::dontSendNotification);
        l.setJustificationType(heading ? juce::Justification::centredLeft : juce::Justification::topLeft);
        l.setFont(juce::Font(heading ? 12.5f : 10.5f, heading ? juce::Font::bold : juce::Font::plain));
        l.setMinimumHorizontalScale(0.72f);
    };
    setupSlotLabel(sourceALabel, "Source A", true);
    setupSlotLabel(sourceBLabel, "Source B", true);
    setupSlotLabel(sourceAMetaLabel, "Empty", false);
    setupSlotLabel(sourceBMetaLabel, "Empty", false);
    setupSlotLabel(sourceAOverviewLabel, "Overview empty", false);
    setupSlotLabel(sourceBOverviewLabel, "Overview empty", false);
    sourceAOverviewLabel.setColour(juce::Label::textColourId, juce::Colour(0xffb8c7d9));
    sourceBOverviewLabel.setColour(juce::Label::textColourId, juce::Colour(0xffb8c7d9));

    sourceAGroup.setText("Source A");
    sourceAGroup.setTextLabelPosition(juce::Justification::centredLeft);
    sourceBGroup.setText("Source B");
    sourceBGroup.setTextLabelPosition(juce::Justification::centredLeft);
    addAndMakeVisible(sourceAGroup);
    addAndMakeVisible(sourceBGroup);

    addAndMakeVisible(sourceALabel);
    addAndMakeVisible(sourceBLabel);
    addAndMakeVisible(sourceAMetaLabel);
    addAndMakeVisible(sourceBMetaLabel);
    addAndMakeVisible(sourceAOverviewLabel);
    addAndMakeVisible(sourceBOverviewLabel);

    sourceALoadAnalyzeButton.setButtonText("Choose A");
    sourceBLoadAnalyzeButton.setButtonText("Choose B");
    sourceAUseCurrentButton.setButtonText("Re-analyze A");
    sourceBUseCurrentButton.setButtonText("Re-analyze B");
    sourceAListenButton.setButtonText("Play A");
    sourceBListenButton.setButtonText("Play B");
    exportSourceAButton.setButtonText("Export ATS");
    exportSourceBButton.setButtonText("Export ATS");
    sourceALoadAnalyzeButton.setHelpText("Choose a WAV or ATS file and assign it to Source A.");
    sourceBLoadAnalyzeButton.setHelpText("Choose a WAV or ATS file and assign it to Source B.");
    sourceAUseCurrentButton.setHelpText("Re-analyze Source A from its chosen WAV file.");
    sourceBUseCurrentButton.setHelpText("Re-analyze Source B from its chosen WAV file.");
    sourceAListenButton.setHelpText("Preview Source A with the current audition mode.");
    sourceBListenButton.setHelpText("Preview Source B with the current audition mode.");
    exportSourceAButton.setHelpText("Export Source A model to an ATS file.");
    exportSourceBButton.setHelpText("Export Source B model to an ATS file.");

    sourceALoadAnalyzeButton.onClick = [this] { pickMorphSourceFile(0); };
    sourceBLoadAnalyzeButton.onClick = [this] { pickMorphSourceFile(1); };

    sourceAUseCurrentButton.onClick = [this] {
        juce::String err;
        if (!audioProcessor.reanalyzeMorphSourceSlot(0, err))
            juce::NativeMessageBox::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Re-analyze Source A", err);
        refreshFromProcessor();
    };
    sourceBUseCurrentButton.onClick = [this] {
        juce::String err;
        if (!audioProcessor.reanalyzeMorphSourceSlot(1, err))
            juce::NativeMessageBox::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Re-analyze Source B", err);
        refreshFromProcessor();
    };
    sourceAListenButton.onClick = [this] {
        if (audioProcessor.isPreviewPlaying()
            && audioProcessor.getPreviewPlaybackKind() == AtsPreviewPlaybackKind::SourceA)
        {
            audioProcessor.stopPreviewPlayback();
            return;
        }

        juce::String err;
        if (!audioProcessor.listenMorphSourceSlot(0, (AtsSourceAuditionMode) getSourceAuditionModeIndex(0), err))
            juce::NativeMessageBox::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Listen A", err);
    };
    sourceBListenButton.onClick = [this] {
        if (audioProcessor.isPreviewPlaying()
            && audioProcessor.getPreviewPlaybackKind() == AtsPreviewPlaybackKind::SourceB)
        {
            audioProcessor.stopPreviewPlayback();
            return;
        }

        juce::String err;
        if (!audioProcessor.listenMorphSourceSlot(1, (AtsSourceAuditionMode) getSourceAuditionModeIndex(1), err))
            juce::NativeMessageBox::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Listen B", err);
    };

    exportSourceAButton.onClick = [this] { exportMorphSourceSlotAts(0); };
    exportSourceBButton.onClick = [this] { exportMorphSourceSlotAts(1); };

    addAndMakeVisible(sourceALoadAnalyzeButton);
    addAndMakeVisible(sourceBLoadAnalyzeButton);
    addAndMakeVisible(sourceAUseCurrentButton);
    addAndMakeVisible(sourceBUseCurrentButton);
    addAndMakeVisible(sourceAListenButton);
    addAndMakeVisible(sourceBListenButton);
    addAndMakeVisible(exportSourceAButton);
    addAndMakeVisible(exportSourceBButton);

    sourceAAuditionLabel.setText("Audition", juce::dontSendNotification);
    sourceBAuditionLabel.setText("Audition", juce::dontSendNotification);
    sourceAAuditionLabel.setJustificationType(juce::Justification::centredLeft);
    sourceBAuditionLabel.setJustificationType(juce::Justification::centredLeft);
    sourceAAuditionLabel.setFont(juce::Font(10.5f, juce::Font::bold));
    sourceBAuditionLabel.setFont(juce::Font(10.5f, juce::Font::bold));
    sourceAAuditionLabel.setMinimumHorizontalScale(0.7f);
    sourceBAuditionLabel.setMinimumHorizontalScale(0.7f);
    addAndMakeVisible(sourceAAuditionLabel);
    addAndMakeVisible(sourceBAuditionLabel);

    auto setupAuditionButtons = [](juce::ToggleButton& both,
                                   juce::ToggleButton& partials,
                                   juce::ToggleButton& residual,
                                   int radioGroup) {
        for (auto* button : { &both, &partials, &residual })
        {
            button->setRadioGroupId(radioGroup);
            button->setClickingTogglesState(true);
        }
        both.setToggleState(true, juce::dontSendNotification);
    };
    setupAuditionButtons(sourceABothButton, sourceAPartialsButton, sourceAResidualButton, 5101);
    setupAuditionButtons(sourceBBothButton, sourceBPartialsButton, sourceBResidualButton, 5102);
    addAndMakeVisible(sourceABothButton);
    addAndMakeVisible(sourceAPartialsButton);
    addAndMakeVisible(sourceAResidualButton);
    addAndMakeVisible(sourceBBothButton);
    addAndMakeVisible(sourceBPartialsButton);
    addAndMakeVisible(sourceBResidualButton);

    morphSnapshotLabel.setText("Morph preview", juce::dontSendNotification);
    morphSnapshotLabel.setJustificationType(juce::Justification::centredLeft);
    morphSnapshotLabel.setFont(juce::Font(11.0f, juce::Font::bold));
    morphSnapshotStatusLabel.setJustificationType(juce::Justification::centredLeft);
    morphSnapshotStatusLabel.setFont(juce::Font(10.5f));
    morphSnapshotStatusLabel.setMinimumHorizontalScale(0.7f);
    morphSnapshotStatusLabel.setHelpText("Shows whether live morph controls match the last prepared snapshot.");
    addAndMakeVisible(morphSnapshotLabel);
    addAndMakeVisible(morphSnapshotStatusLabel);

    for (size_t i = 0; i < morphVariationRecallButtons.size(); ++i)
    {
        const auto slotLabel = juce::String("V") + juce::String((int) i + 1);
        morphVariationRecallButtons[i].setButtonText(slotLabel);
        morphVariationRecallButtons[i].setHelpText("Recall this saved morph variation.");
        morphVariationRecallButtons[i].onClick = [this, i] { recallMorphVariation(i); };
        morphVariationSaveButtons[i].setButtonText("S" + juce::String((int) i + 1));
        morphVariationSaveButtons[i].setHelpText("Save the current macro and lane state into this variation.");
        morphVariationSaveButtons[i].onClick = [this, i] { saveMorphVariation(i); };
    }

    morphTimelineLabel.setText("Morph Timeline", juce::dontSendNotification);
    morphTimelineLabel.setJustificationType(juce::Justification::centredLeft);
    morphTimelineLabel.setFont(juce::Font(11.0f, juce::Font::bold));
    morphTimelineHintLabel.setJustificationType(juce::Justification::centredLeft);
    morphTimelineHintLabel.setFont(juce::Font(10.5f));
    morphTimelineHintLabel.setMinimumHorizontalScale(0.7f);
    morphTimelineHintLabel.setColour(juce::Label::textColourId, juce::Colour(0xffb8c7d9));
    morphTimelineEditor.setHelpText(
        "Morph breakpoints (circles): blend Source A toward B. "
        "Hybrid breakpoints (diamonds): scale Source A amplitudes by Source B's Bark envelope - "
        "Source A partials are preserved unchanged. "
        "For a pure Hybrid effect, press Reset first so the morph stays at A, then add Hybrid breakpoints.");
    morphTimelineEditor.onPointsChanged = [this](std::vector<AtsMorphTimelinePoint> points) {
        audioProcessor.setMorphTimelinePoints(points);
        refreshMorphSnapshotControls();
    };
    morphTimelinePlayButton.setHelpText("Render the drawn morph timeline and play it from the beginning.");
    morphTimelinePlayButton.onClick = [this] {
        if (audioProcessor.isPreviewPlaying()
            && audioProcessor.getPreviewPlaybackKind() == AtsPreviewPlaybackKind::MorphTimeline)
        {
            audioProcessor.stopPreviewPlayback();
            refreshFromProcessor();
            return;
        }
        playMorphTimeline();
    };
    morphTimelineBounceButton.setHelpText("Render the morph timeline to a WAV file and reveal it for import into your DAW.");
    morphTimelineBounceButton.onClick = [this] { bounceMorphTimeline(); };
    morphTimelineAuditionLabel.setText("Audition", juce::dontSendNotification);
    morphTimelineAuditionLabel.setJustificationType(juce::Justification::centredLeft);
    morphTimelineAuditionLabel.setFont(juce::Font(10.5f, juce::Font::bold));
    morphTimelineAuditionLabel.setMinimumHorizontalScale(0.7f);
    for (auto* button : { &morphTimelineBothButton, &morphTimelinePartialsButton, &morphTimelineResidualButton })
    {
        button->setRadioGroupId(5201);
        button->setClickingTogglesState(true);
        button->setHelpText("Choose which layer to audition for timeline playback.");
    }
    morphTimelineBothButton.setToggleState(true, juce::dontSendNotification);
    morphTimelineResetButton.setHelpText("Reset to Source A (morph = 0). Recommended starting point before adding Hybrid breakpoints.");
    morphTimelineResetButton.onClick = [this] {
        setMorphTimelinePreset({ { 0.0, 0.0 }, { 1.0, 0.0 } });
    };
    addAndMakeVisible(morphTimelineLabel);
    addAndMakeVisible(morphTimelineAuditionLabel);
    addAndMakeVisible(morphTimelineBothButton);
    addAndMakeVisible(morphTimelinePartialsButton);
    addAndMakeVisible(morphTimelineResidualButton);
    addAndMakeVisible(morphTimelineHintLabel);
    addAndMakeVisible(morphTimelineEditor);
    addAndMakeVisible(morphTimelinePlayButton);
    addAndMakeVisible(morphTimelineBounceButton);
    addAndMakeVisible(morphTimelineResetButton);

    morphSmoothingLabel.setText("Smooth", juce::dontSendNotification);
    morphSmoothingLabel.setJustificationType(juce::Justification::centredLeft);
    morphSmoothingLabel.setFont(juce::Font(11.0f, juce::Font::bold));
    morphSmoothingSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    morphSmoothingSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 54, 18);
    morphSmoothingSlider.setName("Morph smoothing");
    morphSmoothingSlider.setHelpText("Control smoothing time in milliseconds for live morph lanes.");
    resetMorphLanesButton.setHelpText("Link every morph lane to the main morph control.");
    resetMorphLanesButton.onClick = [this] { resetMorphLanesToLinked(); };
    addAndMakeVisible(morphSmoothingLabel);
    addAndMakeVisible(morphSmoothingSlider);
    addAndMakeVisible(resetMorphLanesButton);
    morphSmoothingAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, ats_param::kMorphSmoothingMs, morphSmoothingSlider);

    for (size_t i = 0; i < morphLaneLabels.size(); ++i)
    {
        morphLaneLabels[i].setText(ats_param::kMorphLaneDisplayNames[i], juce::dontSendNotification);
        morphLaneLabels[i].setJustificationType(juce::Justification::centredLeft);
        morphLaneLabels[i].setFont(juce::Font(10.5f, juce::Font::bold));
        morphLaneLabels[i].setMinimumHorizontalScale(0.72f);
        morphLaneSliders[i].setSliderStyle(juce::Slider::LinearHorizontal);
        morphLaneSliders[i].setTextBoxStyle(juce::Slider::TextBoxRight, false, 42, 16);
        morphLaneSliders[i].setName(juce::String(ats_param::kMorphLaneDisplayNames[i]) + " lane");
        morphLaneSliders[i].setHelpText("Independent morph amount for this lane when unlinked.");
        morphLaneLinkedButtons[i].setButtonText("L");
        morphLaneLinkedButtons[i].setHelpText("Link this lane to the main morph amount.");
        addAndMakeVisible(morphLaneLabels[i]);
        addAndMakeVisible(morphLaneSliders[i]);
        addAndMakeVisible(morphLaneLinkedButtons[i]);
        morphLaneAmountAttach[i] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            audioProcessor.apvts, ats_param::kMorphLaneAmountIds[i], morphLaneSliders[i]);
        morphLaneLinkedAttach[i] = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            audioProcessor.apvts, ats_param::kMorphLaneLinkedIds[i], morphLaneLinkedButtons[i]);
    }

    auto setupPolicyCombo = [this](juce::ComboBox& combo,
                                   const juce::StringArray& items,
                                   const juce::String& helpText) {
        for (int i = 0; i < items.size(); ++i)
            combo.addItem(items[i], i + 1);
        combo.setTextWhenNothingSelected(items.isEmpty() ? juce::String() : items[0]);
        combo.setHelpText(helpText);
        addAndMakeVisible(combo);
    };
    morphPolicyLabel.setText("Policy", juce::dontSendNotification);
    morphPolicyLabel.setJustificationType(juce::Justification::centredLeft);
    morphPolicyLabel.setFont(juce::Font(11.0f, juce::Font::bold));
    addAndMakeVisible(morphPolicyLabel);

    setupPolicyCombo(morphGridTypeCombo, { "Bark", "Mel" }, "Perceptual frequency grid.");
    setupPolicyCombo(morphCorrespondenceCombo,
                     { "Freq rank", "Greedy", "Assign" },
                     "Partial correspondence strategy.");
    setupPolicyCombo(morphSourcePolicyCombo,
                     { "Src fade", "Src in", "Src hold", "Src residual", "Src discard" },
                     "Policy for source-only partials.");
    setupPolicyCombo(morphTargetPolicyCombo,
                     { "Tgt out", "Tgt fade", "Tgt hold", "Tgt residual", "Tgt discard" },
                     "Policy for target-only partials.");
    setupPolicyCombo(morphFrequencyDomainCombo,
                     { "Log Hz", "Bark coord" },
                     "Frequency interpolation domain.");
    setupPolicyCombo(morphAmplitudeDomainCombo,
                     { "Power", "dB" },
                     "Amplitude interpolation domain.");
    setupPolicyCombo(morphPhaseStrategyCombo,
                     { "Osc", "A phase", "B phase", "Rel cluster", "Blend" },
                     "Phase strategy.");
    setupPolicyCombo(morphTransientPolicyCombo,
                     { "A attack", "B attack", "Xfade atk", "Sustain" },
                     "Transient handling policy.");
    morphDiscardOptInButton.setButtonText("Discard");
    morphDiscardOptInButton.setHelpText("Explicit opt-in required before discard policies are honored.");
    addAndMakeVisible(morphDiscardOptInButton);

    morphGridTypeAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.apvts, ats_param::kMorphGridType, morphGridTypeCombo);
    morphCorrespondenceAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.apvts, ats_param::kMorphCorrespondenceStrategy, morphCorrespondenceCombo);
    morphSourcePolicyAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.apvts, ats_param::kMorphUnmatchedSourcePolicy, morphSourcePolicyCombo);
    morphTargetPolicyAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.apvts, ats_param::kMorphUnmatchedTargetPolicy, morphTargetPolicyCombo);
    morphFrequencyDomainAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.apvts, ats_param::kMorphFrequencyInterpolationDomain, morphFrequencyDomainCombo);
    morphAmplitudeDomainAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.apvts, ats_param::kMorphAmplitudeInterpolationDomain, morphAmplitudeDomainCombo);
    morphPhaseStrategyAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.apvts, ats_param::kMorphPhaseStrategy, morphPhaseStrategyCombo);
    morphTransientPolicyAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.apvts, ats_param::kMorphTransientPolicy, morphTransientPolicyCombo);
    morphDiscardOptInAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.apvts, ats_param::kMorphDiscardOptIn, morphDiscardOptInButton);

    morphBandLabel.setText("Bands", juce::dontSendNotification);
    morphBandLabel.setJustificationType(juce::Justification::centredLeft);
    morphBandLabel.setFont(juce::Font(11.0f, juce::Font::bold));
    morphBandInspectorLabel.setJustificationType(juce::Justification::topLeft);
    morphBandInspectorLabel.setFont(juce::Font(10.5f));
    morphBandInspectorLabel.setMinimumHorizontalScale(0.72f);
    morphBandInspectorLabel.setColour(juce::Label::textColourId, juce::Colour(0xffd8e4f2));
    morphBandInspectorLabel.setHelpText("Selected band correspondence, energy, policy, and transient diagnostics.");
    morphBandStrip.setHelpText("Perceptual band strip: blue source A, green source B, amber output, red warnings.");
    morphBandStrip.onBandSelected = [this](int bandIndex) {
        selectedMorphBandIndex = bandIndex;
        refreshMorphBandDiagnostics();
    };
    addAndMakeVisible(morphBandLabel);
    addAndMakeVisible(morphBandStrip);
    addAndMakeVisible(morphBandInspectorLabel);

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
        audioProcessor.setCurrentProgram(id - 1);
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

void SessionTabPanel::resetMorphLanesToLinked()
{
    const float morphAmount = getPlainParameter(audioProcessor, ats_param::kMorphAmount, 0.0f);
    for (size_t i = 0; i < ats_param::kMorphLaneCount; ++i)
    {
        setPlainParameter(audioProcessor, ats_param::kMorphLaneAmountIds[i], morphAmount);
        setBoolParameter(audioProcessor, ats_param::kMorphLaneLinkedIds[i], true);
    }
}

void SessionTabPanel::saveMorphVariation(size_t index)
{
    juce::String err;
    if (!audioProcessor.saveCurrentMorphVariation((int) index, err))
    {
        juce::NativeMessageBox::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                    "Save variation",
                                                    err);
    }
    refreshMorphSnapshotControls();
}

void SessionTabPanel::recallMorphVariation(size_t index)
{
    juce::String err;
    if (!audioProcessor.recallMorphVariation((int) index, err))
    {
        juce::NativeMessageBox::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                    "Recall variation",
                                                    err);
    }
    refreshMorphSnapshotControls();
}

void SessionTabPanel::refreshMorphSnapshotControls()
{
    const auto snapshotState = audioProcessor.getMorphSnapshotUiState();
    morphSnapshotStatusLabel.setText(audioProcessor.getMorphSnapshotStatusText(),
                                     juce::dontSendNotification);
    morphSnapshotStatusLabel.setColour(juce::Label::textColourId, snapshotStateColour(snapshotState));

    for (size_t i = 0; i < morphVariationRecallButtons.size(); ++i)
    {
        AtsMorphSnapshotSlotInfo slot;
        const bool hasSlot = audioProcessor.getMorphVariationSlotInfo((int) i, slot) && slot.hasSnapshot;
        const auto label = slot.name.isNotEmpty() ? slot.name : ("V" + juce::String((int) i + 1));
        morphVariationRecallButtons[i].setButtonText(hasSlot ? label : (label + "-"));
        morphVariationRecallButtons[i].setEnabled(hasSlot);
        morphVariationSaveButtons[i].setEnabled(true);
    }
}

void SessionTabPanel::refreshMorphBandDiagnostics()
{
    morphBandDiagnostics = audioProcessor.buildMorphBandDiagnostics();
    if (morphBandDiagnostics.empty())
    {
        selectedMorphBandIndex = -1;
        morphBandStrip.setBands({}, selectedMorphBandIndex);
        morphBandInspectorLabel.setText("No morph band diagnostics yet.", juce::dontSendNotification);
        return;
    }

    bool foundSelection = false;
    std::vector<MorphBandStrip::BandView> views;
    views.reserve(morphBandDiagnostics.size());
    for (const auto& diagnostic : morphBandDiagnostics)
    {
        foundSelection = foundSelection || diagnostic.bandIndex == selectedMorphBandIndex;
        views.push_back({diagnostic.bandIndex,
                         diagnostic.sourcePower,
                         diagnostic.targetPower,
                         diagnostic.outputPower,
                         diagnostic.matchedCount,
                         diagnostic.unmatchedSourceCount + diagnostic.unmatchedTargetCount,
                         diagnostic.warning});
    }

    if (!foundSelection)
        selectedMorphBandIndex = morphBandDiagnostics.front().bandIndex;

    morphBandStrip.setBands(std::move(views), selectedMorphBandIndex);

    for (const auto& diagnostic : morphBandDiagnostics)
    {
        if (diagnostic.bandIndex == selectedMorphBandIndex)
        {
            morphBandInspectorLabel.setText(diagnostic.inspectorText, juce::dontSendNotification);
            return;
        }
    }
}

void SessionTabPanel::refreshMorphTimeline()
{
    const auto points = audioProcessor.getMorphTimelinePoints();
    if (!morphTimelineEditor.isEditing())
        morphTimelineEditor.setPoints(points);

    morphTimelineHintLabel.setText({}, juce::dontSendNotification);
}

void SessionTabPanel::setMorphTimelinePreset(std::vector<AtsMorphTimelinePoint> points)
{
    audioProcessor.setMorphTimelinePoints(points);
    morphTimelineEditor.setPoints(audioProcessor.getMorphTimelinePoints());
    refreshMorphSnapshotControls();
    refreshMorphTimeline();
}

void SessionTabPanel::playMorphTimeline()
{
    juce::String err;
    if (!audioProcessor.playMorphTimelinePreview((AtsSourceAuditionMode) getMorphTimelineAuditionModeIndex(), err))
    {
        juce::NativeMessageBox::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon,
            "Play Morph Timeline",
            err.isEmpty() ? juce::String("Could not play morph timeline.") : err);
    }
    refreshFromProcessor();
}

void SessionTabPanel::bounceMorphTimeline()
{
    timelineBounceChooser = std::make_unique<juce::FileChooser>("Bounce Morph Timeline",
                                                                 juce::File{},
                                                                 "*.wav");
    constexpr auto flags = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles;
    timelineBounceChooser->launchAsync(flags, [this](const juce::FileChooser& fc) {
        auto file = fc.getResult();
        if (file.getFullPathName().isEmpty())
            return;
        if (!file.hasFileExtension(".wav"))
            file = file.withFileExtension("wav");

        juce::String err;
        if (!audioProcessor.bounceMorphTimelineToFile(file,
                                                      (AtsSourceAuditionMode) getMorphTimelineAuditionModeIndex(),
                                                      err))
        {
            juce::NativeMessageBox::showMessageBoxAsync(
                juce::AlertWindow::WarningIcon,
                "Bounce Timeline",
                err.isEmpty() ? juce::String("Could not bounce morph timeline.") : err);
            return;
        }

        // Best-effort UX for DAW workflows: reveal file for drag/drop/import.
        file.revealToUser();
    });
}

int SessionTabPanel::getSourceAuditionModeIndex(int slotIndex) const
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

int SessionTabPanel::getMorphTimelineAuditionModeIndex() const
{
    if (morphTimelinePartialsButton.getToggleState())
        return (int) AtsSourceAuditionMode::PartialsOnly;
    if (morphTimelineResidualButton.getToggleState())
        return (int) AtsSourceAuditionMode::ResidualOnly;
    return (int) AtsSourceAuditionMode::Both;
}

void SessionTabPanel::exportMorphSourceSlotAts(int slotIndex)
{
    atsExportChooser = std::make_unique<juce::FileChooser>(slotIndex == 0 ? "Export Source A ATS"
                                                                           : "Export Source B ATS",
                                                            juce::File{},
                                                            "*.ats");
    constexpr auto flags = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles;
    atsExportChooser->launchAsync(flags, [this, slotIndex](const juce::FileChooser& fc) {
        auto file = fc.getResult();
        if (file.getFullPathName().isEmpty())
            return;
        if (!file.hasFileExtension(".ats"))
            file = file.withFileExtension("ats");

        juce::String err;
        if (!audioProcessor.exportMorphSourceSlotAts(slotIndex, file, err))
            juce::NativeMessageBox::showMessageBoxAsync(
                juce::AlertWindow::WarningIcon,
                slotIndex == 0 ? "Export Source A" : "Export Source B",
                err.isEmpty() ? juce::String("Could not export source ATS.") : err);
    });
}

void SessionTabPanel::resized()
{
    const auto sc = [](int px) { return scaledUi(px); };

    auto r = getLocalBounds().reduced(sc(8));
    loadButton.setBounds({});
    analyzeButton.setBounds({});
    currentPlayButton.setBounds({});
    cancelButton.setBounds({});
    presetLabel.setBounds({});
    presetCombo.setBounds({});
    savePresetButton.setBounds({});
    importAtsButton.setBounds({});
    exportAtsButton.setBounds({});
    analysisSummaryLabel.setBounds({});

    statusLabel.setBounds(r.removeFromTop(sc(18)));
    r.removeFromTop(sc(1));

    const bool hasErrorText = !errorLabel.getText().isEmpty();
    const int errorHeight = hasErrorText ? sc(16) : sc(0);
    if (errorHeight > 0)
    {
        errorLabel.setBounds(r.removeFromTop(errorHeight));
        r.removeFromTop(sc(1));
    }
    else
    {
        errorLabel.setBounds({});
    }

    const bool hasCpuWarningText = !cpuWarningLabel.getText().isEmpty();
    const int cpuWarningHeight = hasCpuWarningText ? sc(14) : sc(0);
    if (cpuWarningHeight > 0)
    {
        cpuWarningLabel.setBounds(r.removeFromTop(cpuWarningHeight));
        r.removeFromTop(sc(1));
    }
    else
    {
        cpuWarningLabel.setBounds({});
    }

    progressBar.setBounds(r.removeFromTop(sc(12)));
    r.removeFromTop(sc(6));

    const bool narrowSourceCards = r.getWidth() < 680;
    auto sourceCards = r.removeFromTop(narrowSourceCards ? 232 : 112);
    r.removeFromTop(sc(6));

    auto body = r;
    const int specW = juce::jmax(sc(280), body.getWidth() / 3);
    auto specCol = body.removeFromRight(specW);
    spectroPanel.setBounds(specCol);

    auto placeSourceSlot = [&](juce::Rectangle<int> area,
                               juce::GroupComponent& group,
                               juce::Label& heading,
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
        group.setBounds(area);
        auto inner = area.reduced(10, 14);
        auto buttonCol = inner.removeFromRight(narrowSourceCards ? 132 : 192);
        chooseButton.setBounds(buttonCol.removeFromTop(24).reduced(2));
        buttonCol.removeFromTop(4);
        reanalyzeButton.setBounds(buttonCol.removeFromTop(24).reduced(2));
        buttonCol.removeFromTop(4);
        playButton.setBounds(buttonCol.removeFromTop(24).reduced(2));
        buttonCol.removeFromTop(4);
        exportButton.setBounds(buttonCol.removeFromTop(18).reduced(2));
        inner.removeFromRight(6);
        heading.setBounds({});
        meta.setBounds(inner.removeFromTop(24));
        overview.setBounds(inner.removeFromTop(24));
        inner.removeFromTop(4);
        auto auditionRow = inner.removeFromTop(24);
        auditionLabel.setBounds(auditionRow.removeFromLeft(narrowSourceCards ? 54 : 62));
        bothButton.setBounds(auditionRow.removeFromLeft(narrowSourceCards ? 54 : 62));
        partialsButton.setBounds(auditionRow.removeFromLeft(narrowSourceCards ? 72 : 82));
        residualButton.setBounds(auditionRow.removeFromLeft(narrowSourceCards ? 78 : 88));
    };

    if (narrowSourceCards)
    {
        placeSourceSlot(sourceCards.removeFromTop(112).reduced(0, 1),
                        sourceAGroup,
                        sourceALabel,
                        sourceAMetaLabel,
                        sourceAOverviewLabel,
                        sourceALoadAnalyzeButton,
                        sourceAUseCurrentButton,
                        sourceAListenButton,
                        exportSourceAButton,
                        sourceAAuditionLabel,
                        sourceABothButton,
                        sourceAPartialsButton,
                        sourceAResidualButton);
        sourceCards.removeFromTop(4);
        placeSourceSlot(sourceCards.removeFromTop(112).reduced(0, 1),
                        sourceBGroup,
                        sourceBLabel,
                        sourceBMetaLabel,
                        sourceBOverviewLabel,
                        sourceBLoadAnalyzeButton,
                        sourceBUseCurrentButton,
                        sourceBListenButton,
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
        placeSourceSlot(sourceA,
                        sourceAGroup,
                        sourceALabel,
                        sourceAMetaLabel,
                        sourceAOverviewLabel,
                        sourceALoadAnalyzeButton,
                        sourceAUseCurrentButton,
                        sourceAListenButton,
                        exportSourceAButton,
                        sourceAAuditionLabel,
                        sourceABothButton,
                        sourceAPartialsButton,
                        sourceAResidualButton);
        placeSourceSlot(sourceB,
                        sourceBGroup,
                        sourceBLabel,
                        sourceBMetaLabel,
                        sourceBOverviewLabel,
                        sourceBLoadAnalyzeButton,
                        sourceBUseCurrentButton,
                        sourceBListenButton,
                        exportSourceBButton,
                        sourceBAuditionLabel,
                        sourceBBothButton,
                        sourceBPartialsButton,
                        sourceBResidualButton);
    }
    body.removeFromTop(sc(4));

    auto timelineArea = body.removeFromTop(juce::jmin(sc(92), juce::jmax(sc(72), body.getHeight() / 5)));
    auto timelineHeader = timelineArea.removeFromTop(sc(22));
    morphTimelineLabel.setBounds(timelineHeader.removeFromLeft(sc(102)));
    morphTimelinePlayButton.setBounds(timelineHeader.removeFromLeft(sc(112)).reduced(sc(1)));
    morphTimelineBounceButton.setBounds(timelineHeader.removeFromLeft(sc(128)).reduced(sc(1)));
    timelineHeader.removeFromLeft(sc(4));
    morphTimelineAuditionLabel.setBounds(timelineHeader.removeFromLeft(sc(46)));
    morphTimelineBothButton.setBounds(timelineHeader.removeFromLeft(sc(44)));
    morphTimelinePartialsButton.setBounds(timelineHeader.removeFromLeft(sc(58)));
    morphTimelineResidualButton.setBounds(timelineHeader.removeFromLeft(sc(62)));
    timelineHeader.removeFromLeft(sc(2));
    morphTimelineResetButton.setBounds(timelineHeader.removeFromLeft(sc(52)).reduced(sc(1)));
    morphTimelineHintLabel.setBounds({});
    morphTimelineEditor.setBounds(timelineArea.reduced(sc(2)));
    body.removeFromTop(sc(4));

    auto snapshotHeader = body.removeFromTop(sc(18));
    morphSnapshotLabel.setBounds(snapshotHeader.removeFromLeft(sc(84)));
    morphSnapshotStatusLabel.setBounds(snapshotHeader);
    for (auto& button : morphVariationRecallButtons)
        button.setBounds({});
    for (auto& button : morphVariationSaveButtons)
        button.setBounds({});
    body.removeFromTop(sc(4));

    auto laneArea = body.removeFromTop(juce::jmin(sc(116), juce::jmax(sc(78), body.getHeight() / 2)));
    auto laneHeader = laneArea.removeFromTop(sc(24));
    morphSmoothingLabel.setBounds(laneHeader.removeFromLeft(sc(58)));
    morphSmoothingSlider.setBounds(laneHeader.removeFromLeft(sc(160)));
    laneHeader.removeFromLeft(sc(6));
    resetMorphLanesButton.setBounds(laneHeader.removeFromLeft(sc(92)));
    laneArea.removeFromTop(sc(2));

    auto laneGrid = laneArea;
    for (size_t row = 0; row < 2; ++row)
    {
        auto laneRow = laneGrid.removeFromTop(laneGrid.getHeight() / (int) (2 - row));
        for (size_t col = 0; col < 3; ++col)
        {
            const size_t index = row * 3 + col;
            auto cell = laneRow.removeFromLeft(laneRow.getWidth() / (int) (3 - col)).reduced(sc(3));
            auto title = cell.removeFromTop(sc(18));
            morphLaneLinkedButtons[index].setBounds(title.removeFromRight(sc(34)));
            morphLaneLabels[index].setBounds(title);
            morphLaneSliders[index].setBounds(cell.removeFromTop(sc(28)));
        }
    }
    body.removeFromTop(sc(4));

    auto policyArea = body.removeFromTop(juce::jmin(sc(96), juce::jmax(sc(64), body.getHeight() / 2)));
    auto policyHeader = policyArea.removeFromTop(sc(20));
    morphPolicyLabel.setBounds(policyHeader.removeFromLeft(sc(54)));
    morphDiscardOptInButton.setBounds(policyHeader.removeFromLeft(sc(96)));
    policyArea.removeFromTop(sc(2));

    std::array<juce::ComboBox*, 8> policyCombos {
        &morphGridTypeCombo,
        &morphCorrespondenceCombo,
        &morphSourcePolicyCombo,
        &morphTargetPolicyCombo,
        &morphFrequencyDomainCombo,
        &morphAmplitudeDomainCombo,
        &morphPhaseStrategyCombo,
        &morphTransientPolicyCombo,
    };
    for (size_t row = 0; row < 2; ++row)
    {
        auto comboRow = policyArea.removeFromTop(sc(30));
        for (size_t col = 0; col < 4; ++col)
        {
            const size_t index = row * 4 + col;
            policyCombos[index]->setBounds(comboRow.removeFromLeft(comboRow.getWidth() / (int) (4 - col))
                                               .reduced(sc(2)));
        }
    }
    body.removeFromTop(sc(4));

    auto bandArea = body.removeFromTop(juce::jmin(sc(112), juce::jmax(sc(64), body.getHeight() / 2)));
    auto bandHeader = bandArea.removeFromTop(sc(18));
    morphBandLabel.setBounds(bandHeader.removeFromLeft(sc(64)));
    morphBandStrip.setBounds(bandArea.removeFromTop(sc(30)).reduced(sc(2)));
    morphBandInspectorLabel.setBounds(bandArea.reduced(sc(2)));
    body.removeFromTop(sc(4));

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
    const auto prevErrorText = errorLabel.getText();
    const auto prevCpuWarningText = cpuWarningLabel.getText();

    displayProgress = (double) audioProcessor.getAnalysisProgress();
    analysisSummaryLabel.setText(formatAnalysisSummary(audioProcessor), juce::dontSendNotification);
    const bool playing = audioProcessor.isPreviewPlaying();
    const bool workerBusy = audioProcessor.isWorkerBusy();
    const auto playbackKind = audioProcessor.getPreviewPlaybackKind();
    const bool sourceAPlaying = playing && playbackKind == AtsPreviewPlaybackKind::SourceA;
    const bool sourceBPlaying = playing && playbackKind == AtsPreviewPlaybackKind::SourceB;
    const bool currentPreviewPlaying = playing && playbackKind == AtsPreviewPlaybackKind::MorphPreview;
    const bool timelinePlaying = playing && playbackKind == AtsPreviewPlaybackKind::MorphTimeline;
    morphTimelinePlayButton.setEnabled(timelinePlaying || (audioProcessor.canPlayMorphPreview() && !workerBusy));
    currentPlayButton.setButtonText(currentPreviewPlaying ? "Stop" : (workerBusy ? "Wait" : "Play"));
    sourceAListenButton.setButtonText(playing && playbackKind == AtsPreviewPlaybackKind::SourceA ? "Stop A"
                                                                                                  : "Play A");
    sourceBListenButton.setButtonText(playing && playbackKind == AtsPreviewPlaybackKind::SourceB ? "Stop B"
                                                                                                  : "Play B");
    morphTimelinePlayButton.setButtonText(playing && playbackKind == AtsPreviewPlaybackKind::MorphTimeline
                                              ? "Stop Timeline"
                                              : (workerBusy && audioProcessor.canPlayMorphPreview()
                                                     ? "Preparing..."
                                                     : "Play Timeline"));
    morphTimelineBounceButton.setEnabled(!workerBusy && audioProcessor.canPlayMorphPreview());

    switch (audioProcessor.getUiState())
    {
    case AtsUiState::NoSource:
        statusLabel.setText("State: No source", juce::dontSendNotification);
        break;
    case AtsUiState::ReadyToAnalyze:
        statusLabel.setText("State: Ready to analyze", juce::dontSendNotification);
        break;
    case AtsUiState::Analyzing:
    {
        const int pendingSlot = audioProcessor.getPendingMorphSourceSlot();
        if (pendingSlot == 0 || pendingSlot == 1)
            statusLabel.setText("State: Analyzing source " + juce::String(pendingSlot == 0 ? "A" : "B")
                                    + " (CPU heavy)",
                                juce::dontSendNotification);
        else
            statusLabel.setText("State: Analyzing (CPU heavy)", juce::dontSendNotification);
        break;
    }
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

    // Re-query lock-protected model data only when the revision changes.
    const int curModelRev = audioProcessor.getModelRevision();
    if (curModelRev != cachedModelRevision)
    {
        cachedModelRevision = curModelRev;
        audioProcessor.getModelFootprint(cachedModelPartials, cachedModelFrames, cachedModelHasResidual);
    }
    const int partials = cachedModelPartials;
    const int frames   = cachedModelFrames;
    const bool hasRes  = cachedModelHasResidual;
    const bool hasCurrentModel = (partials > 0 || frames > 0);
    if (hasCurrentModel)
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

    const int curMorphRev = audioProcessor.getMorphRecipeRevision();
    if (curMorphRev != cachedMorphRevision)
    {
        cachedMorphRevision = curMorphRev;
        audioProcessor.getMorphSourceSlotInfo(0, cachedSourceA);
        audioProcessor.getMorphSourceSlotInfo(1, cachedSourceB);
    }
    const AtsMorphSourceSlotInfo& sourceInfoA = cachedSourceA;
    const AtsMorphSourceSlotInfo& sourceInfoB = cachedSourceB;
    sourceAMetaLabel.setText(formatSourceMeta(sourceInfoA), juce::dontSendNotification);
    sourceBMetaLabel.setText(formatSourceMeta(sourceInfoB), juce::dontSendNotification);
    sourceAOverviewLabel.setText(formatSourceOverview(sourceInfoA), juce::dontSendNotification);
    sourceBOverviewLabel.setText(formatSourceOverview(sourceInfoB), juce::dontSendNotification);
    const int pendingMorphSlot = audioProcessor.getPendingMorphSourceSlot();
    if (pendingMorphSlot == 0)
        sourceAOverviewLabel.setText("Analyzing WAV into Source A...", juce::dontSendNotification);
    else if (pendingMorphSlot == 1)
        sourceBOverviewLabel.setText("Analyzing WAV into Source B...", juce::dontSendNotification);

    juce::File sourceAFile;
    juce::File sourceBFile;
    const bool hasSourceAPath = audioProcessor.getMorphSourceFile(0, sourceAFile);
    const bool hasSourceBPath = audioProcessor.getMorphSourceFile(1, sourceBFile);
    const bool hasSourceAWav = hasSourceAPath && sourceAFile.existsAsFile()
                               && sourceAFile.hasFileExtension(".wav");
    const bool hasSourceBWav = hasSourceBPath && sourceBFile.existsAsFile()
                               && sourceBFile.hasFileExtension(".wav");

    sourceALoadAnalyzeButton.setEnabled(!workerBusy);
    sourceBLoadAnalyzeButton.setEnabled(!workerBusy);
    exportSourceAButton.setEnabled(sourceInfoA.hasModel && !workerBusy);
    exportSourceBButton.setEnabled(sourceInfoB.hasModel && !workerBusy);
    currentPlayButton.setEnabled(currentPreviewPlaying || (hasCurrentModel && !workerBusy));
    sourceAUseCurrentButton.setEnabled(hasSourceAWav && !workerBusy);
    sourceBUseCurrentButton.setEnabled(hasSourceBWav && !workerBusy);
    sourceAListenButton.setEnabled(sourceAPlaying || (sourceInfoA.hasModel && !workerBusy));
    sourceBListenButton.setEnabled(sourceBPlaying || (sourceInfoB.hasModel && !workerBusy));
    sourceAResidualButton.setEnabled(!sourceInfoA.hasModel || sourceInfoA.hasResidual);
    sourceBResidualButton.setEnabled(!sourceInfoB.hasModel || sourceInfoB.hasResidual);
    if (sourceInfoA.hasModel && !sourceInfoA.hasResidual && sourceAResidualButton.getToggleState())
        sourceABothButton.setToggleState(true, juce::dontSendNotification);
    if (sourceInfoB.hasModel && !sourceInfoB.hasResidual && sourceBResidualButton.getToggleState())
        sourceBBothButton.setToggleState(true, juce::dontSendNotification);

    refreshMorphSnapshotControls();
    refreshMorphTimeline();

    for (size_t i = 0; i < morphLaneSliders.size(); ++i)
    {
        const bool linked = getPlainParameter(audioProcessor, ats_param::kMorphLaneLinkedIds[i], 1.0f) > 0.5f;
        morphLaneSliders[i].setEnabled(!linked);
    }
    const bool discardOptedIn = getPlainParameter(audioProcessor, ats_param::kMorphDiscardOptIn, 0.0f) > 0.5f;
    morphSourcePolicyCombo.setItemEnabled(5, discardOptedIn);
    morphTargetPolicyCombo.setItemEnabled(5, discardOptedIn);
    if (!discardOptedIn)
    {
        if (juce::roundToInt(getPlainParameter(audioProcessor, ats_param::kMorphUnmatchedSourcePolicy, 0.0f)) == 4)
            setPlainParameter(audioProcessor,
                              ats_param::kMorphUnmatchedSourcePolicy,
                              0.0f);
        if (juce::roundToInt(getPlainParameter(audioProcessor, ats_param::kMorphUnmatchedTargetPolicy, 1.0f)) == 4)
            setPlainParameter(audioProcessor,
                              ats_param::kMorphUnmatchedTargetPolicy,
                              1.0f);
    }

    refreshMorphBandDiagnostics();

    if (prevErrorText != errorLabel.getText() || prevCpuWarningText != cpuWarningLabel.getText())
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

void SessionTabPanel::pickMorphSourceFile(int slotIndex)
{
    const auto slotName = slotIndex == 0 ? juce::String("Source A") : juce::String("Source B");
    fileChooser = std::make_unique<juce::FileChooser>("Choose WAV or ATS for " + slotName,
                                                      juce::File{},
                                                      "*.wav;*.ats");
    auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;

    fileChooser->launchAsync(flags, [this, slotIndex, slotName](const juce::FileChooser& fc) {
        const auto f = fc.getResult();
        if (!f.existsAsFile())
            return;

        juce::String err;
        if (f.hasFileExtension(".ats"))
        {
            if (audioProcessor.importAtsFile(f, err) && audioProcessor.setCurrentModelAsMorphSourceSlot(slotIndex, err))
            {
                refreshFromProcessor();
                return;
            }

            juce::NativeMessageBox::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                        slotName,
                                                        err.isEmpty() ? juce::String("Could not load ATS source.") : err);
            refreshFromProcessor();
            return;
        }

        if (!audioProcessor.loadAndAnalyzeMorphSourceSlot(slotIndex, f, err))
            juce::NativeMessageBox::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                        slotName,
                                                        err.isEmpty() ? juce::String("Could not analyze morph source.") : err);
        refreshFromProcessor();
    });
}
