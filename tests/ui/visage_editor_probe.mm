#include "../../src/ui/VisageEditor.h"

#import <Cocoa/Cocoa.h>

#include <cassert>
#include <cstring>
#include <iostream>

namespace
{
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
    for (;;)
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
        ats::ui::EditorEventSink sink { &state, mouseDown, textInput, focusChanged };
        auto editor = std::make_unique<ats::ui::VisageEditor>(sink);

        std::uint32_t width = 0;
        std::uint32_t height = 0;
        editor->getSize(width, height);
        assert(width == 1280 && height == 940);
        assert(editor->setSize(640, 480));
        editor->setScale(1.25);
        assert(editor->setParent((__bridge void*)[hostWindow contentView]));
        assert(editor->isShowing());
        editor->show();
        drainEvents(application);

        NSView* embeddedView = (__bridge NSView*)editor->nativeHandle();
        assert(embeddedView != nil);
        assert([embeddedView superview] == [hostWindow contentView]);
        assert([hostWindow makeFirstResponder:embeddedView]);

        NSEvent* mouse = [NSEvent mouseEventWithType:NSEventTypeLeftMouseDown
                                            location:NSMakePoint(40.0, 40.0)
                                       modifierFlags:0
                                           timestamp:0.0
                                        windowNumber:[hostWindow windowNumber]
                                             context:nil
                                         eventNumber:1
                                          clickCount:1
                                            pressure:1.0];
        [hostWindow sendEvent:mouse];
        NSEvent* mouseUp = [NSEvent mouseEventWithType:NSEventTypeLeftMouseUp
                                              location:NSMakePoint(40.0, 40.0)
                                         modifierFlags:0
                                             timestamp:0.0
                                          windowNumber:[hostWindow windowNumber]
                                               context:nil
                                           eventNumber:1
                                            clickCount:1
                                              pressure:0.0];
        [hostWindow sendEvent:mouseUp];

        NSEvent* key = [NSEvent keyEventWithType:NSEventTypeKeyDown
                                        location:NSMakePoint(40.0, 40.0)
                                   modifierFlags:0
                                       timestamp:0.0
                                    windowNumber:[hostWindow windowNumber]
                                         context:nil
                                      characters:@"x"
                     charactersIgnoringModifiers:@"x"
                                       isARepeat:NO
                                         keyCode:7];
        [hostWindow sendEvent:key];
        drainEvents(application);

        assert(state.mouseDown == 1);
        assert(state.textInput == 1 && std::strcmp(state.text, "x") == 0);
        assert(state.focusChanges > 0 && state.focused);

        editor->hide();
        assert(!editor->isShowing());
        editor.reset();
        assert([[hostWindow contentView] subviews].count == 0);
        [hostWindow orderOut:nil];
        [hostWindow close];

        std::cout << "Visage native editor probe passed: attach, resize, scale, focus, mouse, text, hide, teardown\n";
    }
    return 0;
}
