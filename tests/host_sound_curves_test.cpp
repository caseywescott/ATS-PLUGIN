// Automation curves: the host stores them as one undoable change, carries
// them in the editor snapshot and saved state, and applies them at render.

#include "../src/host/HostAdapter.h"

#include <cassert>
#include <cmath>
#include <iostream>

int main()
{
    using ats::ui::SetSoundCurveAction;
    using ats::ui::SoundCurveKind;
    ats::host::HostAdapter host;
    assert(host.initialize(ats::host::HostFormat::standalone));
    assert(host.prepare({ 48000.0, 512, 2 }));
    ats::Error error;

    // Invalid payloads are refused.
    assert(host.applyEditorAction(SetSoundCurveAction { SoundCurveKind::pitch, { 0.0f } }, error));
    assert(error.code == "invalid_curve");
    error = {};

    // Points are normalised (sorted, clamped to +/-24 semitones) and snapshotted.
    assert(host.applyEditorAction(SetSoundCurveAction { SoundCurveKind::pitch, { 1.0f, 40.0f, 0.0f, 0.0f } },
                                  error) && !error);
    auto snapshot = host.editorSnapshot();
    const std::vector<float> expected { 0.0f, 0.0f, 1.0f, 24.0f };
    assert(snapshot.soundCurves[1] == expected);
    assert(snapshot.soundCurves[0].empty() && snapshot.soundCurves[2].empty());

    // Saved state carries the curves; a fresh host restores them.
    const auto chunk = host.captureState();
    ats::host::HostAdapter restored;
    assert(restored.initialize(ats::host::HostFormat::standalone));
    assert(restored.restoreState(chunk));
    assert(restored.editorSnapshot().soundCurves[1] == expected);

    // Typed morph points (time + easing) survive save / restore (state v21).
    ats::ui::SetMorphTimelineAction morph;
    morph.points = { { 0.0f, 0.0f, 0, 2 }, { 0.3f, 0.9f, 0, 3 }, { 1.0f, 0.2f, 0, 0 } };
    assert(host.applySessionAction(morph, error) && !error);
    const auto morphChunk = host.captureState();
    ats::host::HostAdapter morphRestored;
    assert(morphRestored.initialize(ats::host::HostFormat::standalone));
    assert(morphRestored.restoreState(morphChunk));
    const auto restoredSnapshot = morphRestored.editorSnapshot();
    const std::vector<float> morphExpected { 0.0f, 0.0f, 0.3f, 0.9f, 1.0f, 0.2f };
    assert(restoredSnapshot.morphCurve.size() == morphExpected.size());
    for (std::size_t i = 0; i < morphExpected.size(); ++i)
        assert(std::abs(restoredSnapshot.morphCurve[i] - morphExpected[i]) < 1.0e-6f);
    assert((restoredSnapshot.morphCurveShapes == std::vector<std::uint8_t> { 2, 3, 0 }));
    assert(host.applyEditorAction(ats::ui::HistoryAction { false }, error) && !error); // undo the morph edit

    // Undo returns to the empty curve.
    assert(host.applyEditorAction(ats::ui::HistoryAction { false }, error) && !error);
    assert(host.editorSnapshot().soundCurves[1].empty());

    // Render: a 440 Hz model with a +12 semitone curve plays near 880 Hz.
    ATS_SOUND* tone = static_cast<ATS_SOUND*>(std::calloc(1, sizeof(ATS_SOUND)));
    const int frames = 40;
    init_sound(tone, 48000, 480, 1920, frames, frames * 0.01, 1, 0);
    for (int f = 0; f < frames; ++f)
    {
        tone->time[0][f] = f * 0.01;
        tone->amp[0][f] = 0.5;
        tone->frq[0][f] = 440.0;
        tone->pha[0][f] = tone->smr[0][f] = tone->res[0][f] = 0.0;
        tone->frq_slope[0][f] = tone->amp_slope[0][f] = 0.0;
    }
    ats::AtsSoundPtr owned(tone);
    const auto crossingsPerSecond = [&](const ats::SoundCurves& curves) {
        ats::SynthParams synth;
        synth.sampleRate = 48000.0;
        synth.curves = curves;
        std::vector<float> buffer(48000, 0.0f);
        float* channel = buffer.data();
        ats::CancellationToken live;
        const auto rendered = ats::AtsEngine::render({ 1 }, live, tone, synth,
                                                     { &channel, 1, static_cast<std::uint32_t>(buffer.size()) });
        assert(rendered.status == ats::JobResult<ats::RenderStats>::Status::success);
        const auto n = rendered.value.framesWritten;
        std::size_t crossings = 0;
        for (std::size_t i = 1; i < n; ++i)
            crossings += (buffer[i - 1] < 0.0f) != (buffer[i] < 0.0f);
        return 0.5 * crossings / (static_cast<double>(n) / 48000.0);
    };
    ats::SoundCurves flat;
    ats::SoundCurves octave;
    octave.pitchSemitones.points = { { 0.0f, 12.0f } };
    const auto base = crossingsPerSecond(flat);
    const auto up = crossingsPerSecond(octave);
    assert(std::abs(base - 440.0) < 15.0);
    assert(std::abs(up - 880.0) < 25.0);

    std::cout << "host_sound_curves_test: ok (" << base << " Hz -> " << up << " Hz)\n";
    return 0;
}
