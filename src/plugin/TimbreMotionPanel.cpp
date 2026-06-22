#include "TimbreMotionPanel.h"
#include "UiFonts.h"

#include <algorithm>

namespace
{
constexpr int kModeFollowNatural = 1;
constexpr int kModeInterpolateAB = 2;
constexpr int kModeOffsetNatural = 3;
constexpr int kModeAbsolute      = 4;

int modeToComboId(ats::TimbreTargetMode mode)
{
    switch (mode)
    {
        case ats::TimbreTargetMode::FollowNatural: return kModeFollowNatural;
        case ats::TimbreTargetMode::InterpolateAB: return kModeInterpolateAB;
        case ats::TimbreTargetMode::OffsetNatural: return kModeOffsetNatural;
        case ats::TimbreTargetMode::AbsoluteTarget: return kModeAbsolute;
    }
    return kModeInterpolateAB;
}

ats::TimbreTargetMode comboIdToMode(int id)
{
    switch (id)
    {
        case kModeFollowNatural: return ats::TimbreTargetMode::FollowNatural;
        case kModeOffsetNatural: return ats::TimbreTargetMode::OffsetNatural;
        case kModeAbsolute:      return ats::TimbreTargetMode::AbsoluteTarget;
        case kModeInterpolateAB:
        default:                 return ats::TimbreTargetMode::InterpolateAB;
    }
}

std::vector<ats::TimbreMotionPoint> defaultRampPoints()
{
    return { { 0.0, 0.0, 0 }, { 1.0, 1.0, 0 } };
}
} // namespace

// ─── TimbreCurveEditor ─────────────────────────────────────────────────────────

TimbreCurveEditor::TimbreCurveEditor()
{
    points = defaultRampPoints();
    setInterceptsMouseClicks(true, true);
}

void TimbreCurveEditor::setPoints(std::vector<ats::TimbreMotionPoint> nextPoints)
{
    points = std::move(nextPoints);
    sortAndClamp();
    selectedPoint = -1;
    repaint();
}

void TimbreCurveEditor::setPlayheadNorm(double t)
{
    const double clamped = (t >= 0.0 && t <= 1.0) ? t : -1.0;
    if (std::abs(clamped - playheadNorm) > 1.0e-6)
    {
        playheadNorm = clamped;
        repaint();
    }
}

juce::Rectangle<float> TimbreCurveEditor::getPlotArea() const
{
    return getLocalBounds().toFloat().reduced(6.0f);
}

juce::Point<float> TimbreCurveEditor::positionForPoint(const ats::TimbreMotionPoint& p) const
{
    const auto area = getPlotArea();
    const float x = area.getX() + (float) p.timeNorm * area.getWidth();
    const float y = area.getBottom() - (float) p.valueNorm * area.getHeight();
    return { x, y };
}

int TimbreCurveEditor::findNearestPoint(juce::Point<float> pos) const
{
    int best = -1;
    float bestDist = 12.0f; // px hit radius
    for (int i = 0; i < (int) points.size(); ++i)
    {
        const float d = positionForPoint(points[(size_t) i]).getDistanceFrom(pos);
        if (d < bestDist) { bestDist = d; best = i; }
    }
    return best;
}

void TimbreCurveEditor::sortAndClamp()
{
    if (points.size() < 2)
        points = defaultRampPoints();
    std::sort(points.begin(), points.end(),
              [](const ats::TimbreMotionPoint& a, const ats::TimbreMotionPoint& b) {
                  return a.timeNorm < b.timeNorm;
              });
    for (auto& p : points)
    {
        p.timeNorm  = juce::jlimit(0.0, 1.0, p.timeNorm);
        p.valueNorm = juce::jlimit(0.0, 1.0, p.valueNorm);
    }
    // Endpoint lock: first at t=0, last at t=1 (values stay editable).
    points.front().timeNorm = 0.0;
    points.back().timeNorm  = 1.0;
}

void TimbreCurveEditor::notifyChange()
{
    if (onPointsChanged)
        onPointsChanged(points);
}

void TimbreCurveEditor::mouseDown(const juce::MouseEvent& e)
{
    selectedPoint = findNearestPoint(e.position);
    editing = (selectedPoint >= 0);
}

void TimbreCurveEditor::mouseDrag(const juce::MouseEvent& e)
{
    if (selectedPoint < 0)
        return;

    const auto area = getPlotArea();
    if (area.getWidth() <= 1.0f || area.getHeight() <= 1.0f)
        return;

    auto& p = points[(size_t) selectedPoint];
    const bool isEndpoint = (selectedPoint == 0 || selectedPoint == (int) points.size() - 1);

    p.valueNorm = juce::jlimit(0.0, 1.0,
        (double) ((area.getBottom() - e.position.y) / area.getHeight()));
    if (!isEndpoint)
        p.timeNorm = juce::jlimit(0.0, 1.0,
            (double) ((e.position.x - area.getX()) / area.getWidth()));

    sortAndClamp();
    repaint();
    notifyChange();
}

void TimbreCurveEditor::mouseUp(const juce::MouseEvent&)
{
    editing = false;
    selectedPoint = -1;
}

void TimbreCurveEditor::mouseDoubleClick(const juce::MouseEvent& e)
{
    const int hit = findNearestPoint(e.position);
    if (hit > 0 && hit < (int) points.size() - 1)
    {
        points.erase(points.begin() + hit); // remove interior point
    }
    else if (hit < 0)
    {
        const auto area = getPlotArea();
        if (area.getWidth() <= 1.0f || area.getHeight() <= 1.0f)
            return;
        ats::TimbreMotionPoint np;
        np.timeNorm  = juce::jlimit(0.0, 1.0, (double) ((e.position.x - area.getX()) / area.getWidth()));
        np.valueNorm = juce::jlimit(0.0, 1.0, (double) ((area.getBottom() - e.position.y) / area.getHeight()));
        points.push_back(np);
    }
    sortAndClamp();
    repaint();
    notifyChange();
}

void TimbreCurveEditor::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour(juce::Colour(0xff10151c));
    g.fillRoundedRectangle(bounds, 3.0f);
    g.setColour(juce::Colour(0xff2a3645));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 3.0f, 1.0f);

    const auto area = getPlotArea();

    // Gridlines at value 0, 0.5, 1.
    g.setColour(juce::Colour(0xff222c38));
    for (int i = 0; i <= 2; ++i)
    {
        const float y = area.getBottom() - (float) i * 0.5f * area.getHeight();
        g.drawHorizontalLine((int) y, area.getX(), area.getRight());
    }

    if (playheadNorm >= 0.0)
    {
        g.setColour(juce::Colour(0x66ffd24a));
        const float x = area.getX() + (float) playheadNorm * area.getWidth();
        g.drawVerticalLine((int) x, area.getY(), area.getBottom());
    }

    if (points.size() >= 2)
    {
        juce::Path path;
        auto first = positionForPoint(points.front());
        path.startNewSubPath(first);
        for (size_t i = 1; i < points.size(); ++i)
            path.lineTo(positionForPoint(points[i]));
        g.setColour(juce::Colour(0xff5fd0ff)); // requested curve (editable)
        g.strokePath(path, juce::PathStrokeType(2.0f));

        for (size_t i = 0; i < points.size(); ++i)
        {
            const auto pos = positionForPoint(points[i]);
            g.setColour(juce::Colours::white);
            g.fillEllipse(pos.x - 3.0f, pos.y - 3.0f, 6.0f, 6.0f);
        }
    }
}

// ─── TimbreMacroCard ───────────────────────────────────────────────────────────

TimbreMacroCard::TimbreMacroCard()
{
    addAndMakeVisible(enableButton);
    enableButton.setButtonText({});

    depthSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    depthSlider.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    depthSlider.setRange(0.0, 1.0, 0.001);
    depthSlider.setValue(1.0, juce::dontSendNotification);
    depthSlider.setName("Depth");
    addAndMakeVisible(depthSlider);

    setInterceptsMouseClicks(true, true);
}

void TimbreMacroCard::resized()
{
    auto r = getLocalBounds().reduced(4);
    auto headerRow = r.removeFromTop(18);
    enableButton.setBounds(headerRow.removeFromLeft(20));
    // title painted in the remaining header space
    r.removeFromTop(2);
    auto sparkArea = r.removeFromTop(juce::jmax(10, r.getHeight() - 16));
    juce::ignoreUnused(sparkArea);
    depthSlider.setBounds(r.removeFromBottom(14));
}

void TimbreMacroCard::mouseDown(const juce::MouseEvent& e)
{
    // Clicks on the body (not the toggle/slider children) focus this card.
    juce::ignoreUnused(e);
    if (onFocusRequested)
        onFocusRequested();
}

void TimbreMacroCard::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour(focused ? juce::Colour(0xff1c2733) : juce::Colour(0xff141b23));
    g.fillRoundedRectangle(bounds, 4.0f);
    g.setColour(focused ? juce::Colour(0xff5fd0ff) : juce::Colour(0xff2a3645));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 4.0f, focused ? 1.5f : 1.0f);

    const bool on = enableButton.getToggleState();
    g.setColour(on ? juce::Colours::white : juce::Colour(0xff6b7886));
    g.setFont(UiFonts::body(11.5f));
    auto titleArea = getLocalBounds().reduced(4).removeFromTop(18);
    titleArea.removeFromLeft(22);
    g.drawText(title, titleArea, juce::Justification::centredLeft, true);

    // Mini sparkline of the curve.
    auto r = getLocalBounds().reduced(4);
    r.removeFromTop(20);
    auto sparkArea = r.removeFromTop(juce::jmax(8, r.getHeight() - 16)).toFloat().reduced(2.0f);
    if (curve.size() >= 2 && sparkArea.getWidth() > 2.0f && sparkArea.getHeight() > 2.0f)
    {
        juce::Path path;
        for (size_t i = 0; i < curve.size(); ++i)
        {
            const float x = sparkArea.getX() + (float) curve[i].timeNorm * sparkArea.getWidth();
            const float y = sparkArea.getBottom() - (float) curve[i].valueNorm * sparkArea.getHeight();
            if (i == 0) path.startNewSubPath(x, y);
            else        path.lineTo(x, y);
        }
        g.setColour(on ? juce::Colour(0xff5fd0ff) : juce::Colour(0xff44525f));
        g.strokePath(path, juce::PathStrokeType(1.5f));
    }
}

// ─── TimbreMotionPanel ─────────────────────────────────────────────────────────

const std::array<TimbreMotionPanel::AxisDescriptor, TimbreMotionPanel::kAxisCount>&
TimbreMotionPanel::axes()
{
    static const std::array<AxisDescriptor, kAxisCount> kAxes = { {
        { "combined.centroid_axis",          "Brightness",   "bark"   },
        { "fitted_envelope.tilt_db_per_oct", "Warmth / Tilt", "dB/oct" },
        { "combined.noisiness",              "Tonal / Noise", "ratio"  },
    } };
    return kAxes;
}

TimbreMotionPanel::TimbreMotionPanel()
{
    titleLabel.setText("Timbre Motion", juce::dontSendNotification);
    titleLabel.setFont(UiFonts::body());
    titleLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(titleLabel);

    for (int i = 0; i < kAxisCount; ++i)
    {
        auto& card = cards[(size_t) i];
        card.setTitle(axes()[(size_t) i].displayName);
        card.onFocusRequested = [this, i] { focusAxis(i); };
        card.enableButton.onClick = [this, i] {
            if (updating) return;
            const bool on = cards[(size_t) i].enableButton.getToggleState();
            auto& curve = ensureCurveForAxis(i);
            curve.enabled = on;
            cards[(size_t) i].setCurve(curve.points);
            if (i == focusedAxis) focusAxis(i);
            pushPlanChanged();
        };
        card.depthSlider.onValueChange = [this, i] {
            if (updating) return;
            ensureCurveForAxis(i).depth = cards[(size_t) i].depthSlider.getValue();
            pushPlanChanged();
        };
        addAndMakeVisible(card);
    }

    editorTitleLabel.setFont(UiFonts::body());
    editorTitleLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(editorTitleLabel);

    modeLabel.setText("Mode", juce::dontSendNotification);
    modeLabel.setFont(UiFonts::small_());
    modeLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(modeLabel);

    modeCombo.addItem("Follow natural", kModeFollowNatural);
    modeCombo.addItem("Interpolate A->B", kModeInterpolateAB);
    modeCombo.addItem("Offset natural", kModeOffsetNatural);
    modeCombo.addItem("Absolute", kModeAbsolute);
    modeCombo.onChange = [this] {
        if (updating) return;
        auto& curve = ensureCurveForAxis(focusedAxis);
        curve.mode = comboIdToMode(modeCombo.getSelectedId());
        pushPlanChanged();
    };
    addAndMakeVisible(modeCombo);

    resetButton.onClick = [this] {
        auto& curve = ensureCurveForAxis(focusedAxis);
        curve.points = defaultRampPoints();
        curveEditor.setPoints(curve.points);
        cards[(size_t) focusedAxis].setCurve(curve.points);
        pushPlanChanged();
    };
    addAndMakeVisible(resetButton);

    legendLabel.setText("Drag points to shape the curve. Double-click to add/remove. "
                        "Solid = requested target over normalized time.",
                        juce::dontSendNotification);
    legendLabel.setFont(UiFonts::small_(10.0f));
    legendLabel.setColour(juce::Label::textColourId, juce::Colour(0xff8a98a8));
    legendLabel.setMinimumHorizontalScale(0.7f);
    addAndMakeVisible(legendLabel);

    curveEditor.onPointsChanged = [this](std::vector<ats::TimbreMotionPoint> pts) {
        if (updating) return;
        auto& curve = ensureCurveForAxis(focusedAxis);
        curve.points = std::move(pts);
        cards[(size_t) focusedAxis].setCurve(curve.points);
        pushPlanChanged();
    };
    addAndMakeVisible(curveEditor);

    rebuildFromPlan();
}

int TimbreMotionPanel::curveIndexForAxis(int axis) const
{
    if (axis < 0 || axis >= kAxisCount)
        return -1;
    const juce::String id(axes()[(size_t) axis].descriptorId);
    for (int i = 0; i < (int) plan.targetCurves.size(); ++i)
        if (id == plan.targetCurves[(size_t) i].descriptorId.c_str())
            return i;
    return -1;
}

ats::TimbreTargetCurve& TimbreMotionPanel::ensureCurveForAxis(int axis)
{
    const int idx = curveIndexForAxis(axis);
    if (idx >= 0)
        return plan.targetCurves[(size_t) idx];

    ats::TimbreTargetCurve curve;
    curve.descriptorId = axes()[(size_t) axis].descriptorId;
    curve.mode         = ats::TimbreTargetMode::InterpolateAB;
    curve.enabled      = cards[(size_t) axis].enableButton.getToggleState();
    curve.depth        = cards[(size_t) axis].depthSlider.getValue();
    curve.smoothingMs  = 40.0;
    curve.points       = defaultRampPoints();
    plan.targetCurves.push_back(std::move(curve));
    return plan.targetCurves.back();
}

void TimbreMotionPanel::focusAxis(int axis)
{
    focusedAxis = juce::jlimit(0, kAxisCount - 1, axis);
    for (int i = 0; i < kAxisCount; ++i)
        cards[(size_t) i].setFocused(i == focusedAxis);

    const juce::ScopedValueSetter<bool> guard(updating, true);
    editorTitleLabel.setText(juce::String(axes()[(size_t) focusedAxis].displayName)
                                 + "  (" + axes()[(size_t) focusedAxis].units + ")",
                             juce::dontSendNotification);

    const int idx = curveIndexForAxis(focusedAxis);
    if (idx >= 0)
    {
        modeCombo.setSelectedId(modeToComboId(plan.targetCurves[(size_t) idx].mode),
                                juce::dontSendNotification);
        curveEditor.setPoints(plan.targetCurves[(size_t) idx].points);
    }
    else
    {
        modeCombo.setSelectedId(kModeInterpolateAB, juce::dontSendNotification);
        curveEditor.setPoints(defaultRampPoints());
    }
}

void TimbreMotionPanel::rebuildFromPlan()
{
    const juce::ScopedValueSetter<bool> guard(updating, true);
    for (int i = 0; i < kAxisCount; ++i)
    {
        const int idx = curveIndexForAxis(i);
        const bool enabled = idx >= 0 && plan.targetCurves[(size_t) idx].enabled;
        cards[(size_t) i].setCardEnabled(enabled);
        cards[(size_t) i].setDepth(idx >= 0 ? plan.targetCurves[(size_t) idx].depth : 1.0);
        cards[(size_t) i].setCurve(idx >= 0 ? plan.targetCurves[(size_t) idx].points : defaultRampPoints());
    }
    focusAxis(focusedAxis);
}

void TimbreMotionPanel::setPlan(const ats::TimbreMotionPlan& nextPlan)
{
    plan = nextPlan;
    rebuildFromPlan();
}

void TimbreMotionPanel::pushPlanChanged()
{
    if (!updating && onPlanChanged)
        onPlanChanged(plan);
}

void TimbreMotionPanel::resized()
{
    auto r = getLocalBounds();
    titleLabel.setBounds(r.removeFromTop(16));

    // Macro rack: three equal cards.
    auto rack = r.removeFromTop(54);
    const int cardW = juce::jmax(1, rack.getWidth() / kAxisCount);
    for (int i = 0; i < kAxisCount; ++i)
    {
        auto cell = (i == kAxisCount - 1) ? rack : rack.removeFromLeft(cardW);
        cards[(size_t) i].setBounds(cell.reduced(2));
    }

    r.removeFromTop(4);

    // Editor header: title (left) + mode (right) + reset.
    auto header = r.removeFromTop(22);
    resetButton.setBounds(header.removeFromRight(54).reduced(1));
    header.removeFromRight(4);
    modeCombo.setBounds(header.removeFromRight(128).reduced(1));
    modeLabel.setBounds(header.removeFromRight(38));
    editorTitleLabel.setBounds(header);

    legendLabel.setBounds(r.removeFromTop(14));
    curveEditor.setBounds(r.reduced(0, 1));
}

void TimbreMotionPanel::paint(juce::Graphics&)
{
}
