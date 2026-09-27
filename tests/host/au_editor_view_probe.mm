#include <AudioToolbox/AudioToolbox.h>
#include <AudioUnit/AUCocoaUIView.h>
#include <CoreFoundation/CoreFoundation.h>

#import <Cocoa/Cocoa.h>

#include <dlfcn.h>

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

namespace
{
using FactoryFunction = AudioComponentFactoryFunction;
using GetPropertyInfoFn = OSStatus (*)(void*, AudioUnitPropertyID, AudioUnitScope,
                                       AudioUnitElement, UInt32*, Boolean*);
using GetPropertyFn = OSStatus (*)(void*, AudioUnitPropertyID, AudioUnitScope,
                                   AudioUnitElement, void*, UInt32*);
using GetParameterFn = OSStatus (*)(void*, AudioUnitParameterID, AudioUnitScope,
                                    AudioUnitElement, AudioUnitParameterValue*);
using UnitFn = OSStatus (*)(void*);

[[noreturn]] void fail(const char* expression, int line)
{
    std::cerr << "AU editor view probe failed at line " << line << ": "
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
        auto factory = reinterpret_cast<FactoryFunction>(dlsym(module, "AtsAudioUnitFactory"));
        REQUIRE(factory != nullptr);

        const AudioComponentDescription description {
            kAudioUnitType_Effect, 'Atr1', 'AtPl', 0, 0
        };
        auto* plugin = factory(&description);
        REQUIRE(plugin != nullptr);
        REQUIRE(plugin->Open(plugin, reinterpret_cast<AudioComponentInstance>(plugin)) == noErr);

        auto getPropertyInfo = reinterpret_cast<GetPropertyInfoFn>(
            plugin->Lookup(kAudioUnitGetPropertyInfoSelect));
        auto getProperty = reinterpret_cast<GetPropertyFn>(
            plugin->Lookup(kAudioUnitGetPropertySelect));
        auto getParameter = reinterpret_cast<GetParameterFn>(
            plugin->Lookup(kAudioUnitGetParameterSelect));
        REQUIRE(getPropertyInfo != nullptr && getProperty != nullptr && getParameter != nullptr);

        UInt32 parameterListSize = 0;
        REQUIRE(getPropertyInfo(plugin, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global,
                                0, &parameterListSize, nullptr) == noErr);
        REQUIRE(parameterListSize > 0 &&
                parameterListSize % sizeof(AudioUnitParameterID) == 0);
        std::vector<AudioUnitParameterID> parameterIds(
            parameterListSize / sizeof(AudioUnitParameterID));
        REQUIRE(getProperty(plugin, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0,
                            parameterIds.data(), &parameterListSize) == noErr);
        AudioUnitParameterID analysisLowestFrequency = 0;
        bool foundAnalysisLowestFrequency = false;
        for (const auto id : parameterIds)
        {
            AudioUnitParameterInfo info {};
            UInt32 infoSize = sizeof(info);
            REQUIRE(getProperty(plugin, kAudioUnitProperty_ParameterInfo, kAudioUnitScope_Global,
                                id, &info, &infoSize) == noErr);
            if (std::strcmp(info.name, "Lowest frequency (Hz)") == 0)
            {
                analysisLowestFrequency = id;
                foundAnalysisLowestFrequency = true;
            }
        }
        REQUIRE(foundAnalysisLowestFrequency);
        AudioUnitParameterValue initialAnalysisLowestFrequency = 0.0f;
        REQUIRE(getParameter(plugin, analysisLowestFrequency, kAudioUnitScope_Global, 0,
                             &initialAnalysisLowestFrequency) == noErr);

        UInt32 size = 0;
        REQUIRE(getPropertyInfo(plugin, kAudioUnitProperty_CocoaUI, kAudioUnitScope_Global, 0,
                                &size, nullptr) == noErr);
        REQUIRE(size >= sizeof(AudioUnitCocoaViewInfo));

        AudioUnitCocoaViewInfo cocoaInfo {};
        size = sizeof(cocoaInfo);
        REQUIRE(getProperty(plugin, kAudioUnitProperty_CocoaUI, kAudioUnitScope_Global, 0,
                            &cocoaInfo, &size) == noErr);
        REQUIRE(cocoaInfo.mCocoaAUViewBundleLocation != nullptr);
        REQUIRE(cocoaInfo.mCocoaAUViewClass[0] != nullptr);

        NSURL* bundleUrl = (__bridge NSURL*)cocoaInfo.mCocoaAUViewBundleLocation;
        NSString* className = (__bridge NSString*)cocoaInfo.mCocoaAUViewClass[0];
        NSBundle* viewBundle = [NSBundle bundleWithURL:bundleUrl];
        REQUIRE(viewBundle != nil);
        REQUIRE([viewBundle load]);
        Class factoryClass = [viewBundle classNamed:className];
        REQUIRE(factoryClass != nil);
        REQUIRE([factoryClass conformsToProtocol:@protocol(AUCocoaUIBase)]);

        id<AUCocoaUIBase> viewFactory = [[factoryClass alloc] init];
        REQUIRE(viewFactory != nil);
        NSView* editorView =
            [viewFactory uiViewForAudioUnit:reinterpret_cast<AudioUnit>(plugin)
                                   withSize:NSMakeSize(1280.0, 940.0)];
        REQUIRE(editorView != nil);
        REQUIRE(NSEqualSizes(editorView.frame.size, NSMakeSize(1280.0, 940.0)));

        NSView* parent = [hostWindow contentView];
        [parent addSubview:editorView];
        REQUIRE([parent subviews].count == 1);
        REQUIRE([hostWindow makeFirstResponder:editorView]);
        drainEvents(application);

        // Host-driven resizes are negotiated against EditorSizeLimits: within
        // range they are taken as-is, outside they clamp to min/max.
        [editorView setFrameSize:NSMakeSize(1500.0, 1000.0)];
        REQUIRE(NSEqualSizes(editorView.frame.size, NSMakeSize(1500.0, 1000.0)));
        [editorView setFrameSize:NSMakeSize(2500.0, 2000.0)];
        REQUIRE(NSEqualSizes(editorView.frame.size, NSMakeSize(1900.0, 1400.0)));
        [editorView setFrameSize:NSMakeSize(720.0, 480.0)];
        REQUIRE(NSEqualSizes(editorView.frame.size, NSMakeSize(1100.0, 860.0)));
        drainEvents(application);

        // Exercise the real embedded Visage view, then verify that the AU
        // instance received the descriptor-domain parameter change.
        NSView* embeddedView = editorView.subviews.firstObject;
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
        // The §10.3 page strip is centred in the body and capped at 640, so at
        // the clamped 1100x860 minimum it runs from editor x 230 to 870 at
        // y 78..108 in five 128px tabs. ANALYSIS is tab 1: editor x 358..486.
        const CGFloat pageX[] { 410.0, 430.0, 450.0 };
        const CGFloat pageY[] { 90.0, 96.0 };
        for (const auto x : pageX)
            for (const auto y : pageY)
            {
                sendEmbeddedMouse(NSEventTypeLeftMouseDown, NSMakePoint(x, y), 8);
                sendEmbeddedMouse(NSEventTypeLeftMouseUp, NSMakePoint(x, y), 8);
            }
        // analysisLowestFrequency is control 3, which §10.5 puts on the left half
        // of the low+high freq pair in the left column: editor (30, 299, 131, 28).
        const CGFloat parameterX[] { 40.0, 60.0, 80.0 };
        const CGFloat parameterY[] { 305.0, 313.0, 321.0 };
        for (const auto x : parameterX)
        {
            for (const auto y : parameterY)
            {
                sendEmbeddedMouse(NSEventTypeLeftMouseDown, NSMakePoint(x, y), 9);
                sendEmbeddedMouse(NSEventTypeLeftMouseDragged, NSMakePoint(x + 80.0, y), 10);
                sendEmbeddedMouse(NSEventTypeLeftMouseUp, NSMakePoint(x + 80.0, y), 10);
                AudioUnitParameterValue value = 0.0f;
                REQUIRE(getParameter(plugin, analysisLowestFrequency, kAudioUnitScope_Global, 0,
                                     &value) == noErr);
                if (value != initialAnalysisLowestFrequency)
                    break;
            }
            AudioUnitParameterValue value = 0.0f;
            REQUIRE(getParameter(plugin, analysisLowestFrequency, kAudioUnitScope_Global, 0,
                                 &value) == noErr);
            if (value != initialAnalysisLowestFrequency)
                break;
        }
        AudioUnitParameterValue changedAnalysisLowestFrequency = 0.0f;
        REQUIRE(getParameter(plugin, analysisLowestFrequency, kAudioUnitScope_Global, 0,
                             &changedAnalysisLowestFrequency) == noErr);
        REQUIRE(changedAnalysisLowestFrequency >= 1.0f &&
                changedAnalysisLowestFrequency <= 2000.0f);
        REQUIRE(changedAnalysisLowestFrequency != initialAnalysisLowestFrequency);

        // A host may release the processor while the Cocoa view is still
        // alive. Keep the view/timer installed across Close(), drain the UI
        // run loop, then detach it. The lifetime bridge must suppress both
        // editor actions and queued-analysis polling after the target dies.
        auto uninitialize = reinterpret_cast<UnitFn>(
            plugin->Lookup(kAudioUnitUninitializeSelect));
        if (uninitialize != nullptr)
            (void)uninitialize(plugin);
        REQUIRE(plugin->Close(plugin) == noErr);
        drainEvents(application);

        [editorView removeFromSuperview];
        REQUIRE([parent subviews].count == 0);

        CFRelease(cocoaInfo.mCocoaAUViewClass[0]);
        CFRelease(cocoaInfo.mCocoaAUViewBundleLocation);
        dlclose(module);

        [hostWindow close];
        drainEvents(application);
    }

    std::cout << "ats_au_editor_view_probe: CocoaUI advertise, NSView attach, focus, resize, "
                 "parameter gesture, and teardown ok\n";
    return 0;
}
