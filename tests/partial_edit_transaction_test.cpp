#include "../src/engine/PartialEditService.h"
#include "../src/ui/PartialEditView.h"

#include <cassert>
#include <cmath>

int main()
{
    ats::PartialMatrix matrix(3, 4);
    for (int p = 0; p < matrix.partialCount; ++p)
        for (int f = 0; f < matrix.frameCount; ++f)
            matrix.at(p, f) = { 1.0 + p + f, 100.0 + 10.0 * p + f, 0.25 };

    ats::PartialEditService service;
    auto box = ats::PartialEditCommand{};
    box.kind = ats::PartialEditKind::box;
    box.firstPartial = 0; box.lastPartial = 1; box.firstFrame = 1; box.lastFrame = 2;
    assert(service.apply(matrix, box).accepted);
    assert(service.selection().contains(1, 2));

    auto scale = ats::PartialEditCommand{};
    scale.kind = ats::PartialEditKind::scaleSelection;
    scale.value = 0.5;
    assert(service.apply(matrix, scale).changed);
    assert(matrix.at(1, 2).amplitude == 2.0);
    assert(service.canUndo());
    assert(service.undo(matrix).changed);
    assert(matrix.at(1, 2).amplitude == 4.0);
    assert(service.redo(matrix).changed);
    assert(matrix.at(1, 2).amplitude == 2.0);

    const auto before = matrix;
    auto invalid = ats::PartialEditCommand{};
    invalid.kind = ats::PartialEditKind::writeCell;
    invalid.partial = 1; invalid.frame = 1; invalid.amplitude = -1.0; invalid.frequencyHz = 200.0;
    const auto invalidResult = service.apply(matrix, invalid);
    assert(!invalidResult.accepted && matrix.cells == before.cells && matrix.revision == before.revision);

    auto draw = ats::PartialEditCommand{};
    draw.kind = ats::PartialEditKind::drawDb;
    draw.points = { { 1.0, 0.0 }, { 2.0, 6.0 } };
    assert(service.apply(matrix, draw).changed);
    assert(matrix.at(0, 2).amplitude > 1.9);

    auto copy = ats::PartialEditCommand{};
    copy.kind = ats::PartialEditKind::clipboardCopy;
    assert(service.apply(matrix, copy).accepted && service.clipboard().valid());
    auto select = ats::PartialEditCommand{};
    select.kind = ats::PartialEditKind::box;
    select.firstPartial = 0; select.lastPartial = 1; select.firstFrame = 1; select.lastFrame = 2;
    assert(service.apply(matrix, select).accepted);
    matrix.at(0, 1).frequencyHz = 999.0;
    auto paste = ats::PartialEditCommand{};
    paste.kind = ats::PartialEditKind::clipboardPaste;
    assert(service.apply(matrix, paste).changed);
    assert(matrix.at(0, 1).frequencyHz != 999.0);

    auto lasso = ats::PartialEditCommand{};
    lasso.kind = ats::PartialEditKind::lasso;
    lasso.points = { { -0.5, -0.5 }, { 2.5, -0.5 }, { 2.5, 1.5 }, { -0.5, 1.5 } };
    assert(service.apply(matrix, lasso).accepted);
    assert(service.selection().contains(0, 0) && service.selection().contains(1, 2));

    auto gain = ats::PartialEditCommand{};
    gain.kind = ats::PartialEditKind::gainBox;
    gain.firstPartial = 0; gain.lastPartial = 2; gain.firstFrame = 0; gain.lastFrame = 3;
    gain.frequencyMinHz = 100.0; gain.frequencyMaxHz = 110.0; gain.value = 2.0;
    const double untouched = matrix.at(2, 0).amplitude;
    assert(service.apply(matrix, gain).changed);
    assert(matrix.at(2, 0).amplitude == untouched);

    auto seek = ats::PartialEditCommand{};
    seek.kind = ats::PartialEditKind::seek; seek.value = 1.5;
    assert(service.apply(matrix, seek).seekNorm == 1.0);
    auto zoom = ats::PartialEditCommand{};
    zoom.kind = ats::PartialEditKind::zoom; zoom.value = 2.0; zoom.anchor = 0.5;
    assert(service.apply(matrix, zoom).accepted);
    auto pan = ats::PartialEditCommand{};
    pan.kind = ats::PartialEditKind::pan; pan.points = { { 0.25, 10.0 } };
    assert(service.apply(matrix, pan).accepted);

    auto key = ats::PartialEditCommand{};
    key.kind = ats::PartialEditKind::keyboard; key.keyNote = 60; key.keyVelocity = 0.8f; key.keyDown = true;
    const auto keyResult = service.apply(matrix, key);
    assert(keyResult.accepted && keyResult.keyNote == 60 && keyResult.keyDown);

    ats::ui::PartialEditView view;
    view.beginCellEdit(2, 3, 4.0, 440.0);
    view.setDraftAmplitude(3.0);
    const auto cellCommand = view.commitCellEdit();
    assert(!view.editing() && cellCommand.kind == ats::PartialEditKind::writeCell);
    assert(service.apply(matrix, cellCommand).changed);
    assert(matrix.at(2, 3).amplitude == 3.0 && matrix.at(2, 3).frequencyHz == 440.0);
    assert(std::isfinite(matrix.at(2, 3).phase));
    return 0;
}
