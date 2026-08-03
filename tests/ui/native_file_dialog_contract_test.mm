#include "../../src/ui/FileDialogModel.h"
#include "../../src/ui/VisageNativeMac.h"

#include <cassert>

namespace
{
struct Script
{
    ats::ui::NativeFileDialogResult next{};
    int calls = 0;
};

ats::ui::NativeFileDialogResult scriptedDialog(
    const ats::ui::NativeFileDialogRequest&, void* context)
{
    auto& script = *static_cast<Script*>(context);
    ++script.calls;
    return script.next;
}
}

int main()
{
    using namespace ats::ui;
    Script script;
    detail::setNativeFileDialogTestOverride(&scriptedDialog, &script);

    const NativeFileDialogKind kinds[] {
        NativeFileDialogKind::importWav, NativeFileDialogKind::importAts,
        NativeFileDialogKind::exportAts, NativeFileDialogKind::savePreset,
        NativeFileDialogKind::loadPreset
    };
    const char* paths[] {
        "/tmp/source.wav", "/tmp/source.ats", "/tmp/output.ats",
        "/tmp/user.atspreset", "/tmp/user.atspreset"
    };

    for (int index = 0; index < 5; ++index)
    {
        const NativeFileDialogRequest request { kinds[index], nullptr, {} };

        script.next = { true, paths[index], NativeFileDialogStatus::accepted, {} };
        const auto accepted = detail::runNativeFileDialog(request);
        assert(accepted.accepted && accepted.status == NativeFileDialogStatus::accepted);
        const auto action = FileDialogModel::resolve(kinds[index], accepted, 1);
        assert(action.has_value());
        if (index == 0)
            assert(std::holds_alternative<ImportWavAction>(*action) &&
                   std::get<ImportWavAction>(*action).slot == 1);
        else if (index == 1)
            assert(std::holds_alternative<ImportAtsAction>(*action));
        else if (index == 2)
            assert(std::holds_alternative<ExportAtsAction>(*action) &&
                   std::get<ExportAtsAction>(*action).source == 1);
        else if (index == 3)
            assert(std::holds_alternative<SavePresetAction>(*action));
        else
            assert(std::holds_alternative<LoadPresetAction>(*action));

        script.next = { false, {}, NativeFileDialogStatus::cancelled, {} };
        const auto cancelled = detail::runNativeFileDialog(request);
        assert(!cancelled.accepted && cancelled.status == NativeFileDialogStatus::cancelled);
        assert(!FileDialogModel::resolve(kinds[index], cancelled).has_value());
        const NativeFileDialogResult contradictory { true, paths[index],
                                                       NativeFileDialogStatus::cancelled, {} };
        assert(!FileDialogModel::resolve(kinds[index], contradictory).has_value());

        script.next = { true, {}, NativeFileDialogStatus::initializationFailed,
                        "native panel returned no URL" };
        const auto initializationFailure = detail::runNativeFileDialog(request);
        assert(!FileDialogModel::resolve(kinds[index], initializationFailure).has_value());

        script.next = { true, paths[index], NativeFileDialogStatus::failed, "filesystem error" };
        const auto failure = detail::runNativeFileDialog(request);
        assert(!FileDialogModel::resolve(kinds[index], failure).has_value());
    }

    assert(script.calls == 20);
    detail::clearNativeFileDialogTestOverride();
    return 0;
}
