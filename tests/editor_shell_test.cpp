#include "../src/ui/EditorShell.h"

#include <cassert>

int main()
{
    ats::ui::EditorShellModel shell;
    assert(shell.state().page == ats::ui::EditorPage::session);
    assert(shell.state().theme == ats::ui::ThemeId::ember);
    assert(shell.selectPage(4));
    assert(shell.state().page == ats::ui::EditorPage::math);
    assert(!shell.selectPage(5));
    assert(shell.selectPage(ats::ui::EditorPage::analysis));
    shell.setTheme(ats::ui::ThemeId::porcelain);
    shell.setHistoryAvailability(true, false);
    shell.setActivity(true, true);
    assert(shell.state().theme == ats::ui::ThemeId::porcelain);
    assert(shell.state().canUndo && !shell.state().canRedo);
    assert(shell.state().analysisRunning && shell.state().renderRunning);
    const auto labels = ats::ui::EditorShellModel::pageLabels();
    assert(labels[0] == std::string("SESSION"));
    assert(labels[4] == std::string("MATH"));
    assert(shell.session().state().morph.timeline.size() == 2);
    assert(!shell.session().state().sources[0].hasModel);
    return 0;
}
