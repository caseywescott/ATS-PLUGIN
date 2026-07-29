#include "../../src/ui/VisageEditor.h"
#include "../../src/host/HostAdapter.h"

#import <Cocoa/Cocoa.h>

#include <cstdlib>
#include <cstring>
#include <iostream>

namespace
{
[[noreturn]] void fail(const char* expression, int line)
{
    std::cerr << "Visage editor probe check failed at line " << line << ": " << expression << '\n';
    std::abort();
}

#define REQUIRE(expression) ((expression) ? (void) 0 : fail(#expression, __LINE__))

struct ProbeState
{
    int mouseDown = 0;
    int textInput = 0;
    int focusChanges = 0;
    bool focused = false;
    char text[32]{};
};

void mouseDown(void* context) { ++static_cast<ProbeState*>(context)->mouseDown; }

void textInput(void* context, const char* text)
{
    auto& state = *static_cast<ProbeState*>(context);
    ++state.textInput;
    std::strncpy(state.text, text, sizeof(state.text) - 1);
}

void focusChanged(void* context, bool focused)
{
    auto& state = *static_cast<ProbeState*>(context);
    ++state.focusChanges;
    state.focused = focused;
}

void drainEvents(NSApplication* application)
{
    for (int i = 0; i < 64; ++i)
    {
        NSEvent* event = [application nextEventMatchingMask:NSEventMaskAny
                                                  untilDate:[NSDate dateWithTimeIntervalSinceNow:0.02]
                                                     inMode:NSDefaultRunLoopMode
                                                    dequeue:YES];
        if (event == nil)
            break;
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

        NSRect hostRect = NSMakeRect(100.0, 100.0, 900.0, 700.0);
        NSWindow* hostWindow = [[NSWindow alloc]
            initWithContentRect:hostRect
                      styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskResizable)
                        backing:NSBackingStoreBuffered
                          defer:NO];
        [hostWindow makeKeyAndOrderFront:nil];
        [application activateIgnoringOtherApps:YES];

        ProbeState state;
        ats::ui::EditorEventSink sink { &state, mouseDown, nullptr, textInput, focusChanged };
        auto editor = std::make_unique<ats::ui::VisageEditor>(sink);

        std::uint32_t width = 0;
        std::uint32_t height = 0;
        editor->getSize(width, height);
        REQUIRE(width == 1280 && height == 940);
        REQUIRE(editor->setSize(640, 480));
        editor->setScale(1.25);
        REQUIRE(editor->setParent((__bridge void*)[hostWindow contentView]));
        REQUIRE(editor->isShowing());
        editor->show();
        drainEvents(application);

        NSView* embeddedView = (__bridge NSView*)editor->nativeHandle();
        REQUIRE(embeddedView != nil);
        REQUIRE([embeddedView superview] == [hostWindow contentView]);
        REQUIRE([hostWindow makeFirstResponder:embeddedView]);

        auto windowPointForLogical = [&](NSPoint logicalPoint) {
            const NSPoint viewPoint = NSMakePoint(logicalPoint.x / 1.25,
                                                  embeddedView.bounds.size.height -
                                                      logicalPoint.y / 1.25);
            return [embeddedView convertPoint:viewPoint toView:nil];
        };

        NSEvent* mouse = [NSEvent mouseEventWithType:NSEventTypeLeftMouseDown
                                            location:windowPointForLogical(NSMakePoint(600.0, 120.0))
                                       modifierFlags:0
                                           timestamp:0.0
                                        windowNumber:[hostWindow windowNumber]
                                             context:nil
                                         eventNumber:1
                                          clickCount:1
                                            pressure:1.0];
        [embeddedView mouseDown:mouse];
        NSEvent* mouseUp = [NSEvent mouseEventWithType:NSEventTypeLeftMouseUp
                                              location:windowPointForLogical(NSMakePoint(600.0, 120.0))
                                         modifierFlags:0
                                             timestamp:0.0
                                          windowNumber:[hostWindow windowNumber]
                                               context:nil
                                           eventNumber:1
                                            clickCount:1
                                              pressure:0.0];
        [embeddedView mouseUp:mouseUp];

        NSEvent* key = [NSEvent keyEventWithType:NSEventTypeKeyDown
                                        location:windowPointForLogical(NSMakePoint(600.0, 120.0))
                                   modifierFlags:0
                                       timestamp:0.0
                                    windowNumber:[hostWindow windowNumber]
                                         context:nil
                                      characters:@"x"
                     charactersIgnoringModifiers:@"x"
                                       isARepeat:NO
                                         keyCode:7];
        [embeddedView keyDown:key];
        drainEvents(application);

        REQUIRE(state.mouseDown == 1);
        REQUIRE(state.textInput == 1 && std::strcmp(state.text, "x") == 0);
        REQUIRE(state.focusChanges > 0 && state.focused);

        editor->hide();
        REQUIRE(!editor->isShowing());
        editor.reset();
        REQUIRE([[hostWindow contentView] subviews].count == 0);
        [hostWindow orderOut:nil];
        [hostWindow close];

        ats::host::HostAdapter standaloneHost;
        REQUIRE(standaloneHost.initialize(ats::host::HostFormat::standalone));
        auto standaloneEditor = std::make_unique<ats::ui::VisageEditor>();
        REQUIRE(standaloneHost.attachStandaloneEditor(*standaloneEditor, 1.0f, 480.0f, 320.0f));
        REQUIRE(standaloneHost.editorAttached() && standaloneHost.editorVisible());
        REQUIRE(standaloneEditor->nativeHandle() != nullptr);
        drainEvents(application);
        REQUIRE(standaloneHost.setEditorVisible(false));
        REQUIRE(!standaloneEditor->isShowing());
        standaloneHost.detachEditor();
        standaloneEditor.reset();

        std::cout << "Visage native editor probe passed: embedded and standalone attach, resize, scale, focus, mouse, text, hide, teardown\n";
    }
    return 0;
}
