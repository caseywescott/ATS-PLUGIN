#pragma once

#include <cstdint>
#include <string>

namespace ats::ui
{

enum class NativeFileDialogKind : int
{
    importWav = 0,
    importAts = 1,
    exportAts = 2,
    savePreset = 3
    , loadPreset = 4
};

enum class NativeFileDialogStatus : std::uint8_t
{
    unspecified,
    accepted,
    cancelled,
    initializationFailed,
    failed
};

struct NativeFileDialogRequest
{
    NativeFileDialogKind kind = NativeFileDialogKind::importWav;
    void* parentView = nullptr;
    std::string suggestedName;
};

struct NativeFileDialogResult
{
    bool accepted = false;
    std::string path;
    NativeFileDialogStatus status = NativeFileDialogStatus::unspecified;
    std::string error;
};

namespace detail
{

// Runs on the UI thread and returns an empty path for cancellation or native
// dialog initialization failure. The implementation is platform-owned so the
// editor and its event sink remain backend-neutral.
NativeFileDialogResult runNativeFileDialog(const NativeFileDialogRequest& request);

using NativeFileDialogTestOverride = NativeFileDialogResult (*) (
    const NativeFileDialogRequest&, void* context);

// Test-only seam for the native wrapper. Production calls pass a null
// override; native probes can deterministically exercise accept, cancel, and
// platform initialization failures without leaving a modal panel open.
void setNativeFileDialogTestOverride(NativeFileDialogTestOverride override,
                                     void* context);
void clearNativeFileDialogTestOverride();

} // namespace detail
} // namespace ats::ui
