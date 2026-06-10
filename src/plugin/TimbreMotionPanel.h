#pragma once

#include <array>
#include <functional>
#include <vector>

#include <JuceHeader.h>

#include "TimbreMotionPlan.h"

// Single-curve breakpoint editor for a normalized timbre motion curve (time 0..1, value 0..1).
// Mirrors the morph timeline editor's interaction model: drag points, double-click to add/remove.
// The first and last breakpoints are time-locked to 0 and 1 (endpoint lock, spec §3.3); their
// value and all interior points are freely draggable.
class TimbreCurveEditor : public juce::Component
{
public:
    TimbreCurveEditor();

    void setPoints(std::vector<ats::TimbreMotionPoint> nextPoints);
    [[nodiscard]] std::vector<ats::TimbreMotionPoint> getPoints() const { return points; }
    [[nodiscard]] bool isEditing() const { return editing; }

    // Optional normalized playhead [0,1]; <0 hides it.
    void setPlayheadNorm(double t);

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;

    std::function<void(std::vector<ats::TimbreMotionPoint>)> onPointsChanged;

private:
    [[nodiscard]] juce::Rectangle<float> getPlotArea() const;
    [[nodiscard]] juce::Point<float> positionForPoint(const ats::TimbreMotionPoint& p) const;
    [[nodiscard]] int findNearestPoint(juce::Point<float> pos) const;
    void sortAndClamp();
    void notifyChange();

    std::vector<ats::TimbreMotionPoint> points;
    int selectedPoint = -1;
    bool editing = false;
    double playheadNorm = -1.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TimbreCurveEditor)
};

// Compact macro card: title + enable toggle + depth slider, with a mini sparkline of its curve.
// Clicking the card body focuses its descriptor in the panel's editor.
class TimbreMacroCard : public juce::Component
{
public:
    TimbreMacroCard();

    void setTitle(const juce::String& t) { title = t; repaint(); }
    void setCurve(std::vector<ats::TimbreMotionPoint> pts) { curve = std::move(pts); repaint(); }
    void setFocused(bool f) { focused = f; repaint(); }
    void setCardEnabled(bool e) { enableButton.setToggleState(e, juce::dontSendNotification); repaint(); }
    void setDepth(double d) { depthSlider.setValue(d, juce::dontSendNotification); }

    void resized() override;
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;

    juce::ToggleButton enableButton;
    juce::Slider depthSlider;

    std::function<void()> onFocusRequested;

private:
    juce::String title;
    std::vector<ats::TimbreMotionPoint> curve;
    bool focused = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TimbreMacroCard)
};

// Timbre Motion panel (master spec §8.2): a factory macro rack (Brightness / Warmth-Tilt /
// Tonal-Noise) above a focused curve editor with a mode selector. Edits write the recipe's
// timbre_motion_plan via the onPlanChanged callback (recipe is authoritative, §12).
class TimbreMotionPanel : public juce::Component
{
public:
    static constexpr int kAxisCount = 3; // brightness, warmth/tilt, tonal/noise

    TimbreMotionPanel();

    void setPlan(const ats::TimbreMotionPlan& plan);
    [[nodiscard]] ats::TimbreMotionPlan getPlan() const { return plan; }
    [[nodiscard]] bool isEditing() const { return curveEditor.isEditing(); }

    void setPlayheadNorm(double t) { curveEditor.setPlayheadNorm(t); }

    void resized() override;
    void paint(juce::Graphics&) override;

    // Fired whenever the user changes the plan (enable, depth, mode, or curve points).
    std::function<void(const ats::TimbreMotionPlan&)> onPlanChanged;

private:
    struct AxisDescriptor
    {
        const char* descriptorId;
        const char* displayName;
        const char* units;
    };
    static const std::array<AxisDescriptor, kAxisCount>& axes();

    [[nodiscard]] int curveIndexForAxis(int axis) const;       // index into plan.targetCurves, or -1
    ats::TimbreTargetCurve& ensureCurveForAxis(int axis);      // creates a default curve if missing
    void focusAxis(int axis);
    void pushPlanChanged();
    void rebuildFromPlan();

    ats::TimbreMotionPlan plan;
    int focusedAxis = 0;
    bool updating = false; // guards programmatic control updates from firing onPlanChanged

    juce::Label titleLabel;
    std::array<TimbreMacroCard, kAxisCount> cards;

    juce::Label editorTitleLabel;
    juce::Label modeLabel;
    juce::ComboBox modeCombo;
    juce::TextButton resetButton { "Reset" };
    juce::Label legendLabel;
    TimbreCurveEditor curveEditor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TimbreMotionPanel)
};
