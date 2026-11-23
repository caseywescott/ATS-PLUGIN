#include "AtsDescriptors.h"

#include <cassert>
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace
{
// `partials` harmonics of f0 with amp 1/h, over `frames` frames.  `detune` is a
// relative offset applied alternately: odd harmonics down, even harmonics up
// (a uniform detune would simply be absorbed by the Serra/Bonada f0).
ats::AtsSoundPtr makeHarmonic(double f0, int partials, int frames, double detune = 0.0, bool noise = false)
{
    auto* s = static_cast<ATS_SOUND*>(std::calloc(1, sizeof(ATS_SOUND)));
    init_sound(s, 44100, 441, 1764, frames, frames * 0.01, partials, noise ? 1 : 0);
    for (int f = 0; f < frames; ++f)
    {
        s->time[0][f] = f * 0.01;
        for (int p = 0; p < partials; ++p)
        {
            const auto h = p + 1;
            s->amp[p][f] = 1.0 / h;
            s->frq[p][f] = f0 * h * (1.0 + (h % 2 == 0 ? detune : -detune));
        }
        if (noise)
            for (int b = 0; b < ATSA_CRITICAL_BANDS; ++b)
                s->band_energy[b][f] = 0.0;
    }
    return ats::AtsSoundPtr(s);
}
bool near(double a, double b, double tol) { return std::abs(a - b) < tol; }
}

int main()
{
    const double f0 = 200.0;
    const int n = 6;
    const auto clean = makeHarmonic(f0, n, 5);
    const auto track = ats::describeSound(clean.get());
    assert(track.frames.size() == 5);
    assert(near(track.fundamentalHz, f0, 1.0e-9));
    const auto& d = track.frames[2];
    assert(d.active);

    double sumA = 0.0, sumHA = 0.0, sumFA = 0.0, sumA2 = 0.0;
    for (int h = 1; h <= n; ++h)
    {
        sumA += 1.0 / h;
        sumHA += h * (1.0 / h);
        sumFA += f0 * h * (1.0 / h);
        sumA2 += 1.0 / (h * h);
    }
    assert(near(d.f0Hz, f0, 1.0e-6));
    assert(near(d.centroidHz, sumFA / sumA, 1.0e-6));
    assert(near(d.harmonicRankCentroid, sumHA / sumA, 1.0e-9));
    assert(near(d.tristimulus[0] + d.tristimulus[1] + d.tristimulus[2], 1.0, 1.0e-9));
    assert(near(d.tristimulus[0], 1.0 / sumA2, 1.0e-9));
    assert(d.oddEvenRatio > 1.0);
    assert(near(d.tiltDbPerOctave, -6.02, 0.5));
    assert(near(d.inharmonicity, 0.0, 1.0e-9) && near(d.harmonicDistortionHz, 0.0, 1.0e-6));
    assert(near(d.noisiness, 0.0, 1.0e-12) && d.residualEnergy == 0.0);
    assert(d.centroidBark > 0.0 && near(d.centroidBark, ats::hzToBark(d.centroidHz), 2.0));
    assert(d.spreadHz > 0.0 && d.skewness > 0.0); // 1/h weighting: long tail upward
    assert(near(track.centroidHz.mean, d.centroidHz, 1.0e-9) && near(track.centroidHz.stddev, 0.0, 1.0e-9));
    assert(near(track.attackCentroidHz, d.centroidHz, 1.0e-6));

    // Subband centroids: 200 -> band 2 [200,300), 400 -> band 4 [400,510), 600 -> band 5 [510,630),
    // 800 -> band 7 [770,920), 1000 -> band 8 [920,1080), 1200 -> band 9 [1080,1270).
    assert(near(d.subbandCentroidHz[2], 200.0, 1.0e-9));
    assert(near(d.subbandCentroidHz[4], 400.0, 1.0e-9));
    assert(near(d.subbandCentroidHz[5], 600.0, 1.0e-9));
    assert(near(d.subbandCentroidHz[7], 800.0, 1.0e-9));
    assert(near(d.subbandCentroidHz[8], 1000.0, 1.0e-9));
    assert(near(d.subbandCentroidHz[9], 1200.0, 1.0e-9));
    assert(d.subbandCentroidHz[0] == 0.0 && d.subbandCentroidHz[3] == 0.0 && d.subbandCentroidHz[24] == 0.0);

    // Band energy raises noisiness.
    auto noisy = makeHarmonic(f0, n, 5, 0.0, true);
    for (int b = 0; b < ATSA_CRITICAL_BANDS; ++b)
        noisy.get()->band_energy[b][2] = 1764 * 0.04 * 0.1; // 0.1 RMS^2 per band
    const auto dn = ats::describeFrame(noisy.get(), 2, f0);
    assert(near(dn.residualEnergy, 2.5, 1.0e-9));
    assert(dn.noisiness > 0.5 && near(dn.noisiness, 2.5 / (2.5 + sumA2), 1.0e-9));
    assert(ats::describeFrame(noisy.get(), 1, f0).noisiness == 0.0);

    // Alternating +/-1 % detune -> inharmonicity ~ 0.01 (f0 settles between the two groups).
    const auto detuned = makeHarmonic(f0, n, 3, 0.01);
    const auto dd = ats::describeFrame(detuned.get(), 1, f0);
    assert(near(dd.inharmonicity, 0.01, 0.003));
    assert(dd.harmonicDistortionHz > 1.0);

    // Out of range / empty.
    assert(!ats::describeFrame(clean.get(), 99, f0).active);
    assert(ats::describeSound(nullptr).frames.empty());
    assert(near(ats::hzToBark(1000.0), 13.0 * std::atan(0.76) + 3.5 * std::atan((1000.0 / 7500.0) * (1000.0 / 7500.0)), 1.0e-12));

    std::cout << "ats_descriptors_test: ok\n";
    return 0;
}
