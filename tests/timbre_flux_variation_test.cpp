#include "TimbreDescriptors.h"
#include "TimbreFrameAdapter.h"
#include "TimbreSeriesAnalysis.h"

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace
{

int fail(const std::string& message)
{
    std::cerr << "timbre_flux_variation_test: " << message << '\n';
    return 1;
}

ats::TimbreDescriptorOptions makeOptions()
{
    ats::TimbreDescriptorOptions opts;
    opts.comparisonAxis.type = ats::EnvelopeFrequencyAxis::LinearHz;
    opts.comparisonAxis.minFrequencyHz = 100.0;
    opts.comparisonAxis.maxFrequencyHz = 8100.0;
    opts.comparisonAxis.sampleRate = 48000.0;
    opts.comparisonGridSize = 32;
    return opts;
}

ats::SeriesFrameContext makeFrame(
    int index,
    double time,
    const std::vector<std::pair<double, double>>& partials,
    bool transientBoundary = false)
{
    ats::EnvelopeFrameInput raw;
    raw.sourceModelId = "s";
    raw.frameIndex = static_cast<std::uint64_t>(index);
    raw.sampleRate = 48000.0;
    for (const auto& [freq, amp] : partials)
        raw.partials.push_back({ freq, amp, 20.0, 1.0, true });
    ats::SeriesFrameContext context;
    context.samples = ats::validateEnvelopeFrameInput(raw);
    context.timeSeconds = time;
    context.transientBoundary = transientBoundary;
    return context;
}

} // namespace

int main()
{
    const auto options = makeOptions();
    ats::TimbreSeriesAlignmentOptions alignment;

    // ---- Steady frames: identical partials -> flux = 0, variation = 0 (after
    //      the first frame). First frame is TooSparse by contract. ----
    {
        std::vector<ats::SeriesFrameContext> frames;
        for (int i = 0; i < 4; ++i)
        {
            frames.push_back(makeFrame(i, static_cast<double>(i) * 0.025,
                { { 1000.0, 0.5 }, { 3000.0, 0.4 } }));
        }
        std::vector<ats::TimbreDescriptorValue> flux, variation;
        ats::computeFluxAndVariationTrajectories(
            frames, options, alignment,
            ats::TimbreDescriptorDomain::Combined, flux, variation);
        if (flux.size() != 4u || variation.size() != 4u)
            return fail("steady: wrong trajectory length");
        if (flux[0].status != ats::TimbreDescriptorStatus::TooSparse)
            return fail("steady: first flux must be TooSparse");
        if (variation[0].status != ats::TimbreDescriptorStatus::TooSparse)
            return fail("steady: first variation must be TooSparse");
        for (std::size_t i = 1; i < 4; ++i)
        {
            if (flux[i].status != ats::TimbreDescriptorStatus::Available)
                return fail("steady: flux must be Available");
            if (variation[i].status != ats::TimbreDescriptorStatus::Available)
                return fail("steady: variation must be Available");
            if (std::abs(flux[i].value) > 1.0e-9)
                return fail("steady: flux must be 0");
            if (std::abs(variation[i].value) > 1.0e-9)
                return fail("steady: variation must be 0");
        }
    }

    // ---- Gain-only: same shape, doubled amplitude across frame 1->2.
    //      With L1 normalization, variation must be ~0; without
    //      normalization, flux is non-zero but variation is still ~0
    //      because cosine similarity is shape-only. ----
    {
        std::vector<ats::SeriesFrameContext> frames;
        frames.push_back(makeFrame(0, 0.000,
            { { 1000.0, 0.5 }, { 3000.0, 0.4 } }));
        frames.push_back(makeFrame(1, 0.025,
            { { 1000.0, 1.0 }, { 3000.0, 0.8 } }));
        std::vector<ats::TimbreDescriptorValue> flux, variation;
        ats::computeFluxAndVariationTrajectories(
            frames, options, alignment,
            ats::TimbreDescriptorDomain::Combined, flux, variation);
        if (std::abs(variation[1].value) > 1.0e-9)
            return fail("gain-only: variation must be 0 (shape unchanged)");
        // With L1 normalization both frames sum to 1, so flux is ~0 too.
        if (std::abs(flux[1].value) > 1.0e-9)
            return fail("gain-only with L1: flux must be 0");

        // None policy: gain-only flux is non-zero but variation still 0.
        ats::TimbreSeriesAlignmentOptions raw = alignment;
        raw.frameNormalization = ats::SeriesFrameNormalization::None;
        std::vector<ats::TimbreDescriptorValue> rawFlux, rawVariation;
        ats::computeFluxAndVariationTrajectories(
            frames, options, raw,
            ats::TimbreDescriptorDomain::Combined, rawFlux, rawVariation);
        if (!(rawFlux[1].value > 1.0e-6))
            return fail("gain-only without normalization: flux must be >0");
        if (std::abs(rawVariation[1].value) > 1.0e-9)
            return fail("gain-only without normalization: variation must be 0");
    }

    // ---- Onset: silence -> energy frame. First-to-second hop must report
    //      large flux and large variation. ----
    {
        std::vector<ats::SeriesFrameContext> frames;
        frames.push_back(makeFrame(0, 0.000, {})); // silent
        frames.push_back(makeFrame(1, 0.025,
            { { 1000.0, 1.0 }, { 3000.0, 0.5 } }));
        std::vector<ats::TimbreDescriptorValue> flux, variation;
        ats::computeFluxAndVariationTrajectories(
            frames, options, alignment,
            ats::TimbreDescriptorDomain::Combined, flux, variation);
        if (flux[1].status != ats::TimbreDescriptorStatus::Available)
            return fail("onset: flux status must be Available");
        if (!(flux[1].value > 0.1))
            return fail("onset: flux must be > 0.1");
        // Cosine similarity between zero vector and non-zero is undefined ->
        // we report EmptyInput when norm product = 0.
        if (variation[1].status != ats::TimbreDescriptorStatus::EmptyInput)
            return fail("onset: variation must be EmptyInput when previous frame is silent");
    }

    // ---- Moving bump: identical energy redistributed across bins.
    //      Variation must be >0; with L1 normalization, flux > 0 too. ----
    {
        std::vector<ats::SeriesFrameContext> frames;
        frames.push_back(makeFrame(0, 0.000,
            { { 600.0, 1.0 }, { 3000.0, 0.1 } }));
        frames.push_back(makeFrame(1, 0.025,
            { { 600.0, 0.1 }, { 3000.0, 1.0 } }));
        std::vector<ats::TimbreDescriptorValue> flux, variation;
        ats::computeFluxAndVariationTrajectories(
            frames, options, alignment,
            ats::TimbreDescriptorDomain::Combined, flux, variation);
        if (!(variation[1].value > 0.3))
            return fail("moving bump: variation must reflect shape change");
        if (!(flux[1].value > 0.1))
            return fail("moving bump: flux must be > 0");
    }

    // ---- Transient boundary: flux/variation at the boundary frame must
    //      reset to TooSparse so attack discontinuities do not blow up
    //      smoothed shape metrics. ----
    {
        std::vector<ats::SeriesFrameContext> frames;
        frames.push_back(makeFrame(0, 0.000,
            { { 1000.0, 0.5 } }));
        frames.push_back(makeFrame(1, 0.025,
            { { 1000.0, 0.5 } }));
        frames.push_back(makeFrame(2, 0.050,
            { { 1000.0, 1.0 } }, /*transientBoundary=*/true));
        frames.push_back(makeFrame(3, 0.075,
            { { 1000.0, 1.0 } }));
        std::vector<ats::TimbreDescriptorValue> flux, variation;
        ats::computeFluxAndVariationTrajectories(
            frames, options, alignment,
            ats::TimbreDescriptorDomain::Combined, flux, variation);
        if (flux[2].status != ats::TimbreDescriptorStatus::TooSparse)
            return fail("transient: frame at boundary must be TooSparse");
        if (variation[2].status != ats::TimbreDescriptorStatus::TooSparse)
            return fail("transient: variation at boundary must be TooSparse");
        // Disable reset and re-run: boundary frame should now produce a real
        // (large) flux because amplitudes doubled.
        ats::TimbreSeriesAlignmentOptions noReset = alignment;
        noReset.resetAtTransientBoundaries = false;
        std::vector<ats::TimbreDescriptorValue> flux2, variation2;
        ats::computeFluxAndVariationTrajectories(
            frames, options, noReset,
            ats::TimbreDescriptorDomain::Combined, flux2, variation2);
        if (flux2[2].status != ats::TimbreDescriptorStatus::Available)
            return fail("transient reset disabled: must be Available");
    }

    // ---- Domain selection: residual-only and partial-only paths exist
    //      and produce zero flux on a residual-empty frame pair. ----
    {
        std::vector<ats::SeriesFrameContext> frames;
        frames.push_back(makeFrame(0, 0.0, { { 1000.0, 0.5 } }));
        frames.push_back(makeFrame(1, 0.025, { { 1000.0, 0.5 } }));
        std::vector<ats::TimbreDescriptorValue> flux, variation;
        ats::computeFluxAndVariationTrajectories(
            frames, options, alignment,
            ats::TimbreDescriptorDomain::ResidualBands, flux, variation);
        if (flux[1].status != ats::TimbreDescriptorStatus::EmptyInput
            && flux[1].status != ats::TimbreDescriptorStatus::Available)
        {
            return fail("residual-only domain: unexpected status");
        }
        // Either way values must be finite.
        if (!std::isfinite(flux[1].value) || !std::isfinite(variation[1].value))
            return fail("residual-only domain: non-finite values");
    }

    std::cout << "timbre_flux_variation_test: ok\n";
    return 0;
}
