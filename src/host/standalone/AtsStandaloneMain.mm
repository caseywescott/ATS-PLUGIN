#include "../../host/HostAdapter.h"
#include "../../core/ParameterRegistry.h"
#include "../../ui/VisageEditor.h"

#import <Cocoa/Cocoa.h>

#include <chrono>
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
    const auto* parameter = std::get_if<ats::ui::SetParameterAction>(action);
    if (parameter == nullptr)
        return;
    const auto* descriptor = ats::ParameterRegistry::find(parameter->id);
    if (descriptor == nullptr || !descriptor->automatable || descriptor->readOnly)
        return;
    (void) host->applyParameterChange({ descriptor->denseIndex, parameter->value });
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
    sendMouse(NSEventTypeLeftMouseDown, NSMakePoint(220.0, 74.0), 1);
    sendMouse(NSEventTypeLeftMouseUp, NSMakePoint(220.0, 74.0), 1);
    sendMouse(NSEventTypeLeftMouseDown, NSMakePoint(160.0, 208.0), 2);
    sendMouse(NSEventTypeLeftMouseDragged, NSMakePoint(240.0, 208.0), 3);
    sendMouse(NSEventTypeLeftMouseUp, NSMakePoint(240.0, 208.0), 3);
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
}

int main(int argc, char** argv)
{
    @autoreleasepool
    {
        NSApplication* application = [NSApplication sharedApplication];
        [application finishLaunching];
        [application setActivationPolicy:NSApplicationActivationPolicyAccessory];
        [application activateIgnoringOtherApps:YES];

        const bool smoke = argc > 1 && std::strcmp(argv[1], "--smoke") == 0;
        if (smoke)
            return runSmoke(application);

        ats::host::HostAdapter host;
        ats::ui::EditorEventSink sink;
        sink.context = &host;
        sink.action = &forwardEditorAction;
        ats::ui::VisageEditor editor(sink);
        if (!host.initialize(ats::host::HostFormat::standalone) ||
            !host.attachStandaloneEditor(editor, 1.0f, 1280.0f, 940.0f))
            return 2;
        [application run];
        host.detachEditor();
    }
    return 0;
}
