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

    // A native cancel/error path must leave no stale request behind. An empty
    // accepted path is treated as an error and keeps the active dialog open
    // until the backend explicitly resolves it as cancelled.
    assert(lifecycle.openDialog({ ats::ui::DialogKind::importAts, 0, 0, "source-b", {} }));
    assert(lifecycle.dialog().outcome() == ats::ui::DialogOutcome::pending);
    assert(!lifecycle.acceptDialog({}).has_value());
    assert(lifecycle.dialog().active());
    assert(lifecycle.dialog().outcome() == ats::ui::DialogOutcome::invalid);
    assert(lifecycle.dialog().error().code == "empty_dialog_path");
    assert(lifecycle.cancelDialog());
    assert(!lifecycle.dialog().active());
    assert(lifecycle.dialog().outcome() == ats::ui::DialogOutcome::cancelled);

    assert(lifecycle.openDialog({ ats::ui::DialogKind::exportAts, 0, 1, "export", {} }));
    const auto exportAction = lifecycle.acceptDialog({ "/tmp/export.ats" });
    const auto* exportAts = exportAction ? std::get_if<ats::ui::ExportAtsAction>(&*exportAction) : nullptr;
    assert(exportAts != nullptr && exportAts->source == 1);

    assert(lifecycle.openDialog({ ats::ui::DialogKind::savePreset, 0, 0, "preset", {} }));
    const auto saveAction = lifecycle.acceptDialog({ "/tmp/preset.atspreset" });
    assert(saveAction && std::get_if<ats::ui::SavePresetAction>(&*saveAction) != nullptr);
    assert(lifecycle.dialog().outcome() == ats::ui::DialogOutcome::accepted);
    assert(lifecycle.openDialog({ ats::ui::DialogKind::loadPreset, 0, 0, "preset", {} }));
    const auto loadAction = lifecycle.acceptDialog({ "/tmp/preset.atspreset" });
    assert(loadAction && std::get_if<ats::ui::LoadPresetAction>(&*loadAction) != nullptr);

    assert(lifecycle.openDialog({ ats::ui::DialogKind::bounceTimeline, 0, 0, "bounce", {} }));
    assert(lifecycle.cancelDialog());
    assert(!lifecycle.dialog().active());

    const auto popupId = lifecycle.openPopup({ ats::ui::PopupRootKind::view, "spectrum", true, true }, "spectrum");
    assert(popupId != 0 && lifecycle.focus() == ats::ui::FocusOwner::popup);
    lifecycle.setPointerCaptured(true);
    assert(lifecycle.openDialog({ ats::ui::DialogKind::loadPreset, 0, 0, "closing", {} }));
    lifecycle.beginClose();
    assert(lifecycle.lifecycle() == ats::ui::EditorLifecycle::closing);
    assert(!lifecycle.dialog().active() && !lifecycle.pointerCaptured());
    assert(lifecycle.focus() == ats::ui::FocusOwner::none);
    lifecycle.finishClose();
    assert(lifecycle.lifecycle() == ats::ui::EditorLifecycle::detached);
    assert(!lifecycle.openDialog({ ats::ui::DialogKind::importAts, 0, 0, "ats", {} }));
    return 0;
}
