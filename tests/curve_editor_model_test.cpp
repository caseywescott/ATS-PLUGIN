#include "../src/ui/CurveEditorModel.h"

#include <cassert>
#include <cmath>
#include <iostream>

using namespace ats::ui;

int main()
{
    CurveEditorModel model;
    model.setPlot({ 100.0f, 50.0f, 400.0f, 100.0f });

    // MORPH starts as 0 -> 1; coordinates map both ways.
    assert(model.tab() == CurveTab::morph && model.points().size() == 2);
    const auto mid = model.toScreen({ 0.5f, 0.5f });
    assert(std::abs(mid.x - 300.0f) < 1.0e-4f && std::abs(mid.y - 100.0f) < 1.0e-4f);
    const auto back = model.fromScreen(mid);
    assert(std::abs(back.x - 0.5f) < 1.0e-4f && std::abs(back.y - 0.5f) < 1.0e-4f);

    // Click empty space: a point is added (kept in x order) and dragged.
    assert(model.press({ 300.0f, 60.0f }, false));
    assert(model.points().size() == 3 && model.dragging() == 1);
    model.drag({ 340.0f, 140.0f });
    assert(model.release());
    assert(std::abs(model.points()[1].x - 0.6f) < 1.0e-4f && std::abs(model.points()[1].y - 0.1f) < 1.0e-4f);

    // A drag cannot pass a neighbour.
    assert(!model.press(model.toScreen(model.points()[1]), false) && model.dragging() == 1);
    model.drag({ 600.0f, 100.0f }); // beyond the last point at x = 1
    model.release();
    assert(model.points()[1].x <= model.points()[2].x);

    // Double-click on a point removes it.
    assert(model.press(model.toScreen(model.points()[1]), true));
    assert(model.points().size() == 2 && model.dragging() < 0);

    // Clicks outside the plot do nothing.
    assert(!model.press({ 10.0f, 10.0f }, false));

    // PITCH: empty until edited, range +/-24 st, readout in semitones.
    model.setTab(CurveTab::pitch);
    assert(model.points().empty());
    assert(model.press(model.toScreen({ 0.25f, 12.0f }), false));
    assert(model.readout(4.0) == "1.00s  +12.0 st");
    model.release();
    assert(model.points().size() == 1);

    // TIME: the first point bends the seeded diagonal rather than freezing it.
    model.setTab(CurveTab::timeWarp);
    assert(model.press(model.toScreen({ 0.5f, 0.25f }), false));
    assert(model.points().size() == 3);
    assert(model.points().front().x == 0.0f && model.points().back().y == 1.0f);
    model.release();

    // Host updates are ignored for a curve being dragged.
    model.setTab(CurveTab::spectralWarp);
    assert(model.press(model.toScreen({ 0.5f, 1.5f }), false));
    model.setPoints(CurveTab::spectralWarp, {});
    assert(model.points().size() == 1);
    model.release();
    model.setPoints(CurveTab::spectralWarp, {});
    assert(model.points().empty());

    // RESET: automation curves empty, MORPH back to 0 -> 1.
    model.setTab(CurveTab::timeWarp);
    model.reset();
    assert(model.points().empty());
    model.setTab(CurveTab::morph);
    model.setPoints(CurveTab::morph, { { 0.0f, 0.3f } });
    model.reset();
    assert(model.points().size() == 2 && model.points().back().y == 1.0f);

    // MORPH segment shapes: shift-click cycles the segment that starts at a
    // point; the polyline follows the easing; other tabs ignore it.
    model.setTab(CurveTab::morph);
    model.setPoints(CurveTab::morph, { { 0.0f, 0.0f }, { 1.0f, 1.0f } });
    assert(model.cycleShape(model.toScreen({ 0.0f, 0.0f })));
    assert(model.points().front().shape == 1);
    const auto eased = model.polyline();
    assert(eased.size() > 4); // sampled segment
    // Ease-in at the midpoint: 0.25 of the way up, not 0.5.
    const auto halfway = eased[1 + 8];
    assert(std::abs(model.fromScreen(halfway).y - 0.25f) < 0.02f);
    for (int i = 0; i < 3; ++i) (void) model.cycleShape(model.toScreen({ 0.0f, 0.0f }));
    assert(model.points().front().shape == 0); // wraps after ease in-out
    assert(std::abs(CurveEditorModel::ease(0.5, 3) - 0.5) < 1.0e-12);
    assert(std::abs(CurveEditorModel::ease(0.5, 2) - 0.75) < 1.0e-12);
    model.setTab(CurveTab::pitch);
    assert(!model.cycleShape(model.toScreen({ 0.25f, 12.0f })));

    // Flattening round-trips.
    const std::vector<CurvePoint> pts { { 0.0f, 1.0f }, { 0.5f, -3.0f } };
    const auto flat = CurveEditorModel::flatten(pts);
    assert((flat == std::vector<float> { 0.0f, 1.0f, 0.5f, -3.0f }));
    assert(CurveEditorModel::unflatten(flat).size() == 2);

    std::cout << "curve_editor_model_test: ok\n";
    return 0;
}
