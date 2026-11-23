#include "AtsAutomationCurves.h"

#include <cassert>
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace
{
// Two partials (200 / 300 Hz, rising 1 Hz per frame), amplitude = frame index.
ats::AtsSoundPtr make(int frames)
{
    auto* s = static_cast<ATS_SOUND*>(std::calloc(1, sizeof(ATS_SOUND)));
    init_sound(s, 44100, 441, 1764, frames, frames * 0.01, 2, 1);
    for (int f = 0; f < frames; ++f)
    {
        s->time[0][f] = f * 0.01;
        for (int p = 0; p < 2; ++p)
        {
            s->amp[p][f] = 0.01 * (f + 1);
            s->frq[p][f] = 200.0 + 100.0 * p + f;
            s->pha[p][f] = 1.0;
            s->smr[p][f] = s->res[p][f] = s->frq_slope[p][f] = s->amp_slope[p][f] = 0.0;
        }
        for (int b = 0; b < ATSA_CRITICAL_BANDS; ++b)
            s->band_energy[b][f] = 0.001 * f;
    }
    return ats::AtsSoundPtr(s);
}
bool near(double a, double b, double eps = 1.0e-9) { return std::abs(a - b) < eps; }
}

int main()
{
    // Curve evaluation and normalisation.
    ats::AutomationCurve curve;
    assert(curve.valueAt(0.3, 7.0) == 7.0);
    curve.points = { { 1.0f, 10.0f }, { 0.0f, 0.0f }, { 0.5f, 50.0f } };
    curve.normalise(-100.0f, 40.0f);
    assert(curve.points.front().x == 0.0f && curve.points.back().x == 1.0f);
    assert(curve.points[1].y == 40.0f); // clamped
    assert(near(curve.valueAt(0.25, 0.0), 20.0));
    assert(near(curve.valueAt(-1.0, 0.0), 0.0) && near(curve.valueAt(2.0, 0.0), 10.0));

    // Inactive curves leave the model alone.
    ats::SoundCurves none;
    assert(!none.active());
    none.timeWarp.points = { { 0.0f, 0.0f }, { 1.0f, 1.0f } };
    none.pitchSemitones.points = { { 0.0f, 0.0f } };
    none.spectralWarpGamma.points = { { 0.5f, 1.0f } };
    assert(!none.active());
    const auto base = make(11);
    assert(!ats::AtsAutomationCurves::apply(base.get(), none));

    // +12 semitones doubles every frequency; amplitudes and frame count kept,
    // phases cleared.
    ats::SoundCurves octave;
    octave.pitchSemitones.points = { { 0.0f, 12.0f } };
    const auto up = ats::AtsAutomationCurves::apply(base.get(), octave);
    assert(up && up.get()->frames == 11);
    assert(near(up.get()->frq[0][3], 2.0 * base.get()->frq[0][3]));
    assert(near(up.get()->amp[1][7], base.get()->amp[1][7]) && up.get()->pha[0][2] == 0.0);
    assert(!up.get()->has_slopes);

    // A pitch ramp: 0 at the start, +12 at the end.
    ats::SoundCurves ramp;
    ramp.pitchSemitones.points = { { 0.0f, 0.0f }, { 1.0f, 12.0f } };
    const auto ramped = ats::AtsAutomationCurves::apply(base.get(), ramp);
    assert(near(ramped.get()->frq[0][0], base.get()->frq[0][0]));
    assert(near(ramped.get()->frq[0][10], 2.0 * base.get()->frq[0][10]));
    assert(near(ramped.get()->frq[0][5], base.get()->frq[0][5] * std::pow(2.0, 0.5)));

    // Reversed time: output frame k reads analysis frame 10 - k.
    ats::SoundCurves reverse;
    reverse.timeWarp.points = { { 0.0f, 1.0f }, { 1.0f, 0.0f } };
    const auto backwards = ats::AtsAutomationCurves::apply(base.get(), reverse);
    assert(near(backwards.get()->frq[1][0], base.get()->frq[1][10]));
    assert(near(backwards.get()->amp[0][2], base.get()->amp[0][8]));
    assert(near(backwards.get()->band_energy[4][1], base.get()->band_energy[4][9]));
    // Half speed over the first half: output 0.5 reads analysis 0.25 (frame 2.5).
    ats::SoundCurves slow;
    slow.timeWarp.points = { { 0.0f, 0.0f }, { 0.5f, 0.25f }, { 1.0f, 1.0f } };
    const auto slowed = ats::AtsAutomationCurves::apply(base.get(), slow);
    assert(near(slowed.get()->frq[0][5], 0.5 * (base.get()->frq[0][2] + base.get()->frq[0][3])));

    // Spectral warp: gamma 2 about a 200 Hz anchor squares the ratio.
    ats::SoundCurves warp;
    warp.spectralWarpGamma.points = { { 0.0f, 2.0f } };
    warp.warpAnchorHz = 200.0;
    const auto warped = ats::AtsAutomationCurves::apply(base.get(), warp);
    const auto f = base.get()->frq[1][0]; // 300 Hz
    assert(near(warped.get()->frq[1][0], 200.0 * std::pow(f / 200.0, 2.0)));
    assert(near(warped.get()->frq[0][0], 200.0)); // the anchor itself does not move
    // The automatic anchor is the median lowest active partial.
    assert(near(ats::AtsAutomationCurves::estimateFundamental(base.get()), 205.0));

    std::cout << "ats_automation_curves_test: ok\n";
    return 0;
}
