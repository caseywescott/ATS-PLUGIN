#pragma once

#include "NativeFileDialog.h"

namespace ats::ui::detail
{

// ApplicationWindow delegates embedded visibility to the host NSWindow and
// therefore does not order an embedded child out when hide() is called.
// Keep that native detail behind the backend implementation seam.
void setNativeViewHidden(void* nativeHandle, bool hidden);
bool nativeViewIsHidden(void* nativeHandle);
void setNativeViewFrame(void* nativeHandle, float width, float height);

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
