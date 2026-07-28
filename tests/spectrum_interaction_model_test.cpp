#include "../src/ui/SpectrumInteractionModel.h"

#include <cassert>

int main()
{
    ats::ui::SpectrumInteractionModel model;
    model.setSource(ats::ui::SpectralTraceSource::sourceB);
    const auto menu = model.menu(true, false);
    assert(menu.size() == 7);
    assert(menu[0].checked && menu[0].enabled);
    assert(!menu[2].enabled);
    assert(menu[6].label == "Isometric envelope RDC");
    assert(model.selectMenuItem(2, true, false).mode == -1);
    const auto select = model.selectMenuItem(4, true, true);
    assert(select.source == 2 && select.mode == static_cast<int>(ats::ui::SpectralTraceMode::isometricResidual));

    assert(model.zoom(2.0f, 0.5f, 0.5f));
    assert(model.navigation().timeEnd - model.navigation().timeStart < 1.0f);
    assert(model.pan(10.0f, -10.0f));
    assert(model.navigation().timeEnd <= 1.0f);
    assert(model.navigation().frequencyStart >= 0.0f);
    assert(!model.zoom(0.0f, 0.5f, 0.5f));
    model.fit();
    assert(model.navigation().timeStart == 0.0f && model.navigation().timeEnd == 1.0f);

    model.setPointerCaptured(true);
    assert(model.navigation().pointerCaptured);
    assert(model.selectPartial(12));
    assert(model.selectBand(3));
    assert(model.applyPartialEdit(12, 8, true).apply);
    assert(model.applyResidualEdit(3, 8, -4.0f).value == -4.0f);
    assert(model.setOnsetOverlay({ 0.1f, 0.8f }).normalizedOnsets.size() == 2);
    return 0;
}
