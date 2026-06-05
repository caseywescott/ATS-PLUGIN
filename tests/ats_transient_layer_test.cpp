#include "AtsTransientLayer.h"
#include "AtsTransientSmear.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

namespace
{
int fail(const char* message)
{
    std::cerr << "ats_transient_layer_test: " << message << '\n';
    return 1;
}

// Owns row storage and exposes a configured ATS_SOUND (no time array: hop falls
// back to frame_size / srate).
struct ModelBuilder
{
    int srate = 48000;
    int frameSize = 256;
    int frames = 0;
    std::vector<std::vector<double>> ampRows;
    std::vector<std::vector<double>> bandRows;
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

// Flat `before`, then a linear ramp over `rampFrames` to `after` at `stepFrame`.
std::vector<double> attack(int frames, int stepFrame, int rampFrames, double before, double after)
{
    std::vector<double> v((size_t) frames, before);
    for (int f = stepFrame; f < frames; ++f)
    {
        const int k = f - stepFrame;
        const double t = rampFrames > 0 ? std::min(1.0, (double) k / (double) rampFrames) : 1.0;
        v[(size_t) f] = before + t * (after - before);
    }
    return v;
}

double totalEnergy(const std::vector<double>& x)
{
    double e = 0.0;
    for (double v : x)
        e += v * v;
    return e;
}
} // namespace

int main()
{
    const int srate = 48000;
    const int frameSize = 256;
    const double hop = (double) frameSize / (double) srate;
    const int frames = 64;
    const int stepFrame = 24;

    // --- A stepped attack: partials + several residual bands rise at frame 24. ---
    ModelBuilder m;
    m.srate = srate;
    m.frameSize = frameSize;
    m.setPartials({
        attack(frames, stepFrame, 3, 0.05, 0.50),
        attack(frames, stepFrame, 3, 0.03, 0.30),
    });
    m.setResidualBand(8, attack(frames, stepFrame, 3, 0.01, 0.40));
    m.setResidualBand(12, attack(frames, stepFrame, 3, 0.01, 0.55));
    m.setResidualBand(16, attack(frames, stepFrame, 3, 0.01, 0.30));
    ATS_SOUND* sound = m.build();

    // 1. Onset detection fires near the step.
    const ats::SmearnovDetectParams dparams;
    const auto onsets = ats::detectOnsets(sound, dparams);
    if (onsets.empty())
        return fail("expected at least one onset");
    bool nearStep = false;
    for (const auto& o : onsets)
        if (std::abs(o.frame - stepFrame) <= 2)
            nearStep = true;
    if (!nearStep)
        return fail("no onset within 2 frames of the step");

    // 2. Extraction yields a non-empty, dynamic-range-bounded descriptor.
    const auto events = ats::extractTransientLayer(sound, onsets);
    if (events.size() != onsets.size())
        return fail("extract should yield one event per onset");
    const auto& ev = events.front();
    const double targetEnergy = ats::transientEventEnergy(ev);
    if (!(targetEnergy > 0.0))
        return fail("event energy should be positive for a real attack");
    if (std::abs(ev.timeSeconds - (double) ev.frameIndex * hop) > 1.0e-9)
        return fail("event time should match the onset frame time");

    // Each band/det descriptor must be bounded by its in-window maximum.
    auto bandMaxInWindow = [&](int b) {
        double mx = 0.0;
        for (int f = ev.preFrame; f <= ev.peakFrame; ++f)
            mx = std::max(mx, m.bandRows[(size_t) b][(size_t) f]);
        return mx;
    };
    for (int b = 0; b < ATSA_CRITICAL_BANDS; ++b)
        if (ev.bandEnergy[(size_t) b] > bandMaxInWindow(b) + 1.0e-12)
            return fail("band descriptor exceeds the in-window maximum");

    // 3. Render: total energy matches the target (gain 1) -> boundedness.
    ats::TransientLayerParams rparams;
    const int nSamples = frames * frameSize;
    std::vector<double> out((size_t) nSamples, 0.0);
    if (!ats::renderTransientLayer(events, rparams, srate, out.data(), nSamples))
        return fail("renderTransientLayer failed");

    const double rendered = totalEnergy(out);
    if (rendered > targetEnergy * 1.02 + 1.0e-12)
        return fail("rendered energy exceeds the extracted (model-bounded) energy");
    if (rendered < targetEnergy * 0.90)
        return fail("rendered energy fell well short of the target");

    // 4. Onset-time preservation: |out| peaks around the onset, not smeared.
    const int onsetSample = (int) std::lround(ev.timeSeconds * srate);
    const int attackSamples = std::max(1, (int) std::lround(rparams.attackSeconds * srate));
    int decaySamples = (int) std::lround(ev.windowSeconds * srate);
    decaySamples = std::clamp(decaySamples, 1, (int) std::lround(rparams.maxDecaySeconds * srate));
    int argmax = 0;
    double peakAbs = 0.0;
    for (int n = 0; n < nSamples; ++n)
        if (std::abs(out[(size_t) n]) > peakAbs)
        {
            peakAbs = std::abs(out[(size_t) n]);
            argmax = n;
        }
    if (argmax < onsetSample - attackSamples - 2 || argmax > onsetSample + decaySamples / 2)
        return fail("transient energy peak is not localized at the onset");

    // 5. Continuity / no clicks: compact support, tapered edges.
    const int begin = onsetSample - attackSamples;
    const int end = onsetSample + decaySamples;
    for (int n = 0; n < nSamples; ++n)
        if ((n < begin || n >= end) && std::abs(out[(size_t) n]) > 1.0e-12)
            return fail("transient layer leaks energy outside its compact support");
    if (begin >= 0 && std::abs(out[(size_t) begin]) > 0.05 * peakAbs)
        return fail("burst does not taper to zero at its start (click risk)");
    if (end - 1 < nSamples && std::abs(out[(size_t) (end - 1)]) > 0.05 * peakAbs)
        return fail("burst does not taper to zero at its end (click risk)");

    // 6. Gain scales injected energy quadratically (energy ~ gain^2).
    {
        ats::TransientLayerParams half = rparams;
        half.gain = 0.5;
        std::vector<double> outHalf((size_t) nSamples, 0.0);
        if (!ats::renderTransientLayer(events, half, srate, outHalf.data(), nSamples))
            return fail("renderTransientLayer (half gain) failed");
        const double eHalf = totalEnergy(outHalf);
        if (std::abs(eHalf - 0.5 * targetEnergy) > 0.02 * targetEnergy + 1.0e-12)
            return fail("half gain should inject half the energy");
    }

    // 7. Zero-init / backward-compat: no events -> silence; flat sound -> no events.
    {
        std::vector<double> silent((size_t) nSamples, 7.0);
        if (!ats::renderTransientLayer({}, rparams, srate, silent.data(), nSamples))
            return fail("empty render should succeed");
        for (double v : silent)
            if (v != 0.0)
                return fail("empty render should zero the buffer");

        ModelBuilder flat;
        flat.setPartials({ std::vector<double>((size_t) frames, 0.4) });
        flat.setResidualBand(8, std::vector<double>((size_t) frames, 0.2));
        ATS_SOUND* steady = flat.build();
        const auto flatOnsets = ats::detectOnsets(steady, dparams);
        const auto flatEvents = ats::extractTransientLayer(steady, flatOnsets);
        std::vector<double> outFlat((size_t) nSamples, 0.0);
        if (!ats::renderTransientLayer(flatEvents, rparams, srate, outFlat.data(), nSamples))
            return fail("flat render should succeed");
        if (totalEnergy(outFlat) > 1.0e-9)
            return fail("steady sound should inject ~no transient energy");
    }

    std::cout << "ats_transient_layer_test: OK ("
              << events.size() << " event(s), targetEnergy=" << targetEnergy
              << ", rendered=" << rendered << ")\n";
    return 0;
}
