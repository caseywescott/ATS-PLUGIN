#include "../../host/HostAdapter.h"
#include "../../ui/VisageEditor.h"

#import <Cocoa/Cocoa.h>

#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>

namespace
{
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
    ats::ui::VisageEditor editor;
    if (!host.initialize(ats::host::HostFormat::standalone) ||
        !host.attachStandaloneEditor(editor, 1.0f, 1280.0f, 940.0f))
        return 2;
    drainEvents(application, 0.05);
    if (!host.editorAttached() || !host.editorVisible() || editor.nativeHandle() == nullptr)
        return 3;
    if (!host.setEditorLogicalSize(720.0f, 480.0f))
        return 4;
    if (!host.setEditorVisible(false) || host.editorVisible())
        return 5;
    if (!host.setEditorVisible(true) || !host.editorVisible())
        return 6;
    drainEvents(application, 0.05);
    host.detachEditor();
    if (host.editorAttached() || host.editorVisible() || editor.isShowing())
        return 7;
    std::cout << "ats_standalone_smoke: launch, resize, hide/show, and teardown ok\n";
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
        ats::ui::VisageEditor editor;
        if (!host.initialize(ats::host::HostFormat::standalone) ||
            !host.attachStandaloneEditor(editor, 1.0f, 1280.0f, 940.0f))
            return 2;
        [application run];
        host.detachEditor();
    }
    return 0;
}
