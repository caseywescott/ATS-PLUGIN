#include "AtsTransientSmear.h"

#include <iostream>
#include <vector>

namespace
{
int fail(const char* message)
{
    std::cerr << "ats_transient_smear_test: " << message << '\n';
    return 1;
}

// Owns row storage and exposes a configured ATS_SOUND for detection tests.
struct ModelBuilder
{
    int srate = 48000;
    int frameSize = 256;
    int frames = 0;
    std::vector<std::vector<double>> ampRows;
    std::vector<std::vector<double>> bandRows; // size 0 or ATSA_CRITICAL_BANDS
    std::vector<double*> ampPtrs;
    std::vector<double*> bandPtrs;
    ATS_SOUND sound {};

    void setPartials(std::vector<std::vector<double>> rows)
    {
        ampRows = std::move(rows);
        frames = ampRows.empty() ? frames : (int) ampRows.front().size();
    }

    void setResidualBand(int band, std::vector<double> energy)
    {
        if (bandRows.empty())
            bandRows.assign(ATSA_CRITICAL_BANDS, std::vector<double>((size_t) frames, 0.0));
        bandRows[(size_t) band] = std::move(energy);
    }

    ATS_SOUND* build()
    {
        sound = ATS_SOUND {};
        sound.srate = srate;
        sound.frame_size = frameSize;
        sound.frames = frames;
        sound.partials = (int) ampRows.size();

        ampPtrs.clear();
        for (auto& r : ampRows)
            ampPtrs.push_back(r.data());
        sound.amp = ampPtrs.empty() ? nullptr : ampPtrs.data();

        if (!bandRows.empty())
        {
            bandPtrs.clear();
            for (auto& r : bandRows)
                bandPtrs.push_back(r.data());
            sound.band_energy = bandPtrs.data();
        }
        return &sound;
    }
};

std::vector<double> ramp(int frames, int stepFrame, double before, double after)
{
    std::vector<double> v((size_t) frames, before);
    for (int f = stepFrame; f < frames; ++f)
        v[(size_t) f] = after;
    return v;
}

bool nearFrame(const std::vector<ats::AtsOnset>& onsets, int expected, int tol = 1)
{
    for (const auto& o : onsets)
        if (std::abs(o.frame - expected) <= tol)
            return true;
    return false;
}
} // namespace

int main()
{
    const ats::SmearnovDetectParams params; // defaults

    // 1. Flat signal -> no onsets.
    {
        ModelBuilder m;
        m.setPartials({ std::vector<double>(40, 0.4) });
        const auto onsets = ats::detectOnsets(m.build(), params);
        if (!onsets.empty())
            return fail("flat signal should yield no onsets");
    }

    // 2. Single amplitude step (active throughout: pure flux, no birth).
    {
        ModelBuilder m;
        m.setPartials({ ramp(40, 20, 0.2, 0.9) });
        const auto onsets = ats::detectOnsets(m.build(), params);
        if (onsets.size() != 1)
            return fail("amplitude step should yield exactly one onset");
        if (!nearFrame(onsets, 20))
            return fail("onset should localize at the amplitude step");
        const auto& o = onsets.front();
        if (!(o.preFrame <= o.frame && o.frame <= o.peakFrame))
            return fail("attack window must satisfy preFrame <= frame <= peakFrame");
    }

    // 3. Partial birth (inactive -> active) is an onset.
    {
        ModelBuilder m;
        m.setPartials({ ramp(40, 15, 0.0, 0.7) });
        const auto onsets = ats::detectOnsets(m.build(), params);
        if (onsets.empty() || !nearFrame(onsets, 15))
            return fail("partial birth should produce an onset at the birth frame");
    }

    // 4. Residual band-energy jump with no partials.
    {
        ModelBuilder m;
        m.frames = 40;
        m.setResidualBand(18, ramp(40, 25, 0.01, 0.9)); // a high band steps up
        const auto onsets = ats::detectOnsets(m.build(), params);
        if (onsets.empty() || !nearFrame(onsets, 25))
            return fail("residual energy jump should produce an onset");
    }

    // 5a. Two well-separated onsets -> both found.
    {
        ModelBuilder m;
        std::vector<double> row((size_t) 50, 0.2);
        for (int f = 10; f < 50; ++f) row[(size_t) f] = 0.6;
        for (int f = 35; f < 50; ++f) row[(size_t) f] = 0.95;
        m.setPartials({ row });
        const auto onsets = ats::detectOnsets(m.build(), params);
        if (onsets.size() != 2)
            return fail("two separated steps should yield two onsets");
        if (!nearFrame(onsets, 10) || !nearFrame(onsets, 35))
            return fail("both onsets should localize to their steps");
    }

    // 5b. Two onsets closer than the refractory gap -> collapse to one.
    {
        ModelBuilder m;
        std::vector<double> row((size_t) 40, 0.2);
        for (int f = 10; f < 40; ++f) row[(size_t) f] = 0.6;
        for (int f = 13; f < 40; ++f) row[(size_t) f] = 0.95;
        m.setPartials({ row });
        const auto onsets = ats::detectOnsets(m.build(), params);
        if (onsets.size() != 1)
            return fail("onsets within the gap should collapse to one");
    }

    // 6. Detection-function output is populated and band centres are monotonic.
    {
        ModelBuilder m;
        m.setPartials({ ramp(40, 20, 0.2, 0.9) });
        ats::SmearnovDetectionFunctions fns;
        const auto fnOnsets = ats::detectOnsets(m.build(), params, &fns);
        (void) fnOnsets;
        if ((int) fns.combined.size() != 40 || (int) fns.threshold.size() != 40)
            return fail("detection functions should be returned per frame");

        double prev = -1.0;
        for (int b = 0; b < ATSA_CRITICAL_BANDS; ++b)
        {
            const double c = ats::criticalBandCentreHz(b);
            if (!(c > prev))
                return fail("band centre frequencies must be strictly increasing");
            prev = c;
        }
    }

    return 0;
}
