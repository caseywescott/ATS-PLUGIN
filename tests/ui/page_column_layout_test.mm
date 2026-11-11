// Walks every page's real §10 row table through the §10.7 column engine and
// asserts the geometry invariants that a screenshot used to be the only guard
// for: rows never overlap, never escape their column, and nothing is dropped by
// the overflow guard at the shipping window size.

#include "../../src/ui/AtsEditorPages.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace ats::ui;

namespace
{

struct Column
{
    std::string name;
    const std::vector<PageRow>* rows;
    UiRect area;
    bool fromBottom = false;
};

// The shipping standalone size, which is what §10's fixed column widths are
// dimensioned for.
constexpr float kWindowW = 1280.0f;
constexpr float kWindowH = 940.0f;

UiRect pageArea(float width = kWindowW, float height = kWindowH)
{
    // Pages lay out in their own frame, whose origin is the page area's.
    const auto page = ThemeLayout::pageArea(ThemeLayout::body(width, height));
    return { 0.0f, 0.0f, page.width, page.height };
}

UiRect panel(const std::vector<float>& widths, std::size_t index)
{
    return ThemeLayout::panelContent(ThemeLayout::columns(pageArea(), widths)[index]);
}

std::vector<LayoutSlot> walk(const Column& column)
{
    const auto items = pageLayoutItems(*column.rows);
    return column.fromBottom ? ThemeLayout::columnFromBottom(items, column.area)
                             : ThemeLayout::column(items, column.area);
}

// Every column in the editor, described exactly as its page describes it.
std::vector<Column> allColumns()
{
    const std::vector<float> analysis { 300.0f, 0.0f, 320.0f };
    const std::vector<float> synthesis { 380.0f, 0.0f };
    const std::vector<float> partials { 0.0f, 320.0f };

    // SESSION: the spectrum spans the centre and right columns, so the right
    // panel under it shows one group per tab above the bottom-anchored output
    // group.  Each tab is walked as its own column.
    const auto sessionPanels = AtsSessionPage::sessionPanels(pageArea());
    std::vector<Column> columns {
        { "session.left", &AtsSessionPage::leftRows(),
          ThemeLayout::panelContent(sessionPanels.left), false },
    };
    const auto morphTab = AtsSessionPage::rightColumn(sessionPanels.right, 0);
    for (int tab = 0; tab < 3; ++tab)
        columns.push_back({ std::string("session.right.") + AtsSessionPage::kRightTabLabels[tab],
                            &AtsSessionPage::rightGroupRows(tab),
                            AtsSessionPage::rightColumn(sessionPanels.right, tab).rowArea, false });
    columns.push_back({ "session.right.bottom", &AtsSessionPage::rightBottomRows(),
                        morphTab.content, true });

    const std::vector<Column> others {
        { "analysis.left", &AtsAnalysisPage::leftRows(), panel(analysis, 0), false },
        { "analysis.right", &AtsAnalysisPage::rightRows(), panel(analysis, 2), false },
        { "synthesis.left", &AtsSynthesisPage::leftRows(), panel(synthesis, 0), false },
        { "partials.right", &AtsPartialsPage::cellRows(), panel(partials, 1), false },
    };
    columns.insert(columns.end(), others.begin(), others.end());
    return columns;
}

bool nearly(float a, float b) { return std::fabs(a - b) < 1.0e-4f; }

bool overlaps(UiRect a, UiRect b)
{
    // Rows on the same paired line are allowed to sit side by side.
    const bool verticalGap = a.y + a.height <= b.y + 1.0e-4f || b.y + b.height <= a.y + 1.0e-4f;
    const bool horizontalGap = a.x + a.width <= b.x + 1.0e-4f || b.x + b.width <= a.x + 1.0e-4f;
    return !verticalGap && !horizontalGap;
}

// The defect the user reported: two rows drawn on top of each other.
void checkNoRowOverlap()
{
    for (const auto& column : allColumns())
    {
        const auto slots = walk(column);
        for (std::size_t i = 0; i < slots.size(); ++i)
        {
            if (!slots[i].visible)
                continue;
            for (std::size_t j = i + 1; j < slots.size(); ++j)
            {
                if (!slots[j].visible)
                    continue;
                assert(!overlaps(slots[i].bounds, slots[j].bounds) &&
                       "page row overlap");
            }
        }
    }
}

// A row that spills past the panel's inner rect draws over the panel edge or
// over the column beside it.
void checkRowsStayInsideColumn()
{
    for (const auto& column : allColumns())
    {
        for (const auto& slot : walk(column))
        {
            if (!slot.visible)
                continue;
            const auto& b = slot.bounds;
            assert(b.width > 0.0f && b.height > 0.0f);
            assert(b.x >= column.area.x - 1.0e-4f);
            assert(b.x + b.width <= column.area.x + column.area.width + 1.0e-4f);
            assert(b.y >= column.area.y - 1.0e-4f);
            assert(b.y + b.height <= column.area.y + column.area.height + 1.0e-4f);
        }
    }
}

// At the shipping size no column may rely on the overflow guard: a hidden row
// is a control the user simply cannot reach.
void checkNothingDroppedAtShippingSize()
{
    for (const auto& column : allColumns())
    {
        assert(ThemeLayout::columnHeight(pageLayoutItems(*column.rows)) <=
               column.area.height + 1.0e-4f);
        for (const auto& slot : walk(column))
        {
            if (!slot.visible)
                std::fprintf(stderr, "dropped row in %s (area h %.1f, needs %.1f)\n",
                             column.name.c_str(), column.area.height,
                             ThemeLayout::columnHeight(pageLayoutItems(*column.rows)));
            assert(slot.visible);
        }
    }
}

// Rows are laid out in table order, so a caption always precedes its widget.
void checkRowsAdvanceMonotonically()
{
    for (const auto& column : allColumns())
    {
        const auto slots = walk(column);
        float previousBottom = column.area.y;
        for (std::size_t i = 0; i < slots.size(); ++i)
        {
            if (!slots[i].visible)
                continue;
            // The second half of a pair shares the line above it rather than
            // starting a new one, so it is measured against the same baseline.
            const bool secondOfPair = i > 0 && (*column.rows)[i - 1].pairNext;
            if (secondOfPair)
            {
                assert(nearly(slots[i].bounds.y, slots[i - 1].bounds.y));
                previousBottom = std::max(previousBottom,
                                          slots[i].bounds.y + slots[i].bounds.height);
                continue;
            }
            assert(slots[i].bounds.y >= previousBottom - 1.0e-4f);
            previousBottom = slots[i].bounds.y + slots[i].bounds.height;
        }
    }
}

// A labelled row must leave the widget a usable box under its caption.
void checkLabelledRowsLeaveRoomForTheWidget()
{
    for (const auto& column : allColumns())
    {
        const auto slots = walk(column);
        for (std::size_t i = 0; i < column.rows->size() && i < slots.size(); ++i)
        {
            const auto& row = (*column.rows)[i];
            if (!slots[i].visible || !row.labelled)
                continue;
            const auto widget = pageRowWidgetArea(row, slots[i].bounds);
            assert(widget.y >= slots[i].bounds.y + ThemeRecipes::kControlLabelH - 1.0e-4f);
            assert(widget.height > 0.0f);
            assert(widget.y + widget.height <=
                   slots[i].bounds.y + slots[i].bounds.height + 1.0e-4f);
        }
    }
}

// Without this the whole file would pass by walking nothing at all.
void checkFixtureIsNotEmpty()
{
    const auto columns = allColumns();
    assert(columns.size() >= 7);
    std::size_t visible = 0;
    for (const auto& column : columns)
    {
        assert(!column.rows->empty());
        for (const auto& slot : walk(column))
            visible += slot.visible ? 1 : 0;
    }
    assert(visible >= 90);
}

bool inside(UiRect inner, UiRect outer)
{
    return inner.x >= outer.x - 1.0e-4f && inner.y >= outer.y - 1.0e-4f
        && inner.x + inner.width <= outer.x + outer.width + 1.0e-4f
        && inner.y + inner.height <= outer.y + outer.height + 1.0e-4f;
}

// The spectrum spans everything right of the left column; the centre and right
// panels sit under it, and no panel overlaps another, at every window size.
void checkSessionSpectrumSpansTheRightSide()
{
    for (const auto& size : { std::pair { 1100.0f, 860.0f }, std::pair { kWindowW, kWindowH },
                              std::pair { 1900.0f, 1400.0f } })
    {
        const auto area = pageArea(size.first, size.second);
        const auto p = AtsSessionPage::sessionPanels(area);
        assert(nearly(p.spectrum.x, p.left.x + p.left.width + ThemeLayout::kColumnGap));
        assert(nearly(p.spectrum.x + p.spectrum.width, p.right.x + p.right.width));
        assert(nearly(p.spectrum.x + p.spectrum.width, area.x + area.width));
        assert(nearly(p.spectrum.y, p.left.y));
        assert(p.spectrum.height >= AtsSessionPage::kMinSpectrumPanelH - 1.0f
               || size.second < kWindowH);
        assert(p.spectrum.width > p.centre.width + p.right.width);
        for (const auto& lower : { p.centre, p.right })
        {
            assert(nearly(lower.y, p.spectrum.y + p.spectrum.height + ThemeLayout::kColumnGap));
            assert(nearly(lower.y + lower.height, p.left.y + p.left.height));
            assert(!overlaps(lower, p.spectrum) && !overlaps(lower, p.left));
        }
        assert(!overlaps(p.centre, p.right));
        const auto centre = AtsSessionPage::centreRects(p);
        assert(inside(centre.well, p.spectrum));
        assert(inside(centre.timeline, p.centre) && inside(centre.context, p.centre));

        // The right panel holds its tab strip, each group and the output
        // group without the overflow guard dropping a control.
        for (int tab = 0; tab < 3; ++tab)
        {
            const auto right = AtsSessionPage::rightColumn(p.right, tab);
            assert(inside(right.rowArea, right.content));
            assert(inside(right.tabStrip, right.content));
            assert(right.tabStrip.y + right.tabStrip.height <= right.rowArea.y);
            for (const auto& slot : ThemeLayout::column(pageLayoutItems(*right.rows), right.rowArea))
                assert(slot.visible);
        }
    }

    // At the shipping size both engine grids show every cell (SMEARNOV's
    // twenty need four columns under the shorter centre panel).
    const auto p = AtsSessionPage::sessionPanels(pageArea());
    const auto grid = AtsSessionPage::contextGridArea(AtsSessionPage::centreRects(p).context);
    assert(AtsSessionPage::contextGrid(grid, 10).size() == 10);
    assert(AtsSessionPage::contextGrid(grid, 20).size() == 20);
    for (const auto& cell : AtsSessionPage::contextGrid(grid, 20))
        assert(inside(cell, grid));
}

} // namespace

int main()
{
    checkFixtureIsNotEmpty();
    checkNoRowOverlap();
    checkRowsStayInsideColumn();
    checkNothingDroppedAtShippingSize();
    checkRowsAdvanceMonotonically();
    checkLabelledRowsLeaveRoomForTheWidget();
    checkSessionSpectrumSpansTheRightSide();
    return 0;
}
