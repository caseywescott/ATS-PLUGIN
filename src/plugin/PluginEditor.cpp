#include "PluginEditor.h"

#include "PluginProcessor.h"

namespace
{
void drawChevron(juce::Graphics& g, juce::Rectangle<float> area)
{
    juce::Path arrow;
    arrow.startNewSubPath(area.getX(), area.getY());
    arrow.lineTo(area.getCentreX(), area.getBottom());
    arrow.lineTo(area.getRight(), area.getY());
    g.strokePath(arrow, juce::PathStrokeType(1.7f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

juce::Path connectedButtonPath(const juce::Button& button, juce::Rectangle<float> r, float radius)
{
    juce::Path path;
    path.addRoundedRectangle(r.getX(),
                             r.getY(),
                             r.getWidth(),
                             r.getHeight(),
                             radius,
                             radius,
                             !button.isConnectedOnLeft() && !button.isConnectedOnTop(),
                             !button.isConnectedOnRight() && !button.isConnectedOnTop(),
                             !button.isConnectedOnLeft() && !button.isConnectedOnBottom(),
                             !button.isConnectedOnRight() && !button.isConnectedOnBottom());
    return path;
}

void drawSegmentedRadioButtonBackground(juce::Graphics& g,
                                        juce::Button& button,
                                        bool highlighted,
                                        bool down)
{
    const auto& p = ats_ui::activePalette();
    const auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
    const float alpha = button.isEnabled() ? 1.0f : 0.38f;
    const float radius = juce::jmin(bounds.getHeight() * 0.5f, 12.0f);
    const auto shell = connectedButtonPath(button, bounds, radius);

    g.setColour(p.well.withAlpha(alpha));
    g.fillPath(shell);
    g.setColour((p.dark ? p.shadowDark : p.wellBorder).withAlpha((p.dark ? 0.86f : 1.0f) * alpha));
    g.strokePath(shell, juce::PathStrokeType(1.0f));

    if (p.dark)
    {
        g.setColour(p.shadowLight.withAlpha(0.30f * alpha));
        g.drawLine(bounds.getX() + 2.0f,
                   bounds.getBottom() - 1.5f,
                   bounds.getRight() - 2.0f,
                   bounds.getBottom() - 1.5f,
                   1.0f);
    }

    const bool active = button.getToggleState();
    if (active)
    {
        auto pill = bounds.reduced(3.0f);
        const float pillRadius = juce::jmax(2.0f, radius - 3.0f);

        ats_ui::drawRaisedPanel(g, pill, pillRadius);
        g.setColour(p.accent.withAlpha((p.dark ? 0.16f : 0.10f) * alpha));
        g.fillRoundedRectangle(pill, pillRadius);
        g.setColour(p.accent.withAlpha((down ? 0.92f : 0.80f) * alpha));
        g.drawRoundedRectangle(pill.reduced(0.75f), pillRadius, 1.2f);
    }
    else if (highlighted && button.isEnabled())
    {
        auto hover = bounds.reduced(3.0f);
        g.setColour(p.accent.withAlpha(0.08f));
        g.fillRoundedRectangle(hover, juce::jmax(2.0f, radius - 3.0f));
    }
}

void drawSegmentedRadioButtonText(juce::Graphics& g, juce::Button& button)
{
    const auto& p = ats_ui::activePalette();
    const bool active = button.getToggleState();
    const float alpha = button.isEnabled() ? 1.0f : 0.5f;

    g.setColour((active ? p.accent : p.textSecondary).withAlpha(alpha));
    g.setFont(ats_ui::font::ui(juce::jlimit(10.0f, 12.5f, button.getHeight() * 0.42f)));
    g.drawFittedText(button.getButtonText(),
                     button.getLocalBounds().reduced(6, 1),
                     juce::Justification::centred,
                     1,
                     0.85f);
}
} // namespace

AtsLookAndFeel::AtsLookAndFeel()
{
    ats_ui::applyEditorPalette(*this);
}

void AtsLookAndFeel::drawButtonBackground(juce::Graphics& g,
                                          juce::Button& button,
                                          const juce::Colour&,
                                          bool shouldDrawButtonAsHighlighted,
                                          bool shouldDrawButtonAsDown)
{
    if (button.getRadioGroupId() != 0)
    {
        drawSegmentedRadioButtonBackground(g, button, shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);
        return;
    }

    const auto& p = ats_ui::activePalette();
    const auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
    const float radius = 8.0f;
    const bool down = shouldDrawButtonAsDown || button.getToggleState();

    // Alert-window buttons: SESSION-style — dark fill, slim amber border.
    if (button.findParentComponentOfClass<juce::AlertWindow>() != nullptr)
    {
        const float borderW = down ? 1.4f : 1.0f;
        g.setColour(p.window.brighter(0.06f));
        g.fillRoundedRectangle(bounds, radius);
        if (shouldDrawButtonAsHighlighted || down)
        {
            g.setColour(p.accent.withAlpha(0.10f));
            g.fillRoundedRectangle(bounds, radius);
        }
        g.setColour(p.accent.withAlpha(down ? 0.90f : 0.70f));
        g.drawRoundedRectangle(bounds.reduced(borderW * 0.5f), radius, borderW);
        return;
    }

    if (down)
    {
        // Presses into a sunken accent-tinted well.
        ats_ui::drawWell(g, bounds, radius);
        g.setColour(p.accent.withAlpha(0.15f));
        g.fillRoundedRectangle(bounds, radius);
        return;
    }

    if (p.dark)
    {
        g.setColour(p.shadowDark.withAlpha(0.4f));
        g.fillRoundedRectangle(bounds.translated(1.5f, 1.5f), radius);
        g.setColour(p.shadowLight.withAlpha(0.35f));
        g.fillRoundedRectangle(bounds.translated(-1.5f, -1.5f), radius);
        g.setColour(shouldDrawButtonAsHighlighted ? p.window.brighter(0.1f) : p.window.brighter(0.05f));
        g.fillRoundedRectangle(bounds, radius);
    }
    else
    {
        g.setColour(shouldDrawButtonAsHighlighted ? p.window : p.panel);
        g.fillRoundedRectangle(bounds, radius);
        g.setColour(shouldDrawButtonAsHighlighted ? p.accent.withAlpha(0.6f) : p.panelBorder);
        g.drawRoundedRectangle(bounds, radius, 1.0f);
    }
}

void AtsLookAndFeel::drawButtonText(juce::Graphics& g,
                                    juce::TextButton& button,
                                    bool,
                                    bool shouldDrawButtonAsDown)
{
    if (button.getRadioGroupId() != 0)
    {
        drawSegmentedRadioButtonText(g, button);
        return;
    }

    const auto& p = ats_ui::activePalette();
    const float alpha = button.isEnabled() ? 1.0f : 0.48f;

    // Alert-window buttons: amber text, bold, matching SESSION style.
    if (button.findParentComponentOfClass<juce::AlertWindow>() != nullptr)
    {
        g.setFont(ats_ui::font::uiBold(juce::jlimit(11.0f, 13.0f, button.getHeight() * 0.46f)));
        g.setColour(p.accent.withAlpha(shouldDrawButtonAsDown ? 0.70f : alpha));
        g.drawFittedText(button.getButtonText(),
                         button.getLocalBounds().reduced(8, 1),
                         juce::Justification::centred, 1, 0.9f);
        return;
    }

    g.setColour(ats_ui::colour::textPrimary().withAlpha(alpha));
    g.setFont(juce::Font(juce::jlimit(10.5f, 12.5f, button.getHeight() * 0.44f),
                         button.getToggleState() ? juce::Font::bold : juce::Font::plain));
    g.drawFittedText(button.getButtonText(),
                     button.getLocalBounds().reduced(8, 1),
                     juce::Justification::centred,
                     1,
                     0.68f);
}

void AtsLookAndFeel::drawToggleButton(juce::Graphics& g,
                                      juce::ToggleButton& button,
                                      bool shouldDrawButtonAsHighlighted,
                                      bool shouldDrawButtonAsDown)
{
    if (button.getRadioGroupId() != 0)
    {
        drawSegmentedRadioButtonBackground(g, button, shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);
        drawSegmentedRadioButtonText(g, button);
        return;
    }

    juce::ignoreUnused(shouldDrawButtonAsDown);

    const auto& p = ats_ui::activePalette();
    const bool on = button.getToggleState();
    const float alpha = button.isEnabled() ? 1.0f : 0.4f;
    const bool hasText = button.getButtonText().isNotEmpty();

    auto area = button.getLocalBounds().toFloat();
    const bool rowLayout = hasText && area.getWidth() >= 68.0f;
    const float pillW = juce::jmin(area.getWidth(), 42.0f);
    const float pillH = juce::jmin(area.getHeight(), 21.0f);

    juce::Rectangle<float> pill;
    juce::Rectangle<int> textArea;
    if (rowLayout)
    {
        auto pillSlot = area.removeFromLeft(48.0f);
        pill = pillSlot.withSizeKeepingCentre(pillW, pillH);
        textArea = area.toNearestInt().withTrimmedLeft(4);
    }
    else
    {
        pill = area.withSizeKeepingCentre(pillW, pillH);
    }

    const float radius = pillH * 0.5f;
    ats_ui::drawWell(g, pill, radius);
    if (on)
    {
        g.setColour(p.accent.withAlpha((p.dark ? 0.30f : 0.18f) * alpha));
        g.fillRoundedRectangle(pill, radius);
    }

    const float thumbR = juce::jmax(2.0f, radius - 3.0f);
    const float thumbX = on ? pill.getRight() - radius : pill.getX() + radius;
    const juce::Rectangle<float> thumb(thumbX - thumbR,
                                       pill.getCentreY() - thumbR,
                                       thumbR * 2.0f,
                                       thumbR * 2.0f);

    if (on && p.dark)
    {
        g.setColour(p.accent.withAlpha(0.28f * alpha));
        g.fillEllipse(thumb.expanded(4.0f));
    }

    const auto thumbColour = on ? p.accent : (shouldDrawButtonAsHighlighted ? p.textSecondary : p.textMuted);
    g.setColour(thumbColour.withAlpha(alpha));
    g.fillEllipse(thumb);

    if (rowLayout)
    {
        g.setColour(p.textSecondary.withAlpha(alpha));
        g.setFont(ats_ui::font::ui(juce::jlimit(9.5f, 12.0f, button.getHeight() * 0.42f)));
        g.drawFittedText(button.getButtonText(), textArea, juce::Justification::centredLeft, 1, 0.7f);
    }
}

void AtsLookAndFeel::drawComboBox(juce::Graphics& g,
                                  int width,
                                  int height,
                                  bool isButtonDown,
                                  int,
                                  int,
                                  int,
                                  int,
                                  juce::ComboBox& box)
{
    juce::ignoreUnused(isButtonDown);
    const auto& p = ats_ui::activePalette();
    const auto bounds = juce::Rectangle<float>(0.0f,
                                               0.0f,
                                               static_cast<float>(width),
                                               static_cast<float>(height));

    ats_ui::drawWell(g, bounds.reduced(0.5f), 8.0f);

    if (box.hasKeyboardFocus(false))
    {
        g.setColour(p.accent.withAlpha(box.isEnabled() ? 0.72f : 0.30f));
        g.drawRoundedRectangle(bounds.reduced(1.0f), 7.5f, 1.0f);
    }

    juce::Path chevron;
    const auto cx = static_cast<float>(width) - 13.0f;
    const auto cy = static_cast<float>(height) * 0.5f;
    chevron.startNewSubPath(cx - 4.0f, cy - 2.0f);
    chevron.lineTo(cx, cy + 2.5f);
    chevron.lineTo(cx + 4.0f, cy - 2.0f);

    g.setColour(box.isEnabled() ? p.accent : p.textFaint);
    g.strokePath(chevron, juce::PathStrokeType(1.6f,
                                               juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
}

juce::Font AtsLookAndFeel::getComboBoxFont(juce::ComboBox&)
{
    return ats_ui::font::ui(13.0f);
}

void AtsLookAndFeel::positionComboBoxText(juce::ComboBox& box, juce::Label& label)
{
    label.setBounds(8, 1, box.getWidth() - 30, box.getHeight() - 2);
    label.setFont(getComboBoxFont(box));
    label.setJustificationType(juce::Justification::centredLeft);
}

juce::Font AtsLookAndFeel::getPopupMenuFont()
{
    return ats_ui::font::ui(13.0f);
}

juce::Font AtsLookAndFeel::getLabelFont(juce::Label& label)
{
    return label.getFont();
}

juce::Label* AtsLookAndFeel::createSliderTextBox(juce::Slider& slider)
{
    const auto& p = ats_ui::activePalette();
    auto* label = juce::LookAndFeel_V4::createSliderTextBox(slider);
    label->setFont(ats_ui::font::mono(11.5f));
    label->setJustificationType(slider.getTextBoxPosition() == juce::Slider::TextBoxRight
                                    ? juce::Justification::centredRight
                                    : juce::Justification::centred);
    label->setColour(juce::Label::textColourId, p.textPrimary);
    label->setColour(juce::Label::outlineWhenEditingColourId, p.accent);
    return label;
}

void AtsLookAndFeel::drawLinearSlider(juce::Graphics& g,
                                      int x,
                                      int y,
                                      int width,
                                      int height,
                                      float sliderPos,
                                      float minSliderPos,
                                      float maxSliderPos,
                                      const juce::Slider::SliderStyle style,
                                      juce::Slider& slider)
{
    juce::ignoreUnused(minSliderPos, maxSliderPos);

    if (style != juce::Slider::LinearHorizontal && style != juce::Slider::LinearBar)
    {
        juce::LookAndFeel_V4::drawLinearSlider(g,
                                               x,
                                               y,
                                               width,
                                               height,
                                               sliderPos,
                                               minSliderPos,
                                               maxSliderPos,
                                               style,
                                               slider);
        return;
    }

    const auto& p = ats_ui::activePalette();
    const float fx = (float) x;
    const float fw = (float) width;
    const float cy = (float) y + (float) height * 0.5f;
    const float trackH = juce::jlimit(4.0f, 6.0f, (float) height * 0.32f);
    const float trackR = trackH * 0.5f;
    auto track = juce::Rectangle<float>(fx, cy - trackR, fw, trackH);
    const float pos = juce::jlimit(track.getX(), track.getRight(), sliderPos);
    const float alpha = slider.isEnabled() ? 1.0f : 0.4f;

    ats_ui::drawWell(g, track, trackR);

    const auto fillW = juce::jlimit(0.0f, fw, pos - fx);
    if (fillW > 1.0f)
    {
        const auto fill = track.withWidth(fillW);
        if (p.dark)
        {
            g.setColour(p.accent.withAlpha(0.25f * alpha));
            g.fillRoundedRectangle(fill.expanded(0.0f, 2.0f), trackR + 2.0f);
        }

        juce::ColourGradient gradient(p.accentDim.withAlpha(alpha),
                                      track.getX(),
                                      cy,
                                      p.accent.withAlpha(alpha),
                                      track.getRight(),
                                      cy,
                                      false);
        g.setGradientFill(gradient);
        g.fillRoundedRectangle(fill, trackR);
    }

    const float thumbR = juce::jlimit(5.5f, 7.0f, (float) height * 0.36f);
    const auto thumb = juce::Rectangle<float>(pos - thumbR, cy - thumbR, thumbR * 2.0f, thumbR * 2.0f);
    if (p.dark)
    {
        g.setColour(p.shadowDark.withAlpha(0.6f * alpha));
        g.fillEllipse(thumb.translated(1.5f, 1.5f));
    }
    g.setColour(p.accent.withAlpha(alpha));
    g.fillEllipse(thumb);
    g.setColour((p.dark ? p.window : p.panel).withAlpha(alpha));
    g.fillEllipse(thumb.reduced(thumbR * 0.64f));
}

void AtsLookAndFeel::drawRotarySlider(juce::Graphics& g,
                                      int x,
                                      int y,
                                      int width,
                                      int height,
                                      float sliderPosProportional,
                                      float rotaryStartAngle,
                                      float rotaryEndAngle,
                                      juce::Slider& slider)
{
    const auto& p = ats_ui::activePalette();
    const auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat().reduced(3.0f);
    const auto radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) / 2.0f;
    const auto centre = bounds.getCentre();
    const auto arcRadius = radius - 2.5f;
    const auto knobRadius = juce::jmax(6.0f, arcRadius - 7.0f);
    const auto angle = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);
    const float alpha = slider.isEnabled() ? 1.0f : 0.4f;

    const auto knob = juce::Rectangle<float>(centre.x - knobRadius,
                                             centre.y - knobRadius,
                                             knobRadius * 2.0f,
                                             knobRadius * 2.0f);

    if (p.dark)
    {
        g.setColour(p.shadowDark.withAlpha(0.55f * alpha));
        g.fillEllipse(knob.translated(2.0f, 2.0f).expanded(0.5f));
        g.setColour(p.shadowLight.withAlpha(0.45f * alpha));
        g.fillEllipse(knob.translated(-2.0f, -2.0f).expanded(0.5f));

        juce::ColourGradient gradient(p.panel.brighter(0.09f).withAlpha(alpha),
                                      knob.getX(),
                                      knob.getY(),
                                      p.panel.darker(0.22f).withAlpha(alpha),
                                      knob.getRight(),
                                      knob.getBottom(),
                                      false);
        g.setGradientFill(gradient);
        g.fillEllipse(knob);
    }
    else
    {
        g.setColour(p.panel.withAlpha(alpha));
        g.fillEllipse(knob);
        g.setColour(p.panelBorder.withAlpha(alpha));
        g.drawEllipse(knob.reduced(0.5f), 1.0f);
    }

    juce::Path track;
    track.addCentredArc(centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                        rotaryStartAngle, rotaryEndAngle, true);
    g.setColour((p.dark ? p.well : p.well.darker(0.04f)).withAlpha(alpha));
    g.strokePath(track, juce::PathStrokeType(3.0f,
                                             juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));

    juce::Path value;
    value.addCentredArc(centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                        rotaryStartAngle, angle, true);
    if (p.dark)
    {
        g.setColour(p.accent.withAlpha(0.26f * alpha));
        g.strokePath(value, juce::PathStrokeType(6.5f,
                                                 juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
    }

    g.setColour(p.accent.withAlpha(alpha));
    g.strokePath(value, juce::PathStrokeType(2.4f,
                                             juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));

    const auto pointer = juce::Line<float>::fromStartAndAngle(centre, knobRadius - 2.5f, angle);
    g.setColour(p.textPrimary.withAlpha(alpha));
    g.drawLine(juce::Line<float>(pointer.getPointAlongLine(knobRadius * 0.35f),
                                 pointer.getEnd()),
               2.0f);
}

void AtsLookAndFeel::drawGroupComponentOutline(juce::Graphics& g,
                                               int width,
                                               int height,
                                               const juce::String& text,
                                               const juce::Justification&,
                                               juce::GroupComponent& group)
{
    const auto& p = ats_ui::activePalette();
    auto bounds = juce::Rectangle<float>(0.5f, 9.5f, (float) width - 1.0f, (float) height - 10.0f);
    ats_ui::drawRaisedPanel(g, bounds, 8.0f);

    if (text.isNotEmpty())
    {
        g.setFont(juce::Font(11.5f, juce::Font::bold));
        const auto textWidth = juce::jmin(width - 18, g.getCurrentFont().getStringWidth(text) + 16);
        auto textArea = juce::Rectangle<int>(10, 0, textWidth, 20);
        g.setColour(group.isEnabled() ? p.textSecondary : p.textFaint);
        g.drawFittedText(text.toUpperCase(), textArea.reduced(8, 0), juce::Justification::centredLeft, 1, 0.72f);
    }
}

void AtsLookAndFeel::drawAlertBox(juce::Graphics& g,
                                   juce::AlertWindow& alert,
                                   const juce::Rectangle<int>& textArea,
                                   juce::TextLayout& textLayout)
{
    const auto& pal = ats_ui::activePalette();
    const float radius = 10.0f;
    const auto localBounds = alert.getLocalBounds().toFloat().reduced(1.0f);

    // Background with faint amber tint
    g.setColour(pal.window);
    g.fillRoundedRectangle(localBounds, radius);
    g.setColour(pal.accent.withAlpha(0.05f));
    g.fillRoundedRectangle(localBounds, radius);

    // Amber perimeter line
    g.setColour(pal.accent.withAlpha(0.52f));
    g.drawRoundedRectangle(localBounds.reduced(0.5f), radius, 1.0f);

    // --- Measure layout lines ---
    float contentTop    = 0.0f;
    float contentBottom = 0.0f;
    float titleEndY     = 0.0f;   // bottom of last title line in layout coords
    float sumMsgCX      = 0.0f;
    int   msgLineN      = 0;
    const int numLines  = textLayout.getNumLines();

    if (numLines > 0)
    {
        const auto& first = textLayout.getLine(0);
        contentTop = first.lineOrigin.y - first.ascent;
        const auto& last  = textLayout.getLine(numLines - 1);
        contentBottom = last.lineOrigin.y + last.descent;

        for (int i = 0; i < numLines; ++i)
        {
            const auto& line    = textLayout.getLine(i);
            const bool isTitleLine = !line.runs.isEmpty() && line.runs[0]->font.isBold();
            if (isTitleLine)
            {
                titleEndY = juce::jmax(titleEndY, line.lineOrigin.y + line.descent);
            }
            else if (!line.runs.isEmpty())
            {
                const auto bx = line.getLineBoundsX();
                sumMsgCX += line.lineOrigin.x + (bx.getStart() + bx.getEnd()) * 0.5f;
                ++msgLineN;
            }
        }
    }

    // --- Vertical centering in the body above the button row ---
    const float contentH    = contentBottom - contentTop;
    const float buttonZoneH = (float) getAlertWindowButtonHeight() + 28.0f;
    const float bodyH       = localBounds.getHeight() - buttonZoneH;
    const float drawY       = localBounds.getY() + (bodyH - contentH) * 0.5f - contentTop;
    const float padding     = 24.0f;
    const float textW       = localBounds.getWidth() - padding * 2.0f;

    // --- Title: draw independently, perfectly centred ---
    const float titleScreenY = drawY + contentTop;
    const float titleH       = juce::jmax(2.0f, titleEndY - contentTop + 2.0f);
    g.setFont(getAlertWindowTitleFont());
    g.setColour(pal.textPrimary);
    g.drawFittedText(alert.getName(),
                     juce::Rectangle<int>((int)(localBounds.getX() + padding),
                                          (int) titleScreenY,
                                          (int) textW,
                                          (int) titleH),
                     juce::Justification::centred, 1, 0.9f);

    // --- Message: clip textLayout to rows below the title, centre the block ---
    if (numLines > 0)
    {
        const float msgAvgCX = msgLineN > 0 ? sumMsgCX / (float) msgLineN : 0.0f;
        const float msgDrawX = msgLineN > 0 ? localBounds.getCentreX() - msgAvgCX
                                            : localBounds.getX() + padding;
        const float clipTop  = drawY + titleEndY;
        const float clipH    = juce::jmax(1.0f, drawY + contentBottom + 4.0f - clipTop);

        juce::Graphics::ScopedSaveState ss(g);
        g.reduceClipRegion(juce::Rectangle<int>((int) localBounds.getX(), (int) clipTop,
                                                 (int) localBounds.getWidth(), (int) clipH));
        textLayout.draw(g, juce::Rectangle<float>(msgDrawX, drawY,
                                                   localBounds.getWidth(), contentBottom + 10.0f));
    }
}

void AtsLookAndFeel::drawPopupMenuItem(juce::Graphics& g,
                                        const juce::Rectangle<int>& area,
                                        bool isSeparator, bool isActive,
                                        bool isHighlighted, bool isTicked,
                                        bool hasSubMenu, const juce::String& text,
                                        const juce::String& shortcutKeyText,
                                        const juce::Drawable* icon,
                                        const juce::Colour* textColourToUse)
{
    const auto& pal = ats_ui::activePalette();

    if (isSeparator)
    {
        const float y = (float) area.getCentreY();
        ats_ui::drawEngravedDivider(g, (float) area.getX() + 6.0f, (float) area.getRight() - 6.0f, y);
        return;
    }

    auto r = area.reduced(1);
    const float alpha = isActive ? 1.0f : 0.38f;

    if (isHighlighted && isActive)
    {
        g.setColour(pal.accent.withAlpha(0.18f));
        g.fillRoundedRectangle(r.toFloat().reduced(2.0f, 1.0f), 4.0f);
    }

    const auto font = getPopupMenuFont();   // 13 px

    // Tick zone: fixed width matching the font height so the glyph is text-sized.
    const float tickZoneW = std::round(font.getHeight()) + 6.0f;
    auto tickZone = r.removeFromLeft((int) tickZoneW).toFloat();

    if (icon != nullptr)
    {
        const float sz = font.getHeight();
        icon->drawWithin(g, tickZone.withSizeKeepingCentre(sz, sz),
                         juce::RectanglePlacement::centred | juce::RectanglePlacement::onlyReduceInSize, alpha);
    }
    else if (isTicked)
    {
        const float sz = font.getHeight() * 0.88f;   // tick ≤ text height
        const auto tickBounds = tickZone.withSizeKeepingCentre(sz, sz);
        const auto tickPath = getTickShape(1.0f);
        g.setColour(pal.accent.withAlpha(alpha));
        g.fillPath(tickPath, tickPath.getTransformToScaleToFit(tickBounds, true));
    }

    const juce::Colour textCol = textColourToUse ? *textColourToUse
                                                 : (isHighlighted && isActive ? pal.textPrimary
                                                                               : pal.textPrimary);
    g.setColour(textCol.withAlpha(alpha));
    g.setFont(font);
    g.drawFittedText(text, r.withTrimmedRight(6), juce::Justification::centredLeft, 1, 0.9f);

    if (shortcutKeyText.isNotEmpty())
    {
        g.setFont(font.withHeight(font.getHeight() * 0.75f));
        g.drawText(shortcutKeyText, r, juce::Justification::centredRight, true);
    }

    if (hasSubMenu)
    {
        const float ah = font.getAscent() * 0.52f;
        const float ax = (float) r.removeFromRight((int)(ah * 1.6f)).getX();
        const float ay = (float) r.getCentreY();
        juce::Path arrow;
        arrow.addTriangle(ax, ay - ah * 0.5f, ax, ay + ah * 0.5f, ax + ah * 0.65f, ay);
        g.setColour(pal.accent.withAlpha(alpha));
        g.fillPath(arrow);
    }
}

int AtsLookAndFeel::getAlertWindowButtonHeight()
{
    return 26;
}

juce::Font AtsLookAndFeel::getAlertWindowTitleFont()
{
    return ats_ui::font::uiBold(13.0f);
}

juce::Font AtsLookAndFeel::getAlertWindowMessageFont()
{
    return ats_ui::font::ui(12.0f);
}

HistoryButton::HistoryButton(Direction d)
    : juce::Button(d == Direction::Undo ? "Undo" : "Redo")
    , direction(d)
{
    setWantsKeyboardFocus(false);
}

void HistoryButton::paintButton(juce::Graphics& g,
                                bool shouldDrawButtonAsHighlighted,
                                bool shouldDrawButtonAsDown)
{
    const auto& p = ats_ui::activePalette();
    const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
    const float alpha = isEnabled() ? 1.0f : 0.36f;

    if (shouldDrawButtonAsDown)
    {
        ats_ui::drawWell(g, bounds, 8.0f);
    }
    else if (p.dark)
    {
        g.setColour(p.shadowDark.withAlpha(0.4f));
        g.fillRoundedRectangle(bounds.translated(1.5f, 1.5f), 8.0f);
        g.setColour(p.shadowLight.withAlpha(0.35f));
        g.fillRoundedRectangle(bounds.translated(-1.5f, -1.5f), 8.0f);
        g.setColour(shouldDrawButtonAsHighlighted ? p.window.brighter(0.1f) : p.window.brighter(0.05f));
        g.fillRoundedRectangle(bounds, 8.0f);
    }
    else
    {
        g.setColour(shouldDrawButtonAsHighlighted ? p.window : p.panel);
        g.fillRoundedRectangle(bounds, 8.0f);
        g.setColour(shouldDrawButtonAsHighlighted ? p.accent.withAlpha(0.6f) : p.panelBorder);
        g.drawRoundedRectangle(bounds, 8.0f, 1.0f);
    }

    const auto glyph = !isEnabled() ? p.textFaint
                                    : (shouldDrawButtonAsHighlighted ? p.accent : p.textSecondary);

    auto icon = bounds.reduced(bounds.getWidth() * 0.26f, bounds.getHeight() * 0.24f);
    const auto centre = icon.getCentre();
    const float r = juce::jmin(icon.getWidth(), icon.getHeight()) * 0.42f;
    const float start = direction == Direction::Undo ? 0.22f * juce::MathConstants<float>::pi
                                                     : 0.78f * juce::MathConstants<float>::pi;
    const float end = direction == Direction::Undo ? 1.72f * juce::MathConstants<float>::pi
                                                   : -0.72f * juce::MathConstants<float>::pi;

    juce::Path arc;
    arc.addCentredArc(centre.x, centre.y, r, r, 0.0f, start, end, true);
    g.setColour(glyph.withAlpha(alpha));
    g.strokePath(arc, juce::PathStrokeType(1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Path head;
    if (direction == Direction::Undo)
    {
        const auto p = juce::Point<float>(centre.x - r * 0.88f, centre.y - r * 0.18f);
        head.startNewSubPath(p.x, p.y);
        head.lineTo(p.x + 6.0f, p.y - 4.0f);
        head.lineTo(p.x + 5.5f, p.y + 4.5f);
    }
    else
    {
        const auto p = juce::Point<float>(centre.x + r * 0.88f, centre.y - r * 0.18f);
        head.startNewSubPath(p.x, p.y);
        head.lineTo(p.x - 6.0f, p.y - 4.0f);
        head.lineTo(p.x - 5.5f, p.y + 4.5f);
    }
    head.closeSubPath();
    g.fillPath(head);
}

AtsAudioProcessorEditor::AtsAudioProcessorEditor(AtsAudioProcessor& p)
    : AudioProcessorEditor(&p)
    , audioProcessor(p)
    , tooltipWindow(this, 650)
    , sessionTab(p)
    , analysisTab(p)
    , synthTab(p)
    , partialTab(p)
    , mathTab(p)
{
    setLookAndFeel(&pluginLookAndFeel);
    juce::LookAndFeel::setDefaultLookAndFeel(&pluginLookAndFeel);

    titleLabel.setText("ATS RESYNTH", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(17.0f, juce::Font::bold).withExtraKerningFactor(0.18f));
    titleLabel.setJustificationType(juce::Justification::centredLeft);
    titleLabel.setColour(juce::Label::textColourId, ats_ui::colour::accent());
    addAndMakeVisible(titleLabel);

    contextLabel.setText("SPECTRAL MODEL EDITOR", juce::dontSendNotification);
    contextLabel.setFont(juce::Font(9.0f).withExtraKerningFactor(0.14f));
    contextLabel.setJustificationType(juce::Justification::centredLeft);
    contextLabel.setMinimumHorizontalScale(0.68f);
    contextLabel.setColour(juce::Label::textColourId, ats_ui::colour::textFaint());
    addAndMakeVisible(contextLabel);

    // Persistent page components; the page strip hard-swaps visibility (§10.3).
    addChildComponent(sessionTab);
    addChildComponent(analysisTab);
    addChildComponent(synthTab);
    addChildComponent(partialTab);
    addChildComponent(mathTab);

    pageStrip.setItems({ "SESSION", "ANALYSIS", "SYNTHESIS", "PARTIALS", "MATH" }, 0);
    pageStrip.onChange = [this](int i) { showPage(i); };
    addAndMakeVisible(pageStrip);
    showPage(0);

    // Theme switch (Porcelain = id 1, Ember = id 2).
    themeBox.addItem("Porcelain", 1);
    themeBox.addItem("Ember", 2);
    themeBox.setSelectedId(ats_ui::activeTheme() == 1 ? 2 : 1, juce::dontSendNotification);
    themeBox.onChange = [this] { applyTheme(themeBox.getSelectedId() == 2 ? 1 : 0); };
    addAndMakeVisible(themeBox);

    addAndMakeVisible(engineDots);

    // Undo / redo, drawn on top of the empty space at the right of the tab bar.
    addAndMakeVisible(undoButton);
    addAndMakeVisible(redoButton);
    undoButton.onClick = [this] { audioProcessor.undo(); refreshUndoButtons(); };
    redoButton.onClick = [this] { audioProcessor.redo(); refreshUndoButtons(); };
    refreshUndoButtons();

    // Allow Cmd/Ctrl+Z and Cmd/Ctrl+Shift+Z to reach keyPressed().
    setWantsKeyboardFocus(true);

    setResizable(true, true);
    setResizeLimits(scaled(960), scaled(680), scaled(1600), scaled(1200));
    setSize(scaled(1180), scaled(820));

    applyTheme(ats_ui::activeTheme());

    startTimerHz(10);
}

AtsAudioProcessorEditor::~AtsAudioProcessorEditor()
{
    stopTimer();
    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
    setLookAndFeel(nullptr);
}

int AtsAudioProcessorEditor::scaled(int px) const
{
    // Sizes are logical points; JUCE applies the display's DPI backing scale
    // automatically. Multiplying by disp->scale here double-applies Retina
    // scaling and blows the window up 2x, so this is an identity pass-through.
    return px;
}

void AtsAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(ats_ui::colour::background());

    auto area = getLocalBounds();
    auto header = area.removeFromTop(juce::jmax(44, scaled(48))).toFloat();
    g.setColour(ats_ui::colour::header());
    g.fillRect(header);
    g.setColour(ats_ui::colour::border());
    g.drawHorizontalLine(juce::roundToInt(header.getBottom()) - 1, 0.0f, (float) getWidth());
}

void AtsAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();

    const int headerH = juce::jmax(44, scaled(48));
    const int pad = juce::jmax(8, scaled(10));
    auto header = area.removeFromTop(headerH).reduced(pad, scaled(6));
    const int buttonSize = juce::jmax(30, scaled(32));

    themeBox.setBounds(header.removeFromRight(scaled(118)).withSizeKeepingCentre(scaled(118), scaled(30)));
    header.removeFromRight(scaled(14));
    engineDots.setBounds(header.removeFromRight(scaled(92)));
    header.removeFromRight(scaled(14));
    redoButton.setBounds(header.removeFromRight(buttonSize));
    header.removeFromRight(scaled(6));
    undoButton.setBounds(header.removeFromRight(buttonSize));
    header.removeFromRight(scaled(12));

    auto titleBlock = header.removeFromLeft(juce::jmin(header.getWidth(), scaled(260)));
    titleLabel.setBounds(titleBlock.removeFromTop(titleBlock.getHeight() / 2 + scaled(2)));
    contextLabel.setBounds(titleBlock);

    auto body = area.reduced(scaled(16));
    auto stripRow = body.removeFromTop(scaled(30));
    const int stripW = juce::jmin(scaled(640), stripRow.getWidth());
    pageStrip.setBounds(stripRow.withSizeKeepingCentre(stripW, scaled(30)));
    body.removeFromTop(scaled(10));

    sessionTab.setBounds(body);
    analysisTab.setBounds(body);
    synthTab.setBounds(body);
    partialTab.setBounds(body);
    mathTab.setBounds(body);
}

void AtsAudioProcessorEditor::showPage(int page)
{
    currentPage = juce::jlimit(0, 4, page);
    sessionTab.setVisible(currentPage == 0);
    analysisTab.setVisible(currentPage == 1);
    synthTab.setVisible(currentPage == 2);
    partialTab.setVisible(currentPage == 3);
    mathTab.setVisible(currentPage == 4);
}

bool AtsAudioProcessorEditor::keyPressed(const juce::KeyPress& key)
{
    // commandModifier maps to Cmd on macOS and Ctrl on Windows/Linux.
    const bool cmd = key.getModifiers().isCommandDown();
    if (cmd && key.getKeyCode() == (int) 'Z')
    {
        if (key.getModifiers().isShiftDown())
            audioProcessor.redo();
        else
            audioProcessor.undo();
        refreshUndoButtons();
        return true;
    }
    if (cmd && key.getKeyCode() == (int) 'Y') // Windows-style redo
    {
        audioProcessor.redo();
        refreshUndoButtons();
        return true;
    }
    return false;
}

void AtsAudioProcessorEditor::applyTheme(int theme)
{
    ats_ui::setActiveTheme(theme);
    ats_ui::applyEditorPalette(pluginLookAndFeel);

    titleLabel.setColour(juce::Label::textColourId, ats_ui::colour::accent());
    contextLabel.setColour(juce::Label::textColourId, ats_ui::colour::textFaint());

    sendLookAndFeelChange();
    repaint();
}

void AtsAudioProcessorEditor::refreshUndoButtons()
{
    const bool canUndo = audioProcessor.canUndo();
    const bool canRedo = audioProcessor.canRedo();
    undoButton.setEnabled(canUndo);
    redoButton.setEnabled(canRedo);

    const auto undoDesc = audioProcessor.getUndoDescription();
    const auto redoDesc = audioProcessor.getRedoDescription();
    undoButton.setTooltip(canUndo && undoDesc.isNotEmpty() ? "Undo " + undoDesc : juce::String("Nothing to undo"));
    redoButton.setTooltip(canRedo && redoDesc.isNotEmpty() ? "Redo " + redoDesc : juce::String("Nothing to redo"));
}

void AtsAudioProcessorEditor::timerCallback()
{
    sessionTab.refreshFromProcessor();
    analysisTab.refreshFromProcessor();
    partialTab.tick();
    mathTab.tick();
    refreshUndoButtons();
    engineDots.setState(audioProcessor.isAnalysisRunning(), audioProcessor.isRenderRunning());
}
