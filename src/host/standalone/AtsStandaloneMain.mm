#include "../../host/HostAdapter.h"
#include "../../core/ParameterRegistry.h"
#include "../../state/FactoryPresetState.h"
#include "../../state/ParameterPresetCodec.h"
#include "../../platform/FileService.h"
#include "../../ui/VisageEditor.h"

#import <Cocoa/Cocoa.h>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <thread>
#include <variant>

namespace
{
void forwardEditorAction(void* context, const ats::ui::EditorAction* action)
{
    auto* host = static_cast<ats::host::HostAdapter*>(context);
    if (host == nullptr || action == nullptr)
        return;
    ats::Error error;
    (void) host->applyEditorAction(*action, error);
}

void drainEvents(NSApplication* application, double seconds)
{
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::duration<double>(seconds);
    while (std::chrono::steady_clock::now() < deadline)
    {
        NSEvent* event = [application nextEventMatchingMask:NSEventMaskAny
                                                  untilDate:[NSDate dateWithTimeIntervalSinceNow:0.01]
                                                     inMode:NSDefaultRunLoopMode
                                                    dequeue:YES];
        if (event != nil)
            [application sendEvent:event];
        else
            std::this_thread::yield();
    }
}

int runSmoke(NSApplication* application)
{
    ats::host::HostAdapter host;
    ats::ui::EditorEventSink sink;
    sink.context = &host;
    sink.action = &forwardEditorAction;
    ats::ui::VisageEditor editor(sink);
    if (!host.initialize(ats::host::HostFormat::standalone) ||
        !host.attachStandaloneEditor(editor, 1.0f, 1280.0f, 940.0f))
        return 2;
    drainEvents(application, 0.05);
    if (!host.editorAttached() || !host.editorVisible() || editor.nativeHandle() == nullptr)
        return 3;
    if (!host.setEditorLogicalSize(720.0f, 480.0f))
        return 4;

    // Drive the actual standalone native view: select Analysis then drag the
    // Analysis Low Freq slider. The backend action must reach HostAdapter.
    NSView* nativeView = (__bridge NSView*)editor.nativeHandle();
    if (nativeView == nil || nativeView.window == nil ||
        ![nativeView.window makeFirstResponder:nativeView])
        return 8;
    auto sendMouse = [&](NSEventType type, NSPoint logicalPoint, int eventNumber) {
        const NSPoint viewPoint = NSMakePoint(logicalPoint.x,
                                              nativeView.bounds.size.height - logicalPoint.y);
        const NSPoint windowPoint = [nativeView convertPoint:viewPoint toView:nil];
        NSEvent* event = [NSEvent mouseEventWithType:type
                                            location:windowPoint
                                       modifierFlags:0 timestamp:0.0
                                            windowNumber:nativeView.window.windowNumber
                                                 context:nil eventNumber:eventNumber
                                              clickCount:1
                                                pressure:type == NSEventTypeLeftMouseUp ? 0.0 : 1.0];
        if (type == NSEventTypeLeftMouseDragged)
            [nativeView mouseDragged:event];
        else if (type == NSEventTypeLeftMouseUp)
            [nativeView mouseUp:event];
        else
            [nativeView mouseDown:event];
    };
    const auto* descriptor = ats::ParameterRegistry::find("analysisLowestFreq");
    if (descriptor == nullptr)
        return 9;
    const auto initialParameter = host.parameters().read(descriptor->denseIndex);
    // At the 720x480 logical size the §10.3 page strip runs from editor x 40 to
    // 680 at y 78..108 in five 128px tabs, so ANALYSIS is x 168..296. §10.5 then
    // puts analysisLowestFreq on the left half of the low+high freq pair, which
    // resolves to editor-logical (30, 299, 131, 28).
    sendMouse(NSEventTypeLeftMouseDown, NSMakePoint(220.0, 90.0), 1);
    sendMouse(NSEventTypeLeftMouseUp, NSMakePoint(220.0, 90.0), 1);
    sendMouse(NSEventTypeLeftMouseDown, NSMakePoint(60.0, 313.0), 2);
    sendMouse(NSEventTypeLeftMouseDragged, NSMakePoint(140.0, 313.0), 3);
    sendMouse(NSEventTypeLeftMouseUp, NSMakePoint(140.0, 313.0), 3);
    const auto changedParameter = host.parameters().read(descriptor->denseIndex);
    if (changedParameter == initialParameter || changedParameter < descriptor->minimum ||
        changedParameter > descriptor->maximum)
        return 10;
    if (!host.setEditorVisible(false) || host.editorVisible())
        return 5;
    if (!host.setEditorVisible(true) || !host.editorVisible())
        return 6;
    drainEvents(application, 0.05);
    host.detachEditor();
    if (host.editorAttached() || host.editorVisible() || editor.isShowing())
        return 7;
    std::cout << "ats_standalone_smoke: launch, resize, native parameter gesture, hide/show, "
                 "and teardown ok\n";
    return 0;
}

int runDropSmoke(NSApplication* application, const char* sourcePath)
{
    ats::host::HostAdapter host;
    ats::ui::EditorEventSink sink;
    sink.context = &host;
    sink.action = &forwardEditorAction;
    ats::ui::VisageEditor editor(sink);
    if (!host.initialize(ats::host::HostFormat::standalone) ||
        !host.attachStandaloneEditor(editor, 1.0f, 1280.0f, 940.0f))
        return 20;

    ats::Error error;
    if (!host.applyEditorAction(ats::ui::ImportWavAction { sourcePath, 0 }, error) || error)
        return 21;
    if (!host.applyEditorAction(ats::ui::AnalyzeMorphSourceAction { 0 }, error) || error)
        return 22;

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(3);
    bool completed = false;
    while (std::chrono::steady_clock::now() < deadline)
    {
        completed = host.pollAnalysis(error);
        editor.setEditorSnapshot(host.editorSnapshot());
        drainEvents(application, 0.02);
        if (completed)
            break;
    }
    if (!completed || error)
        return 23;

    const auto snapshot = host.editorSnapshot();
    editor.setEditorSnapshot(snapshot);
    if (!snapshot.spectrum || snapshot.spectrum->partials.empty())
        return 24;
    std::cout << "ats_standalone_drop_smoke: rendered "
              << snapshot.spectrum->partials.size() << " partials x "
              << snapshot.spectrum->frameCount << " frames\n" << std::flush;
    // Keep the completed frame visible long enough for the macOS smoke driver
    // to capture and inspect the native Metal window.
    drainEvents(application, 2.0);
    host.detachEditor();
    return 0;
}
}

int main(int argc, char** argv)
{
    @autoreleasepool
    {
        NSApplication* application = [NSApplication sharedApplication];
        [application setActivationPolicy:NSApplicationActivationPolicyAccessory];

        const bool smoke = argc > 1 && std::strcmp(argv[1], "--smoke") == 0;
        if (smoke)
        {
            // The direct-dispatch smoke does not enter Visage's event loop, so
            // it must finish AppKit setup itself.
            [application finishLaunching];
            [application activateIgnoringOtherApps:YES];
            return runSmoke(application);
        }

        const bool dropSmoke = argc > 2 && std::strcmp(argv[1], "--drop-smoke") == 0;
        if (dropSmoke)
        {
            [application finishLaunching];
            [application activateIgnoringOtherApps:YES];
            return runDropSmoke(application, argv[2]);
        }

        const bool closeSmoke = argc > 1 && std::strcmp(argv[1], "--close-smoke") == 0;

        ats::host::HostAdapter host;
        ats::ui::EditorEventSink sink;
        sink.context = &host;
        sink.action = &forwardEditorAction;
        ats::ui::VisageEditor editor(sink);
        if (!host.initialize(ats::host::HostFormat::standalone) ||
            !host.attachStandaloneEditor(editor, 1.0f, 1280.0f, 940.0f))
            return 2;
        auto* hostAdapter = &host;
        auto* editorView = &editor;
        NSTimer* analysisTimer = [NSTimer scheduledTimerWithTimeInterval:0.02 repeats:YES block:^(NSTimer*) {
            ats::Error error;
            (void) hostAdapter->pollAnalysis(error);
            editorView->setEditorSnapshot(hostAdapter->editorSnapshot());
        }];
        NSTimer* closeTimer = nil;
        if (closeSmoke)
        {
            // Exercise the same NSWindowDelegate path as the title-bar red
            // button after Visage has installed its application delegate.
            closeTimer = [NSTimer scheduledTimerWithTimeInterval:0.10 repeats:NO block:^(NSTimer*) {
                NSView* nativeView = (__bridge NSView*) editorView->nativeHandle();
                [nativeView.window performClose:nil];
            }];
        }
        editor.runStandaloneEventLoop();
        [closeTimer invalidate];
        [analysisTimer invalidate];
        // Visage's application delegate retains the native window delegate.
        // Release that chain while the editor/WindowMac still exists; leaving
        // it attached until the outer autorelease pool drains can make AppKit
        // message a delegate whose C++ window has already been destroyed.
        [application setDelegate:nil];
        const bool closed = !editor.isShowing();
        host.detachEditor();
        const int exitCode = closeSmoke && !closed ? 3 : 0;
        // This is the terminal standalone path: worker polling is stopped and
        // the host/editor are detached above. Visage leaves an AppKit object in
        // the outer autorelease pool after its last native window closes, so
        // returning through that pool can crash after an otherwise clean exit.
        // Exit directly once all owned work has been shut down.
        std::_Exit(exitCode);
    }
    return 0;
}
