#include "pluginterfaces/base/ipluginbase.h"
#include "base/source/fobject.h"
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

class TestComponentHandler final : public Steinberg::FObject,
                                   public Steinberg::Vst::IComponentHandler
{
public:
    OBJ_METHODS(TestComponentHandler, Steinberg::FObject)
    DEFINE_INTERFACES
        DEF_INTERFACE(Steinberg::Vst::IComponentHandler)
    END_DEFINE_INTERFACES(Steinberg::FObject)
    REFCOUNT_METHODS(Steinberg::FObject)

    Steinberg::tresult PLUGIN_API beginEdit(Steinberg::Vst::ParamID id) override
    {
        ++beginCount;
        beginId = id;
        return Steinberg::kResultTrue;
    }

    Steinberg::tresult PLUGIN_API performEdit(Steinberg::Vst::ParamID id,
                                              Steinberg::Vst::ParamValue value) override
    {
        ++performCount;
        performId = id;
        lastValue = value;
        return Steinberg::kResultTrue;
    }

    Steinberg::tresult PLUGIN_API endEdit(Steinberg::Vst::ParamID id) override
    {
        ++endCount;
        endId = id;
        return Steinberg::kResultTrue;
    }

    Steinberg::tresult PLUGIN_API restartComponent(Steinberg::int32) override
    {
        return Steinberg::kResultTrue;
    }

    int beginCount = 0;
    int performCount = 0;
    int endCount = 0;
    Steinberg::Vst::ParamID beginId = Steinberg::Vst::kNoParamId;
    Steinberg::Vst::ParamID performId = Steinberg::Vst::kNoParamId;
    Steinberg::Vst::ParamID endId = Steinberg::Vst::kNoParamId;
    Steinberg::Vst::ParamValue lastValue = -1.0;
};
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
        TestComponentHandler componentHandler;
        REQUIRE(controller->setComponentHandler(&componentHandler) == Steinberg::kResultTrue);
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

        // This is a live editor-to-host probe: the native NSView receives a
        // real mouse gesture, Visage emits an editor action, and the VST3
        // controller forwards it through IComponentHandler automation calls.
        NSView* embeddedView = [parent subviews].firstObject;
        REQUIRE(embeddedView != nil);
        REQUIRE([hostWindow makeFirstResponder:embeddedView]);
        auto sendEmbeddedMouse = [&](NSEventType type, NSPoint logicalPoint, int eventNumber) {
            const NSPoint viewPoint = NSMakePoint(logicalPoint.x,
                                                  embeddedView.bounds.size.height - logicalPoint.y);
            const NSPoint windowPoint = [embeddedView convertPoint:viewPoint toView:nil];
            NSEvent* event = [NSEvent mouseEventWithType:type
                                                location:windowPoint
                                           modifierFlags:0 timestamp:0.0
                                                windowNumber:[hostWindow windowNumber]
                                                     context:nil eventNumber:eventNumber
                                                  clickCount:1
                                                    pressure:type == NSEventTypeLeftMouseUp ? 0.0 : 1.0];
            if (type == NSEventTypeLeftMouseDragged)
                [embeddedView mouseDragged:event];
            else if (type == NSEventTypeLeftMouseUp)
                [embeddedView mouseUp:event];
            else
                [embeddedView mouseDown:event];
        };

        // Prime the focused embedded view in the same way a host delivers its
        // first input sequence before routed controls receive a drag.
        sendEmbeddedMouse(NSEventTypeLeftMouseDown, NSMakePoint(600.0, 120.0), 1);
        sendEmbeddedMouse(NSEventTypeLeftMouseUp, NSMakePoint(600.0, 120.0), 1);
        NSEvent* key = [NSEvent keyEventWithType:NSEventTypeKeyDown
                                        location:[embeddedView convertPoint:NSMakePoint(600.0,
                                                   embeddedView.bounds.size.height - 120.0)
                                                               toView:nil]
                                   modifierFlags:0 timestamp:0.0
                                    windowNumber:[hostWindow windowNumber]
                                         context:nil characters:@"x"
                     charactersIgnoringModifiers:@"x" isARepeat:NO keyCode:7];
        [embeddedView keyDown:key];
        drainEvents(application);

        // Select the Analysis page, then drag its Analysis Low Freq slider.
        // The small candidate bands accommodate the backing-view conversion
        // used by AppKit after VST3 logical resize negotiation.
        const CGFloat pageX[] { 220.0, 240.0, 260.0 };
        const CGFloat pageY[] { 74.0, 90.0 };
        for (const auto x : pageX)
        {
            for (const auto y : pageY)
            {
                sendEmbeddedMouse(NSEventTypeLeftMouseDown, NSMakePoint(x, y), 8);
                sendEmbeddedMouse(NSEventTypeLeftMouseUp, NSMakePoint(x, y), 8);
            }
        }

        const CGFloat parameterX[] { 160.0, 220.0, 300.0 };
        const CGFloat parameterY[] { 200.0, 208.0, 216.0 };
        for (const auto x : parameterX)
        {
            for (const auto y : parameterY)
            {
                if (componentHandler.beginCount != 0)
                    break;
                sendEmbeddedMouse(NSEventTypeLeftMouseDown, NSMakePoint(x, y), 9);
                sendEmbeddedMouse(NSEventTypeLeftMouseDragged, NSMakePoint(x + 80.0, y), 10);
                sendEmbeddedMouse(NSEventTypeLeftMouseUp, NSMakePoint(x + 80.0, y), 10);
            }
            if (componentHandler.beginCount != 0)
                break;
        }
        REQUIRE(componentHandler.beginCount == 1);
        REQUIRE(componentHandler.performCount >= 2);
        REQUIRE(componentHandler.endCount == 1);
        REQUIRE(componentHandler.beginId == componentHandler.performId);
        REQUIRE(componentHandler.beginId == componentHandler.endId);
        REQUIRE(componentHandler.lastValue >= 0.0 && componentHandler.lastValue <= 1.0);

        REQUIRE(view->removed() == Steinberg::kResultTrue);
        REQUIRE([parent subviews].count == 0);
        view->release();
        REQUIRE(controller->setComponentHandler(nullptr) == Steinberg::kResultTrue);
        controller->terminate();
        controller->release();
        dlclose(module);
        [hostWindow orderOut:nil];
        [hostWindow close];

        std::cout << "ats_vst3_editor_view_probe: native NSView attach, focus, resize, "
                     "parameter gesture, VST3 automation, and teardown ok\n";
    }
    return 0;
}
