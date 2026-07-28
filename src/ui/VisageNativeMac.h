#pragma once

#include "NativeFileDialog.h"

namespace ats::ui::detail
{

// ApplicationWindow delegates embedded visibility to the host NSWindow and
// therefore does not order an embedded child out when hide() is called.
// Keep that native detail behind the backend implementation seam.
void setNativeViewHidden(void* nativeHandle, bool hidden);
bool nativeViewIsHidden(void* nativeHandle);
NativeFileDialogResult runNativeFileDialog(const NativeFileDialogRequest& request);

} // namespace ats::ui::detail
