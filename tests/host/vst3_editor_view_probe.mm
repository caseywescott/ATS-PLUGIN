#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"

#import <Cocoa/Cocoa.h>

#include <dlfcn.h>

#include <cstdlib>
#include <cstring>
#include <iostream>

namespace
{
using GetFactoryFunction = Steinberg::IPluginFactory* (*)();

[[noreturn]] void fail(const char* expression, int line)
{
    std::cerr << "VST3 editor view probe failed at line " << line << ": "
              << expression << '\n';
    std::abort();
}

#define REQUIRE(expression) ((expression) ? (void) 0 : fail(#expression, __LINE__))

void drainEvents(NSApplication* application)
{
    for (int i = 0; i < 64; ++i)
    {
        NSEvent* event = [application nextEventMatchingMask:NSEventMaskAny
                                                  untilDate:[NSDate dateWithTimeIntervalSinceNow:0.01]
                                                     inMode:NSDefaultRunLoopMode
                                                    dequeue:YES];
        if (event == nil)
            break;
        [application sendEvent:event];
    }
}
}

int main(int argc, char** argv)
{
    REQUIRE(argc == 2);

    @autoreleasepool
    {
        NSApplication* application = [NSApplication sharedApplication];
        [application finishLaunching];
        [application setActivationPolicy:NSApplicationActivationPolicyAccessory];

        NSWindow* hostWindow = [[NSWindow alloc]
            initWithContentRect:NSMakeRect(120.0, 120.0, 960.0, 720.0)
                      styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskResizable)
                        backing:NSBackingStoreBuffered
                          defer:NO];
        [hostWindow makeKeyAndOrderFront:nil];
        [application activateIgnoringOtherApps:YES];

        void* module = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
        REQUIRE(module != nullptr);
        const auto getFactory = reinterpret_cast<GetFactoryFunction>(
            dlsym(module, "GetPluginFactory"));
        REQUIRE(getFactory != nullptr);
        auto* factory = getFactory();
        REQUIRE(factory != nullptr);

        Steinberg::IPluginFactory2* factory2 = nullptr;
        REQUIRE(factory->queryInterface(Steinberg::IPluginFactory2::iid,
                                         reinterpret_cast<void**>(&factory2)) ==
                Steinberg::kResultTrue);

        Steinberg::PClassInfo2 controllerInfo {};
        Steinberg::TUID controllerUid {};
        for (Steinberg::int32 index = 0; index < factory->countClasses(); ++index)
        {
            Steinberg::PClassInfo2 info {};
            REQUIRE(factory2->getClassInfo2(index, &info) == Steinberg::kResultTrue);
            if (std::strstr(info.name, "Controller") != nullptr)
            {
                controllerInfo = info;
                std::memcpy(controllerUid, info.cid, sizeof(Steinberg::TUID));
            }
        }
        factory2->release();
        REQUIRE(controllerInfo.name[0] != '\0');

        Steinberg::Vst::IEditController* controller = nullptr;
        REQUIRE(factory->createInstance(controllerUid,
                                        Steinberg::Vst::IEditController::iid,
                                        reinterpret_cast<void**>(&controller)) ==
                Steinberg::kResultTrue);
        REQUIRE(controller->initialize(nullptr) == Steinberg::kResultTrue);
        auto* view = controller->createView(Steinberg::Vst::ViewType::kEditor);
        REQUIRE(view != nullptr);
        REQUIRE(view->isPlatformTypeSupported(Steinberg::kPlatformTypeNSView) ==
                Steinberg::kResultTrue);

        NSView* parent = [hostWindow contentView];
        REQUIRE(view->attached((__bridge void*)parent, Steinberg::kPlatformTypeNSView) ==
                Steinberg::kResultTrue);
        Steinberg::ViewRect initial {};
        REQUIRE(view->getSize(&initial) == Steinberg::kResultTrue);
        REQUIRE(initial.getWidth() == 1280 && initial.getHeight() == 940);
        REQUIRE([parent subviews].count == 1);

        Steinberg::ViewRect resized { 0, 0, 720, 480 };
        REQUIRE(view->onSize(&resized) == Steinberg::kResultTrue);
        Steinberg::ViewRect afterResize {};
        REQUIRE(view->getSize(&afterResize) == Steinberg::kResultTrue);
        REQUIRE(afterResize.getWidth() == 720 && afterResize.getHeight() == 480);
        REQUIRE(view->canResize() == Steinberg::kResultTrue);
        REQUIRE(view->onFocus(true) == Steinberg::kResultTrue);
        drainEvents(application);

        REQUIRE(view->removed() == Steinberg::kResultTrue);
        REQUIRE([parent subviews].count == 0);
        view->release();
        controller->terminate();
        controller->release();
        dlclose(module);
        [hostWindow orderOut:nil];
        [hostWindow close];

        std::cout << "ats_vst3_editor_view_probe: native NSView attach, focus, resize, and teardown ok\n";
    }
    return 0;
}
