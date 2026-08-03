#pragma once

#include "EditorHost.h"
#include "NativeFileDialog.h"

#include <optional>

namespace ats::ui
{

// Maps a completed native file dialog result to the typed EditorAction to
// dispatch. All dialog work is otherwise platform-owned so this class stays
// backend-neutral.
class FileDialogModel
{
public:
    // Returns nullopt when the dialog was cancelled or produced an empty path
    // (dialog initialisation failure). `slot` is forwarded to import/export
    // actions; it is ignored for preset save/load which are slot-agnostic.
    [[nodiscard]] static std::optional<EditorAction> resolve(
        NativeFileDialogKind kind,
        const NativeFileDialogResult& result,
        int slot = 0) noexcept
    {
        if (!result.accepted || result.path.empty()
            || result.status == NativeFileDialogStatus::cancelled
            || result.status == NativeFileDialogStatus::initializationFailed
            || result.status == NativeFileDialogStatus::failed)
            return std::nullopt;

        const ats::Path path { result.path };
        switch (kind)
        {
        case NativeFileDialogKind::importWav:
            return EditorAction{ ImportWavAction{ path, slot } };
        case NativeFileDialogKind::importAts:
            return EditorAction{ ImportAtsAction{ path, slot } };
        case NativeFileDialogKind::exportAts:
            return EditorAction{ ExportAtsAction{ path, slot } };
        case NativeFileDialogKind::savePreset:
            return EditorAction{ SavePresetAction{ path } };
        case NativeFileDialogKind::loadPreset:
            return EditorAction{ LoadPresetAction{ path } };
        }
        return std::nullopt;
    }
};

} // namespace ats::ui
