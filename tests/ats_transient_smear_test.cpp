#include "AtsTransientSmear.h"

#include <algorithm>
#include <cmath>
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
    std::vector<std::vector<double>> phaRows;
    std::vector<std::vector<double>> bandRows; // size 0 or ATSA_CRITICAL_BANDS
    std::vector<double*> ampPtrs;
    std::vector<double*> phaPtrs;
    std::vector<double*> bandPtrs;
    ATS_SOUND sound {};

    void setPartials(std::vector<std::vector<double>> rows)
    {
        ampRows = std::move(rows);
        frames = ampRows.empty() ? frames : (int) ampRows.front().size();
    }

    void setPhases(std::vector<std::vector<double>> rows)
    {
        phaRows = std::move(rows);
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

        phaPtrs.clear();
        for (auto& r : phaRows)
            phaPtrs.push_back(r.data());
        sound.pha = phaPtrs.empty() ? nullptr : phaPtrs.data();

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

double windowSum(const std::vector<double>& row, int start, int end)
{
    double sum = 0.0;
    for (int f = start; f <= end; ++f)
        sum += row[(size_t) f];
    return sum;
}

double maxWindowSlope(const std::vector<double>& row, int start, int end)
{
    double slope = 0.0;
    for (int f = start + 1; f <= end; ++f)
        slope = std::max(slope, std::abs(row[(size_t) f] - row[(size_t) (f - 1)]));
    return slope;
}

double maxWindowValue(const std::vector<double>& row, int start, int end)
{
    double peak = 0.0;
    for (int f = start; f <= end; ++f)
        peak = std::max(peak, row[(size_t) f]);
    return peak;
}

bool near(double a, double b, double tolerance)
{
    return std::abs(a - b) <= tolerance;
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

    // 7. Blur spreads attack energy forward past the peak, conserving energy over the
    //    blur-extended window and never increasing the steepest slope.
    {
        ModelBuilder m;
        std::vector<double> row((size_t) 32, 0.0);
        for (int f = 8; f <= 13; ++f)
            row[(size_t) f] = 1.0;
        const auto before = row;
        m.setPartials({ row });
        auto* sound = m.build();

        ats::SmearnovParams smear;
        smear.blurMs = 12.0; // hop = 256/48000 s -> sigma ~= 2.25 frames -> reach ceil(3*sigma) = 7
        smear.targets = ats::SmearnovTarget::Partials;
        const ats::AtsOnset onset { 8, 4, 13, 1.0 }; // peakFrame 13 -> extended end 20
        if (!ats::smearAttacks(sound, { onset }, smear))
            return fail("blur smear should report a mutation");

        const int extendedEnd = 20;
        const std::vector<double> after(sound->amp[0], sound->amp[0] + 32);
        if (!near(windowSum(before, 4, extendedEnd), windowSum(after, 4, extendedEnd), 1.0e-9))
            return fail("blur should conserve energy over the blur-extended window");
        if (windowSum(after, 14, extendedEnd) <= 1.0e-6)
            return fail("blur should spread attack energy forward past the peak");
        if (maxWindowSlope(after, 4, extendedEnd) > maxWindowSlope(before, 4, extendedEnd) + 1.0e-9)
            return fail("blur should not increase the maximum in-window slope");
        if (!near(after[3], before[3], 0.0) || !near(after[(size_t) (extendedEnd + 1)],
                                                      before[(size_t) (extendedEnd + 1)], 0.0))
            return fail("blur should not edit frames outside the blur-extended window");
    }

    // 8. Sharpen is bounded by the original peak when exaggerate is off.
    {
        ModelBuilder m;
        std::vector<double> row((size_t) 16, 0.0);
        for (int f = 4; f <= 10; ++f)
            row[(size_t) f] = 0.15 * (double) (f - 3);
        const auto before = row;
        m.setPartials({ row });
        auto* sound = m.build();

        ats::SmearnovParams smear;
        smear.sharpenAmount = 8.0;
        smear.targets = ats::SmearnovTarget::Partials;
        const ats::AtsOnset onset { 4, 4, 10, 1.0 };
        if (!ats::smearAttacks(sound, { onset }, smear))
            return fail("sharpen smear should report a mutation");

        const std::vector<double> after(sound->amp[0], sound->amp[0] + 16);
        if (maxWindowValue(after, 4, 10) > maxWindowValue(before, 4, 10) + 1.0e-9)
            return fail("sharpen should clamp to the original peak without exaggerate");
    }

    // 9. Blur also preserves sum when the finite-window kernel hits the peak cap.
    {
        ModelBuilder m;
        std::vector<double> row {
            0.732352769848356,
            0.07307038424536816,
            0.21909386750684778,
            0.5288807962908402,
            0.8694801786408357,
            0.885406342966534,
            0.8884887543033256
        };
        const auto before = row;
        m.setPartials({ row });
        auto* sound = m.build();

        ats::SmearnovParams smear;
        smear.blurMs = 1000.0 * (double) m.frameSize / (double) m.srate; // sigma ~= 1 frame
        smear.targets = ats::SmearnovTarget::Partials;
        const ats::AtsOnset onset { 0, 0, 6, 1.0 };
        if (!ats::smearAttacks(sound, { onset }, smear))
            return fail("peak-capped blur should report a mutation");

        const std::vector<double> after(sound->amp[0], sound->amp[0] + 7);
        if (!near(windowSum(before, 0, 6), windowSum(after, 0, 6), 1.0e-9))
            return fail("peak-capped blur should conserve in-window amplitude sum");
        if (maxWindowValue(after, 0, 6) > maxWindowValue(before, 0, 6) + 1.0e-9)
            return fail("peak-capped blur should stay within the original window peak");
    }

    // 10. Exaggerate respects the max-dB clamp for partial amplitudes and residual energy.
    {
        ModelBuilder m;
        std::vector<double> row((size_t) 16, 0.2);
        for (int f = 4; f <= 10; ++f)
            row[(size_t) f] = 0.1 * (double) (f - 3);
        m.setPartials({ row });
        m.setResidualBand(5, row);
        auto* sound = m.build();

        ats::SmearnovParams smear;
        smear.exaggerateDb = 24.0;
        smear.maxDb = 6.0;
        smear.targets = ats::SmearnovTarget::Both;
        const ats::AtsOnset onset { 4, 4, 10, 1.0 };
        if (!ats::smearAttacks(sound, { onset }, smear))
            return fail("exaggerate smear should report a mutation");

        const std::vector<double> ampAfter(sound->amp[0], sound->amp[0] + 16);
        const std::vector<double> bandAfter(sound->band_energy[5], sound->band_energy[5] + 16);
        const double ampBound = 0.7 * std::pow(10.0, 6.0 / 20.0);
        const double energyBound = 0.7 * std::pow(10.0, 6.0 / 10.0);
        if (maxWindowValue(ampAfter, 4, 10) > ampBound + 1.0e-9)
            return fail("partial exaggerate should respect the max-dB clamp");
        if (maxWindowValue(bandAfter, 4, 10) > energyBound + 1.0e-9)
            return fail("residual exaggerate should respect the max-dB energy clamp");
        if (!(ampAfter[10] > row[10] && bandAfter[10] > row[10]))
            return fail("positive exaggerate should boost the attack peak");
    }

    // 11. Target gating leaves residual rows alone when only partials are selected.
    {
        ModelBuilder m;
        std::vector<double> row((size_t) 16, 0.0);
        for (int f = 6; f <= 10; ++f)
            row[(size_t) f] = 1.0;
        m.setPartials({ row });
        m.setResidualBand(7, row);
        auto* sound = m.build();

        ats::SmearnovParams smear;
        smear.blurMs = 8.0;
        smear.targets = ats::SmearnovTarget::Partials;
        const ats::AtsOnset onset { 6, 4, 10, 1.0 };
        if (!ats::smearAttacks(sound, { onset }, smear))
            return fail("partials-only smear should mutate partial amplitudes");

        for (int f = 0; f < 16; ++f)
            if (!near(sound->band_energy[7][f], row[(size_t) f], 0.0))
                return fail("partials-only smear should leave residual bands untouched");
    }

    // 12. Phase arrays are never edited.
    {
        ModelBuilder m;
        std::vector<double> row((size_t) 16, 0.0);
        std::vector<double> phase((size_t) 16, 0.0);
        for (int f = 0; f < 16; ++f)
        {
            row[(size_t) f] = f >= 4 && f <= 10 ? 0.1 * (double) (f - 3) : 0.0;
            phase[(size_t) f] = 0.25 * (double) f;
        }
        m.setPartials({ row });
        m.setPhases({ phase });
        auto* sound = m.build();

        ats::SmearnovParams smear;
        smear.blurMs = 8.0;
        smear.sharpenAmount = 1.5;
        smear.exaggerateDb = 6.0;
        smear.targets = ats::SmearnovTarget::Partials;
        const ats::AtsOnset onset { 4, 4, 10, 1.0 };
        (void) ats::smearAttacks(sound, { onset }, smear);

        for (int f = 0; f < 16; ++f)
            if (!near(sound->pha[0][f], phase[(size_t) f], 0.0))
                return fail("smear transforms must leave phase arrays untouched");
    }

    // 11. Dry/wet mix blends linearly between the dry envelope and the full effect.
    {
        std::vector<double> before((size_t) 32, 0.0);
        for (int f = 8; f <= 13; ++f)
            before[(size_t) f] = 1.0;
        const ats::AtsOnset onset { 8, 4, 13, 1.0 };

        ats::SmearnovParams base;
        base.blurMs = 12.0;
        base.targets = ats::SmearnovTarget::Partials;

        // Full effect (mix defaults to 1.0).
        ModelBuilder wetM;
        wetM.setPartials({ before });
        auto* wetSound = wetM.build();
        if (!ats::smearAttacks(wetSound, { onset }, base))
            return fail("mix: full-effect smear should report a mutation");
        const std::vector<double> wet(wetSound->amp[0], wetSound->amp[0] + 32);

        // Fully dry: identity, no mutation reported.
        ModelBuilder dryM;
        dryM.setPartials({ before });
        auto* drySound = dryM.build();
        ats::SmearnovParams dryParams = base;
        dryParams.mix = 0.0;
        if (ats::smearAttacks(drySound, { onset }, dryParams))
            return fail("mix=0 should be a no-op (fully dry)");
        for (int f = 0; f < 32; ++f)
            if (!near(drySound->amp[0][f], before[(size_t) f], 1.0e-12))
                return fail("mix=0 must leave the dry envelope untouched");

        // Halfway: each frame is the midpoint of dry and the full effect.
        ModelBuilder halfM;
        halfM.setPartials({ before });
        auto* halfSound = halfM.build();
        ats::SmearnovParams halfParams = base;
        halfParams.mix = 0.5;
        if (!ats::smearAttacks(halfSound, { onset }, halfParams))
            return fail("mix=0.5 should report a mutation");
        for (int f = 0; f < 32; ++f)
        {
            const double expected = 0.5 * (before[(size_t) f] + wet[(size_t) f]);
            if (!near(halfSound->amp[0][f], expected, 1.0e-9))
                return fail("mix=0.5 must be the midpoint of dry and the full effect");
        }
    }

    return 0;
}
