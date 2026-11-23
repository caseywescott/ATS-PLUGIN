// Select-by-criteria needs each partial's mean SMR.  Imported .ats files do
// not store SMR, so the host evaluates it with ATSA's masking model; the
// spectrum snapshot must carry it.

#include "../src/host/HostAdapter.h"

#include <cassert>
#include <cmath>
#include <iostream>

int main()
{
    ats::host::HostAdapter host;
    assert(host.initialize(ats::host::HostFormat::standalone));
    assert(host.prepare({ 48000.0, 512, 2 }));
    ats::Error error;
    const ats::Path path { std::string(ATS_SOURCE_DIR) + "/crotales.ats" };
    assert(host.importSource(path, 3, ats::engine::SourceImportKind::ats, error) && !error);
    // Show the current model in the spectrum view.
    assert(host.applyEditorAction(ats::ui::SetSpectralViewAction { 0, 1 }, error) && !error);
    const auto snapshot = host.editorSnapshot();
    assert(snapshot.spectrum);
    std::size_t known = 0;
    for (const auto& trace : snapshot.spectrum->partials)
    {
        assert(std::isfinite(trace.meanLevelDb) && trace.activeSeconds > 0.0);
        if (std::isfinite(trace.meanSmrDb))
        {
            assert(trace.meanSmrDb >= 0.0 && trace.meanSmrDb < 200.0);
            ++known;
        }
    }
    assert(!snapshot.spectrum->partials.empty());
    assert(known == snapshot.spectrum->partials.size());
    // Cached: a second snapshot returns the same values.
    const auto again = host.editorSnapshot();
    assert(again.spectrum->partials.front().meanSmrDb == snapshot.spectrum->partials.front().meanSmrDb);
    std::cout << "host_partial_smr_test: ok (" << known << " partials)\n";
    return 0;
}
