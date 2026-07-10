#pragma once

#include <JuceHeader.h>

#include "UiConstants.h"

namespace ats_ui
{
/** Segmented radio (spec §7): a capsule well with one accent "active" pill.
    Font 12.5 for <=3 segments, 10 for more. Emits onChange with the new index. */
class SegmentedRadio : public juce::Component
{
public:
    SegmentedRadio() = default;

    void setItems(juce::StringArray newItems, int initialIndex = 0)
    {
        items = std::move(newItems);
        selected = juce::jlimit(0, juce::jmax(0, items.size() - 1), initialIndex);
        repaint();
    }

    void setSelectedIndex(int index, bool notify)
    {
        index = juce::jlimit(0, juce::jmax(0, items.size() - 1), index);
        if (index == selected)
            return;
        selected = index;
        repaint();
        if (notify && onChange)
            onChange(selected);
    }

    int getSelectedIndex() const { return selected; }

    void setGreyedOut(bool shouldGrey)
    {
        greyed = shouldGrey;
        setInterceptsMouseClicks(!greyed, !greyed);
        repaint();
    }

    std::function<void(int)> onChange;

    void paint(juce::Graphics& g) override
    {
        const auto& p = activePalette();
        auto r = getLocalBounds().toFloat();
        const float radius = juce::jmin(r.getHeight() * 0.5f, 12.0f);

        drawWell(g, r, radius);

        if (items.isEmpty())
            return;

        const float segW = r.getWidth() / (float) items.size();
        const float fontSize = items.size() > 3 ? 10.0f : 12.5f;
        const float veil = greyed ? 0.35f : 1.0f;

        for (int i = 0; i < items.size(); ++i)
        {
            auto seg = r.withX(r.getX() + segW * (float) i).withWidth(segW);
            const bool active = (i == selected);
            const bool hot = (i == hover) && !greyed;

            if (active)
            {
                auto pill = seg.reduced(3.0f);
                const float pillRadius = juce::jmax(2.0f, radius - 3.0f);
                if (greyed)
                {
                    g.setColour(p.textFaint.withAlpha(0.18f));
                    g.fillRoundedRectangle(pill, pillRadius);
                }
                else
                {
                    drawRaisedPanel(g, pill, pillRadius);
                    g.setColour(p.accent.withAlpha(p.dark ? 0.16f : 0.10f));
                    g.fillRoundedRectangle(pill, pillRadius);
                    g.setColour(p.accent.withAlpha(0.80f));
                    g.drawRoundedRectangle(pill.reduced(0.75f), pillRadius, 1.2f);
                }
            }
            else if (hot)
            {
                auto pill = seg.reduced(3.0f);
                const float pillRadius = juce::jmax(2.0f, radius - 3.0f);
                g.setColour(p.accent.withAlpha(0.08f));
                g.fillRoundedRectangle(pill, pillRadius);
            }

            juce::Colour textCol = active ? (greyed ? p.textFaint : p.accent)
                                          : (hot ? p.textPrimary : p.textSecondary);
            g.setColour(textCol.withAlpha(veil));
            g.setFont(font::ui(fontSize));
            g.drawFittedText(items[i], seg.toNearestInt(), juce::Justification::centred, 1, 0.85f);
        }

        if (greyed)
        {
            g.setColour(p.panel.withAlpha(p.dark ? 0.28f : 0.18f));
            g.fillRoundedRectangle(r, radius);
        }
    }

    void mouseMove(const juce::MouseEvent& e) override
    {
        const int idx = indexAt(e.position.x);
        if (idx != hover) { hover = idx; repaint(); }
    }

    void mouseExit(const juce::MouseEvent&) override
    {
        if (hover != -1) { hover = -1; repaint(); }
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (greyed)
            return;
        const int idx = indexAt(e.position.x);
        if (idx >= 0)
            setSelectedIndex(idx, true);
    }

private:
    int indexAt(float x) const
    {
        if (items.isEmpty())
            return -1;
        const float segW = (float) getWidth() / (float) items.size();
        return juce::jlimit(0, items.size() - 1, (int) (x / segW));
    }

    juce::StringArray items;
    int selected = 0;
    int hover = -1;
    bool greyed = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SegmentedRadio)
};

/** ENGINE worker-activity meter (spec §8.4): caption + 2 labelled dots.
    Lit = accent while the worker runs; unlit = faint. Poll `setState` at 15fps. */
class ActivityDots : public juce::Component
{
public:
    ActivityDots() = default;

    void setState(bool analysisRunning, bool renderRunning)
    {
        if (analysisRunning != anl || renderRunning != rnd)
        {
            anl = analysisRunning;
            rnd = renderRunning;
            repaint();
        }
    }

    void paint(juce::Graphics& g) override
    {
        const auto& p = activePalette();
        auto r = getLocalBounds().toFloat();

        g.setColour(p.textMuted);
        g.setFont(sectionFont());
        g.drawText("ENGINE", r.removeFromTop(11.0f).toNearestInt(),
                   juce::Justification::centredLeft, false);

        const char* labels[2] = { "ANL", "RND" };
        const bool lit[2] = { anl, rnd };
        const float cellW = r.getWidth() / 2.0f;
        for (int i = 0; i < 2; ++i)
        {
            auto cell = r.withX(r.getX() + cellW * (float) i).withWidth(cellW);
            auto dot = juce::Rectangle<float>(0, 0, 6.0f, 6.0f)
                           .withCentre({ cell.getCentreX(), cell.getY() + 8.0f });
            if (lit[i])
            {
                if (p.dark)
                {
                    g.setColour(p.accent.withAlpha(0.3f));
                    g.fillEllipse(dot.expanded(3.0f));
                }
                g.setColour(p.accent);
            }
            else
            {
                g.setColour(p.dark ? p.textFaint.withAlpha(0.5f) : p.panelBorder);
            }
            g.fillEllipse(dot);

            g.setColour(p.textFaint);
            g.setFont(monoFont().withHeight(8.5f));
            g.drawText(labels[i], cell.withTop(cell.getY() + 14.0f).toNearestInt(),
                       juce::Justification::centredTop, false);
        }
    }

private:
    static juce::Font sectionFont() { return juce::Font(10.0f).withExtraKerningFactor(0.2f); }
    static juce::Font monoFont() { return juce::Font(juce::Font::getDefaultMonospacedFontName(), 10.5f, juce::Font::plain); }

    bool anl = false, rnd = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ActivityDots)
};

/** Small inset "cheek" button (spec §4 radius 5): drawWell base, accent wash
    when active/down. Used on keyboard, source cards, snapshot strip. */
class CheekButton : public juce::Button
{
public:
    explicit CheekButton(const juce::String& text = {}) : juce::Button(text)
    {
        setButtonText(text);
    }

    void setMonoText(bool m) { monoText = m; repaint(); }

    void paintButton(juce::Graphics& g, bool highlighted, bool down) override
    {
        const auto& p = activePalette();
        auto r = getLocalBounds().toFloat().reduced(0.5f);
        const bool active = getToggleState();
        const float alpha = isEnabled() ? 1.0f : 0.4f;

        drawWell(g, r, 5.0f);

        if (active)
        {
            g.setColour(p.accent.withAlpha(0.24f * alpha));
            g.fillRoundedRectangle(r, 5.0f);
            g.setColour(p.accent.withAlpha(0.9f * alpha));
            g.drawRoundedRectangle(r.reduced(0.5f), 5.0f, 1.0f);
        }
        else if (down)
        {
            g.setColour(p.accent.withAlpha(0.15f * alpha));
            g.fillRoundedRectangle(r, 5.0f);
        }
        else if (highlighted)
        {
            g.setColour(p.accent.withAlpha(0.08f * alpha));
            g.fillRoundedRectangle(r, 5.0f);
        }

        g.setColour((active ? p.textPrimary : p.textSecondary).withAlpha(alpha));
        g.setFont(monoText ? font::mono(10.5f) : font::ui(11.0f));
        g.drawFittedText(getButtonText(), getLocalBounds().reduced(3, 1),
                         juce::Justification::centred, 1, 0.7f);
    }

private:
    bool monoText = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CheekButton)
};

/** Source A/B card (spec §7.3). Raised panel, letter chip + filename, three
    cheek action buttons, and an optional audition strip (Analysis 84px variant). */
class SourceCard : public juce::Component
{
public:
    SourceCard()
    {
        for (auto* b : { &btn0, &btn1, &btn2 })
            addAndMakeVisible(b);
        addChildComponent(audition);
    }

    void setLetter(const juce::String& s)   { letter = s; repaint(); }
    void setFilename(const juce::String& s) { filename = s; repaint(); }
    void setHasModel(bool h)                { hasModel = h; repaint(); }

    void setActionLabels(const juce::String& a, const juce::String& b, const juce::String& c)
    {
        btn0.setButtonText(a);
        btn1.setButtonText(b);
        btn2.setButtonText(c);
    }

    void setShowAudition(bool show)
    {
        showAudition = show;
        audition.setVisible(show);
        resized();
    }

    CheekButton& action0() { return btn0; }
    CheekButton& action1() { return btn1; }
    CheekButton& action2() { return btn2; }
    SegmentedRadio& auditionStrip() { return audition; }

    static constexpr int baseHeight     = 56;
    static constexpr int auditionHeight = 84;

    void paint(juce::Graphics& g) override
    {
        const auto& p = activePalette();
        drawRaisedPanel(g, getLocalBounds().toFloat(), 8.0f);

        auto inner = getLocalBounds().reduced(8, 6);
        auto row1 = inner.removeFromTop(20);

        // Letter chip 18x14.
        auto chip = row1.removeFromLeft(18).withSizeKeepingCentre(18, 14).toFloat();
        g.setColour((hasModel ? p.accent : p.textFaint).withAlpha(p.dark ? 0.9f : 0.85f));
        g.fillRoundedRectangle(chip, 3.0f);
        g.setColour(hasModel ? (p.dark ? p.window : p.panel) : (p.dark ? p.window : p.textPrimary));
        g.setFont(font::ui(8.5f));
        g.drawText(letter, chip.toNearestInt(), juce::Justification::centred, false);

        row1.removeFromLeft(6);
        g.setColour(p.textPrimary);
        g.setFont(font::mono(10.5f));
        g.drawText(filename.isNotEmpty() ? filename : juce::String("(empty)"),
                   row1, juce::Justification::centredLeft, true);
    }

    void resized() override
    {
        auto inner = getLocalBounds().reduced(8, 6);
        inner.removeFromTop(20); // row 1 (painted)
        inner.removeFromTop(2);

        auto row2 = inner.removeFromTop(22);
        const int gap = 4;
        const int bw = (row2.getWidth() - 2 * gap) / 3;
        btn0.setBounds(row2.removeFromLeft(bw));
        row2.removeFromLeft(gap);
        btn1.setBounds(row2.removeFromLeft(bw));
        row2.removeFromLeft(gap);
        btn2.setBounds(row2);

        if (showAudition)
        {
            inner.removeFromTop(4);
            audition.setBounds(inner.removeFromTop(24));
        }
    }

private:
    juce::String letter { "A" }, filename;
    bool hasModel = false;
    bool showAudition = false;

    CheekButton btn0 { "ANALYZE" }, btn1 { "USE CUR" }, btn2 { "EXPORT" };
    SegmentedRadio audition;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SourceCard)
};

/** Quick snapshot strip (spec §7.1): 5 cheek buttons at 0/25/50/75/100 that set
    morphAmount; the nearest becomes active. */
class SnapshotStrip : public juce::Component
{
public:
    SnapshotStrip()
    {
        static const char* labels[5] = { "0", "25", "50", "75", "100" };
        for (int i = 0; i < 5; ++i)
        {
            buttons[i] = std::make_unique<CheekButton>(labels[i]);
            buttons[i]->setMonoText(true);
            const float v = (float) i * 0.25f;
            buttons[i]->onClick = [this, v] { if (onSelect) onSelect(v); };
            addAndMakeVisible(*buttons[i]);
        }
    }

    /** Highlight the button nearest to the given morph amount (0..1). */
    void setAmount(float amount)
    {
        const int nearest = juce::jlimit(0, 4, (int) std::round(amount * 4.0f));
        for (int i = 0; i < 5; ++i)
            buttons[i]->setToggleState(i == nearest, juce::dontSendNotification);
    }

    std::function<void(float)> onSelect;

    void resized() override
    {
        auto r = getLocalBounds();
        const int gap = 6;
        const int bw = (r.getWidth() - 4 * gap) / 5;
        for (int i = 0; i < 5; ++i)
        {
            buttons[i]->setBounds(r.removeFromLeft(bw));
            r.removeFromLeft(gap);
        }
    }

private:
    std::unique_ptr<CheekButton> buttons[5];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SnapshotStrip)
};

/** Morph lane row (spec §7.1): 58px caption, slider, mono value, 34x18 link pill. */
class LaneRow : public juce::Component
{
public:
    explicit LaneRow(const juce::String& caption)
    {
        label = caption;
        slider.setSliderStyle(juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        addAndMakeVisible(slider);

        link.setButtonText({});
        addAndMakeVisible(link);
    }

    juce::Slider& getSlider()      { return slider; }
    juce::ToggleButton& getLink()  { return link; }

    /** Reflect linked state: linked lane slider is dimmed (mirrors morphAmount). */
    void setLinked(bool linked)
    {
        slider.setEnabled(!linked);
        slider.setAlpha(linked ? 0.4f : 1.0f);
        link.setToggleState(linked, juce::dontSendNotification);
    }

    void updateValueText()
    {
        valueText = juce::String(slider.getValue(), 2);
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        const auto& p = activePalette();
        auto r = getLocalBounds();
        g.setColour(p.textSecondary);
        g.setFont(font::ui(12.0f));
        g.drawText(label, r.removeFromLeft(58), juce::Justification::centredLeft, false);

        auto valueArea = getLocalBounds().withTrimmedRight(34 + 6).removeFromRight(40);
        g.setColour(p.textPrimary);
        g.setFont(font::mono(11.0f));
        g.drawText(valueText, valueArea, juce::Justification::centredRight, false);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        r.removeFromLeft(58);
        link.setBounds(r.removeFromRight(34).withSizeKeepingCentre(34, 18));
        r.removeFromRight(6);
        r.removeFromRight(40); // value readout (painted)
        r.removeFromRight(4);
        slider.setBounds(r);
    }

private:
    juce::String label, valueText { "0.00" };
    juce::Slider slider;
    juce::ToggleButton link;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LaneRow)
};

/** Column layout engine (spec §10.7): top-down placement with explicit pairing
    and an overflow guard that hides (never squashes) controls that don't fit. */
class ColumnLayout
{
public:
    void addHeading(const juce::String& text, bool topDivider)
    {
        items.push_back({ nullptr, nullptr, sectionHeadingHeight(topDivider), 0, true, text, topDivider });
    }

    void add(juce::Component* c, int height, int gapAbove = 2)
    {
        items.push_back({ c, nullptr, height, gapAbove, false, {}, false });
    }

    void addPair(juce::Component* a, juce::Component* b, int height, int gapAbove = 2)
    {
        items.push_back({ a, b, height, gapAbove, false, {}, false });
    }

    void addGap(int px) { items.push_back({ nullptr, nullptr, px, 0, false, {}, false, true }); }

    void clear() { items.clear(); }

    /** Position child components within `column` (top-down). Returns the y used. */
    int performLayout(juce::Rectangle<int> column)
    {
        headings.clear();
        int y = column.getY();
        const int left = column.getX();
        const int width = column.getWidth();
        const int bottom = column.getBottom();

        for (auto& it : items)
        {
            if (it.spacer)
            {
                y += it.height;
                continue;
            }

            y += it.gapAbove;

            if (it.isHeading)
            {
                headings.push_back({ juce::Rectangle<int>(left, y, width, it.height), it.headingText, it.topDivider });
                y += it.height;
                continue;
            }

            // Overflow guard: hide controls that no longer fit at full height.
            const bool fits = (y + it.height) <= bottom && it.height >= 1;
            if (!fits)
            {
                if (it.a) it.a->setBounds({});
                if (it.b) it.b->setBounds({});
                continue;
            }

            juce::Rectangle<int> row(left, y, width, it.height);
            if (it.b != nullptr)
            {
                const int half = width / 2 - 5;
                it.a->setBounds(row.getX(), y, half, it.height);
                it.b->setBounds(row.getRight() - half, y, half, it.height);
            }
            else
            {
                it.a->setBounds(row);
            }
            y += it.height;
        }
        return y;
    }

    /** Section headings collected during the last performLayout, to paint in paint(). */
    struct Heading { juce::Rectangle<int> block; juce::String text; bool topDivider; };
    const std::vector<Heading>& getHeadings() const { return headings; }
    void resetHeadings() { headings.clear(); }

private:
    struct Item
    {
        juce::Component* a = nullptr;
        juce::Component* b = nullptr;
        int height = 0;
        int gapAbove = 0;
        bool isHeading = false;
        juce::String headingText;
        bool topDivider = false;
        bool spacer = false;
    };

    std::vector<Item> items;
    std::vector<Heading> headings;
};
} // namespace ats_ui
