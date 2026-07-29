#include "VisageNativeMac.h"

#import <Cocoa/Cocoa.h>

#include <string>

namespace ats::ui::detail
{

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
            ? @[ @"wav", @"wave", @"aiff", @"aif" ] : @[ @"ats" ];
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
        return {};
    NSURL* url = savePanel != nil ? savePanel.URL : openPanel.URL;
    if (url == nil || url.path == nil)
        return {};
    const char* utf8 = url.path.UTF8String;
    return { utf8 != nullptr, utf8 != nullptr ? std::string(utf8) : std::string() };
}

} // namespace ats::ui::detail
