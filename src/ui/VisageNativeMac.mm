#include "VisageNativeMac.h"

#import <Cocoa/Cocoa.h>

#include <string>

namespace ats::ui::detail
{

namespace
{
NativeFileDialogTestOverride testOverride = nullptr;
void* testOverrideContext = nullptr;
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
                      kind == NativeFileDialogKind::savePreset;
    if (save)
        savePanel = [NSSavePanel savePanel];
    else
        openPanel = [NSOpenPanel openPanel];

    if (savePanel != nil)
    {
        savePanel.canCreateDirectories = YES;
        savePanel.canSelectHiddenExtension = YES;
        savePanel.nameFieldStringValue = [NSString stringWithUTF8String:request.suggestedName.c_str()];
        savePanel.allowedFileTypes = kind == NativeFileDialogKind::savePreset
            ? @[ @"atspreset" ] : @[ @"ats" ];
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
