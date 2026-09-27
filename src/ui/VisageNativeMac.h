#pragma once

#include "NativeFileDialog.h"

#include <string>

namespace ats::ui::detail
{

// ApplicationWindow delegates embedded visibility to the host NSWindow and
// therefore does not order an embedded child out when hide() is called.
// Keep that native detail behind the backend implementation seam.
void setNativeViewHidden(void* nativeHandle, bool hidden);
bool nativeViewIsHidden(void* nativeHandle);
void setNativeViewFrame(void* nativeHandle, float width, float height);
// Applies the editor's size limits to the NSWindow that owns a standalone
// editor view, so live window resizing honours min and max.
// ~/Library/Application Support/ATS Resynth/editor-preferences.txt, creating
// the folder if needed; empty when it cannot be resolved.
std::string defaultEditorPreferencesPath();
void setNativeWindowContentLimits(void* nativeHandle, float minWidth, float minHeight,
                                  float maxWidth, float maxHeight);

using NativeMagnifyCallback = void (*)(void* context, float x, float y,
                                        float scaleFactor);
void* installNativeMagnifyMonitor(void* nativeHandle, void* context,
                                  NativeMagnifyCallback callback,
                                  float logicalWidth, float logicalHeight);
void removeNativeMagnifyMonitor(void* monitor);
NativeFileDialogResult runNativeFileDialog(const NativeFileDialogRequest& request);
void setNativeFileDialogTestOverride(NativeFileDialogTestOverride override, void* context);
void clearNativeFileDialogTestOverride();

} // namespace ats::ui::detail
