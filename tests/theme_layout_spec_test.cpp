// Pins the layout engine to docs/ats-mearcstapa-ui-spec.md section 10. Every
// margin, column width, gap and guard threshold below is transcribed from that
// document, which is the parity target.

#include "../src/ui/ThemeLayout.h"

#include <cassert>
#include <cmath>

using namespace ats::ui;

static bool nearly(float a, float b) { return std::fabs(a - b) < 1.0e-4f; }

static bool sameRect(UiRect r, float x, float y, float w, float h)
{
    return nearly(r.x, x) && nearly(r.y, y) && nearly(r.width, w) && nearly(r.height, h);
}

// 10.1: body = withTrimmedTop(62).reduced(16). 10.3: a 30px strip with a 10px
// gap below it, centred and capped at 640.
static void checkBodyAndStrip()
{
    const auto body = ThemeLayout::body(1280.0f, 940.0f);
    assert(sameRect(body, 16.0f, 78.0f, 1248.0f, 846.0f));

    const auto strip = ThemeLayout::pageStrip(body);
    assert(sameRect(strip, 320.0f, 78.0f, 640.0f, 30.0f));

    const auto page = ThemeLayout::pageArea(body);
    assert(sameRect(page, 16.0f, 118.0f, 1248.0f, 806.0f));

    // Below 640 of body width the strip stops being capped and just fills.
    const auto narrow = ThemeLayout::body(600.0f, 400.0f);
    const auto narrowStrip = ThemeLayout::pageStrip(narrow);
    assert(nearly(narrowStrip.width, narrow.width));
    assert(nearly(narrowStrip.x, narrow.x));
}

// 10.4: left 260 - gap 16 - center flex - gap 16 - right 360.
static void checkColumns()
{
    const auto page = ThemeLayout::pageArea(ThemeLayout::body(1280.0f, 940.0f));
    const auto cols = ThemeLayout::columns(page, { 260.0f, 0.0f, 360.0f });
    assert(cols.size() == 3);

    // 1248 - 32 of gaps - 620 of fixed leaves 596 for the flex centre.
    assert(sameRect(cols[0], 16.0f, 118.0f, 260.0f, 806.0f));
    assert(sameRect(cols[1], 292.0f, 118.0f, 596.0f, 806.0f));
    assert(sameRect(cols[2], 904.0f, 118.0f, 360.0f, 806.0f));

    // 10.5's analysis split, same arithmetic.
    const auto analysis = ThemeLayout::columns(page, { 300.0f, 0.0f, 320.0f });
    assert(nearly(analysis[1].width, 596.0f));
    assert(nearly(analysis[2].x, 944.0f));

    // 10.6's two-column synthesis split.
    const auto synth = ThemeLayout::columns(page, { 380.0f, 0.0f });
    assert(sameRect(synth[0], 16.0f, 118.0f, 380.0f, 806.0f));
    assert(sameRect(synth[1], 412.0f, 118.0f, 852.0f, 806.0f));

    // A single flex column takes the whole area (10.6 MATH).
    const auto math = ThemeLayout::columns(page, { 0.0f });
    assert(sameRect(math[0], page.x, page.y, page.width, page.height));
}

// 10.4: "inner reduced(14) + 14px top pad".
static void checkPanelContent()
{
    const auto content = ThemeLayout::panelContent({ 100.0f, 200.0f, 260.0f, 500.0f });
    assert(sameRect(content, 114.0f, 228.0f, 232.0f, 458.0f));
}

// 10.7: top-down walk, gapAbove honoured, headings full width.
static void checkColumnWalk()
{
    const UiRect area { 10.0f, 20.0f, 300.0f, 400.0f };
    const std::vector<LayoutItem> items {
        { 14.0f, 0.0f, LayoutItemKind::heading, false },
        { 42.0f, 0.0f, LayoutItemKind::control, false },
        { 26.0f, 8.0f, LayoutItemKind::control, false },
    };
    const auto slots = ThemeLayout::column(items, area);
    assert(slots.size() == 3);
    assert(sameRect(slots[0].bounds, 10.0f, 20.0f, 300.0f, 14.0f));
    assert(sameRect(slots[1].bounds, 10.0f, 34.0f, 300.0f, 42.0f));
    assert(sameRect(slots[2].bounds, 10.0f, 84.0f, 300.0f, 26.0f));
    assert(slots[0].visible && slots[1].visible && slots[2].visible);

    assert(nearly(ThemeLayout::columnHeight(items), 90.0f));
}

// 10.7: "two consecutive non-heading controls share a row -- each width/2 - 5,
// 10px gutter", and the row takes the taller of the two.
static void checkPairing()
{
    const UiRect area { 0.0f, 0.0f, 300.0f, 400.0f };
    const std::vector<LayoutItem> items {
        { 42.0f, 0.0f, LayoutItemKind::control, true },
        { 26.0f, 0.0f, LayoutItemKind::control, false },
        { 30.0f, 0.0f, LayoutItemKind::control, false },
    };
    const auto slots = ThemeLayout::column(items, area);
    assert(sameRect(slots[0].bounds, 0.0f, 0.0f, 145.0f, 42.0f));
    assert(sameRect(slots[1].bounds, 155.0f, 0.0f, 145.0f, 26.0f));
    // 145 + 10 + 145 == 300, and the next row clears the taller of the pair.
    assert(sameRect(slots[2].bounds, 0.0f, 42.0f, 300.0f, 30.0f));

    // The row clears the taller half whichever side it is on.
    const std::vector<LayoutItem> tallSecond {
        { 26.0f, 0.0f, LayoutItemKind::control, true },
        { 42.0f, 0.0f, LayoutItemKind::control, false },
        { 30.0f, 0.0f, LayoutItemKind::control, false },
    };
    const auto tallSlots = ThemeLayout::column(tallSecond, area);
    assert(sameRect(tallSlots[0].bounds, 0.0f, 0.0f, 145.0f, 26.0f));
    assert(sameRect(tallSlots[1].bounds, 155.0f, 0.0f, 145.0f, 42.0f));
    assert(sameRect(tallSlots[2].bounds, 0.0f, 42.0f, 300.0f, 30.0f));
    assert(nearly(ThemeLayout::columnHeight(tallSecond), 72.0f));

    // A heading never pairs even when the flag is set on the item before it.
    const std::vector<LayoutItem> withHeading {
        { 42.0f, 0.0f, LayoutItemKind::control, true },
        { 14.0f, 0.0f, LayoutItemKind::heading, false },
    };
    const auto headingSlots = ThemeLayout::column(withHeading, area);
    assert(sameRect(headingSlots[0].bounds, 0.0f, 0.0f, 300.0f, 42.0f));
    assert(sameRect(headingSlots[1].bounds, 0.0f, 42.0f, 300.0f, 14.0f));
}

// 10.7: below kMinCompactControlH=38 of remaining height a control is hidden
// with empty bounds, never squashed. Headings are never hidden.
static void checkOverflowGuard()
{
    static_assert(ThemeLayout::kMinCompactControlH == 38.0f);

    const UiRect area { 0.0f, 0.0f, 200.0f, 100.0f };
    const std::vector<LayoutItem> items {
        { 42.0f, 0.0f, LayoutItemKind::control, false },
        { 42.0f, 0.0f, LayoutItemKind::control, false },
        { 42.0f, 0.0f, LayoutItemKind::control, false },
    };
    const auto slots = ThemeLayout::column(items, area);
    assert(slots[0].visible && slots[1].visible);
    // After two 42px rows only 16px is left, which is under the 38px floor.
    assert(!slots[2].visible);
    assert(sameRect(slots[2].bounds, 0.0f, 0.0f, 0.0f, 0.0f));

    // Both halves of a pair drop together rather than leaving a half row.
    const std::vector<LayoutItem> pairAtEnd {
        { 70.0f, 0.0f, LayoutItemKind::control, false },
        { 42.0f, 0.0f, LayoutItemKind::control, true },
        { 42.0f, 0.0f, LayoutItemKind::control, false },
    };
    const auto pairSlots = ThemeLayout::column(pairAtEnd, area);
    assert(pairSlots[0].visible);
    assert(!pairSlots[1].visible && !pairSlots[2].visible);

    const std::vector<LayoutItem> headingAtEnd {
        { 70.0f, 0.0f, LayoutItemKind::control, false },
        { 14.0f, 0.0f, LayoutItemKind::heading, false },
    };
    const auto headingSlots = ThemeLayout::column(headingAtEnd, area);
    assert(headingSlots[1].visible);
    assert(sameRect(headingSlots[1].bounds, 0.0f, 70.0f, 200.0f, 14.0f));
}

// 10.4 right column: the output group is pinned to the bottom of the column but
// still reads top-down within itself.
static void checkBottomAnchor()
{
    const UiRect area { 0.0f, 0.0f, 200.0f, 400.0f };
    const std::vector<LayoutItem> items {
        { 42.0f, 0.0f, LayoutItemKind::control, false },   // OUT
        { 42.0f, 10.0f, LayoutItemKind::control, true },   // partGain
        { 42.0f, 0.0f, LayoutItemKind::control, false },   // resGain
        { 27.0f, 0.0f, LayoutItemKind::heading, false },   // engraved divider
    };
    // 42 + 10 + 42 + 27 == 121, so the group starts at 400 - 121 = 279.
    const auto slots = ThemeLayout::columnFromBottom(items, area);
    assert(sameRect(slots[0].bounds, 0.0f, 279.0f, 200.0f, 42.0f));
    assert(sameRect(slots[1].bounds, 0.0f, 331.0f, 95.0f, 42.0f));
    assert(sameRect(slots[2].bounds, 105.0f, 331.0f, 95.0f, 42.0f));
    assert(sameRect(slots[3].bounds, 0.0f, 373.0f, 200.0f, 27.0f));
    // The last row ends exactly on the column's bottom edge.
    assert(nearly(slots[3].bounds.y + slots[3].bounds.height, 400.0f));
}

int main()
{
    checkBodyAndStrip();
    checkColumns();
    checkPanelContent();
    checkColumnWalk();
    checkPairing();
    checkOverflowGuard();
    checkBottomAnchor();
    return 0;
}
