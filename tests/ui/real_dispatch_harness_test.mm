#include "../../src/ui/UiActionDispatcher.h"
#include "../../src/ui/EditorActionProtocol.h"
#include "../../src/ui/UiScheduler.h"
#include "../../src/ui/VisageEditor.h"

#import <Cocoa/Cocoa.h>

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>

namespace
{
[[noreturn]] void fail(const char* expression, int line)
{
    std::cerr << "Real dispatch harness check failed at line " << line << ": " << expression << '\n';
    std::abort();
}

#define REQUIRE(expression) ((expression) ? (void) 0 : fail(#expression, __LINE__))

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
    REQUIRE(state.actions.submit(ats::ui::SetParameterAction { "synth.frequency", 0.5f, true, false }));
    REQUIRE(state.scheduler.post(state.registration, ats::MonotonicClock::now(),
                                { true, false, false, false, true, 17 }));
}

void mouseUp(void* context)
{
    auto& state = *static_cast<HarnessState*>(context);
    REQUIRE(state.actions.submit(ats::ui::SetParameterAction { "synth.frequency", 0.5f, false, true }));
}

void textInput(void* context, const char* text)
{
    auto& state = *static_cast<HarnessState*>(context);
    ats::ui::ApplyMathAction action;
    action.script = text;
    REQUIRE(state.actions.submit(std::move(action)));
}

void focusChanged(void* context, bool focused)
{
    auto& state = *static_cast<HarnessState*>(context);
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
                REQUIRE(event.sourceRevision == 17);
            });
        REQUIRE(state.registration != ats::ui::UiScheduler::kInvalidRegistration);

        ats::ui::EditorEventSink sink { &state, mouseDown, mouseUp, textInput, focusChanged };
        auto editor = std::make_unique<ats::ui::VisageEditor>(sink);
        REQUIRE(editor->setSize(640, 480));
        editor->setScale(1.25);
        REQUIRE(editor->setParent((__bridge void*)[hostWindow contentView]));
        REQUIRE(editor->isShowing());
        drainEvents(application);

        NSView* embeddedView = (__bridge NSView*)editor->nativeHandle();
        REQUIRE(embeddedView != nil);
        REQUIRE([embeddedView superview] == [hostWindow contentView]);
        REQUIRE([hostWindow makeFirstResponder:embeddedView]);

        NSEvent* mouse = [NSEvent mouseEventWithType:NSEventTypeLeftMouseDown
                                            location:NSMakePoint(40.0, 40.0)
                                       modifierFlags:0 timestamp:0.0
                                        windowNumber:[hostWindow windowNumber]
                                             context:nil eventNumber:1 clickCount:1 pressure:1.0];
        [embeddedView mouseDown:mouse];
        NSEvent* mouseUp = [NSEvent mouseEventWithType:NSEventTypeLeftMouseUp
                                              location:NSMakePoint(40.0, 40.0)
                                         modifierFlags:0 timestamp:0.0
                                          windowNumber:[hostWindow windowNumber]
                                               context:nil eventNumber:1 clickCount:1 pressure:0.0];
        [embeddedView mouseUp:mouseUp];
        NSEvent* key = [NSEvent keyEventWithType:NSEventTypeKeyDown
                                        location:NSMakePoint(40.0, 40.0)
                                   modifierFlags:0 timestamp:0.0
                                    windowNumber:[hostWindow windowNumber]
                                         context:nil characters:@"x"
                     charactersIgnoringModifiers:@"x" isARepeat:NO keyCode:7];
        [embeddedView keyDown:key];
        drainEvents(application);

        auto beginAction = state.actions.tryPop();
        REQUIRE(beginAction.has_value() && std::holds_alternative<ats::ui::SetParameterAction>(*beginAction));
        const auto& begin = std::get<ats::ui::SetParameterAction>(*beginAction);
        REQUIRE(begin.begin && !begin.end && begin.id == "synth.frequency");
        REQUIRE(ats::ui::validateAction(*beginAction).valid);
        auto endAction = state.actions.tryPop();
        REQUIRE(endAction.has_value() && std::holds_alternative<ats::ui::SetParameterAction>(*endAction));
        const auto& end = std::get<ats::ui::SetParameterAction>(*endAction);
        REQUIRE(!end.begin && end.end && end.id == "synth.frequency");
        REQUIRE(ats::ui::validateAction(*endAction).valid);
        auto textAction = state.actions.tryPop();
        REQUIRE(textAction.has_value() && std::holds_alternative<ats::ui::ApplyMathAction>(*textAction));
        REQUIRE(std::get<ats::ui::ApplyMathAction>(*textAction).script == "x");
        REQUIRE(state.refreshes == 1);
        REQUIRE(state.focusChanges > 0 && state.focused);

        state.scheduler.cancel(state.registration);
        editor->hide();
        REQUIRE(!editor->isShowing());
        editor.reset();
        REQUIRE([[hostWindow contentView] subviews].count == 0);
        [hostWindow close];
    }
    return 0;
}
