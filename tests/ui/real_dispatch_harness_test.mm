#include "../../src/ui/UiActionDispatcher.h"
#include "../../src/ui/EditorActionProtocol.h"
#include "../../src/ui/UiScheduler.h"
#include "../../src/ui/VisageEditor.h"
#include "../../src/host/HostAdapter.h"

#import <Cocoa/Cocoa.h>

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>

@interface AtsTestPasteboard : NSObject
- (instancetype)initWithFileUrl:(NSURL*)fileUrl;
@end

@implementation AtsTestPasteboard
{
    NSURL* fileUrl_;
}

- (instancetype)initWithFileUrl:(NSURL*)fileUrl
{
    self = [super init];
    if (self != nil)
        fileUrl_ = fileUrl;
    return self;
}

- (NSArray*)readObjectsForClasses:(NSArray<Class>*)classes options:(NSDictionary*)options
{
    (void)classes;
    (void)options;
    return @[ fileUrl_ ];
}
@end

@interface AtsTestDraggingInfo : NSObject
- (instancetype)initWithPasteboard:(id)pasteboard location:(NSPoint)location;
@end

@implementation AtsTestDraggingInfo
{
    id pasteboard_;
    NSPoint location_;
}

- (instancetype)initWithPasteboard:(id)pasteboard location:(NSPoint)location
{
    self = [super init];
    if (self != nil)
    {
        pasteboard_ = pasteboard;
        location_ = location;
    }
    return self;
}

- (NSPasteboard*)draggingPasteboard { return static_cast<NSPasteboard*>(pasteboard_); }
- (NSPoint)draggingLocation { return location_; }
@end

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
    int popupShown = 0;
    int popupSelections = 0;
    int popupDismissed = 0;
    int pageSelections = 0;
    int lastPage = -1;
    int parameterGestures = 0;
    bool parameterBegin = false;
    bool parameterEnd = false;
    float parameterValue = 0.0f;
    bool focused = false;
    int typedParameterActions = 0;
    bool typedParameterBegin = false;
    bool typedParameterEnd = false;
    int typedImportWavActions = 0;
    int fileDrops = 0;
    int lastDropSlot = -1;
    bool lastDropAccepted = false;
};

void mouseDown(void* context)
{
    auto& state = *static_cast<HarnessState*>(context);
    REQUIRE(state.actions.submit(ats::ui::SetParameterAction { "synth.frequency", 0.5f, true, false }));
    const ats::ui::UiRefreshInput refreshInput { true, false, false, false, true, 17 };
    const auto now = ats::MonotonicClock::now();
    state.scheduler.post(state.registration, now, refreshInput);
    if (state.refreshes == 0)
        REQUIRE(state.scheduler.post(state.registration, now + std::chrono::milliseconds(100),
                                     refreshInput));
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

void popupShown(void* context) { ++static_cast<HarnessState*>(context)->popupShown; }

void popupSelected(void* context, int optionId)
{
    auto& state = *static_cast<HarnessState*>(context);
    REQUIRE(optionId == 1);
    ++state.popupSelections;
}

void popupDismissed(void* context) { ++static_cast<HarnessState*>(context)->popupDismissed; }

void pageSelected(void* context, int pageIndex)
{
    auto& state = *static_cast<HarnessState*>(context);
    ++state.pageSelections;
    state.lastPage = pageIndex;
}

void parameterGesture(void* context, const char* id, float value, bool begin, bool end)
{
    auto& state = *static_cast<HarnessState*>(context);
    REQUIRE(std::strcmp(id, "analysisLowestFreq") == 0);
    REQUIRE(value >= 1.0f && value <= 2000.0f);
    ++state.parameterGestures;
    state.parameterBegin = state.parameterBegin || begin;
    state.parameterEnd = state.parameterEnd || end;
    state.parameterValue = value;
}

void typedAction(void* context, const ats::ui::EditorAction* action)
{
    auto& state = *static_cast<HarnessState*>(context);
    REQUIRE(action != nullptr);
    if (std::holds_alternative<ats::ui::SetParameterAction>(*action))
    {
        const auto& parameter = std::get<ats::ui::SetParameterAction>(*action);
        REQUIRE(parameter.id == "analysisLowestFreq");
        ++state.typedParameterActions;
        state.typedParameterBegin = state.typedParameterBegin || parameter.begin;
        state.typedParameterEnd = state.typedParameterEnd || parameter.end;
        return;
    }
    REQUIRE(std::holds_alternative<ats::ui::ImportWavAction>(*action));
    const auto& imported = std::get<ats::ui::ImportWavAction>(*action);
    REQUIRE(imported.slot >= 0 && imported.slot <= 1);
    REQUIRE(imported.path.string() == "/tmp/ats-native-drop.wav");
    ++state.typedImportWavActions;
}

void fileDrop(void* context, int slot, const char* path, bool accepted)
{
    auto& state = *static_cast<HarnessState*>(context);
    REQUIRE(path != nullptr && std::strcmp(path, "/tmp/ats-native-drop.wav") == 0);
    ++state.fileDrops;
    state.lastDropSlot = slot;
    state.lastDropAccepted = accepted;
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

        ats::ui::EditorEventSink sink { &state, mouseDown, mouseUp, textInput, focusChanged,
                                        popupShown, popupSelected, popupDismissed,
                                        pageSelected, parameterGesture, nullptr, nullptr, nullptr,
                                        nullptr, nullptr, typedAction };
        sink.fileDrop = fileDrop;
        auto editor = std::make_unique<ats::ui::VisageEditor>(sink);
        ats::host::HostAdapter host;
        REQUIRE(host.initialize(ats::host::HostFormat::standalone));
        REQUIRE(host.attachEditor(*editor, (__bridge void*)[hostWindow contentView], 1.25f,
                                  640.0f, 480.0f));
        REQUIRE(host.editorAttached());
        REQUIRE(host.editorVisible());
        REQUIRE(host.editorScale() == 1.25f);
        drainEvents(application);

        NSView* embeddedView = (__bridge NSView*)editor->nativeHandle();
        REQUIRE(embeddedView != nil);
        REQUIRE([embeddedView superview] == [hostWindow contentView]);
        REQUIRE([hostWindow makeFirstResponder:embeddedView]);

        REQUIRE(host.setEditorLogicalSize(700.0f, 520.0f));
        std::uint32_t resizedWidth = 0;
        std::uint32_t resizedHeight = 0;
        editor->getSize(resizedWidth, resizedHeight);
        REQUIRE(resizedWidth == 700 && resizedHeight == 520);

        auto windowPointForLogical = [&](NSPoint logicalPoint) {
            const NSPoint viewPoint = NSMakePoint(logicalPoint.x / 1.25,
                                                  embeddedView.bounds.size.height -
                                                      logicalPoint.y / 1.25);
            return [embeddedView convertPoint:viewPoint toView:nil];
        };

        AtsTestPasteboard* dropPasteboard = [[AtsTestPasteboard alloc]
            initWithFileUrl:[NSURL fileURLWithPath:@"/tmp/ats-native-drop.wav"]];
        // The native backend converts AppKit window coordinates through the
        // host view and its scale. Exercise a compact grid spanning both
        // source cards rather than assuming a particular backing-space origin.
        const CGFloat dropXs[] { 40.0, 100.0, 200.0, 350.0, 430.0, 520.0, 600.0 };
        const CGFloat dropYs[] { 120.0, 140.0, 160.0 };
        bool nativeDropAccepted = false;
        for (const auto x : dropXs)
        {
            for (const auto y : dropYs)
            {
                const auto dropPoint = windowPointForLogical(NSMakePoint(x, y));
                AtsTestDraggingInfo* dropInfo = [[AtsTestDraggingInfo alloc]
                    initWithPasteboard:dropPasteboard location:dropPoint];
                const auto nativeDropInfo = static_cast<id<NSDraggingInfo>>(dropInfo);
                if ([embeddedView draggingEntered:nativeDropInfo] != NSDragOperationCopy)
                    continue;
                REQUIRE([embeddedView performDragOperation:nativeDropInfo]);
                nativeDropAccepted = true;
                break;
            }
            if (nativeDropAccepted)
                break;
        }
        REQUIRE(nativeDropAccepted);
        REQUIRE(state.fileDrops == 1 && state.lastDropSlot >= 0 && state.lastDropSlot <= 1 &&
                state.lastDropAccepted);
        REQUIRE(state.typedImportWavActions == 1);

        NSEvent* mouse = [NSEvent mouseEventWithType:NSEventTypeLeftMouseDown
                                            location:windowPointForLogical(NSMakePoint(600.0, 120.0))
                                       modifierFlags:0 timestamp:0.0
                                        windowNumber:[hostWindow windowNumber]
                                             context:nil eventNumber:1 clickCount:1 pressure:1.0];
        [embeddedView mouseDown:mouse];
        NSEvent* mouseUp = [NSEvent mouseEventWithType:NSEventTypeLeftMouseUp
                                              location:windowPointForLogical(NSMakePoint(600.0, 120.0))
                                         modifierFlags:0 timestamp:0.0
                                          windowNumber:[hostWindow windowNumber]
                                               context:nil eventNumber:1 clickCount:1 pressure:0.0];
        [embeddedView mouseUp:mouseUp];
        NSEvent* key = [NSEvent keyEventWithType:NSEventTypeKeyDown
                                        location:windowPointForLogical(NSMakePoint(600.0, 120.0))
                                   modifierFlags:0 timestamp:0.0
                                    windowNumber:[hostWindow windowNumber]
                                         context:nil characters:@"x"
                     charactersIgnoringModifiers:@"x" isARepeat:NO keyCode:7];
        [embeddedView keyDown:key];
        drainEvents(application);
        REQUIRE(state.focusChanges > 0 && state.focused);

        auto sendEmbeddedMouse = [&](NSEventType type, NSPoint logicalPoint, int eventNumber) {
            const NSPoint viewPoint = NSMakePoint(logicalPoint.x / 1.25,
                                                  embeddedView.bounds.size.height - logicalPoint.y / 1.25);
            const NSPoint windowPoint = [embeddedView convertPoint:viewPoint toView:nil];
            NSEvent* event = [NSEvent mouseEventWithType:type
                                                location:windowPoint
                                           modifierFlags:0 timestamp:0.0
                                                windowNumber:[hostWindow windowNumber]
                                                     context:nil eventNumber:eventNumber
                                                  clickCount:1 pressure:type == NSEventTypeLeftMouseUp ? 0.0 : 1.0];
            if (type == NSEventTypeLeftMouseDragged)
                [embeddedView mouseDragged:event];
            else if (type == NSEventTypeLeftMouseUp)
                [embeddedView mouseUp:event];
            else
                [embeddedView mouseDown:event];
        };

        const CGFloat pageX[] { 220.0, 240.0, 260.0 };
        const CGFloat pageY[] { 74.0, 90.0 };
        for (const auto x : pageX)
        {
            for (const auto y : pageY)
            {
                if (state.lastPage == 1)
                    break;
                sendEmbeddedMouse(NSEventTypeLeftMouseDown, NSMakePoint(x, y), 8);
                sendEmbeddedMouse(NSEventTypeLeftMouseUp, NSMakePoint(x, y), 8);
            }
            if (state.lastPage == 1)
                break;
        }
        REQUIRE(state.lastPage == 1);

        const CGFloat parameterX[] { 220.0, 300.0, 360.0 };
        const CGFloat parameterY[] { 240.0, 260.0, 280.0 };
        for (const auto x : parameterX)
        {
            for (const auto y : parameterY)
            {
                if (state.parameterBegin && state.parameterEnd)
                    break;
                sendEmbeddedMouse(NSEventTypeLeftMouseDown, NSMakePoint(x, y), 9);
                sendEmbeddedMouse(NSEventTypeLeftMouseDragged, NSMakePoint(x + 80.0, y), 10);
                sendEmbeddedMouse(NSEventTypeLeftMouseUp, NSMakePoint(x + 80.0, y), 10);
            }
            if (state.parameterBegin && state.parameterEnd)
                break;
        }
        REQUIRE(state.parameterGestures >= 2 && state.parameterBegin && state.parameterEnd);
        REQUIRE(state.typedParameterActions >= 2 && state.typedParameterBegin &&
                state.typedParameterEnd);

        NSEvent* rightMouse = [NSEvent mouseEventWithType:NSEventTypeRightMouseDown
                                                 location:NSMakePoint(40.0, 40.0)
                                            modifierFlags:0 timestamp:0.0
                                             windowNumber:[hostWindow windowNumber]
                                                  context:nil eventNumber:2 clickCount:1 pressure:1.0];
        [embeddedView rightMouseDown:rightMouse];
        drainEvents(application);
        REQUIRE(state.popupShown == 1);

        auto sendPopupClick = [&](CGFloat logicalX, CGFloat logicalY, int eventNumber) {
            const NSPoint popupWindowPoint = windowPointForLogical(NSMakePoint(logicalX, logicalY));
            NSEvent* popupMove = [NSEvent mouseEventWithType:NSEventTypeMouseMoved
                                                    location:popupWindowPoint
                                               modifierFlags:0 timestamp:0.0
                                               windowNumber:[hostWindow windowNumber]
                                                    context:nil eventNumber:eventNumber clickCount:0 pressure:0.0];
            [embeddedView mouseMoved:popupMove];
            NSEvent* popupMouse = [NSEvent mouseEventWithType:NSEventTypeLeftMouseDown
                                                     location:popupWindowPoint
                                                modifierFlags:0 timestamp:0.0
                                                windowNumber:[hostWindow windowNumber]
                                                     context:nil eventNumber:eventNumber clickCount:1 pressure:1.0];
            [embeddedView mouseDown:popupMouse];
            NSEvent* popupMouseUp = [NSEvent mouseEventWithType:NSEventTypeLeftMouseUp
                                                       location:popupWindowPoint
                                                  modifierFlags:0 timestamp:0.0
                                                  windowNumber:[hostWindow windowNumber]
                                                       context:nil eventNumber:eventNumber clickCount:1 pressure:0.0];
            [embeddedView mouseUp:popupMouseUp];
            drainEvents(application);
        };

        // PopupMenuFrame places the first option at x=40 and y=44 when the
        // deterministic probe popup is shown at {40, 40}. Retry a tight band
        // around that row while allowing the native animation/timer to settle.
        const CGFloat popupRowCandidates[] { 48.0, 52.0, 56.0, 60.0, 64.0 };
        for (const auto logicalY : popupRowCandidates)
        {
            if (state.popupSelections != 0)
                break;
            if (state.popupDismissed >= state.popupShown)
            {
                [embeddedView rightMouseDown:rightMouse];
                drainEvents(application);
            }
            sendPopupClick(60.0, logicalY, 3 + static_cast<int>(logicalY));
            drainEvents(application);
        }
        REQUIRE(state.popupSelections == 1 && state.popupDismissed >= 1);

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
        REQUIRE(state.refreshes >= 1);

        state.scheduler.cancel(state.registration);
        REQUIRE(host.setEditorVisible(false));
        REQUIRE(!host.editorVisible());
        REQUIRE(!editor->isShowing());
        REQUIRE(host.setEditorVisible(true));
        REQUIRE(host.editorVisible());
        REQUIRE(editor->isShowing());
        host.detachEditor();
        REQUIRE(!host.editorAttached());
        REQUIRE(!editor->isShowing());
        editor.reset();
        REQUIRE([[hostWindow contentView] subviews].count == 0);
        [hostWindow close];
    }
    return 0;
}
