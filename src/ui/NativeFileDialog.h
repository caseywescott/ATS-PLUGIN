#pragma once

#include <string>

namespace ats::ui
{

enum class NativeFileDialogKind : int
{
    importWav = 0,
    importAts = 1,
    exportAts = 2,
    savePreset = 3
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
};

namespace detail
{

// Runs on the UI thread and returns an empty path for cancellation or native
// dialog initialization failure. The implementation is platform-owned so the
// editor and its event sink remain backend-neutral.
NativeFileDialogResult runNativeFileDialog(const NativeFileDialogRequest& request);

} // namespace detail
} // namespace ats::ui
