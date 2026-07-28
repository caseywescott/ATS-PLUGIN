#include "../src/ui/EditorLifecycleModel.h"

#include <cassert>

int main()
{
    ats::ui::EditorLifecycleModel lifecycle;
    assert(lifecycle.attach());
    assert(lifecycle.openDialog({ ats::ui::DialogKind::importWav, 0, 1, "source-a", {} }));
    assert(!lifecycle.openDialog({ ats::ui::DialogKind::savePreset, 0, 0, "preset", {} }));
    const auto import = lifecycle.acceptDialog({ "/tmp/source.wav" });
    assert(import.has_value());
    const auto* importAction = std::get_if<ats::ui::ImportWavAction>(&*import);
    assert(importAction != nullptr && importAction->slot == 1);
    assert(!lifecycle.dialog().active());

    assert(lifecycle.openDialog({ ats::ui::DialogKind::bounceTimeline, 0, 0, "bounce", {} }));
    assert(lifecycle.cancelDialog());
    assert(!lifecycle.dialog().active());

    const auto popupId = lifecycle.openPopup({ ats::ui::PopupRootKind::view, "spectrum", true, true }, "spectrum");
    assert(popupId != 0 && lifecycle.focus() == ats::ui::FocusOwner::popup);
    lifecycle.setPointerCaptured(true);
    lifecycle.beginClose();
    assert(lifecycle.lifecycle() == ats::ui::EditorLifecycle::closing);
    assert(!lifecycle.dialog().active() && !lifecycle.pointerCaptured());
    assert(lifecycle.focus() == ats::ui::FocusOwner::none);
    lifecycle.finishClose();
    assert(lifecycle.lifecycle() == ats::ui::EditorLifecycle::detached);
    assert(!lifecycle.openDialog({ ats::ui::DialogKind::importAts, 0, 0, "ats", {} }));
    return 0;
}
