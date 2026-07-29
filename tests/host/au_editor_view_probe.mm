#include <AudioToolbox/AudioToolbox.h>
#include <AudioUnit/AUCocoaUIView.h>
#include <CoreFoundation/CoreFoundation.h>

#import <Cocoa/Cocoa.h>

#include <dlfcn.h>

#include <cstdlib>
#include <iostream>

namespace
{
using FactoryFunction = AudioComponentFactoryFunction;
using GetPropertyInfoFn = OSStatus (*)(void*, AudioUnitPropertyID, AudioUnitScope,
                                       AudioUnitElement, UInt32*, Boolean*);
using GetPropertyFn = OSStatus (*)(void*, AudioUnitPropertyID, AudioUnitScope,
                                   AudioUnitElement, void*, UInt32*);
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
        REQUIRE(getPropertyInfo != nullptr && getProperty != nullptr);

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

        [editorView setFrameSize:NSMakeSize(720.0, 480.0)];
        REQUIRE(NSEqualSizes(editorView.frame.size, NSMakeSize(720.0, 480.0)));
        drainEvents(application);

        [editorView removeFromSuperview];
        REQUIRE([parent subviews].count == 0);

        CFRelease(cocoaInfo.mCocoaAUViewClass[0]);
        CFRelease(cocoaInfo.mCocoaAUViewBundleLocation);

        auto uninitialize = reinterpret_cast<UnitFn>(
            plugin->Lookup(kAudioUnitUninitializeSelect));
        if (uninitialize != nullptr)
            (void)uninitialize(plugin);
        REQUIRE(plugin->Close(plugin) == noErr);
        dlclose(module);

        [hostWindow close];
        drainEvents(application);
    }

    std::cout << "ats_au_editor_view_probe: CocoaUI advertise, NSView attach, focus, resize, and teardown ok\n";
    return 0;
}
