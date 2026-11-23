#include "AtsTimeEdit.h"

#include <cassert>
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace
{
// partials x frames; partial p, frame f: amp = (f + 1) / 100, frq = 100 * (p + 1) + f.
ats::AtsSoundPtr make(int partials, int frames, double ampScale = 1.0)
{
    auto* s = static_cast<ATS_SOUND*>(std::calloc(1, sizeof(ATS_SOUND)));
    init_sound(s, 44100, 441, 1764, frames, frames * 0.01, partials, 1);
    for (int f = 0; f < frames; ++f)
    {
        s->time[0][f] = f * 0.01;
        for (int p = 0; p < partials; ++p)
        {
            s->amp[p][f] = ampScale * (f + 1) / 100.0;
            s->frq[p][f] = 100.0 * (p + 1) + f;
            s->pha[p][f] = s->smr[p][f] = s->res[p][f] = 0.0;
            s->frq_slope[p][f] = s->amp_slope[p][f] = 0.0;
        }
        for (int b = 0; b < ATSA_CRITICAL_BANDS; ++b)
            s->band_energy[b][f] = ampScale * (b + 1) * 0.001;
    }
    return ats::AtsSoundPtr(s);
}
bool near(double a, double b) { return std::abs(a - b) < 1.0e-9; }
}

int main()
{
    const auto base = make(2, 10);
    const auto* s = base.get();

    // copy / crop keep frames [2, 5] and regenerate the time axis.
    const auto clip = ats::AtsTimeEdit::copy(s, 5, 2); // reversed range is normalised
    assert(clip && clip.get()->frames == 4 && clip.get()->partials == 2);
    assert(near(clip.get()->amp[0][0], s->amp[0][2]) && near(clip.get()->frq[1][3], s->frq[1][5]));
    assert(near(clip.get()->time[0][0], 0.0) && near(clip.get()->time[0][3], 3 * 441.0 / 44100.0));
    assert(near(clip.get()->dur, 4 * 0.01));
    assert(near(clip.get()->band_energy[3][1], s->band_energy[3][3]));
    assert(ats::AtsTimeEdit::crop(s, 2, 5).get()->frames == 4);

    // remove drops frames [3, 6]; frame 7 moves to 3.
    const auto removed = ats::AtsTimeEdit::remove(s, 3, 6);
    assert(removed && removed.get()->frames == 6);
    assert(near(removed.get()->frq[0][3], s->frq[0][7]));
    assert(!ats::AtsTimeEdit::remove(s, 0, 9)); // nothing would remain

    // paste without crossfade inserts the clip; later frames shift right.
    const auto pasted = ats::AtsTimeEdit::paste(s, clip.get(), 4, 0);
    assert(pasted && pasted.get()->frames == 14);
    assert(near(pasted.get()->frq[0][3], s->frq[0][3]));
    assert(near(pasted.get()->frq[0][4], clip.get()->frq[0][0]));
    assert(near(pasted.get()->frq[0][8], s->frq[0][4]));

    // A crossfade blends only the seam frames: equal-power amplitude, linear energy.
    const auto loud = make(2, 4, 10.0); // clip ten times louder
    const auto faded = ats::AtsTimeEdit::paste(s, loud.get(), 4, 2);
    assert(faded && faded.get()->frames == 14);
    const auto* o = faded.get();
    // First seam frame: neighbour weight 2/3 -> clip gain sin(pi/6) = 0.5.
    const double gIn = std::sin((1.0 / 3.0) * 1.5707963267948966);
    const double gOut = std::cos((1.0 / 3.0) * 1.5707963267948966);
    assert(near(o->amp[0][4], gOut * s->amp[0][3] + gIn * loud.get()->amp[0][0]));
    assert(near(o->band_energy[2][4], (2.0 / 3.0) * s->band_energy[2][3] + (1.0 / 3.0) * loud.get()->band_energy[2][0]));
    assert(near(o->frq[0][4], (2.0 / 3.0) * s->frq[0][3] + (1.0 / 3.0) * loud.get()->frq[0][0]));
    // Frames inside the clip but outside both seams are untouched... (clip of 4, fade 2: all seam)
    // ...and frames outside the clip are unchanged.
    assert(near(o->amp[0][2], s->amp[0][2]) && near(o->amp[0][9], s->amp[0][5]));

    // No glide toward 0 Hz: a silent neighbour leaves the clip's frequency.
    auto silent = make(2, 10);
    for (int p = 0; p < 2; ++p) { silent.get()->amp[p][3] = 0.0; silent.get()->frq[p][3] = 0.0; }
    const auto noGlide = ats::AtsTimeEdit::paste(silent.get(), loud.get(), 4, 2);
    assert(near(noGlide.get()->frq[0][4], loud.get()->frq[0][0]));

    // Partial counts must match.
    const auto three = make(3, 4);
    assert(!ats::AtsTimeEdit::paste(s, three.get(), 2, 0));

    std::cout << "ats_time_edit_test: ok\n";
    return 0;
}
