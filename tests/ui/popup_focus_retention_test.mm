// A popup opened from a nested frame must survive the mouse-down bubbling back
// up to the editor root. Visage propagates every mouse event to each ancestor
// that receives child mouse events, so the root's handler runs *after* the one
// that showed the menu; if the root then claims keyboard focus the popup frame
// loses it, fades out and removes itself without ever reporting a cancel.
//
// The real-world symptom is that a menu closes before the user can pick a row.
// It only shows up when the run loop is actually allowed to spin, so this test
// holds the button and lets Visage's own timers and animations run.

#include "../../src/ui/VisageEditor.h"
#include "../../src/ui/Editor.h"

#import <Cocoa/Cocoa.h>

#include <cstdlib>
#include <iostream>
#include <memory>

namespace
{
[[noreturn]] void fail(const char* expression, int line)
{
    std::cerr << "Popup focus retention check failed at line " << line << ": " << expression << '\n';
    std::abort();
}

#define REQUIRE(expression) ((expression) ? (void) 0 : fail(#expression, __LINE__))

struct State
{
    int popupShown = 0;
    int popupDismissed = 0;
    int spectrumModeSelections = 0;
    int lastSpectrumMode = -1;
};

void onPopupShown(void* context) { ++static_cast<State*>(context)->popupShown; }
void onPopupDismissed(void* context) { ++static_cast<State*>(context)->popupDismissed; }
void onSpectrumModeSelected(void* context, int, int mode)
{
    ++static_cast<State*>(context)->spectrumModeSelections;
    static_cast<State*>(context)->lastSpectrumMode = mode;
}

void drainEvents(NSApplication* application)
{
    for (int index = 0; index < 64; ++index)
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

// Draining alone races past the defect: Visage gives a freshly shown menu a
// 20 ms grace period, and a synthetic click can complete inside it. Real clicks
// are far slower, so the run loop has to genuinely advance.
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
        [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode
                                 beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
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

        NSWindow* window = [[NSWindow alloc]
            initWithContentRect:NSMakeRect(60.0, 60.0, 1280.0, 940.0)
                      styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskResizable)
                        backing:NSBackingStoreBuffered
                          defer:NO];
        [window makeKeyAndOrderFront:nil];
        [application activateIgnoringOtherApps:YES];

        State state;
        ats::ui::EditorEventSink sink;
        sink.context = &state;
        sink.popupShown = onPopupShown;
        sink.popupDismissed = onPopupDismissed;
        sink.spectrumModeSelected = onSpectrumModeSelected;

        // Mirror the AU: an intermediate host view between window and editor.
        NSView* host = [[NSView alloc] initWithFrame:NSMakeRect(0.0, 0.0, 1280.0, 940.0)];
        [[window contentView] addSubview:host];

        auto editor = std::make_unique<ats::ui::VisageEditor>(sink);
        editor->setSize(1280, 940);
        REQUIRE(editor->setParent((__bridge void*) host));
        editor->show();
        drainEvents(application);
        // The defect only shows once Visage renders the popup fade, which it
        // does for an on-screen window only; otherwise skip (ctest code 77)
        // instead of passing vacuously.
        bool locked = false;
        if (CFDictionaryRef session = CGSessionCopyCurrentDictionary())
        {
            const auto value = static_cast<CFBooleanRef>(
                CFDictionaryGetValue(session, CFSTR("CGSSessionScreenIsLocked")));
            locked = value != nullptr && CFBooleanGetValue(value);
            CFRelease(session);
        }
        if (locked || !(window.occlusionState & NSWindowOcclusionStateVisible))
        {
            std::cout << "popup_focus_retention_test: skipped (window not rendering)\n";
            return 77;
        }
        // A loaded current source enables the spectrum mode rows.
        ats::ui::EditorSnapshot snapshot;
        snapshot.revision = 1;
        snapshot.sourceAvailable[0] = true;
        editor->setEditorSnapshot(snapshot);
        drainEvents(application);

        NSView* view = (__bridge NSView*) editor->nativeHandle();
        view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        drainEvents(application);

        const CGFloat viewHeight = view.bounds.size.height;
        auto windowPoint = [&](CGFloat logicalX, CGFloat logicalY) {
            return [view convertPoint:NSMakePoint(logicalX, viewHeight - logicalY) toView:nil];
        };
        int eventNumber = 100;
        auto send = [&](NSEventType type, CGFloat logicalX, CGFloat logicalY) {
            NSEvent* event = [NSEvent mouseEventWithType:type
                                                location:windowPoint(logicalX, logicalY)
                                           modifierFlags:0
                                               timestamp:0.0
                                            windowNumber:[window windowNumber]
                                                 context:nil
                                             eventNumber:eventNumber++
                                              clickCount:(type == NSEventTypeMouseMoved ? 0 : 1)
                                                pressure:(type == NSEventTypeLeftMouseUp ? 0.0 : 1.0)];
            switch (type)
            {
            case NSEventTypeMouseMoved: [view mouseMoved:event]; break;
            case NSEventTypeLeftMouseDown: [view mouseDown:event]; break;
            case NSEventTypeLeftMouseDragged: [view mouseDragged:event]; break;
            default: [view mouseUp:event]; break;
            }
        };

        // At 1280x940 the SESSION well is wide enough for the view buttons,
        // so the popup chip at its top-left is the source menu
        // (SpectrumChromeLayout::sourceMenuButton, editor x 311-443, y 139-161);
        // this point is its centre. REQUIRE below turns layout drift into a
        // loud failure rather than a vacuous pass.
        const CGFloat chipX = 377.0;
        const CGFloat chipY = 150.0;

        send(NSEventTypeMouseMoved, chipX, chipY);
        send(NSEventTypeLeftMouseDown, chipX, chipY);
        drainEvents(application);
        REQUIRE(state.popupShown == 1);

        // A slow, human-paced press: hold, twitch, release, then pause.
        runFor(application, 0.15);
        send(NSEventTypeLeftMouseDragged, chipX + 1.0, chipY);
        runFor(application, 0.05);
        send(NSEventTypeLeftMouseUp, chipX + 1.0, chipY);
        // Long enough for Visage's 80 ms fade to finish and the frame to
        // remove itself if it lost focus; the fade only advances on rendered
        // frames, so the window must be on screen (see the windowed label).
        runFor(application, 0.6);

        // The menu must still be there. Visage geometry (4 px selection padding,
        // 22 px rows) places the list at editor (311, 165); its first row,
        // centred at y 180, is the "Current" source. The list is flat: no
        // submenu to open first. A menu that faded out on focus loss accepts
        // none of this.
        REQUIRE(state.popupDismissed == 0);
        const CGFloat currentRowY = 180.0;
        send(NSEventTypeMouseMoved, chipX, currentRowY);
        drainEvents(application);
        for (int click = 0; click < 2 && state.spectrumModeSelections == 0; ++click)
        {
            send(NSEventTypeLeftMouseDown, chipX, currentRowY);
            send(NSEventTypeLeftMouseUp, chipX, currentRowY);
            drainEvents(application);
        }

        REQUIRE(state.spectrumModeSelections == 1);
        // The selection must have come through the live popup, which reports
        // its own dismissal, not through a click that fell through to the well.
        REQUIRE(state.popupDismissed == 1);

        // The views are one click each: the "Partials" button (second in the
        // strip, editor x 525-587) switches without any popup.
        const CGFloat partialsX = 556.0;
        send(NSEventTypeMouseMoved, partialsX, chipY);
        send(NSEventTypeLeftMouseDown, partialsX, chipY);
        send(NSEventTypeLeftMouseUp, partialsX, chipY);
        drainEvents(application);
        REQUIRE(state.spectrumModeSelections == 2);
        REQUIRE(state.lastSpectrumMode == 1);
        REQUIRE(state.popupShown == 1);

        editor.reset();
        std::cout << "popup_focus_retention_test passed\n";
        return 0;
    }
}
