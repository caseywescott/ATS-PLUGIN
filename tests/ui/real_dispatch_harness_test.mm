#include "../../src/ui/UiActionDispatcher.h"
#include "../../src/ui/EditorActionProtocol.h"
#include "../../src/ui/UiScheduler.h"
#include "../../src/ui/VisageEditor.h"
#include "../../src/host/HostAdapter.h"

#import <Cocoa/Cocoa.h>

#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
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
    int typedImportAtsActions = 0;
    int typedAnalyzeActions = 0;
    int fileDrops = 0;
    int sourceDropCounts[2] {};
    int lastDropSlot = -1;
    bool lastDropAccepted = false;
    ats::host::HostAdapter* host = nullptr;
    int decodedSourceDrops = 0;
};

void writeU16(std::ofstream& stream, std::uint16_t value)
{
    stream.put(static_cast<char>(value & 0xffu));
    stream.put(static_cast<char>((value >> 8u) & 0xffu));
}

void writeU32(std::ofstream& stream, std::uint32_t value)
{
    writeU16(stream, static_cast<std::uint16_t>(value & 0xffffu));
    writeU16(stream, static_cast<std::uint16_t>(value >> 16u));
}

void writeDropWave(const std::filesystem::path& path)
{
    constexpr std::int16_t samples[] = { 0, 4096, -4096, 0 };
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    REQUIRE(stream.good());
    stream.write("RIFF", 4); writeU32(stream, 36 + sizeof(samples)); stream.write("WAVEfmt ", 8);
    writeU32(stream, 16); writeU16(stream, 1); writeU16(stream, 1); writeU32(stream, 48000);
    writeU32(stream, 96000); writeU16(stream, 2); writeU16(stream, 16);
    stream.write("data", 4); writeU32(stream, sizeof(samples));
    for (const auto sample : samples) writeU16(stream, static_cast<std::uint16_t>(sample));
    REQUIRE(stream.good());
}

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
    if (const auto* imported = std::get_if<ats::ui::ImportWavAction>(action))
    {
        REQUIRE(imported->slot >= 0 && imported->slot <= 1);
        REQUIRE(imported->slot == state.lastDropSlot);
        REQUIRE(imported->path.string() == "/tmp/ats-native-drop.wav");
        ++state.typedImportWavActions;
        REQUIRE(state.host != nullptr);
        if (state.host->source(imported->slot) == nullptr)
        {
            ats::Error error;
            REQUIRE(state.host->importSource(imported->path, imported->slot,
                                             ats::engine::SourceImportKind::audio, error));
            REQUIRE(!error);
            ++state.decodedSourceDrops;
        }
        return;
    }
    if (const auto* analyze = std::get_if<ats::ui::AnalyzeMorphSourceAction>(action))
    {
        REQUIRE(analyze->slot >= 0 && analyze->slot <= 1);
        REQUIRE(analyze->slot == state.lastDropSlot);
        ++state.typedAnalyzeActions;
        return;
    }
    REQUIRE(std::holds_alternative<ats::ui::ImportAtsAction>(*action));
    const auto& imported = std::get<ats::ui::ImportAtsAction>(*action);
    REQUIRE(imported.slot >= 0 && imported.slot <= 1);
    REQUIRE(imported.slot == state.lastDropSlot);
    REQUIRE(imported.path.string() == "/tmp/ats-native-drop.ats");
    ++state.typedImportAtsActions;
}

void fileDrop(void* context, int slot, const char* path, bool accepted)
{
    auto& state = *static_cast<HarnessState*>(context);
    REQUIRE(path != nullptr);
    const bool knownPath = std::strcmp(path, "/tmp/ats-native-drop.wav") == 0 ||
                           std::strcmp(path, "/tmp/ats-native-drop.ats") == 0;
    REQUIRE(knownPath);
    ++state.fileDrops;
    state.lastDropSlot = slot;
    state.lastDropAccepted = accepted;
    if (accepted && slot >= 0 && slot <= 1)
        ++state.sourceDropCounts[slot];
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

        const std::filesystem::path dropWavePath { "/tmp/ats-native-drop.wav" };
        writeDropWave(dropWavePath);
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
        state.host = &host;
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

        const auto performNativeDrop = [&](id pasteboard, NSPoint nativeViewPoint) {
            const auto dropPoint = [embeddedView convertPoint:nativeViewPoint toView:nil];
            AtsTestDraggingInfo* dropInfo = [[AtsTestDraggingInfo alloc]
                initWithPasteboard:pasteboard location:dropPoint];
            const auto nativeDropInfo = static_cast<id<NSDraggingInfo>>(dropInfo);
            if ([embeddedView draggingEntered:nativeDropInfo] != NSDragOperationCopy)
                return false;
            REQUIRE([embeddedView performDragOperation:nativeDropInfo]);
            return true;
        };

        AtsTestPasteboard* dropPasteboard = [[AtsTestPasteboard alloc]
            initWithFileUrl:[NSURL fileURLWithPath:@"/tmp/ats-native-drop.wav"]];
        // Probe AppKit coordinates directly rather than inferring them from
        // the host scale. Record one accepted point per source-card widget;
        // this makes the mapping evidence resilient to backing-scale changes.
        NSPoint sourceBPoint = NSZeroPoint;
        bool foundSourceAPoint = false;
        bool foundSourceBPoint = false;
        for (CGFloat y = 6.0; y < embeddedView.bounds.size.height &&
             !(foundSourceAPoint && foundSourceBPoint); y += 12.0)
        {
            for (CGFloat x = 6.0; x < embeddedView.bounds.size.width; x += 12.0)
            {
                if (!performNativeDrop(dropPasteboard, NSMakePoint(x, y)))
                    continue;
                if (state.lastDropSlot == 1)
                {
                    sourceBPoint = NSMakePoint(x, y);
                    foundSourceBPoint = true;
                }
                else
                    foundSourceAPoint = true;
                if (foundSourceAPoint && foundSourceBPoint)
                    break;
            }
        }
        REQUIRE(state.sourceDropCounts[0] > 0 && foundSourceAPoint);
        REQUIRE(state.sourceDropCounts[1] > 0 && foundSourceBPoint);
        REQUIRE(state.typedImportWavActions == state.fileDrops);
        REQUIRE(state.typedAnalyzeActions == state.fileDrops);
        REQUIRE(state.decodedSourceDrops == 2);
        REQUIRE(host.source(0) != nullptr && host.source(0)->sampleRate == 48000 &&
                host.source(0)->monoSamples.size() == 4);
        REQUIRE(host.source(1) != nullptr && host.source(1)->sampleRate == 48000 &&
                host.source(1)->monoSamples.size() == 4);

        AtsTestPasteboard* atsDropPasteboard = [[AtsTestPasteboard alloc]
            initWithFileUrl:[NSURL fileURLWithPath:@"/tmp/ats-native-drop.ats"]];
        REQUIRE(performNativeDrop(atsDropPasteboard, sourceBPoint));
        REQUIRE(state.lastDropSlot == 1 && state.lastDropAccepted);
        REQUIRE(state.typedImportAtsActions == 1);

        NSEvent* mouse = [NSEvent mouseEventWithType:NSEventTypeLeftMouseDown
                                            location:windowPointForLogical(NSMakePoint(8.0, 300.0))
                                       modifierFlags:0 timestamp:0.0
                                        windowNumber:[hostWindow windowNumber]
                                             context:nil eventNumber:1 clickCount:1 pressure:1.0];
        [embeddedView mouseDown:mouse];
        NSEvent* mouseUp = [NSEvent mouseEventWithType:NSEventTypeLeftMouseUp
                                              location:windowPointForLogical(NSMakePoint(8.0, 300.0))
                                         modifierFlags:0 timestamp:0.0
                                          windowNumber:[hostWindow windowNumber]
                                               context:nil eventNumber:1 clickCount:1 pressure:0.0];
        [embeddedView mouseUp:mouseUp];
        NSEvent* key = [NSEvent keyEventWithType:NSEventTypeKeyDown
                                        location:windowPointForLogical(NSMakePoint(8.0, 300.0))
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

        // The page strip is centred in the body and capped at 640 (§10.3), so at
        // this editor size it runs from editor x 30 to 670 at y 78..108, split
        // into five 128px tabs. ANALYSIS is tab 1: editor x 158..286.
        const CGFloat pageX[] { 210.0, 240.0, 270.0 };
        const CGFloat pageY[] { 112.0, 120.0 };
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

        // analysisLowestFreq is control 3, which §10.5 puts on the left half of
        // the low+high freq pair in the left column. At this editor size that
        // resolves to editor-logical (30, 299, 131, 28). These are window
        // points, so the logical editor point scaled by the 1.25 host scale.
        const CGFloat parameterX[] { 60.0, 100.0, 140.0 };
        const CGFloat parameterY[] { 385.0, 391.0, 397.0 };
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

        // popup.show(this, {40, 40}) places the list at Visage-logical (40, 40).
        // Option 0 starts at Visage-logical y = 40 + SelectionPadding(4) = 44.
        // windowPointForLogical takes editor-logical coords, which are Visage-
        // logical × scale (1.25), so the first option row is at editor-logical
        // y = 44 × 1.25 = 55. Candidates below 55 fall above the popup list
        // and cause it to lose focus without incrementing popupDismissed.
        const CGFloat popupRowCandidates[] { 56.0, 60.0, 64.0, 68.0, 72.0 };
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
        std::error_code removeError;
        std::filesystem::remove(dropWavePath, removeError);
        REQUIRE([[hostWindow contentView] subviews].count == 0);
        [hostWindow close];
    }
    return 0;
}
