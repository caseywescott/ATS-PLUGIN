#include "../../src/ui/UiActionDispatcher.h"
#include "../../src/ui/UiScheduler.h"
#include "../../src/ui/VisageEditor.h"

#import <Cocoa/Cocoa.h>

#include <cassert>
#include <cstring>
#include <memory>

namespace
{
struct HarnessState
{
    ats::ui::UiActionDispatcher actions;
    ats::ui::UiScheduler scheduler;
    ats::ui::UiScheduler::RegistrationId registration = ats::ui::UiScheduler::kInvalidRegistration;
    ats::MonotonicClock::TimePoint start = ats::MonotonicClock::now();
    int refreshes = 0;
    int focusChanges = 0;
    bool focused = false;
};

void mouseDown(void* context)
{
    auto& state = *static_cast<HarnessState*>(context);
    assert(state.actions.submit(ats::ui::SetSpectralViewAction { 0, 1 }));
    assert(state.scheduler.post(state.registration, ats::MonotonicClock::now(),
                                { true, false, false, false, true, 17 }));
}

void textInput(void* context, const char* text)
{
    auto& state = *static_cast<HarnessState*>(context);
    ats::ui::ApplyMathAction action;
    action.script = text;
    assert(state.actions.submit(std::move(action)));
}

void focusChanged(void* context, bool focused)
{
    auto& state = *static_cast<HarnessState*>(context);
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
            return;
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

        NSWindow* hostWindow = [[NSWindow alloc]
            initWithContentRect:NSMakeRect(100.0, 100.0, 900.0, 700.0)
                      styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskResizable)
                        backing:NSBackingStoreBuffered
                          defer:NO];
        [hostWindow makeKeyAndOrderFront:nil];
        [application activateIgnoringOtherApps:YES];

        HarnessState state;
        state.registration = state.scheduler.registerSurface(
            ats::ui::UiRefreshSurface::editor, std::chrono::milliseconds(100),
            [&state](const ats::ui::UiRefreshEvent& event) {
                ++state.refreshes;
                assert(event.sourceRevision == 17);
            });
        assert(state.registration != ats::ui::UiScheduler::kInvalidRegistration);

        ats::ui::EditorEventSink sink { &state, mouseDown, textInput, focusChanged };
        auto editor = std::make_unique<ats::ui::VisageEditor>(sink);
        assert(editor->setSize(640, 480));
        editor->setScale(1.25);
        assert(editor->setParent((__bridge void*)[hostWindow contentView]));
        assert(editor->isShowing());
        drainEvents(application);

        NSView* embeddedView = (__bridge NSView*)editor->nativeHandle();
        assert(embeddedView != nil);
        assert([embeddedView superview] == [hostWindow contentView]);
        assert([hostWindow makeFirstResponder:embeddedView]);

        NSEvent* mouse = [NSEvent mouseEventWithType:NSEventTypeLeftMouseDown
                                            location:NSMakePoint(40.0, 40.0)
                                       modifierFlags:0 timestamp:0.0
                                        windowNumber:[hostWindow windowNumber]
                                             context:nil eventNumber:1 clickCount:1 pressure:1.0];
        [hostWindow sendEvent:mouse];
        NSEvent* key = [NSEvent keyEventWithType:NSEventTypeKeyDown
                                        location:NSMakePoint(40.0, 40.0)
                                   modifierFlags:0 timestamp:0.0
                                    windowNumber:[hostWindow windowNumber]
                                         context:nil characters:@"x"
                     charactersIgnoringModifiers:@"x" isARepeat:NO keyCode:7];
        [hostWindow sendEvent:key];
        drainEvents(application);

        auto viewAction = state.actions.tryPop();
        assert(viewAction.has_value() && std::holds_alternative<ats::ui::SetSpectralViewAction>(*viewAction));
        assert(ats::ui::validateAction(*viewAction).valid);
        auto textAction = state.actions.tryPop();
        assert(textAction.has_value() && std::holds_alternative<ats::ui::ApplyMathAction>(*textAction));
        assert(std::get<ats::ui::ApplyMathAction>(*textAction).script == "x");
        assert(state.refreshes == 1);
        assert(state.focusChanges > 0 && state.focused);

        state.scheduler.cancel(state.registration);
        editor->hide();
        assert(!editor->isShowing());
        editor.reset();
        assert([[hostWindow contentView] subviews].count == 0);
        [hostWindow close];
    }
    return 0;
}
