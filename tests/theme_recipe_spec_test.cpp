// Pins the neumorphic drawing recipes to docs/mearcstapa-ui-style-reference.md
// sections 3, 4 and 6. Every offset, alpha and radius below is transcribed from
// that document, which is the parity target.

#include "../src/ui/ThemeMetrics.h"
#include "../src/ui/ThemeRecipes.h"

#include <cassert>
#include <cmath>

using namespace ats::ui;

static bool nearly(float a, float b) { return std::fabs(a - b) < 1.0e-4f; }

static bool sameRect(UiRect r, float x, float y, float w, float h)
{
    return nearly(r.x, x) && nearly(r.y, y) && nearly(r.width, w) && nearly(r.height, h);
}

static std::uint8_t alphaOf(UiColor c) { return c.alpha(); }

static void checkRaisedPanelEmber()
{
    const auto t = ThemeTokens::forTheme(ThemeId::ember);
    const UiRect r { 100.0f, 50.0f, 200.0f, 80.0f };
    const auto ops = ThemeRecipes::raisedPanel(t, r, 8.0f);

    // Three shadow pairs, then the graded face on top.
    assert(ops.size() == 7);

    // i=1: offset 2, expand 0.5, radius 9, dark alpha 0.22, light alpha 0.16.
    assert(sameRect(ops[0].bounds, 101.5f, 51.5f, 201.0f, 81.0f));
    assert(nearly(ops[0].radius, 9.0f));
    assert(alphaOf(ops[0].color) == static_cast<std::uint8_t>(0.22f * 255.0f + 0.5f));
    assert(sameRect(ops[1].bounds, 97.5f, 47.5f, 201.0f, 81.0f));
    assert(alphaOf(ops[1].color) == static_cast<std::uint8_t>(0.16f * 255.0f + 0.5f));

    // i=2: offset 4, expand 1.0, radius 10, alphas halved.
    assert(sameRect(ops[2].bounds, 103.0f, 53.0f, 202.0f, 82.0f));
    assert(nearly(ops[2].radius, 10.0f));
    assert(alphaOf(ops[2].color) == static_cast<std::uint8_t>(0.11f * 255.0f + 0.5f));

    // i=3: offset 6, expand 1.5, radius 11, alphas at a third.
    assert(sameRect(ops[4].bounds, 104.5f, 54.5f, 203.0f, 83.0f));
    assert(nearly(ops[4].radius, 11.0f));

    // Face: diagonal gradient panel.brighter(0.05) -> panel.darker(0.06) at the
    // requested radius, undisplaced.
    assert(ops[6].shape == ThemeShape::roundedRectGradient);
    assert(sameRect(ops[6].bounds, 100.0f, 50.0f, 200.0f, 80.0f));
    assert(nearly(ops[6].radius, 8.0f));
    assert(ops[6].colorFrom == t.panel.brighter(0.05f));
    assert(ops[6].color == t.panel.darker(0.06f));

    // Shadows are drawn behind the face, never over it.
    assert(ops[6].shape != ops[0].shape);
}

static void checkRaisedPanelPorcelain()
{
    const auto t = ThemeTokens::forTheme(ThemeId::porcelain);
    const UiRect r { 0.0f, 0.0f, 40.0f, 20.0f };
    const auto ops = ThemeRecipes::raisedPanel(t, r, 8.0f);

    // Flat fill plus a 1px border, and crucially no shadow stack.
    assert(ops.size() == 2);
    assert(ops[0].shape == ThemeShape::roundedRect && ops[0].color == t.panel);
    assert(ops[1].shape == ThemeShape::roundedRectBorder && ops[1].color == t.panelBorder);
    assert(sameRect(ops[1].bounds, 0.5f, 0.5f, 39.0f, 19.0f));
    for (const auto& op : ops)
        assert(op.shape != ThemeShape::roundedRectGradient);
}

static void checkWell()
{
    const auto ember = ThemeTokens::forTheme(ThemeId::ember);
    const UiRect r { 10.0f, 20.0f, 100.0f, 50.0f };
    const auto ops = ThemeRecipes::well(ember, r, 3.0f);
    assert(ops.size() == 3);
    assert(ops[0].color == ember.well);
    assert(alphaOf(ops[1].color) == static_cast<std::uint8_t>(0.85f * 255.0f + 0.5f));
    assert(sameRect(ops[1].bounds, 10.5f, 20.5f, 99.0f, 49.0f));

    // The light lip is stroked outside the rect but clipped to the lower 40%.
    assert(ops[2].clipped);
    assert(sameRect(ops[2].bounds, 9.5f, 19.5f, 101.0f, 51.0f));
    assert(sameRect(ops[2].clip, 10.0f, 50.0f, 100.0f, 20.0f));
    assert(alphaOf(ops[2].color) == static_cast<std::uint8_t>(0.4f * 255.0f + 0.5f));

    const auto porcelain = ThemeTokens::forTheme(ThemeId::porcelain);
    const auto light = ThemeRecipes::well(porcelain, r, 3.0f);
    assert(light.size() == 2);
    assert(light[1].color == porcelain.wellBorder);
    assert(!light[1].clipped);
}

static void checkButtonBackground()
{
    const auto t = ThemeTokens::forTheme(ThemeId::ember);
    const UiRect r { 0.0f, 0.0f, 80.0f, 24.0f };

    // Up, not hovered: shadow pair at +/-1.5 then a face lifted 0.05.
    const auto up = ThemeRecipes::buttonBackground(t, r, false, false);
    assert(up.size() == 3);
    assert(sameRect(up[0].bounds, 2.0f, 2.0f, 79.0f, 23.0f));
    assert(sameRect(up[1].bounds, -1.0f, -1.0f, 79.0f, 23.0f));
    assert(up[2].color == t.window.brighter(0.05f));
    assert(nearly(up[2].radius, 8.0f));

    // Hover only changes the face, never the shadow geometry.
    const auto hover = ThemeRecipes::buttonBackground(t, r, false, true);
    assert(hover[2].color == t.window.brighter(0.1f));
    assert(sameRect(hover[0].bounds, up[0].bounds.x, up[0].bounds.y, up[0].bounds.width,
                    up[0].bounds.height));

    // Down presses into a sunken accent-tinted well, so it must emit the well
    // recipe rather than the raised one.
    const auto down = ThemeRecipes::buttonBackground(t, r, true, false);
    assert(down.size() == 4);
    assert(down[0].color == t.well);
    assert(down[3].color == t.accent.withAlpha(0.15f));
}

static void checkAccentHalo()
{
    const auto ember = ThemeTokens::forTheme(ThemeId::ember);
    const UiRect r { 5.0f, 5.0f, 50.0f, 6.0f };

    // Slider fill halo: accent@0.25 expanded (0, +2).
    const auto halo = ThemeRecipes::accentHalo(ember, r, 3.0f, 0.25f, 0.0f, 2.0f);
    assert(halo.size() == 1);
    assert(sameRect(halo[0].bounds, 5.0f, 3.0f, 50.0f, 10.0f));
    assert(alphaOf(halo[0].color) == static_cast<std::uint8_t>(0.25f * 255.0f + 0.5f));

    // Porcelain relies on its border stroke instead and emits no halo at all.
    const auto porcelain = ThemeTokens::forTheme(ThemeId::porcelain);
    assert(ThemeRecipes::accentHalo(porcelain, r, 3.0f, 0.25f, 0.0f, 2.0f).empty());
}

static void checkSliderThumb()
{
    const auto ember = ThemeTokens::forTheme(ThemeId::ember);
    const auto ops = ThemeRecipes::sliderThumb(ember, 100.0f, 50.0f, 7.0f);

    // Drop shadow, accent disc, then the punched-out centre that makes it a ring.
    assert(ops.size() == 3);
    for (const auto& op : ops)
        assert(op.shape == ThemeShape::ellipse);
    assert(sameRect(ops[0].bounds, 94.5f, 44.5f, 14.0f, 14.0f));
    assert(alphaOf(ops[0].color) == static_cast<std::uint8_t>(0.6f * 255.0f + 0.5f));
    assert(sameRect(ops[1].bounds, 93.0f, 43.0f, 14.0f, 14.0f));
    assert(ops[1].color == ember.accent);
    assert(sameRect(ops[2].bounds, 97.5f, 47.5f, 5.0f, 5.0f));
    assert(ops[2].color == ember.window);

    // Porcelain has no shadow pass, and its inner disc is panel, not window --
    // the two tokens differ there so the ring would be the wrong shade.
    const auto porcelain = ThemeTokens::forTheme(ThemeId::porcelain);
    const auto light = ThemeRecipes::sliderThumb(porcelain, 100.0f, 50.0f, 7.0f);
    assert(light.size() == 2);
    assert(light[1].color == porcelain.panel);
    assert(porcelain.panel != porcelain.window);
}

static void checkTogglePill()
{
    const auto ember = ThemeTokens::forTheme(ThemeId::ember);
    const UiRect r { 10.0f, 20.0f, 42.0f, 21.0f };

    // On: well track, accent@0.3 wash, Ember glow, then the thumb parked right.
    const auto on = ThemeRecipes::togglePill(ember, r, true, false);
    assert(on.size() == 6);
    assert(on[0].color == ember.well);
    assert(nearly(on[3].radius, 10.5f));
    assert(alphaOf(on[3].color) == static_cast<std::uint8_t>(0.3f * 255.0f + 0.5f));
    assert(on[4].shape == ThemeShape::ellipse);
    assert(sameRect(on[4].bounds, 30.0f, 19.0f, 23.0f, 23.0f));
    assert(alphaOf(on[4].color) == static_cast<std::uint8_t>(0.28f * 255.0f + 0.5f));
    assert(sameRect(on[5].bounds, 34.0f, 23.0f, 15.0f, 15.0f));
    assert(on[5].color == ember.accent);

    // Off: no wash and no glow, thumb parked left at the mirrored offset.
    const auto off = ThemeRecipes::togglePill(ember, r, false, false);
    assert(off.size() == 4);
    assert(sameRect(off[3].bounds, 13.0f, 23.0f, 15.0f, 15.0f));
    assert(off[3].color == ember.textMuted);

    // Hover lifts the off thumb only; it must not tint the on thumb accent.
    assert(ThemeRecipes::togglePill(ember, r, false, true).back().color == ember.textSecondary);
    assert(ThemeRecipes::togglePill(ember, r, true, true).back().color == ember.accent);

    const auto porcelain = ThemeTokens::forTheme(ThemeId::porcelain);
    const auto lightOn = ThemeRecipes::togglePill(porcelain, r, true, false);
    assert(lightOn.size() == 4);
    assert(alphaOf(lightOn[2].color) == static_cast<std::uint8_t>(0.18f * 255.0f + 0.5f));
}

static void checkSegmentedSegment()
{
    const auto ember = ThemeTokens::forTheme(ThemeId::ember);
    const UiRect seg { 0.0f, 0.0f, 80.0f, 28.0f };
    const float trackRadius = ThemeRadius::segmentedTrack(28.0f);
    assert(nearly(trackRadius, 12.0f));

    // Active: the full raised-panel stack, then the accent wash and stroke.
    const auto active = ThemeRecipes::segmentedSegment(ember, seg, trackRadius, true, false);
    assert(active.size() == 9);
    assert(sameRect(active[7].bounds, 3.0f, 3.0f, 74.0f, 22.0f));
    assert(nearly(active[7].radius, 9.0f));
    assert(alphaOf(active[7].color) == static_cast<std::uint8_t>(0.16f * 255.0f + 0.5f));
    assert(active[8].shape == ThemeShape::roundedRectBorder);
    assert(nearly(active[8].thickness, 1.2f));
    assert(alphaOf(active[8].color) == static_cast<std::uint8_t>(0.8f * 255.0f + 0.5f));

    // Inactive segments draw nothing so the sunken track shows through; that
    // absence is what makes the active pill read as lifted.
    assert(ThemeRecipes::segmentedSegment(ember, seg, trackRadius, false, false).empty());

    const auto hovered = ThemeRecipes::segmentedSegment(ember, seg, trackRadius, false, true);
    assert(hovered.size() == 1);
    assert(alphaOf(hovered[0].color) == static_cast<std::uint8_t>(0.08f * 255.0f + 0.5f));

    const auto porcelain = ThemeTokens::forTheme(ThemeId::porcelain);
    const auto lightActive = ThemeRecipes::segmentedSegment(porcelain, seg, trackRadius, true, false);
    assert(lightActive.size() == 4);
    assert(alphaOf(lightActive[2].color) == static_cast<std::uint8_t>(0.1f * 255.0f + 0.5f));

    assert(ThemeRecipes::segmentedText(ember, true, false) == ember.accent);
    assert(ThemeRecipes::segmentedText(ember, false, true) == ember.textPrimary);
    assert(ThemeRecipes::segmentedText(ember, false, false) == ember.textSecondary);
}

static void checkTooltip()
{
    const auto ember = ThemeTokens::forTheme(ThemeId::ember);
    const auto ops = ThemeRecipes::tooltip(ember, { 0.0f, 0.0f, 120.0f, 22.0f });
    assert(ops.size() == 2);
    assert(sameRect(ops[0].bounds, 1.0f, 1.0f, 118.0f, 20.0f));
    assert(nearly(ops[0].radius, 8.0f));
    assert(ops[0].color == ember.window.brighter(0.06f));
    assert(ops[1].shape == ThemeShape::roundedRectBorder);
    assert(ops[1].color == ember.panelBorder);

    // Porcelain's window is already off-white, so the card uses panel to stay
    // distinguishable from the surface behind it.
    const auto porcelain = ThemeTokens::forTheme(ThemeId::porcelain);
    assert(ThemeRecipes::tooltip(porcelain, { 0.0f, 0.0f, 120.0f, 22.0f })[0].color
           == porcelain.panel);
}

static void checkEngravedDivider()
{
    const auto ember = ThemeTokens::forTheme(ThemeId::ember);
    const UiRect r { 40.0f, 100.0f, 260.0f, 27.0f };
    const auto ops = ThemeRecipes::engravedDivider(ember, r, 12.0f);

    // Exactly two hairlines, and the light one sits directly under the dark one
    // with no gap — that adjacency is what reads as a carved edge.
    assert(ops.size() == 2);
    assert(sameRect(ops[0].bounds, 40.0f, 112.0f, 260.0f, 1.0f));
    assert(sameRect(ops[1].bounds, 40.0f, 113.0f, 260.0f, 1.0f));
    assert(nearly(ops[1].bounds.y, ops[0].bounds.y + ops[0].bounds.height));
    assert(ops[0].color == ember.shadowDark.withAlpha(0.85f));
    assert(ops[1].color == ember.shadowLight.withAlpha(0.6f));

    // Hairlines, not pills: a radius here would bow the ends of the line.
    assert(nearly(ops[0].radius, 0.0f) && nearly(ops[1].radius, 0.0f));

    // Porcelain keeps the two-line structure but swaps in the light palette.
    const auto porcelain = ThemeTokens::forTheme(ThemeId::porcelain);
    const auto light = ThemeRecipes::engravedDivider(porcelain, r, 12.0f);
    assert(light.size() == 2);
    assert(light[0].color == porcelain.wellBorder);
    assert(light[1].color == UiColor { 0xffffffff }.withAlpha(0.6f));

    // 12 pad + 1 dark + 2 light + 12 pad, per 7.8's preferredHeight arithmetic.
    static_assert(ThemeRecipes::kDividerHeight == 27.0f);
    static_assert(ThemeRecipes::kDividerHeight == ThemeRecipes::kSectionLabelTopPad + 1.0f +
                                                      ThemeRecipes::kDividerLineH +
                                                      ThemeRecipes::kSectionLabelTopPad);
}

static void checkSectionHeading()
{
    // 7.7's constants, and the two derived quantities a column engine reserves.
    static_assert(ThemeRecipes::kSectionLabelTopPad == 12.0f);
    static_assert(ThemeRecipes::kSectionLabelH == 15.0f);
    static_assert(ThemeRecipes::kSectionLabelBottomPad == 2.0f);
    static_assert(ThemeRecipes::kDividerLineH == 2.0f);
    static_assert(ThemeRecipes::kControlLabelH == 14.0f);

    static_assert(ThemeRecipes::sectionHeadingHeight(false) == 29.0f);
    static_assert(ThemeRecipes::sectionHeadingHeight(true) == 43.0f);
    static_assert(ThemeRecipes::sectionLabelTop(false) == 12.0f);
    static_assert(ThemeRecipes::sectionLabelTop(true) == 26.0f);

    // The label must fit inside the block the caller reserved, or headings will
    // bleed into the row beneath them.
    static_assert(ThemeRecipes::sectionLabelTop(false) + ThemeRecipes::kSectionLabelH <=
                  ThemeRecipes::sectionHeadingHeight(false));
    static_assert(ThemeRecipes::sectionLabelTop(true) + ThemeRecipes::kSectionLabelH <=
                  ThemeRecipes::sectionHeadingHeight(true));

    // A top divider adds exactly one pad plus its two lines.
    static_assert(ThemeRecipes::sectionHeadingHeight(true) -
                      ThemeRecipes::sectionHeadingHeight(false) ==
                  ThemeRecipes::kSectionLabelTopPad + ThemeRecipes::kDividerLineH);

    // ... and both of its lines land above the label text.
    const auto ember = ThemeTokens::forTheme(ThemeId::ember);
    const UiRect row { 0.0f, 0.0f, 200.0f, ThemeRecipes::sectionHeadingHeight(true) };
    const auto divider = ThemeRecipes::engravedDivider(ember, row, ThemeRecipes::kSectionLabelTopPad);
    assert(divider.back().bounds.y + divider.back().bounds.height <=
           ThemeRecipes::sectionLabelTop(true));

    // The trailing hairline starts 8px past the text, is vertically centred,
    // and fills whatever width is left.
    const auto rule = ThemeRecipes::sectionRule(ember, { 10.0f, 40.0f, 200.0f, 15.0f }, 62.0f);
    assert(rule.size() == 1);
    assert(sameRect(rule[0].bounds, 80.0f, 47.5f, 130.0f, 1.0f));
    assert(rule[0].color == ember.textFaint.withAlpha(0.4f));

    // Text wider than the row emits nothing rather than a negative-width bar.
    assert(ThemeRecipes::sectionRule(ember, { 10.0f, 40.0f, 200.0f, 15.0f }, 200.0f).empty());
}

static void checkMetrics()
{
    static_assert(ThemeRadius::spectrumWell == 10.0f);
    static_assert(ThemeRadius::control == 8.0f);
    static_assert(ThemeRadius::sliderTrack == 3.0f);
    static_assert(ThemeRadius::cheekButton == 5.0f);
    static_assert(ThemeRadius::selectedPanel == 9.0f);
    static_assert(ThemeRadius::bodyColumnPanel == 14.0f);

    // Toggle pill is a true capsule; the segmented track clamps at 12.
    static_assert(ThemeRadius::togglePill(21.0f) == 10.5f);
    static_assert(ThemeRadius::segmentedTrack(21.0f) == 10.5f);
    static_assert(ThemeRadius::segmentedTrack(40.0f) == 12.0f);
    static_assert(ThemeRadius::segmentedActivePill(10.5f) == 7.5f);
    static_assert(ThemeRadius::segmentedActivePill(3.0f) == 2.0f);

    // Type scale: segments shrink past three, and none of it is the old flat 10.
    static_assert(ThemeFont::segment(3) == 12.5f);
    static_assert(ThemeFont::segment(4) == 10.0f);
    static_assert(ThemeFont::title == 17.0f && ThemeFont::titleKerning == 0.18f);
    static_assert(ThemeFont::sectionLabel == 10.0f && ThemeFont::sectionLabelKerning == 0.2f);
    static_assert(ThemeFont::controlLabel == 12.0f);
    static_assert(ThemeFont::listComboPopup == 13.0f);
    static_assert(ThemeFont::monoValue == 11.5f);
}

int main()
{
    checkRaisedPanelEmber();
    checkRaisedPanelPorcelain();
    checkWell();
    checkButtonBackground();
    checkAccentHalo();
    checkSliderThumb();
    checkTogglePill();
    checkSegmentedSegment();
    checkTooltip();
    checkEngravedDivider();
    checkSectionHeading();
    checkMetrics();
    return 0;
}
