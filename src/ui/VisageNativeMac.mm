#include "VisageNativeMac.h"

#import <Cocoa/Cocoa.h>

#include <algorithm>
#include <string>

namespace ats::ui::detail
{

namespace
{
NativeFileDialogTestOverride testOverride = nullptr;
void* testOverrideContext = nullptr;

struct MagnifyMonitor
{
    id token = nil;
};
}

void setNativeViewHidden(void* nativeHandle, bool hidden)
{
    NSView* view = (__bridge NSView*)nativeHandle;
    if (view != nil)
        [view setHidden:hidden];
}

bool nativeViewIsHidden(void* nativeHandle)
{
    NSView* view = (__bridge NSView*)nativeHandle;
    return view == nil || [view isHidden];
}

void setNativeViewFrame(void* nativeHandle, float width, float height)
{
    NSView* view = (__bridge NSView*)nativeHandle;
    if (view != nil && width > 0.0f && height > 0.0f)
        [view setFrame:NSMakeRect(0.0, 0.0, width, height)];
}

std::string defaultEditorPreferencesPath()
{
    NSArray<NSString*>* roots = NSSearchPathForDirectoriesInDomains(
        NSApplicationSupportDirectory, NSUserDomainMask, YES);
    if (roots.count == 0)
        return {};
    NSString* folder = [roots.firstObject stringByAppendingPathComponent:@"ATS Resynth"];
    if (![[NSFileManager defaultManager] createDirectoryAtPath:folder
                                   withIntermediateDirectories:YES
                                                    attributes:nil
                                                         error:nil])
        return {};
    return [[folder stringByAppendingPathComponent:@"editor-preferences.txt"] UTF8String];
}

void setNativeWindowContentLimits(void* nativeHandle, float minWidth, float minHeight,
                                  float maxWidth, float maxHeight)
{
    NSView* view = (__bridge NSView*)nativeHandle;
    NSWindow* window = view != nil ? view.window : nil;
    if (window == nil)
        return;
    window.contentMinSize = NSMakeSize(minWidth, minHeight);
    window.contentMaxSize = NSMakeSize(maxWidth, maxHeight);
}

void* installNativeMagnifyMonitor(void* nativeHandle, void* context,
                                  NativeMagnifyCallback callback,
                                  float logicalWidth, float logicalHeight)
{
    NSView* view = (__bridge NSView*)nativeHandle;
    if (view == nil || context == nullptr || callback == nullptr
        || logicalWidth <= 0.0f || logicalHeight <= 0.0f)
        return nullptr;

    auto* monitor = new MagnifyMonitor;
    monitor->token = [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskMagnify
                                                              handler:^NSEvent*(NSEvent* event) {
        if (event.window == nil || view.window == nil || event.window != view.window)
            return event;

        const auto location = [view convertPoint:event.locationInWindow fromView:nil];
        const auto viewWidth = std::max<CGFloat>(1.0, view.bounds.size.width);
        const auto viewHeight = std::max<CGFloat>(1.0, view.bounds.size.height);
        const auto x = static_cast<float>(location.x / viewWidth * logicalWidth);
        const auto y = static_cast<float>((viewHeight - location.y) / viewHeight * logicalHeight);
        const auto scaleFactor = static_cast<float>(1.0 + event.magnification);
        callback(context, x, y, scaleFactor);
        return event;
    }];
    return monitor;
}

void removeNativeMagnifyMonitor(void* opaqueMonitor)
{
    auto* monitor = static_cast<MagnifyMonitor*>(opaqueMonitor);
    if (monitor == nullptr)
        return;
    if (monitor->token != nil)
        [NSEvent removeMonitor:monitor->token];
    delete monitor;
}

NativeFileDialogResult runNativeFileDialog(const NativeFileDialogRequest& request)
{
    if (testOverride != nullptr)
        return testOverride(request, testOverrideContext);

    NSView* parent = (__bridge NSView*)request.parentView;
    NSWindow* window = parent != nil ? [parent window] : nil;
    NSSavePanel* savePanel = nil;
    NSOpenPanel* openPanel = nil;
    const auto kind = request.kind;
    const bool save = kind == NativeFileDialogKind::exportAts ||
                      kind == NativeFileDialogKind::savePreset ||
                      kind == NativeFileDialogKind::bounceWav;
    if (save)
        savePanel = [NSSavePanel savePanel];
    else
        openPanel = [NSOpenPanel openPanel];

    if (savePanel != nil)
    {
        savePanel.canCreateDirectories = YES;
        savePanel.canSelectHiddenExtension = YES;
        savePanel.nameFieldStringValue = [NSString stringWithUTF8String:request.suggestedName.c_str()];
        savePanel.allowedFileTypes = kind == NativeFileDialogKind::savePreset ? @[ @"atspreset" ]
            : kind == NativeFileDialogKind::bounceWav ? @[ @"wav" ] : @[ @"ats" ];
    }
    if (openPanel != nil)
    {
        openPanel.allowsMultipleSelection = NO;
        openPanel.canChooseDirectories = NO;
        openPanel.canChooseFiles = YES;
        openPanel.allowedFileTypes = kind == NativeFileDialogKind::importWav
            ? @[ @"wav", @"wave", @"aiff", @"aif" ]
            : kind == NativeFileDialogKind::loadPreset ? @[ @"atspreset" ] : @[ @"ats" ];
    }

    NSModalResponse response = NSModalResponseCancel;
    if (window != nil)
    {
        if (savePanel != nil)
            response = [savePanel runModal];
        else
            response = [openPanel runModal];
    }
    else if (savePanel != nil)
        response = [savePanel runModal];
    else
        response = [openPanel runModal];

    if (response != NSModalResponseOK)
        return { false, {}, NativeFileDialogStatus::cancelled, {} };
    NSURL* url = savePanel != nil ? savePanel.URL : openPanel.URL;
    if (url == nil || url.path == nil)
        return { false, {}, NativeFileDialogStatus::initializationFailed,
                 "native dialog returned no URL" };
    const char* utf8 = url.path.UTF8String;
    if (utf8 == nullptr || *utf8 == '\0')
        return { false, {}, NativeFileDialogStatus::initializationFailed,
                 "native dialog returned no UTF-8 path" };
    return { true, std::string(utf8), NativeFileDialogStatus::accepted, {} };
}

void setNativeFileDialogTestOverride(NativeFileDialogTestOverride override, void* context)
{
    testOverride = override;
    testOverrideContext = context;
}

void clearNativeFileDialogTestOverride()
{
    testOverride = nullptr;
    testOverrideContext = nullptr;
}

} // namespace ats::ui::detail
