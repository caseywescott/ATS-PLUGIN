#include "../src/ui/PopupManager.h"
#include "../src/ui/ThemeTokens.h"

#include <cassert>

int main()
{
    const auto ember = ats::ui::ThemeTokens::forTheme(ats::ui::ThemeId::ember);
    const auto porcelain = ats::ui::ThemeTokens::forTheme(ats::ui::ThemeId::porcelain);
    assert(ember.spectrumCanvas == porcelain.spectrumCanvas);
    assert(ember.background != porcelain.background);
    assert(ember.accent.argb != 0 && porcelain.accent.argb != 0);

    ats::ui::PopupManager popups;
    const auto id = popups.open({ ats::ui::PopupKind::view, "spectrum", true, false }, "viewButton");
    assert(popups.open() && popups.state().id == id && popups.state().restoreFocusId == "viewButton");
    assert(popups.outsideClick() && !popups.open());
    const auto modal = popups.open({ ats::ui::PopupKind::timeline, "timeline", false, true });
    assert(!popups.outsideClick() && popups.open());
    assert(popups.dismiss(modal) && !popups.open());
    assert(!popups.dismiss(modal));
    return 0;
}
