// Time edits (copy / cut / delete / crop / paste with crossfade) on the
// edited model: frame counts, undo, error codes, and frame-aligned overlays.

#include "../src/host/HostAdapter.h"

#include <cassert>
#include <iostream>

namespace
{
int frames(ats::host::HostAdapter& host)
{
    const auto* source = host.source(ats::host::SourceIdentity::current);
    return source && source->sound ? source->sound.get()->frames : -1;
}
}

int main()
{
    ats::host::HostAdapter host;
    assert(host.initialize(ats::host::HostFormat::standalone));
    assert(host.prepare({ 48000.0, 512, 2 }));
    ats::Error error;
    using ats::ui::TimeEditAction;
    using ats::ui::TimeEditKind;

    // No model to edit yet.
    assert(host.applyEditorAction(TimeEditAction { TimeEditKind::remove, 0, 1, 0 }, error));
    assert(error.code == "no_model");

    const ats::Path path { std::string(ATS_SOURCE_DIR) + "/crotales.ats" };
    assert(host.importSource(path, 3, ats::engine::SourceImportKind::ats, error) && !error);
    // Time edits act on the audition source, as spectrum edits do.
    assert(host.applyEditorAction(ats::ui::PreviewAction { 3, 0, false }, error) && !error);
    const auto original = frames(host);
    assert(original > 40);

    // Invalid range and empty clipboard.
    assert(host.applyEditorAction(TimeEditAction { TimeEditKind::remove, 5, 2, 0 }, error));
    assert(error.code == "invalid_time_edit");
    error = {};
    assert(host.applyEditorAction(TimeEditAction { TimeEditKind::paste, 0, 0, 0 }, error));
    assert(error.code == "empty_clipboard");
    error = {};

    // Copy leaves the model alone; paste inserts with a crossfade; undo restores.
    assert(host.applyEditorAction(TimeEditAction { TimeEditKind::copy, 10, 19, 0 }, error) && !error);
    assert(frames(host) == original);
    assert(host.applyEditorAction(TimeEditAction { TimeEditKind::paste, 30, 0, 3 }, error) && !error);
    assert(frames(host) == original + 10);
    assert(host.applyEditorAction(ats::ui::HistoryAction { false }, error) && !error);
    assert(frames(host) == original);

    // Cut removes the range and fills the clipboard.
    assert(host.applyEditorAction(TimeEditAction { TimeEditKind::cut, 0, 4, 0 }, error) && !error);
    assert(frames(host) == original - 5);
    assert(host.applyEditorAction(TimeEditAction { TimeEditKind::paste, 0, 0, 0 }, error) && !error);
    assert(frames(host) == original);

    // Delete and crop.
    assert(host.applyEditorAction(TimeEditAction { TimeEditKind::remove, 20, 29, 0 }, error) && !error);
    assert(frames(host) == original - 10);
    assert(host.applyEditorAction(TimeEditAction { TimeEditKind::crop, 5, 14, 0 }, error) && !error);
    assert(frames(host) == 10);
    // Deleting everything is refused, and the model is kept.
    assert(host.applyEditorAction(TimeEditAction { TimeEditKind::remove, 0, 9, 0 }, error));
    assert(error.code == "time_edit_failed" && frames(host) == 10);
    error = {};

    // A structural edit drops frame-aligned onset overlays.
    assert(host.applyEditorAction(ats::ui::EditSmearnovOnsetsAction { { 0.25f, 0.75f } }, error) && !error);
    assert(!host.editorSnapshot().onsets.empty());
    assert(host.applyEditorAction(TimeEditAction { TimeEditKind::remove, 0, 1, 0 }, error) && !error);
    assert(host.editorSnapshot().onsets.empty());

    std::cout << "host_time_edit_test: ok (" << original << " frames)\n";
    return 0;
}
