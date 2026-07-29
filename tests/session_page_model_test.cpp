#include "../src/ui/SessionPageModel.h"
#include "../src/ui/SessionFileDrop.h"

#include <cassert>
#include <string>

int main()
{
    ats::ui::SessionPageModel model;
    model.setSource(0, { true, true, true, "A", 2.5f, 124, true });
    model.setSource(1, { true, false, true, "B", 1.25f, 42, false });
    model.setPreset("Morph stable default", true);
    assert(model.state().presetSaveEnabled);

    model.setTransport(ats::ui::SessionSource::sourceB, ats::ui::SessionLayer::residual);
    model.setPlaying(true);
    assert(model.previewAction(true).source == 1);
    assert(model.previewAction(true).layer == 2);
    assert(model.previewAction(true).start);
    model.setPlaying(false);

    assert(model.setPlayhead(0.375f));
    assert(model.seekAction().normalizedTime == 0.375f);
    assert(!model.setPlayhead(1.1f));

    assert(model.setTimeline({ 0.0f, 0.4f, 1.0f }));
    assert(!model.setTimeline({ 0.0f, 0.7f, 0.5f }));
    assert(model.timelineAction().normalizedPoints.size() == 3);

    model.setBandCount(8);
    assert(model.setSelectedBand(7));
    assert(!model.setSelectedBand(8));
    model.setBandCount(2);
    assert(model.state().morph.selectedBand == 1);

    assert(!model.setPolicy(0, ats::ui::MorphPolicy::discard));
    model.setDiscardOptIn(true);
    assert(model.setPolicy(0, ats::ui::MorphPolicy::discard));
    assert(model.setVariationStored(2, true));
    assert(!model.setVariationStored(3, true));
    assert(model.variationAction(2, false).slot == 2);
    model.setSmearEnabled(true);
    assert(model.smearAction().enabled);

    model.setSmearnovPanel(true, true, false);
    model.setSmearnovDetection(true, false);
    model.setOnsetOverlay({ 0.2f, 0.8f });
    assert(model.state().smearnov.panelVisible);
    assert(model.onsetAction().normalizedOnsets.size() == 2);

    const auto import = model.importWav(ats::ui::SessionSource::sourceA, { "/tmp/source.wav" });
    assert(import.slot == 0 && import.path.string() == "/tmp/source.wav");
    const auto exportAction = model.exportAts(ats::ui::SessionSource::sourceB, { "/tmp/source.ats" });
    assert(exportAction.source == 1);

    const auto wavDrop = ats::ui::classifySessionSourceDrop(0, { "/tmp/Source.WAVE" });
    assert(wavDrop && wavDrop->kind == ats::ui::SessionDropKind::wav);
    assert(wavDrop->slot == 0 && wavDrop->path.string() == "/tmp/Source.WAVE");
    const auto atsDrop = ats::ui::classifySessionSourceDrop(1, { "/tmp/source.ats" });
    assert(atsDrop && atsDrop->kind == ats::ui::SessionDropKind::ats && atsDrop->slot == 1);
    assert(!ats::ui::classifySessionSourceDrop(2, { "/tmp/source.wav" }));
    assert(!ats::ui::classifySessionSourceDrop(0, { "/tmp/a.wav", "/tmp/b.ats" }));
    assert(!ats::ui::classifySessionSourceDrop(0, { "/tmp/source.txt" }));
    return 0;
}
