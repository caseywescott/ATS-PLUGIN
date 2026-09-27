// Theme persistence across editor instances (audit T-401, ADR-0004 §5):
// choosing a theme from the real header menu writes the per-user preference,
// and a new editor that enables preferences opens in that theme.

#include "../../src/ui/VisageEditor.h"
#include "../../src/ui/Editor.h"

#import <Cocoa/Cocoa.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>

namespace
{
[[noreturn]] void fail(const char* expression, int line)
{
    std::cerr << "Editor preferences probe failed at line " << line << ": " << expression << '\n';
    std::abort();
}

#define REQUIRE(expression) ((expression) ? (void) 0 : fail(#expression, __LINE__))

int themeSelections = -1;
void onThemeSelected(void*, int index) { themeSelections = index; }

void runFor(NSApplication* application, double seconds)
{
    NSDate* deadline = [NSDate dateWithTimeIntervalSinceNow:seconds];
    while ([deadline timeIntervalSinceNow] > 0.0)
    {
        NSEvent* event = [application nextEventMatchingMask:NSEventMaskAny
                                                  untilDate:[NSDate dateWithTimeIntervalSinceNow:0.01]
                                                     inMode:NSDefaultRunLoopMode
                                                    dequeue:YES];
        if (event != nil)
            [application sendEvent:event];
    }
}
}

int main()
{
    @autoreleasepool
    {
        NSApplication* application = [NSApplication sharedApplication];
        [application finishLaunching];
        [application setActivationPolicy:NSApplicationActivationPolicyAccessory];
        const auto path = (std::filesystem::temp_directory_path() / "ats-editor-preferences-probe.txt").string();
        std::remove(path.c_str());

        auto makeWindow = [&] {
            NSWindow* window = [[NSWindow alloc]
                initWithContentRect:NSMakeRect(60.0, 60.0, 1280.0, 940.0)
                          styleMask:NSWindowStyleMaskTitled
                            backing:NSBackingStoreBuffered
                              defer:NO];
            [window makeKeyAndOrderFront:nil];
            [application activateIgnoringOtherApps:YES];
            return window;
        };

        {
            NSWindow* window = makeWindow();
            ats::ui::EditorEventSink sink;
            sink.themeSelected = onThemeSelected;
            auto editor = std::make_unique<ats::ui::VisageEditor>(sink);
            editor->setSize(1280, 940);
            REQUIRE(editor->setParent((__bridge void*) [window contentView]));
            editor->enableUserPreferences(path);
            editor->show();
            REQUIRE(editor->theme() == ats::ui::ThemeId::ember); // no file yet
            runFor(application, 0.2);

            NSView* view = (__bridge NSView*) editor->nativeHandle();
            const CGFloat height = view.bounds.size.height;
            int eventNumber = 1;
            auto click = [&](CGFloat x, CGFloat y) {
                for (NSEventType type : { NSEventTypeLeftMouseDown, NSEventTypeLeftMouseUp })
                {
                    NSEvent* event = [NSEvent mouseEventWithType:type
                        location:[view convertPoint:NSMakePoint(x, height - y) toView:nil]
                        modifierFlags:0 timestamp:0.0 windowNumber:[window windowNumber]
                        context:nil eventNumber:eventNumber++ clickCount:1
                        pressure:type == NSEventTypeLeftMouseUp ? 0.0 : 1.0];
                    if (type == NSEventTypeLeftMouseDown) [view mouseDown:event];
                    else [view mouseUp:event];
                }
                runFor(application, 0.05);
            };
            // Header theme chip: y < 48, x >= width - 220. The menu opens at
            // (width - 208, 8); rows are 22 px after 4 px padding, so
            // "Porcelain" (row 1) spans y 34..56. Visage swallows a list's
            // first mouse-up, hence the repeated row click.
            click(1180.0, 22.0);
            for (int attempt = 0; attempt < 3 && themeSelections != 1; ++attempt)
                click(1120.0, 45.0);
            REQUIRE(themeSelections == 1);
            REQUIRE(editor->theme() == ats::ui::ThemeId::porcelain);
            editor.reset();
            [window orderOut:nil];
        }

        std::ifstream saved(path);
        std::stringstream contents;
        contents << saved.rdbuf();
        REQUIRE(contents.str().find("theme=porcelain") != std::string::npos);

        {
            NSWindow* window = makeWindow();
            auto editor = std::make_unique<ats::ui::VisageEditor>();
            editor->setSize(1280, 940);
            REQUIRE(editor->setParent((__bridge void*) [window contentView]));
            REQUIRE(editor->theme() == ats::ui::ThemeId::ember); // opt-in only
            editor->enableUserPreferences(path);
            REQUIRE(editor->theme() == ats::ui::ThemeId::porcelain);
            editor.reset();
            [window orderOut:nil];
        }
        std::remove(path.c_str());
        std::cout << "editor_preferences_probe: theme persists across editor instances\n";
        return 0;
    }
}
