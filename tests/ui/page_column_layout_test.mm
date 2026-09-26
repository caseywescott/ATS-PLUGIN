// Walks every page's real §10 row table through the §10.7 column engine and
// asserts the geometry invariants that a screenshot used to be the only guard
// for: rows never overlap, never escape their column, and nothing is dropped by
// the overflow guard at the shipping window size.

#include "../../src/ui/AtsEditorPages.h"

#include <cassert>
#include <cmath>
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

UiRect panel(const std::vector<float>& widths, std::size_t index)
{
    const auto page = ThemeLayout::pageArea(ThemeLayout::body(kWindowW, kWindowH));
    return ThemeLayout::panelContent(ThemeLayout::columns(page, widths)[index]);
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
    const std::vector<float> session { 260.0f, 0.0f, 360.0f };
    const std::vector<float> analysis { 300.0f, 0.0f, 320.0f };
    const std::vector<float> synthesis { 380.0f, 0.0f };
    const std::vector<float> partials { 0.0f, 320.0f };

    // §10.4's right column is anchored from both ends, so the top group only
    // owns what the bottom group leaves behind.
    const UiRect sessionRight = panel(session, 2);
    const float reserved =
        ThemeLayout::columnHeight(pageLayoutItems(AtsSessionPage::rightBottomRows()));
    const UiRect sessionRightTop { sessionRight.x, sessionRight.y, sessionRight.width,
                                   sessionRight.height - reserved };

    return {
        { "session.left", &AtsSessionPage::leftRows(), panel(session, 0), false },
        { "session.right.top", &AtsSessionPage::rightTopRows(), sessionRightTop, false },
        { "session.right.bottom", &AtsSessionPage::rightBottomRows(), sessionRight, true },
        { "analysis.left", &AtsAnalysisPage::leftRows(), panel(analysis, 0), false },
        { "analysis.right", &AtsAnalysisPage::rightRows(), panel(analysis, 2), false },
        { "synthesis.left", &AtsSynthesisPage::leftRows(), panel(synthesis, 0), false },
        { "partials.right", &AtsPartialsPage::cellRows(), panel(partials, 1), false },
    };
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
            assert(slot.visible);
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
    assert(columns.size() == 7);
    std::size_t visible = 0;
    for (const auto& column : columns)
    {
        assert(!column.rows->empty());
        for (const auto& slot : walk(column))
            visible += slot.visible ? 1 : 0;
    }
    assert(visible >= 90);
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
    return 0;
}
