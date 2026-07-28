#include "VisageNativeMac.h"

#import <Cocoa/Cocoa.h>

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

} // namespace ats::ui::detail
