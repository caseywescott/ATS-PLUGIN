#include "../src/engine/PluginModel.h"
#include "../src/ui/ParameterBinding.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace
{
void writeU16(std::ofstream& stream, std::uint16_t value)
{
    stream.put(static_cast<char>(value & 0xffu));
    stream.put(static_cast<char>((value >> 8u) & 0xffu));
}

void writeU32(std::ofstream& stream, std::uint32_t value)
{
    writeU16(stream, static_cast<std::uint16_t>(value & 0xffffu));
    writeU16(stream, static_cast<std::uint16_t>(value >> 16u));
}

void writeMonoWave(const std::filesystem::path& path)
{
    constexpr std::int16_t samples[] = { 0, 2000, -2000, 0 };
    constexpr std::uint32_t dataBytes = sizeof(samples);
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    assert(stream);
    stream.write("RIFF", 4); writeU32(stream, 36 + dataBytes); stream.write("WAVEfmt ", 8);
    writeU32(stream, 16); writeU16(stream, 1); writeU16(stream, 1); writeU32(stream, 44100);
    writeU32(stream, 44100 * 2); writeU16(stream, 2); writeU16(stream, 16);
    stream.write("data", 4); writeU32(stream, dataBytes);
    for (const auto sample : samples) writeU16(stream, static_cast<std::uint16_t>(sample));
    assert(stream);
}
}

int main()
{
    ats::engine::PluginModel model(1, 8);
    ats::ui::HeadlessEditorHost editor(model, model.telemetry());
    assert(editor.lifecycle() == ats::ui::EditorLifecycle::detached);
    assert(!editor.attach(0.0f, 1280.0f, 940.0f));
    assert(editor.attach(2.0f, 1280.0f, 940.0f));
    editor.setPage(ats::ui::EditorPage::synthesis);
    editor.setFocus(ats::ui::FocusOwner::textEntry);
    editor.openPopup(ats::ui::PopupKind::presetMenu);
    assert(editor.page() == ats::ui::EditorPage::synthesis);
    assert(editor.popup() == ats::ui::PopupKind::presetMenu);
    assert(editor.focus() == ats::ui::FocusOwner::popup);
    editor.dismissPopup();
    assert(editor.focus() == ats::ui::FocusOwner::none);
    editor.requestResize(960.0f, 680.0f);
    assert(editor.requestedSize()[0] == 960.0f);

    ats::ui::ParameterBinding binding(model.parameters(), "outputTrim", model);
    assert(binding.beginGesture(1.0f));
    assert(binding.updateGesture(1.25f));
    assert(binding.endGesture(1.25f));
    assert(model.processActions() == 3);
    auto result = model.results().tryPop();
    assert(result && result->status == ats::ui::ActionStatus::success);
    result = model.results().tryPop();
    assert(result && result->status == ats::ui::ActionStatus::success);
    result = model.results().tryPop();
    assert(result && result->status == ats::ui::ActionStatus::success);
    assert(std::abs(model.parameters().read("outputTrim") - 1.25f) < 1.0e-6f);
    const auto revision = model.telemetry().snapshot().modelRevision;
    assert(revision == 3);

    assert(model.actions().submit(ats::ui::SetParameterAction { "missing", 0.5f }));
    assert(model.processActions() == 1);
    result = model.results().tryPop();
    assert(result && result->status == ats::ui::ActionStatus::failed);
    assert(result->code == "unknown_parameter");
    assert(model.telemetry().snapshot().modelRevision == revision);

    assert(model.actions().submit(ats::ui::SetParameterAction { "outputTrim", NAN }));
    assert(model.processActions() == 1);
    result = model.results().tryPop();
    assert(result && result->code == "invalid_parameter");

    assert(model.actions().submit(ats::ui::CancelJobsAction {}));
    assert(model.processActions() == 1);
    result = model.results().tryPop();
    assert(result && result->status == ats::ui::ActionStatus::success);
    assert(model.telemetry().snapshot().status == "jobs cancelled");

    const auto wav = std::filesystem::temp_directory_path() / "ats-plugin-model-import.wav";
    writeMonoWave(wav);
    assert(model.actions().submit(ats::ui::ImportWavAction { { wav.u8string() }, 1 }));
    assert(model.processActions() == 1);
    result = model.results().tryPop();
    assert(result && result->status == ats::ui::ActionStatus::success);
    const auto* sourceB = model.source(1);
    assert(sourceB && sourceB->sampleRate == 44100 && sourceB->monoSamples.size() == 4);
    assert(model.telemetry().snapshot().hasSourceB);
    std::error_code filesystemError;
    std::filesystem::remove(wav, filesystemError);

    assert(model.actions().submit(ats::ui::PreviewAction { 0, 0, true }));
    assert(model.processActions() == 1);
    result = model.results().tryPop();
    assert(result && result->status == ats::ui::ActionStatus::failed);
    assert(result->code == "action_not_bound");
    assert(model.results().tryPop() == std::nullopt);
    editor.close();
    assert(editor.lifecycle() == ats::ui::EditorLifecycle::detached);
    std::cout << "plugin_model_test: typed action ownership and results ok\n";
}
