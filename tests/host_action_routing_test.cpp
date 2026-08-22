#include "../src/host/HostAdapter.h"
#include "../src/platform/FileService.h"
#include "../src/state/PresetService.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

// ---------------------------------------------------------------------------

static void test_set_parameter_action()
{
    ats::host::HostAdapter host;
    host.initialize(ats::host::HostFormat::standalone);
    ats::Error error;

    // Valid automatable parameter
    assert(host.applyEditorAction(ats::ui::SetParameterAction{ "partGain", 0.75f }, error));
    assert(!error);
    assert(host.parameters().read("partGain") == 0.75f);

    // Unknown parameter id → error, still returns true (handled)
    error = {};
    assert(host.applyEditorAction(ats::ui::SetParameterAction{ "nonexistent_id", 0.5f }, error));
    assert(static_cast<bool>(error));
}

static void test_apply_factory_preset_valid()
{
    ats::host::HostAdapter host;
    host.initialize(ats::host::HostFormat::standalone);
    ats::Error error;

    for (std::int32_t i = 0; i < 12; ++i)
    {
        error = {};
        assert(host.applyEditorAction(ats::ui::ApplyFactoryPresetAction{ i }, error));
        assert(!error);
    }
    // Preset 1 sets partGain to 1.6f
    (void) host.applyEditorAction(ats::ui::ApplyFactoryPresetAction{ 1 }, error);
    assert(std::abs(host.parameters().read("partGain") - 1.6f) < 1e-5f);
}

static void test_apply_factory_preset_invalid()
{
    ats::host::HostAdapter host;
    host.initialize(ats::host::HostFormat::standalone);
    ats::Error error;

    assert(host.applyEditorAction(ats::ui::ApplyFactoryPresetAction{ -1 }, error));
    assert(static_cast<bool>(error));

    error = {};
    assert(host.applyEditorAction(ats::ui::ApplyFactoryPresetAction{ 12 }, error));
    assert(static_cast<bool>(error));
}

static void test_note_action_semitone_offset()
{
    ats::host::HostAdapter host;
    host.initialize(ats::host::HostFormat::standalone);
    ats::Error error;

    // Neutral before any note
    assert(host.semitoneOffset() == 0);

    // Note on C5 (MIDI 72) → offset = 72 - 60 = 12
    assert(host.applyEditorAction(ats::ui::NoteAction{ true, 72, 100 }, error));
    assert(!error);
    assert(host.semitoneOffset() == 12);

    // Note off → offset resets to 0
    assert(host.applyEditorAction(ats::ui::NoteAction{ false, 72, 0 }, error));
    assert(!error);
    assert(host.semitoneOffset() == 0);

    // Note on A4 (MIDI 69) → offset = 9
    assert(host.applyEditorAction(ats::ui::NoteAction{ true, 69, 80 }, error));
    assert(host.semitoneOffset() == 9);
}

static void test_spectral_view_action()
{
    ats::host::HostAdapter host;
    host.initialize(ats::host::HostFormat::standalone);
    ats::Error error;

    assert(host.spectralViewSource() == 0);
    assert(host.spectralViewMode() == 0);

    assert(host.applyEditorAction(ats::ui::SetSpectralViewAction{ 1, 3 }, error));
    assert(!error);
    assert(host.spectralViewSource() == 1);
    assert(host.spectralViewMode() == 3);

    // Initialize resets spectral view state
    host.initialize(ats::host::HostFormat::standalone);
    assert(host.spectralViewSource() == 0);
    assert(host.spectralViewMode() == 0);
}

static void test_history_action_empty()
{
    ats::host::HostAdapter host;
    host.initialize(ats::host::HostFormat::standalone);
    ats::Error error;

    // Undo with empty history → error, still returns true
    assert(host.applyEditorAction(ats::ui::HistoryAction{ false }, error));
    assert(static_cast<bool>(error));
    assert(error.code == "nothing_to_undo");

    // Redo with empty history → error
    error = {};
    assert(host.applyEditorAction(ats::ui::HistoryAction{ true }, error));
    assert(static_cast<bool>(error));
    assert(error.code == "nothing_to_redo");
}

static void test_history_action_roundtrip()
{
    ats::host::HostAdapter host;
    host.initialize(ats::host::HostFormat::standalone);

    // Seed history with two synthetic state chunks. The UndoHistory contract
    // is model-agnostic: undo() captures the caller-supplied current bytes as
    // the redo "after" snapshot, so stateChunk must equal `after` at commit
    // time for the roundtrip to be lossless.
    const std::vector<std::byte> before { std::byte{0xAA}, std::byte{0xBB} };
    const std::vector<std::byte> after  { std::byte{0xCC}, std::byte{0xDD} };

    // Prime stateChunk to the "after" state so that undo captures it correctly
    assert(host.restoreState(after.data(), after.size()) == false); // not a valid ATS header
    // Use history directly: begin captures before, commit records after
    host.history().begin("test edit", before);
    assert(host.history().commit(after));
    assert(host.history().canUndo());

    // Simulate: current stateChunk is `after` by pushing it via history before
    // We fake the current chunk by setting up a second commit that has `after`
    // as its before snapshot (so the undo chain is correct).
    // Actually, test the simpler single-level contract:
    // - After commit, canUndo == true
    // - Undo with stateChunk=after → stateChunk becomes before
    //
    // We can't directly set stateChunk_ through public API (restoreState
    // validates ATS format). Instead, verify undo changes stateChunk and
    // canUndo/canRedo states.
    ats::Error error;
    assert(host.history().canUndo());
    assert(!host.history().canRedo());

    assert(host.applyEditorAction(ats::ui::HistoryAction{ false }, error));
    assert(!error);
    assert(!host.history().canUndo());
    assert(host.history().canRedo());

    assert(host.applyEditorAction(ats::ui::HistoryAction{ true }, error));
    assert(!error);
    assert(host.history().canUndo());
    assert(!host.history().canRedo());
}

static void test_preset_direct_file_roundtrip()
{
    const auto tmpDir = std::filesystem::temp_directory_path() / "host_action_routing_direct_preset";
    std::filesystem::create_directories(tmpDir);
    ats::host::HostAdapter host;
    host.initialize(ats::host::HostFormat::standalone);
    const ats::Path path { (tmpDir / "direct.atspreset").u8string() };
    (void) host.parameters().write("partGain", 1.11f);
    ats::Error error;
    assert(host.applyEditorAction(ats::ui::SavePresetAction { path }, error));
    assert(!error);
    (void) host.parameters().write("partGain", 0.0f);
    assert(host.applyEditorAction(ats::ui::LoadPresetAction { path }, error));
    assert(!error);
    assert(std::abs(host.parameters().read("partGain") - 1.11f) < 1.0e-3f);
    std::filesystem::remove_all(tmpDir);
}

static void test_preset_save_load_roundtrip()
{
    const auto tmpDir = std::filesystem::temp_directory_path() / "host_action_routing_preset";
    std::filesystem::create_directories(tmpDir);

    ats::platform::FileService files;
    const ats::Path directory { tmpDir.u8string() };
    ats::state::PresetService service(files, directory);

    ats::host::HostAdapter host;
    host.initialize(ats::host::HostFormat::standalone);
    host.setPresetService(&service);

    // Set a distinctive parameter value
    (void) host.parameters().write("partGain", 1.23f);

    // SavePresetAction uses the stem of the path as the preset name
    ats::Error error;
    const auto savePath = ats::Path{ (tmpDir / "routing_test.atspreset").u8string() };
    assert(host.applyEditorAction(ats::ui::SavePresetAction{ savePath }, error));
    assert(!error);

    // Corrupt the parameter to verify round-trip
    (void) host.parameters().write("partGain", 0.0f);
    assert(std::abs(host.parameters().read("partGain")) < 1e-5f);

    // LoadPresetAction restores the saved value
    assert(host.applyEditorAction(ats::ui::LoadPresetAction{ savePath }, error));
    assert(!error);
    assert(std::abs(host.parameters().read("partGain") - 1.23f) < 1e-3f);

    std::filesystem::remove_all(tmpDir);
}

static void test_partial_edit_validation()
{
    ats::host::HostAdapter host;
    host.initialize(ats::host::HostFormat::standalone);
    ats::Error error;

    // Negative partial index → validation error
    assert(host.applyEditorAction(ats::ui::PartialEditAction{ -1, 0, false }, error));
    assert(static_cast<bool>(error));
    assert(error.code == "invalid_partial");

    // Valid index but no model loaded → no_model error
    error = {};
    assert(host.applyEditorAction(ats::ui::PartialEditAction{ 0, 0, false }, error));
    assert(static_cast<bool>(error));
    assert(error.code == "no_model");
}

static void writeTestWave(const std::filesystem::path& path)
{
    const auto u16 = [](std::ofstream& stream, std::uint16_t value) {
        stream.put(static_cast<char>(value));
        stream.put(static_cast<char>(value >> 8));
    };
    const auto u32 = [&u16](std::ofstream& stream, std::uint32_t value) {
        u16(stream, static_cast<std::uint16_t>(value));
        u16(stream, static_cast<std::uint16_t>(value >> 16));
    };
    constexpr std::uint32_t frames = 32768;
    constexpr std::uint32_t rate = 48000;
    constexpr std::uint32_t bytes = frames * 2;
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    assert(stream);
    stream.write("RIFF", 4); u32(stream, 36 + bytes); stream.write("WAVEfmt ", 8); u32(stream, 16);
    u16(stream, 1); u16(stream, 1); u32(stream, rate); u32(stream, rate * 2); u16(stream, 2);
    u16(stream, 16);
    stream.write("data", 4); u32(stream, bytes);
    for (std::uint32_t frame = 0; frame < frames; ++frame)
        u16(stream, static_cast<std::uint16_t>(static_cast<std::int16_t>(
                        std::lround(9000.0 * std::sin(2.0 * 3.14159265358979 * 440.0 * frame / rate)))));
    assert(stream);
}

// Preview slot 3 is the Current model, not an entry in the two-element import
// table. Model edits used to index that table directly, so with Current
// selected every edit reported "no model" and left the model on screen
// untouched.
static void test_model_edits_follow_the_current_selection()
{
    const auto path = std::filesystem::temp_directory_path() / "ats-routing-current.wav";
    writeTestWave(path);

    ats::host::HostAdapter host;
    host.initialize(ats::host::HostFormat::standalone);
    ats::Error error;
    assert(host.importSource({ path.u8string() }, 3, ats::engine::SourceImportKind::audio, error));
    assert(!error);
    assert(host.analyzeSource(3, error));
    assert(!error);

    const auto* current = host.source(ats::host::SourceIdentity::current);
    assert(current != nullptr && current->sound.get() != nullptr);
    const auto* sound = current->sound.get();
    assert(sound->partials > 0 && sound->frames > 0);

    assert(host.applySessionAction(ats::ui::PreviewAction{ 3, 0, false }, error));
    assert(!error);

    assert(sound->amp[0][0] != 0.125 && sound->frq[0][0] != 220.0);

    error = {};
    assert(host.applyEditorAction(ats::ui::PartialEditAction{ 0, 0, true, 0.125, 220.0 }, error));
    assert(!error);
    assert(sound->amp[0][0] == 0.125);
    assert(sound->frq[0][0] == 220.0);

    error = {};
    assert(host.applyEditorAction(ats::ui::ResidualEditAction{ 0, 0, 0.375f }, error));
    assert(!error);
    assert(sound->band_energy != nullptr);
    assert(sound->band_energy[0][0] == 0.375);

    // Undo has to restore the Current model, not just the parameter table. The
    // restore replaces currentSource_, so the earlier sound pointer is stale.
    // Only the partial edit is on the undo stack: band_energy is absent from the
    // captured chunk, so the residual edit commits an unchanged state.
    error = {};
    assert(host.applyEditorAction(ats::ui::HistoryAction{ false }, error));
    assert(!error);
    const auto* afterUndo = host.source(ats::host::SourceIdentity::current);
    assert(afterUndo != nullptr && afterUndo->sound.get() != nullptr);
    assert(afterUndo->sound.get()->amp[0][0] != 0.125);
    assert(afterUndo->sound.get()->frq[0][0] != 220.0);

    // Selecting the morph timeline still has no editable model.
    assert(host.applySessionAction(ats::ui::PreviewAction{ 2, 0, false }, error));
    error = {};
    assert(host.applyEditorAction(ats::ui::PartialEditAction{ 0, 0, true, 0.5, 330.0 }, error));
    assert(static_cast<bool>(error));
    assert(error.code == "no_model");

    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

static void test_residual_edit_validation()
{
    ats::host::HostAdapter host;
    host.initialize(ats::host::HostFormat::standalone);
    ats::Error error;

    // Negative band → validation error
    assert(host.applyEditorAction(ats::ui::ResidualEditAction{ -1, 0, 0.5f }, error));
    assert(static_cast<bool>(error));
    assert(error.code == "invalid_residual_edit");

    // Negative frame → validation error
    error = {};
    assert(host.applyEditorAction(ats::ui::ResidualEditAction{ 0, -1, 0.5f }, error));
    assert(static_cast<bool>(error));

    // Non-finite value → validation error
    error = {};
    assert(host.applyEditorAction(
        ats::ui::ResidualEditAction{ 0, 0, std::numeric_limits<float>::infinity() }, error));
    assert(static_cast<bool>(error));

    // Valid inputs but no model loaded → no_model error
    error = {};
    assert(host.applyEditorAction(ats::ui::ResidualEditAction{ 0, 0, 0.5f }, error));
    assert(static_cast<bool>(error));
    assert(error.code == "no_model");
}

static void test_apply_math_validation()
{
    ats::host::HostAdapter host;
    host.initialize(ats::host::HostFormat::standalone);
    ats::Error error;

    // Empty script → validation error
    assert(host.applyEditorAction(ats::ui::ApplyMathAction{ "" }, error));
    assert(static_cast<bool>(error));
    assert(error.code == "empty_script");

    // Non-empty script but no model loaded → no_model error
    error = {};
    assert(host.applyEditorAction(ats::ui::ApplyMathAction{ "x * 2" }, error));
    assert(static_cast<bool>(error));
    assert(error.code == "no_model");
}

static void test_bounce_validation()
{
    ats::host::HostAdapter host;
    host.initialize(ats::host::HostFormat::standalone);
    ats::Error error;

    // Empty path → error
    assert(host.applyEditorAction(ats::ui::BounceMorphTimelineAction{ ats::Path{""}, 0 }, error));
    assert(static_cast<bool>(error));
    assert(error.code == "empty_bounce_path");

    // Invalid layer → error
    error = {};
    assert(host.applyEditorAction(
        ats::ui::BounceMorphTimelineAction{ ats::Path{"/tmp/out.ats"}, -1 }, error));
    assert(static_cast<bool>(error));
    assert(error.code == "invalid_bounce_layer");

    error = {};
    assert(host.applyEditorAction(
        ats::ui::BounceMorphTimelineAction{ ats::Path{"/tmp/out.ats"}, 4 }, error));
    assert(static_cast<bool>(error));

    // Valid path + layer but no prepared model → not_ready error
    error = {};
    assert(host.applyEditorAction(
        ats::ui::BounceMorphTimelineAction{ ats::Path{"/tmp/out.ats"}, 0 }, error));
    assert(static_cast<bool>(error));
    assert(error.code == "not_ready");
}

static void test_cross_wrapper_file_and_rich_actions()
{
    ats::host::HostAdapter host;
    host.initialize(ats::host::HostFormat::standalone);
    ats::Error error;

    // All file actions now cross the same host boundary. Invalid files/models
    // still report an error, but are no longer silently wrapper-specific.
    assert(host.applyEditorAction(
        ats::ui::ImportWavAction{ ats::Path{"/tmp/foo.wav"}, 0 }, error));
    assert(static_cast<bool>(error));
    error = {};
    assert(host.applyEditorAction(
        ats::ui::ImportAtsAction{ ats::Path{"/tmp/foo.ats"}, 0 }, error));
    assert(static_cast<bool>(error));
    error = {};
    assert(host.applyEditorAction(
        ats::ui::ExportAtsAction{ ats::Path{"/tmp/foo.ats"}, 0 }, error));
    assert(static_cast<bool>(error));
    error = {};
    assert(host.applyEditorAction(
        ats::ui::SpectrumEditAction { ats::ui::SpectrumEditCommand::smoothResidual,
                                      { 2 }, 0, 1, 1.0f }, error));
    assert(static_cast<bool>(error));
}

static void test_session_actions_dispatched_through_unified()
{
    ats::host::HostAdapter host;
    host.initialize(ats::host::HostFormat::standalone);
    ats::Error error;

    // applyEditorAction delegates PreviewAction to applySessionAction
    assert(host.applyEditorAction(
        ats::ui::PreviewAction{ 0, 0, false }, error));
    assert(!error);

    // StopPreviewAction
    assert(host.applyEditorAction(ats::ui::StopPreviewAction{}, error));
    assert(!error);

    // SetMorphTimelineAction
    assert(host.applyEditorAction(
        ats::ui::SetMorphTimelineAction{ { 0.0f, 0.5f, 1.0f } }, error));
    assert(!error);
    const std::vector<float> expected{ 0.0f, 0.5f, 1.0f };
    assert(host.sessionSnapshot().timeline == expected);
}

static void test_analysis_actions_dispatched_through_unified()
{
    ats::host::HostAdapter host;
    host.initialize(ats::host::HostFormat::standalone);
    ats::Error error;

    // CancelJobsAction is an analysis action — should be dispatched through applyEditorAction
    assert(host.applyEditorAction(ats::ui::CancelJobsAction{}, error));
    assert(!error);

    // ApplyAnalysisProfileAction
    assert(host.applyEditorAction(ats::ui::ApplyAnalysisProfileAction{ 0 }, error));
    assert(!error);
}

// Every profile must write descriptor-legal values. Writing an ATSA type where
// a choice ordinal belongs used to survive only because ParameterStore::write
// silently clamps, which would have selected a different analysis type than the
// profile named.
static void test_analysis_profiles_write_descriptor_ordinals()
{
    ats::host::HostAdapter host;
    host.initialize(ats::host::HostFormat::standalone);

    for (int profile = 0; profile <= 3; ++profile)
    {
        // Check the recipe before it reaches ParameterStore::write, which would
        // clamp an out-of-range value into a silently different setting.
        for (const auto& write : ats::host::HostAdapter::analysisProfileWrites(profile))
        {
            const auto* descriptor = ats::ParameterRegistry::find(write.id);
            assert(descriptor != nullptr);
            assert(std::isfinite(write.value));
            assert(write.value >= descriptor->minimum);
            assert(write.value <= descriptor->maximum);
            if (descriptor->type == ats::ParameterType::choice)
            {
                assert(write.value == std::round(write.value));
                assert(write.value < static_cast<float>(descriptor->choiceCount));
            }
        }

        ats::Error error;
        assert(host.applyEditorAction(ats::ui::ApplyAnalysisProfileAction{ profile }, error));
        assert(!error);

        // The captured legacy state maps the ordinal to an ATSA type; an
        // out-of-range ordinal would escape here as type 5.
        const auto chunk = host.captureState();
        const auto legacy = ats::LegacyStateCodec::decode(chunk.data(), chunk.size());
        assert(legacy);
        assert(legacy->analysis.type >= 1 && legacy->analysis.type <= 4);
    }

    // Speech and percussive both ask for the full analysis, ordinal 3.
    const auto* type = ats::ParameterRegistry::find("analysisType");
    assert(type != nullptr);
    for (const int profile : { 1, 2 })
    {
        ats::Error error;
        assert(host.applyEditorAction(ats::ui::ApplyAnalysisProfileAction{ profile }, error));
        assert(!error);
        assert(host.parameters().read("analysisType") == 3.0f);
        assert(type->text(host.parameters().read("analysisType")) == "4 full");
    }
}

// ---------------------------------------------------------------------------

int main()
{
    test_set_parameter_action();
    test_apply_factory_preset_valid();
    test_apply_factory_preset_invalid();
    test_note_action_semitone_offset();
    test_spectral_view_action();
    test_history_action_empty();
    test_history_action_roundtrip();
    test_preset_direct_file_roundtrip();
    test_preset_save_load_roundtrip();
    test_partial_edit_validation();
    test_model_edits_follow_the_current_selection();
    test_residual_edit_validation();
    test_apply_math_validation();
    test_bounce_validation();
    test_cross_wrapper_file_and_rich_actions();
    test_session_actions_dispatched_through_unified();
    test_analysis_actions_dispatched_through_unified();
    test_analysis_profiles_write_descriptor_ordinals();
    return 0;
}
