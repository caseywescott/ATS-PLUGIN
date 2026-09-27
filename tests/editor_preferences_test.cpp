#include "../src/ui/EditorPreferences.h"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <iostream>

int main()
{
    using ats::ui::EditorPreferences;
    using ats::ui::ThemeId;

    // Round trip.
    EditorPreferences porcelain;
    porcelain.theme = ThemeId::porcelain;
    assert(EditorPreferences::parse(porcelain.serialize()).theme == ThemeId::porcelain);
    EditorPreferences ember;
    assert(EditorPreferences::parse(ember.serialize()).theme == ThemeId::ember);

    // Tolerant parsing: comments, CRLF, unknown keys, bad values, missing file.
    assert(EditorPreferences::parse("# c\r\nfuture=1\r\ntheme=porcelain\r\n").theme == ThemeId::porcelain);
    assert(EditorPreferences::parse("theme=neon\n").theme == ThemeId::ember);
    assert(EditorPreferences::parse("garbage").theme == ThemeId::ember);
    assert(EditorPreferences::load("/nonexistent/ats-editor-preferences.txt").theme == ThemeId::ember);

    // File persistence, as the wrappers use it across editor instances.
    const auto path = (std::filesystem::temp_directory_path() / "ats-editor-preferences-test.txt").string();
    std::remove(path.c_str());
    assert(porcelain.save(path));
    assert(EditorPreferences::load(path).theme == ThemeId::porcelain);
    assert(ember.save(path));
    assert(EditorPreferences::load(path).theme == ThemeId::ember);
    std::remove(path.c_str());
    std::cout << "editor_preferences_test: theme preference round trip ok\n";
    return 0;
}
