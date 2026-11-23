#include "../src/state/FactoryPresetState.h"
#include "../src/state/ParameterPresetCodec.h"
#include "../src/state/PresetService.h"
#include "../src/ui/FileDialogModel.h"
#include "../src/ui/PresetInteractionModel.h"

#include <cassert>
#include <cmath>
#include <filesystem>

namespace { bool close(float a, float b) { return std::abs(a - b) < 1.0e-3f; } }

// UI-FILE-01: FileDialogModel — cancellation and empty-path produce nullopt
static void test_dialog_cancel_and_error()
{
    using namespace ats::ui;
    for (int i = 0; i <= 4; ++i)
    {
        const auto kind = static_cast<NativeFileDialogKind>(i);
        // cancellation (accepted == false)
        assert(!FileDialogModel::resolve(kind, { false, "/some/path" }).has_value());
        // accepted but empty path (dialog init failure)
        assert(!FileDialogModel::resolve(kind, { true, "" }).has_value());
        // both false
        assert(!FileDialogModel::resolve(kind, { false, "" }).has_value());
    }
}

// UI-FILE-01: importWav accepted → ImportWavAction with correct slot and path
static void test_dialog_import_wav()
{
    using namespace ats::ui;
    const auto result = FileDialogModel::resolve(
        NativeFileDialogKind::importWav, { true, "/audio/source.wav" }, 1);
    assert(result.has_value());
    assert(std::holds_alternative<ImportWavAction>(*result));
    const auto& a = std::get<ImportWavAction>(*result);
    assert(a.path.string() == "/audio/source.wav");
    assert(a.slot == 1);

    // slot 0 (source A)
    const auto slotA = FileDialogModel::resolve(
        NativeFileDialogKind::importWav, { true, "/a.wav" }, 0);
    assert(std::get<ImportWavAction>(*slotA).slot == 0);
}

// UI-FILE-01: importAts accepted → ImportAtsAction
static void test_dialog_import_ats()
{
    using namespace ats::ui;
    const auto result = FileDialogModel::resolve(
        NativeFileDialogKind::importAts, { true, "/audio/source.ats" }, 0);
    assert(result.has_value());
    assert(std::holds_alternative<ImportAtsAction>(*result));
    const auto& a = std::get<ImportAtsAction>(*result);
    assert(a.slot == 0);
    assert(a.path.string() == "/audio/source.ats");
}

// UI-FILE-01: exportAts accepted → ExportAtsAction
static void test_dialog_export_ats()
{
    using namespace ats::ui;
    const auto result = FileDialogModel::resolve(
        NativeFileDialogKind::exportAts, { true, "/out/export.ats" }, 1);
    assert(result.has_value());
    assert(std::holds_alternative<ExportAtsAction>(*result));
    const auto& a = std::get<ExportAtsAction>(*result);
    assert(a.source == 1);
    assert(a.path.string() == "/out/export.ats");
}

// UI-FILE-01 / UI-PRESET-02: savePreset dialog accepted → SavePresetAction
static void test_dialog_save_preset()
{
    using namespace ats::ui;
    const auto result = FileDialogModel::resolve(
        NativeFileDialogKind::savePreset, { true, "/presets/demo.atspreset" });
    assert(result.has_value());
    assert(std::holds_alternative<SavePresetAction>(*result));
    assert(std::get<SavePresetAction>(*result).path.string() == "/presets/demo.atspreset");
}

// UI-FILE-01 / UI-PRESET-02: loadPreset dialog accepted → LoadPresetAction
static void test_dialog_load_preset()
{
    using namespace ats::ui;
    const auto result = FileDialogModel::resolve(
        NativeFileDialogKind::loadPreset, { true, "/presets/demo.atspreset" });
    assert(result.has_value());
    assert(std::holds_alternative<LoadPresetAction>(*result));
    assert(std::get<LoadPresetAction>(*result).path.string() == "/presets/demo.atspreset");
}

// UI-PRESET-01: selectFactory validates index range 0..11
static void test_factory_selection()
{
    using namespace ats::ui;
    for (std::int32_t i = 0; i < PresetInteractionModel::kFactoryCount; ++i)
    {
        const auto action = PresetInteractionModel::selectFactory(i);
        assert(action.has_value());
        assert(std::holds_alternative<ApplyFactoryPresetAction>(*action));
        assert(std::get<ApplyFactoryPresetAction>(*action).index == i);
    }
    assert(!PresetInteractionModel::selectFactory(-1).has_value());
    assert(!PresetInteractionModel::selectFactory(12).has_value());
    assert(!PresetInteractionModel::selectFactory(100).has_value());
}

// UI-PRESET-01: selectUserPreset and savePreset reject empty paths
static void test_user_preset_selection()
{
    using namespace ats::ui;
    const auto ok = PresetInteractionModel::selectUserPreset({ "/presets/mine.atspreset" });
    assert(ok.has_value());
    assert(std::holds_alternative<LoadPresetAction>(*ok));
    assert(std::get<LoadPresetAction>(*ok).path.string() == "/presets/mine.atspreset");
    assert(!PresetInteractionModel::selectUserPreset({}).has_value());

    const auto save = PresetInteractionModel::savePreset({ "/presets/new.atspreset" });
    assert(save.has_value());
    assert(std::holds_alternative<SavePresetAction>(*save));
    assert(!PresetInteractionModel::savePreset({}).has_value());
}

// UI-PRESET-01: all 12 factory presets apply without error and produce known values
static void test_all_factory_presets_apply()
{
    ats::ParameterStore store;

    // Boundary rejection
    assert(!ats::state::applyFactoryPreset(-1, store));
    assert(!ats::state::applyFactoryPreset(12, store));

    // All 12 apply without error
    for (int i = 0; i < 12; ++i)
        assert(ats::state::applyFactoryPreset(i, store));

    // Preset 0: neutral defaults
    ats::state::applyFactoryPreset(0, store);
    assert(close(store.read("partGain"), 1.0f));
    assert(close(store.read("resGain"),  1.0f));
    assert(close(store.read("freq"),     1.0f));

    // Preset 1: bright partials
    ats::state::applyFactoryPreset(1, store);
    assert(close(store.read("partGain"), 1.6f));
    assert(close(store.read("resGain"),  0.45f));
    assert(close(store.read("freq"),     1.05f));

    // Preset 3: residuals only
    ats::state::applyFactoryPreset(3, store);
    assert(store.read("residualOnly") == 1.0f);
    assert(store.read("partGain") == 0.0f);

    // Preset 7: morph stable default
    ats::state::applyFactoryPreset(7, store);
    assert(close(store.read("morphAmount"), 0.5f));
    assert(close(store.read("morphPerformanceMacro"), 0.35f));
    assert(close(store.read("morphSmoothingMs"), 60.0f));

    // Preset 9: morph vocal/formant
    ats::state::applyFactoryPreset(9, store);
    assert(store.read("morphCorrespondenceStrategy") == 1.0f);
    assert(store.read("morphUnmatchedSourcePolicy") == 3.0f);
    assert(close(store.read("morphLaneResidualAmount"), 0.65f));
    assert(store.read("morphLaneResidualLinked") == 0.0f);

    // Preset 11: morph noisy texture
    ats::state::applyFactoryPreset(11, store);
    assert(store.read("morphGridType") == 1.0f);
    assert(close(store.read("morphLaneFrequencyAmount"), 0.35f));
    assert(close(store.read("morphLaneResidualAmount"), 0.85f));
}

// UI-PRESET-02: ParameterPresetCodec encode/decode error cases
static void test_codec_errors()
{
    ats::ParameterStore store;
    const auto valid = ats::state::ParameterPresetCodec::encode(store);
    std::string err;

    // Truncated
    auto truncated = valid;
    truncated.resize(4);
    assert(!ats::state::ParameterPresetCodec::decode(truncated, store, err));
    assert(!err.empty());

    // Empty
    err.clear();
    assert(!ats::state::ParameterPresetCodec::decode({}, store, err));

    // Corrupt magic byte
    auto corrupt = valid;
    corrupt[0] = std::byte{0xFF};
    err.clear();
    assert(!ats::state::ParameterPresetCodec::decode(corrupt, store, err));
}

// UI-PRESET-02: extension normalisation
static void test_extension_normalization()
{
    assert(ats::state::PresetService::withExtension("demo") == "demo.atspreset");
    assert(ats::state::PresetService::withExtension("demo.atspreset") == "demo.atspreset");
    assert(ats::state::PresetService::withExtension("multi.word") == "multi.word.atspreset");
}

// UI-PRESET-01/02: PresetService entries list: 12 factory + sorted user entries
static void test_preset_entries_list(const std::filesystem::path& folder)
{
    ats::platform::FileService files;
    ats::state::PresetService presets(files, { folder.u8string() });

    // Factory list before any user saves
    const auto factory_only = presets.entries();
    assert(factory_only.size() == 12);
    assert(factory_only[0].factory && factory_only[0].factoryIndex == 0);
    assert(factory_only[11].factory && factory_only[11].factoryIndex == 11);

    // Save two user presets out-of-alpha-order
    const std::vector<std::byte> p { std::byte{1}, std::byte{2} };
    assert(presets.save("zeta", p, false).succeeded());
    assert(presets.save("alpha", p, false).succeeded());

    const auto all = presets.entries();
    assert(all.size() == 14);
    // Factory entries always first
    assert(all[0].factory);
    assert(all[11].factory);
    // User entries sorted
    assert(all[12].name == "User: alpha");
    assert(all[13].name == "User: zeta");
    assert(!all[12].factory && !all[13].factory);
}

// UI-PRESET-02: user preset full encode→save→load→decode round-trip
static void test_user_preset_roundtrip(const std::filesystem::path& folder)
{
    ats::platform::FileService files;
    ats::state::PresetService presets(files, { folder.u8string() });

    ats::ParameterStore source;
    assert(source.write("partGain", 1.6f));
    assert(source.write("morphLaneResidualAmount", 0.65f));
    const auto payload = ats::state::ParameterPresetCodec::encode(source);

    assert(presets.save("roundtrip", payload, false).succeeded());

    const auto entries = presets.entries();
    const ats::state::PresetEntry* entry = nullptr;
    for (const auto& e : entries)
        if (e.name == "User: roundtrip") { entry = &e; break; }
    assert(entry != nullptr);

    const auto loaded = presets.load(entry->path);
    assert(loaded.succeeded());
    assert(loaded.bytes == payload);

    ats::ParameterStore restored;
    std::string err;
    assert(ats::state::ParameterPresetCodec::decode(loaded.bytes, restored, err));
    assert(err.empty());
    assert(close(restored.read("partGain"), 1.6f));
    assert(close(restored.read("morphLaneResidualAmount"), 0.65f));

    // Duplicate rejected
    const auto dup = presets.save("roundtrip", payload, false);
    assert(!dup.succeeded());
    assert(dup.status == ats::platform::FileStatus::alreadyExists);

    // Overwrite succeeds and replaces payload
    const auto replacement = ats::state::ParameterPresetCodec::encode(ats::ParameterStore{});
    assert(presets.save("roundtrip", replacement, true).succeeded());
    const auto replaced = presets.load(entry->path);
    assert(replaced.succeeded() && replaced.bytes == replacement);

    // Cancellation propagates
    ats::CancellationToken token;
    token.cancel();
    assert(presets.save("cancelled", payload, false, &token).cancelled());
}

// UI-PRESET-02: factory preset encode→save→load→decode round-trip
static void test_factory_preset_codec_roundtrip(const std::filesystem::path& folder)
{
    ats::platform::FileService files;
    ats::state::PresetService presets(files, { folder.u8string() });

    ats::ParameterStore store;
    assert(ats::state::applyFactoryPreset(9, store)); // morph vocal/formant
    const auto payload = ats::state::ParameterPresetCodec::encode(store);
    assert(presets.save("factory9", payload, false).succeeded());

    const auto entries = presets.entries();
    const ats::state::PresetEntry* entry = nullptr;
    for (const auto& e : entries)
        if (e.name == "User: factory9") { entry = &e; break; }
    assert(entry != nullptr);

    const auto loaded = presets.load(entry->path);
    assert(loaded.succeeded());

    ats::ParameterStore decoded;
    std::string err;
    assert(ats::state::ParameterPresetCodec::decode(loaded.bytes, decoded, err));
    assert(close(decoded.read("morphCorrespondenceStrategy"), 1.0f));
    assert(close(decoded.read("morphUnmatchedSourcePolicy"), 3.0f));
    assert(close(decoded.read("morphLaneResidualAmount"), 0.65f));
    assert(decoded.read("morphLaneResidualLinked") == 0.0f);
}

int main()
{
    // Dialog model tests (no filesystem)
    test_dialog_cancel_and_error();
    test_dialog_import_wav();
    test_dialog_import_ats();
    test_dialog_export_ats();
    test_dialog_save_preset();
    test_dialog_load_preset();

    // Preset interaction model tests (no filesystem)
    test_factory_selection();
    test_user_preset_selection();
    test_all_factory_presets_apply();
    test_codec_errors();
    test_extension_normalization();

    // Filesystem-dependent tests
    const auto folder = std::filesystem::temp_directory_path() / "ats-preset-file-workflow-test";
    std::error_code ec;
    std::filesystem::remove_all(folder, ec);
    std::filesystem::create_directories(folder, ec);
    assert(!ec);

    test_preset_entries_list(folder);

    // Sub-directory for round-trip tests to avoid name conflicts with entries list
    const auto rtFolder = folder / "roundtrip";
    std::filesystem::create_directories(rtFolder, ec);
    assert(!ec);
    test_user_preset_roundtrip(rtFolder);

    const auto factFolder = folder / "factory";
    std::filesystem::create_directories(factFolder, ec);
    assert(!ec);
    test_factory_preset_codec_roundtrip(factFolder);

    std::filesystem::remove_all(folder, ec);
    // BOUNCE: a save-panel path becomes a bounce of the given audition layer.
    {
        const auto bounce = ats::ui::FileDialogModel::resolve(ats::ui::NativeFileDialogKind::bounceWav,
                                                              { true, "/out/morph.wav" }, 1);
        assert(bounce);
        const auto* action = std::get_if<ats::ui::BounceMorphTimelineAction>(&*bounce);
        assert(action != nullptr && action->path.utf8 == "/out/morph.wav" && action->layer == 1);
    }
    return 0;
}
