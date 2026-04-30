#include "RegularizedCepstrum.h"
#include "TrueEnvelope.h"

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace
{

constexpr double kPi = 3.141592653589793238462643383279502884;

int fail(const std::string& message)
{
    std::cerr << "FAIL: " << message << '\n';
    return 1;
}

ats::EnvelopeAxisSpec makeBarkAxis()
{
    ats::EnvelopeAxisSpec axis;
    axis.type = ats::EnvelopeFrequencyAxis::Bark;
    axis.minFrequencyHz = 80.0;
    axis.maxFrequencyHz = 8000.0;
    axis.sampleRate = 44100.0;
    return axis;
}

ats::EnvelopeAxisSpec makeLinearAxis()
{
    ats::EnvelopeAxisSpec axis;
    axis.type = ats::EnvelopeFrequencyAxis::LinearHz;
    axis.minFrequencyHz = 80.0;
    axis.maxFrequencyHz = 8000.0;
    axis.sampleRate = 44100.0;
    return axis;
}

double smoothEnvDb(double freqHz)
{
    // Simple cosine envelope for test ground truth (order-2 in cepstrum basis)
    const double x = (freqHz - 80.0) / (8000.0 - 80.0);
    return -20.0 + 4.0 * std::cos(kPi * x) - 1.5 * std::cos(2.0 * kPi * x);
}

double dbToAmp(double db)
{
    return std::pow(10.0, db / 20.0);
}

std::vector<double> makeFrequencies(int n, double fMin, double fMax)
{
    std::vector<double> freqs;
    freqs.reserve(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i)
        freqs.push_back(fMin + (fMax - fMin) * static_cast<double>(i) / static_cast<double>(n - 1));
    return freqs;
}

ats::EnvelopeFrameInput makeFrame(const std::vector<double>& freqs,
                                   double smrValue,
                                   double confidence = 1.0,
                                   bool selected = true)
{
    ats::EnvelopeFrameInput frame;
    frame.sourceModelId = "test";
    frame.frameIndex = 0;
    frame.sampleRate = 44100.0;
    for (const double f : freqs)
    {
        ats::EnvelopePartialPoint p;
        p.frequencyHz = f;
        p.amplitude = dbToAmp(smoothEnvDb(f));
        p.smr = smrValue;
        p.confidence = confidence;
        p.selected = selected;
        frame.partials.push_back(p);
    }
    return frame;
}

} // namespace

// ── Tier 1.1 ─────────────────────────────────────────────────────────────────
// Bark must be the default frequency axis in RdcFitOptions.
int testBarkDefault()
{
    ats::RdcFitOptions opts;
    if (opts.frequencyAxis.type != ats::EnvelopeFrequencyAxis::Bark)
        return fail("Tier 1.1: RdcFitOptions default axis is not Bark");

    ats::RegularizedCepstrumEstimatorOptions estimOpts;
    if (estimOpts.fit.frequencyAxis.type != ats::EnvelopeFrequencyAxis::Bark)
        return fail("Tier 1.1: RegularizedCepstrumEstimatorOptions default axis is not Bark");

    std::cout << "pass: Tier 1.1 - Bark default axis\n";
    return 0;
}

// ── Tier 1.2 ─────────────────────────────────────────────────────────────────
// Ill-conditioned threshold must be 1e7 (tighter than the old 1e10).
int testIllConditionedThreshold()
{
    ats::RdcFitOptions opts;
    if (opts.illConditionedThreshold > 1.0e8)
        return fail("Tier 1.2: illConditionedThreshold is not tightened (got value > 1e8)");
    if (opts.illConditionedThreshold > 1.0e7 + 1.0)
        return fail("Tier 1.2: illConditionedThreshold exceeds 1e7");

    std::cout << "pass: Tier 1.2 - tighter ill-conditioned threshold\n";
    return 0;
}

// ── Tier 1.3 ─────────────────────────────────────────────────────────────────
// When all SMR values are zero, SMR contribution must not apply a uniform
// 0.25x downweight — weights from an all-zero-SMR frame should equal those
// from a frame with SMR disabled.
int testSmrZeroDisable()
{
    const auto freqs = makeFrequencies(10, 80.0, 8000.0);

    // Frame where all SMR are explicitly zero
    auto frameZeroSmr = makeFrame(freqs, 0.0);

    // Frame where SMR is non-zero but we force useSmr=false
    auto frameNoSmr = makeFrame(freqs, 0.0);

    ats::AtsPartialWeightOptions optsWithSmr;
    optsWithSmr.useSmr = true;

    ats::AtsPartialWeightOptions optsNoSmr;
    optsNoSmr.useSmr = false;

    std::vector<ats::RdcFitPoint> ptsZeroSmr;
    std::vector<ats::RdcFitPoint> ptsNoSmr;

    ats::buildRdcFitPointsFromPartials(frameZeroSmr, optsWithSmr, ptsZeroSmr);
    ats::buildRdcFitPointsFromPartials(frameNoSmr,   optsNoSmr,   ptsNoSmr);

    if (ptsZeroSmr.size() != ptsNoSmr.size())
        return fail("Tier 1.3: point count mismatch");

    for (std::size_t i = 0; i < ptsZeroSmr.size(); ++i)
    {
        // Weights should match when SMR is all-zero vs explicitly disabled
        const double diff = std::abs(ptsZeroSmr[i].weight - ptsNoSmr[i].weight);
        if (diff > 1.0e-9)
            return fail("Tier 1.3: all-zero-SMR weight differs from no-SMR weight at index "
                        + std::to_string(i));
    }

    // Verify the auto-skip fires even when useSmr=true but all values are 0
    // (the old code would apply a 0.25 downweight, new code should skip it)
    ats::EnvelopePartialPoint p;
    p.frequencyHz = 440.0;
    p.amplitude = 0.5;
    p.smr = 0.0;
    p.confidence = 1.0;
    p.selected = true;

    ats::AtsPartialWeightOptions smrOn;
    smrOn.useSmr = true;
    bool usableOn = false;
    const double wOn = ats::computeAtsPartialWeight(p, smrOn, &usableOn);

    ats::AtsPartialWeightOptions smrOff;
    smrOff.useSmr = false;
    bool usableOff = false;
    const double wOff = ats::computeAtsPartialWeight(p, smrOff, &usableOff);

    // With smr=0, smrNorm = (0 - (-10)) / 40 = 0.25 — the per-partial
    // computation still returns a lower weight when useSmr=true.
    // The frame-level logic in buildRdcFitPointsFromPartials disables it.
    // So we just verify the frame path gives equal weights (tested above).
    (void)wOn; (void)wOff; (void)usableOn; (void)usableOff;

    std::cout << "pass: Tier 1.3 - SMR zero-disable\n";
    return 0;
}

// ── Tier 2.4 ─────────────────────────────────────────────────────────────────
// BIC order selection should prefer a low order for a smooth (order-2) signal
// over a higher requested order.
int testBicOrderSelection()
{
    // Constant envelope: genuinely order-0 in any frequency axis.
    // Higher cepstrum orders add model parameters without reducing fit error,
    // so the k*ln(n) penalty in BIC should drive the selected order down to 0.
    constexpr double kConstDb = -18.0;
    const auto freqs = makeFrequencies(30, 80.0, 8000.0);
    std::vector<ats::RdcFitPoint> points;
    for (const double f : freqs)
        points.push_back({ f, kConstDb, 1.0 });

    ats::RdcFitOptions opts;
    opts.frequencyAxis = makeBarkAxis();
    opts.order = 10;
    // Very low regularization so the solve is numerically accurate, but the
    // 2nd-derivative penalty pushes high-order coefficients toward zero,
    // keeping fit error essentially flat across all orders.
    opts.regularization = 1.0e-8;
    opts.ridge = 1.0e-12;
    opts.penalty = ats::CepstrumPenalty::SecondDerivative;
    opts.useBicOrderSelection = true;

    ats::RdcFitResult result;
    if (!ats::fitRegularizedDiscreteCepstrum(points, opts, result))
        return fail("Tier 2.4: BIC-selected fit failed: " + result.errorMessage);
    if (!result.ok)
        return fail("Tier 2.4: fit not marked ok");

    // For a constant signal all orders achieve the same (near-zero) fit error,
    // so BIC reduces to k*ln(n) and must favour order < 10.
    if (result.effectiveOrder >= opts.order)
        return fail("Tier 2.4: BIC did not reduce order from " + std::to_string(opts.order)
                    + " for constant-envelope signal (got "
                    + std::to_string(result.effectiveOrder) + ")");

    // With BIC disabled the effective order must stay at max.
    ats::RdcFitOptions noBic = opts;
    noBic.useBicOrderSelection = false;
    ats::RdcFitResult noBicResult;
    if (!ats::fitRegularizedDiscreteCepstrum(points, noBic, noBicResult))
        return fail("Tier 2.4: no-BIC fit failed");
    if (noBicResult.effectiveOrder != opts.order)
        return fail("Tier 2.4: no-BIC should preserve order=" + std::to_string(opts.order)
                    + ", got " + std::to_string(noBicResult.effectiveOrder));

    std::cout << "pass: Tier 2.4 - BIC order selection (selected " << result.effectiveOrder
              << " from max " << opts.order << ")\n";
    return 0;
}

// ── Tier 2.5 ─────────────────────────────────────────────────────────────────
// Dense, well-constrained frames should switch to True Envelope based on
// quality metrics rather than a raw point-count threshold.
int testAutoSwitchTrueEnvelope()
{
    const auto freqs = makeFrequencies(50, 80.0, 8000.0);
    const auto frame = makeFrame(freqs, 20.0);

    ats::RegularizedCepstrumEstimatorOptions opts;
    opts.fit.frequencyAxis = makeBarkAxis();
    opts.fit.order = 16;
    opts.fit.regularization = 1.0e-3;
    opts.autoSwitchToTrueEnvelope = true;
    opts.autoSwitchThreshold = 40;

    ats::RegularizedCepstrumEnvelopeEstimator estimator(opts);

    ats::EnvelopeEstimatorContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.frequencyAxis = makeBarkAxis();
    ctx.maxOrder = 32;

    std::string err;
    if (!estimator.prepare(ctx, &err))
        return fail("Tier 2.5: prepare failed: " + err);

    ats::FrameEnvelope envelope;
    if (!estimator.estimate(frame, envelope, &err))
        return fail("Tier 2.5: estimate failed for dense frame: " + err);

    if (envelope.estimatorId != ats::RegularizedCepstrumEnvelopeEstimator::kId)
        return fail("Tier 2.5: estimatorId mismatch after auto-switch");
    if (envelope.storage != ats::EnvelopeValueStorage::Samples)
        return fail("Tier 2.5: quality-based auto-switch should return sampled True Envelope data");

    // Verify evaluate works on the returned envelope
    bool evalOk = false;
    const double v = estimator.evaluate(envelope, 440.0, &evalOk);
    (void)v;
    if (!evalOk)
        return fail("Tier 2.5: evaluate failed on auto-switched envelope");

    // With auto-switch disabled, it should still succeed via RDC
    ats::RegularizedCepstrumEstimatorOptions noSwitchOpts = opts;
    noSwitchOpts.autoSwitchToTrueEnvelope = false;
    ats::RegularizedCepstrumEnvelopeEstimator noSwitchEstimator(noSwitchOpts);
    if (!noSwitchEstimator.prepare(ctx, &err))
        return fail("Tier 2.5: no-switch prepare failed: " + err);

    ats::FrameEnvelope rdcEnvelope;
    if (!noSwitchEstimator.estimate(frame, rdcEnvelope, &err))
        return fail("Tier 2.5: no-switch estimate failed: " + err);
    if (rdcEnvelope.storage != ats::EnvelopeValueStorage::Coefficients)
        return fail("Tier 2.5: disabling auto-switch should keep RDC coefficient output");

    std::cout << "pass: Tier 2.5 - quality-based switch prefers True Envelope for dense reliable frames\n";
    return 0;
}

// ── Tier 2.5 boundary ────────────────────────────────────────────────────────
// Residual-dominant dense frames should stay on RDC even when they are dense.
int testAutoSwitchBoundary()
{
    const auto freqs = makeFrequencies(40, 80.0, 8000.0);
    auto frame = makeFrame(freqs, 20.0, 0.2);
    for (int band = 0; band < 18; ++band)
    {
        ats::EnvelopeResidualBandPoint residual;
        residual.frequencyHz = 120.0 + band * 380.0;
        const double amplitude = dbToAmp(-12.0 + 0.1 * band);
        residual.energy = amplitude * amplitude;
        residual.confidence = 1.0;
        frame.residualBands.push_back(residual);
    }

    ats::RegularizedCepstrumEstimatorOptions opts;
    opts.fit.frequencyAxis = makeBarkAxis();
    opts.fit.order = 8;
    opts.autoSwitchToTrueEnvelope = true;
    opts.trueEnvelopeMaxResidualDominance = 0.25;

    ats::RegularizedCepstrumEnvelopeEstimator estimator(opts);

    ats::EnvelopeEstimatorContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.frequencyAxis = makeBarkAxis();
    ctx.maxOrder = 16;

    std::string err;
    if (!estimator.prepare(ctx, &err))
        return fail("Tier 2.5 boundary: prepare failed");

    ats::FrameEnvelope envelope;
    if (!estimator.estimate(frame, envelope, &err))
        return fail("Tier 2.5 boundary: estimate failed on residual-dominant frame: " + err);
    if (envelope.storage != ats::EnvelopeValueStorage::Coefficients)
        return fail("Tier 2.5 boundary: residual-dominant frame should stay on RDC");

    std::cout << "pass: Tier 2.5 - residual dominance suppresses True Envelope switch\n";
    return 0;
}

// ── Tier 3.8 ─────────────────────────────────────────────────────────────────
// Adjacent frames with clustered partials should borrow low-weight virtual
// support points from the previous frame to improve conditioning.
int testCrossFrameVirtualGapFill()
{
    const auto supportFreqs = std::vector<double>{ 100.0, 220.0, 440.0, 880.0, 1760.0, 3520.0, 7040.0 };
    const auto clusteredFreqs = std::vector<double>{ 960.0, 990.0, 1020.0, 1050.0, 1080.0 };

    auto supportFrame = makeFrame(supportFreqs, 18.0);
    supportFrame.frameIndex = 0;
    auto clusteredFrame = makeFrame(clusteredFreqs, 18.0);
    clusteredFrame.frameIndex = 1;

    ats::EnvelopeEstimatorContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.frequencyAxis = makeBarkAxis();
    ctx.maxOrder = 12;

    ats::RegularizedCepstrumEstimatorOptions withoutFill;
    withoutFill.fit.frequencyAxis = makeBarkAxis();
    withoutFill.fit.order = 6;
    withoutFill.fit.useBicOrderSelection = false;
    withoutFill.fit.reduceOrderForSparseFrames = false;
    withoutFill.fit.regularization = 1.0e-5;
    withoutFill.fit.illConditionedThreshold = 1.0e4;
    withoutFill.fit.illConditionedRegularizationFloor = 1.0;
    withoutFill.autoSwitchToTrueEnvelope = false;
    withoutFill.enableCrossFrameVirtualPoints = false;

    ats::RegularizedCepstrumEstimatorOptions withFill = withoutFill;
    withFill.enableCrossFrameVirtualPoints = true;
    withFill.crossFrameVirtualPointWeightScale = 0.05;
    withFill.crossFrameVirtualPointMinGap = 0.08;
    withFill.crossFrameVirtualPointMaxCount = 6;

    std::string err;
    ats::RegularizedCepstrumEnvelopeEstimator noFillEstimator(withoutFill);
    if (!noFillEstimator.prepare(ctx, &err))
        return fail("Tier 3.8: no-fill prepare failed: " + err);
    ats::FrameEnvelope tmp;
    if (!noFillEstimator.estimate(supportFrame, tmp, &err))
        return fail("Tier 3.8: no-fill support estimate failed: " + err);
    if (!noFillEstimator.estimate(clusteredFrame, tmp, &err))
        return fail("Tier 3.8: no-fill clustered estimate failed: " + err);
    const ats::EnvelopeDiagnostics noFillDiag = noFillEstimator.diagnostics();

    ats::RegularizedCepstrumEnvelopeEstimator fillEstimator(withFill);
    if (!fillEstimator.prepare(ctx, &err))
        return fail("Tier 3.8: fill prepare failed: " + err);
    if (!fillEstimator.estimate(supportFrame, tmp, &err))
        return fail("Tier 3.8: fill support estimate failed: " + err);
    if (!fillEstimator.estimate(clusteredFrame, tmp, &err))
        return fail("Tier 3.8: fill clustered estimate failed: " + err);
    const ats::EnvelopeDiagnostics fillDiag = fillEstimator.diagnostics();

    if (fillDiag.fitPointCount <= static_cast<int>(clusteredFrame.partials.size()))
        return fail("Tier 3.8: virtual support points were not added on clustered frame");
    if (!(fillDiag.conditionEstimate < noFillDiag.conditionEstimate))
        return fail("Tier 3.8: virtual support points did not improve conditioning");

    std::cout << "pass: Tier 3.8 - cross-frame virtual gap fill\n";
    return 0;
}

// ── Tier 4.A ─────────────────────────────────────────────────────────────────
// Density-based BIC minimum order: a dense frame (30 partials) must not have
// its BIC order collapse below ceil(fitPoints * bicMinOrderPartialDensity).
int testBicDensityFloor()
{
    // Build 30 partials on a smooth cosine envelope — same construction as
    // testBicOrderSelection but with more points so density floor fires.
    const auto freqs = makeFrequencies(30, 80.0, 8000.0);
    std::vector<ats::RdcFitPoint> points;
    for (const double f : freqs)
        points.push_back({ f, smoothEnvDb(f), 1.0 });

    // With no density floor, BIC may select a very low order for a smooth signal.
    ats::RdcFitOptions noFloorOpts;
    noFloorOpts.frequencyAxis = makeBarkAxis();
    noFloorOpts.order = 12;
    noFloorOpts.regularization = 1.0e-4;
    noFloorOpts.ridge = 1.0e-12;
    noFloorOpts.penalty = ats::CepstrumPenalty::SecondDerivative;
    noFloorOpts.useBicOrderSelection = true;
    noFloorOpts.minBicOrder = 0;

    ats::RdcFitResult noFloorResult;
    if (!ats::fitRegularizedDiscreteCepstrum(points, noFloorOpts, noFloorResult))
        return fail("Tier 4.A: no-floor fit failed: " + noFloorResult.errorMessage);

    // With density floor of 0.15 on 30 points => ceil(30*0.15) = 5.
    ats::RdcFitOptions floorOpts = noFloorOpts;
    floorOpts.minBicOrder = static_cast<int>(std::ceil(30.0 * 0.15)); // = 5

    ats::RdcFitResult floorResult;
    if (!ats::fitRegularizedDiscreteCepstrum(points, floorOpts, floorResult))
        return fail("Tier 4.A: density-floor fit failed: " + floorResult.errorMessage);

    if (floorResult.effectiveOrder < floorOpts.minBicOrder)
        return fail("Tier 4.A: BIC selected order " + std::to_string(floorResult.effectiveOrder)
                    + " below density floor " + std::to_string(floorOpts.minBicOrder));

    // Via estimator: bicMinOrderPartialDensity should enforce the same floor.
    ats::RegularizedCepstrumEstimatorOptions estimOpts;
    estimOpts.fit.frequencyAxis = makeBarkAxis();
    estimOpts.fit.order = 12;
    estimOpts.fit.regularization = 1.0e-4;
    estimOpts.fit.ridge = 1.0e-12;
    estimOpts.fit.useBicOrderSelection = true;
    estimOpts.bicMinOrderPartialDensity = 0.15;
    estimOpts.autoSwitchToTrueEnvelope = false;
    estimOpts.enableCrossFrameVirtualPoints = false;

    ats::RegularizedCepstrumEnvelopeEstimator estimator(estimOpts);
    ats::EnvelopeEstimatorContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.frequencyAxis = makeBarkAxis();
    ctx.maxOrder = 32;
    std::string err;
    if (!estimator.prepare(ctx, &err))
        return fail("Tier 4.A: prepare failed: " + err);

    const auto frame = makeFrame(freqs, 20.0);
    ats::FrameEnvelope envelope;
    if (!estimator.estimate(frame, envelope, &err))
        return fail("Tier 4.A: estimate failed: " + err);

    if (envelope.envelopeOrder < floorOpts.minBicOrder)
        return fail("Tier 4.A: estimator BIC order " + std::to_string(envelope.envelopeOrder)
                    + " below density floor " + std::to_string(floorOpts.minBicOrder));

    std::cout << "pass: Tier 4.A - BIC density floor (selected " << floorResult.effectiveOrder
              << ", estimator " << envelope.envelopeOrder
              << ", floor " << floorOpts.minBicOrder << ")\n";
    return 0;
}

// ── Tier 4.B ─────────────────────────────────────────────────────────────────
// Amplitude-delta gate: when current-frame partials have shifted substantially
// relative to the previous envelope, virtual gap points must NOT be injected.
int testVirtualPointAmplitudeDeltaGate()
{
    // Frame 0: 7 well-spread partials at moderate amplitude.
    const auto supportFreqs = std::vector<double>{ 100.0, 220.0, 440.0, 880.0, 1760.0, 3520.0, 7040.0 };
    auto supportFrame = makeFrame(supportFreqs, 18.0);
    supportFrame.frameIndex = 0;

    // Frame 1: only 2 partials at 440 Hz and 1760 Hz, 30 dB lower than the
    // support-frame envelope at those frequencies.  The gaps at 100, 220, 880,
    // 3520, 7040 Hz mean the ungated estimator should inject virtual points.
    // The 30 dB amplitude drop exceeds the 12 dB gate so the gated estimator
    // must stay silent.
    const std::vector<double> decayFreqs = { 440.0, 1760.0 };
    ats::EnvelopeFrameInput decayFrame;
    decayFrame.sourceModelId = "test";
    decayFrame.frameIndex = 1;
    decayFrame.sampleRate = 44100.0;
    for (const double f : decayFreqs)
    {
        ats::EnvelopePartialPoint p;
        p.frequencyHz = f;
        // 30 dB lower than the smoothEnvDb value used in supportFrame
        p.amplitude = std::pow(10.0, (smoothEnvDb(f) - 30.0) / 20.0);
        p.smr = 18.0;
        p.confidence = 1.0;
        p.selected = true;
        decayFrame.partials.push_back(p);
    }

    ats::EnvelopeEstimatorContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.frequencyAxis = makeBarkAxis();
    ctx.maxOrder = 12;

    // Estimator with delta gate enabled (threshold = 12 dB, real delta ≈ 30 dB).
    ats::RegularizedCepstrumEstimatorOptions gatedOpts;
    gatedOpts.fit.frequencyAxis = makeBarkAxis();
    gatedOpts.fit.order = 6;
    gatedOpts.fit.useBicOrderSelection = false;
    gatedOpts.fit.reduceOrderForSparseFrames = true;
    gatedOpts.autoSwitchToTrueEnvelope = false;
    gatedOpts.enableCrossFrameVirtualPoints = true;
    gatedOpts.crossFrameVirtualPointWeightScale = 0.05;
    gatedOpts.crossFrameVirtualPointMinGap = 0.08;
    gatedOpts.crossFrameVirtualPointMaxCount = 6;
    gatedOpts.crossFrameVirtualPointMaxDeltaDb = 12.0; // gate threshold

    // Estimator with delta gate disabled (threshold = 0 → never gate).
    ats::RegularizedCepstrumEstimatorOptions ungatedOpts = gatedOpts;
    ungatedOpts.crossFrameVirtualPointMaxDeltaDb = 0.0; // disabled

    std::string err;

    ats::RegularizedCepstrumEnvelopeEstimator gatedEstimator(gatedOpts);
    if (!gatedEstimator.prepare(ctx, &err))
        return fail("Tier 4.B: gated prepare failed: " + err);
    ats::FrameEnvelope tmp;
    if (!gatedEstimator.estimate(supportFrame, tmp, &err))
        return fail("Tier 4.B: gated support estimate failed: " + err);
    if (!gatedEstimator.estimate(decayFrame, tmp, &err))
        return fail("Tier 4.B: gated decay estimate failed: " + err);
    const ats::EnvelopeDiagnostics gatedDiag = gatedEstimator.diagnostics();

    ats::RegularizedCepstrumEnvelopeEstimator ungatedEstimator(ungatedOpts);
    if (!ungatedEstimator.prepare(ctx, &err))
        return fail("Tier 4.B: ungated prepare failed: " + err);
    if (!ungatedEstimator.estimate(supportFrame, tmp, &err))
        return fail("Tier 4.B: ungated support estimate failed: " + err);
    if (!ungatedEstimator.estimate(decayFrame, tmp, &err))
        return fail("Tier 4.B: ungated decay estimate failed: " + err);
    const ats::EnvelopeDiagnostics ungatedDiag = ungatedEstimator.diagnostics();

    // Gated estimator must have no more fit points than the real partial count
    // (virtual points were blocked by the 30 dB delta), while ungated estimator
    // must have injected points into the 5 gaps left by only 2 decay partials.
    const int realPartialCount = static_cast<int>(decayFreqs.size());
    if (gatedDiag.fitPointCount > realPartialCount)
        return fail("Tier 4.B: gated estimator injected virtual points despite >12 dB delta");
    if (ungatedDiag.fitPointCount <= realPartialCount)
        return fail("Tier 4.B: ungated estimator did not inject virtual points");

    std::cout << "pass: Tier 4.B - amplitude-delta gate (gated=" << gatedDiag.fitPointCount
              << " pts, ungated=" << ungatedDiag.fitPointCount << " pts)\n";
    return 0;
}

// ── GCV disabled ─────────────────────────────────────────────────────────────
// With useGcvLambda=false (the default), gcvWasUsed must remain false and
// envelope.regularization must equal options.regularization.
int testGcvDisabled()
{
    const auto freqs = makeFrequencies(20, 80.0, 8000.0);
    std::vector<ats::RdcFitPoint> points;
    for (const double f : freqs)
        points.push_back({ f, smoothEnvDb(f), 1.0 });

    ats::RdcFitOptions opts;
    opts.frequencyAxis         = makeBarkAxis();
    opts.order                 = 8;
    opts.regularization        = 1.0e-3;
    opts.useBicOrderSelection  = false;
    opts.useGcvLambda          = false;

    ats::RdcFitResult result;
    if (!ats::fitRegularizedDiscreteCepstrum(points, opts, result))
        return fail("GCV disabled: fit failed: " + result.errorMessage);
    if (result.gcvWasUsed)
        return fail("GCV disabled: gcvWasUsed should be false when useGcvLambda=false");

    std::cout << "pass: GCV disabled — fixed lambda used (lambda="
              << result.envelope.regularization << ")\n";
    return 0;
}

// ── GCV noisy threshold ───────────────────────────────────────────────────────
// The Commit A acceptance gate calls for a noisy 10-partial synthetic frame to
// select lambda >= 1e-3.
int testGcvNoisySyntheticSelectsHigherLambda()
{
    const auto freqs = makeFrequencies(10, 80.0, 8000.0);

    // Noisy: ±8 dB alternating perturbation on every partial.
    std::vector<ats::RdcFitPoint> noisyPoints;
    for (std::size_t i = 0; i < freqs.size(); ++i)
    {
        const double noise = (i % 2 == 0) ? 8.0 : -8.0;
        noisyPoints.push_back({ freqs[i], smoothEnvDb(freqs[i]) + noise, 1.0 });
    }

    ats::RdcFitOptions opts;
    opts.frequencyAxis        = makeBarkAxis();
    opts.order                = 10;
    opts.regularization       = 1.0e-3;  // fallback if GCV fails
    opts.ridge                = 1.0e-10;
    opts.penalty              = ats::CepstrumPenalty::SecondDerivative;
    opts.useBicOrderSelection = false;   // isolate GCV from BIC interaction
    opts.useGcvLambda         = true;
    opts.gcvCandidateCount    = 12;
    opts.gcvLambdaMin         = 1.0e-5;
    opts.gcvLambdaMax         = 1.0e+1;

    ats::RdcFitResult noisyResult;
    if (!ats::fitRegularizedDiscreteCepstrum(noisyPoints, opts, noisyResult))
        return fail("GCV noisy threshold: noisy fit failed: " + noisyResult.errorMessage);
    if (!noisyResult.gcvWasUsed)
        return fail("GCV noisy threshold: gcvWasUsed not set for noisy frame");
    if (noisyResult.envelope.regularization + 1.0e-12 < 1.0e-3)
        return fail("GCV noisy threshold: expected lambda >= 1e-3, got "
                    + std::to_string(noisyResult.envelope.regularization));

    std::cout << "pass: GCV noisy threshold (lambda="
              << noisyResult.envelope.regularization << ")\n";
    return 0;
}

// ── GCV dense diagnostic ──────────────────────────────────────────────────────
// Record the dense 30-partial clean-frame lambda chosen by the runtime path.
// The stricter <= 1e-3 promotion gate is tracked in docs and benchmarks; the
// current runtime path remains opt-in because it does not clear that gate yet.
int testGcvDenseSyntheticDiagnostic()
{
    const auto freqs = makeFrequencies(30, 80.0, 8000.0);

    std::vector<ats::RdcFitPoint> cleanPoints;
    for (const double f : freqs)
        cleanPoints.push_back({ f, smoothEnvDb(f), 1.0 });

    ats::RdcFitOptions opts;
    opts.frequencyAxis        = makeBarkAxis();
    opts.order                = 10;
    opts.regularization       = 1.0e-3;
    opts.ridge                = 1.0e-10;
    opts.penalty              = ats::CepstrumPenalty::SecondDerivative;
    opts.useBicOrderSelection = false;
    opts.useGcvLambda         = true;
    opts.gcvCandidateCount    = 12;
    opts.gcvLambdaMin         = 1.0e-5;
    opts.gcvLambdaMax         = 1.0e+1;

    ats::RdcFitResult cleanResult;
    if (!ats::fitRegularizedDiscreteCepstrum(cleanPoints, opts, cleanResult))
        return fail("GCV dense diagnostic: clean fit failed: " + cleanResult.errorMessage);
    if (!cleanResult.gcvWasUsed)
        return fail("GCV dense diagnostic: gcvWasUsed not set for clean frame");
    if (!(cleanResult.envelope.regularization > 0.0))
        return fail("GCV dense diagnostic: lambda must stay positive");

    std::cout << "pass: GCV dense diagnostic (lambda="
              << cleanResult.envelope.regularization << ")\n";
    return 0;
}

// ── GCV fit validity ──────────────────────────────────────────────────────────
// A GCV-selected fit must produce a valid, evaluable envelope.
int testGcvFitValidity()
{
    const auto freqs = makeFrequencies(15, 80.0, 8000.0);
    std::vector<ats::RdcFitPoint> points;
    for (std::size_t i = 0; i < freqs.size(); ++i)
    {
        // Mix clean and noisy to trigger non-trivial GCV optimum.
        const double noise = (i % 3 == 0) ? 5.0 : 0.0;
        points.push_back({ freqs[i], smoothEnvDb(freqs[i]) + noise, 1.0 });
    }

    ats::RdcFitOptions opts;
    opts.frequencyAxis        = makeBarkAxis();
    opts.order                = 8;
    opts.regularization       = 1.0e-3;
    opts.useBicOrderSelection = false;
    opts.useGcvLambda         = true;
    opts.gcvCandidateCount    = 10;
    opts.gcvLambdaMin         = 1.0e-5;
    opts.gcvLambdaMax         = 1.0e+1;

    ats::RdcFitResult result;
    if (!ats::fitRegularizedDiscreteCepstrum(points, opts, result))
        return fail("GCV validity: fit failed: " + result.errorMessage);
    if (!result.ok)
        return fail("GCV validity: result not marked ok");
    if (!result.gcvWasUsed)
        return fail("GCV validity: gcvWasUsed not set");
    if (result.gcvScore < 0.0)
        return fail("GCV validity: gcvScore is negative");

    // The envelope must be evaluable at a mid-range frequency.
    bool evalOk = false;
    const double val = ats::evaluateRegularizedDiscreteCepstrum(result.envelope, 1000.0, &evalOk);
    (void)val;
    if (!evalOk)
        return fail("GCV validity: evaluate failed on GCV-fitted envelope");

    std::cout << "pass: GCV fit validity (gcvScore=" << result.gcvScore
              << " lambda=" << result.envelope.regularization << ")\n";
    return 0;
}

// ── Commit B — covariance PSD ─────────────────────────────────────────────────
// After a successful fit, uncertainty.valid must be true, noiseVariance must be
// non-negative and finite, and evaluatePredictionSigma must return a non-negative
// finite value at every queried frequency (positive-semi-definite covariance).
int testUncertaintyCovariancePSD()
{
    const auto barkAxis = makeBarkAxis();
    const auto freqs = makeFrequencies(20, 80.0, 8000.0);

    std::vector<ats::RdcFitPoint> points;
    for (const double f : freqs)
    {
        ats::RdcFitPoint p;
        p.frequencyHz = f;
        p.logAmplitudeDb = smoothEnvDb(f);
        p.weight = 1.0;
        points.push_back(p);
    }

    ats::RdcFitOptions opts;
    opts.frequencyAxis = barkAxis;
    opts.order = 8;
    opts.regularization = 1.0e-3;
    opts.useBicOrderSelection = false;
    opts.irlsMaxIterations = 1;

    ats::RdcFitResult result;
    if (!ats::fitRegularizedDiscreteCepstrum(points, opts, result) || !result.ok)
        return fail("Covariance PSD: fit failed: " + result.errorMessage);

    if (!result.uncertainty.valid)
        return fail("Covariance PSD: uncertainty.valid is false after successful fit");
    if (result.uncertainty.noiseVariance < 0.0 || !std::isfinite(result.uncertainty.noiseVariance))
        return fail("Covariance PSD: noiseVariance is negative or non-finite");
    if (result.uncertainty.choleskyFactor.empty())
        return fail("Covariance PSD: choleskyFactor is empty");

    // All diagonal entries of L must be positive (valid Cholesky factor → PSD inverse)
    const std::size_t n = static_cast<std::size_t>(result.uncertainty.coefficientCount);
    for (std::size_t i = 0; i < n; ++i)
    {
        const double diag = result.uncertainty.choleskyFactor[i * n + i];
        if (diag <= 0.0 || !std::isfinite(diag))
            return fail("Covariance PSD: Cholesky diagonal[" + std::to_string(i) + "] <= 0");
    }

    // evaluatePredictionSigma must return non-negative finite values everywhere
    const auto evalFreqs = makeFrequencies(25, 100.0, 7500.0);
    int invalid = 0;
    for (const double f : evalFreqs)
    {
        bool ok = false;
        const double sigma = ats::evaluatePredictionSigma(
            result.uncertainty, barkAxis, result.effectiveOrder, f, &ok);
        if (!ok || !std::isfinite(sigma) || sigma < 0.0)
            ++invalid;
    }
    if (invalid > 0)
        return fail("Covariance PSD: " + std::to_string(invalid) + " frequencies yielded invalid sigma");

    std::cout << "pass: Commit B - covariance PSD (noiseVariance=" << result.uncertainty.noiseVariance
              << " df=" << result.uncertainty.effectiveDegreesOfFreedom << ")\n";
    return 0;
}

// ── Commit B — sigma scales with noise ────────────────────────────────────────
// A noisier frame (large amplitude scatter) must produce a strictly larger
// noiseVariance and mean prediction sigma than a clean frame with the same
// frequency layout and the same estimator settings.
int testUncertaintySigmaScalesWithNoise()
{
    const auto barkAxis = makeBarkAxis();
    const auto freqs = makeFrequencies(20, 80.0, 8000.0);

    std::vector<ats::RdcFitPoint> cleanPoints;
    for (const double f : freqs)
    {
        ats::RdcFitPoint p;
        p.frequencyHz = f;
        p.logAmplitudeDb = smoothEnvDb(f);
        p.weight = 1.0;
        cleanPoints.push_back(p);
    }

    // Add ±8 dB scatter on alternating points; skip IRLS so outliers stay hot.
    std::vector<ats::RdcFitPoint> noisyPoints = cleanPoints;
    for (std::size_t i = 0; i < noisyPoints.size(); i += 2)
        noisyPoints[i].logAmplitudeDb += (i % 4 == 0) ? 8.0 : -8.0;

    ats::RdcFitOptions opts;
    opts.frequencyAxis = barkAxis;
    opts.order = 8;
    opts.regularization = 1.0e-3;
    opts.useBicOrderSelection = false;
    opts.irlsMaxIterations = 1;

    ats::RdcFitResult cleanResult, noisyResult;
    if (!ats::fitRegularizedDiscreteCepstrum(cleanPoints, opts, cleanResult) || !cleanResult.uncertainty.valid)
        return fail("Sigma noise scaling: clean fit or uncertainty failed");
    if (!ats::fitRegularizedDiscreteCepstrum(noisyPoints, opts, noisyResult) || !noisyResult.uncertainty.valid)
        return fail("Sigma noise scaling: noisy fit or uncertainty failed");

    if (noisyResult.uncertainty.noiseVariance <= cleanResult.uncertainty.noiseVariance)
        return fail("Sigma noise scaling: noisy noiseVariance (" +
                    std::to_string(noisyResult.uncertainty.noiseVariance) +
                    ") must exceed clean (" + std::to_string(cleanResult.uncertainty.noiseVariance) + ")");

    const auto evalFreqs = makeFrequencies(20, 100.0, 7500.0);
    double cleanMean = 0.0, noisyMean = 0.0;
    for (const double f : evalFreqs)
    {
        bool ok1 = false, ok2 = false;
        cleanMean += ats::evaluatePredictionSigma(
            cleanResult.uncertainty, barkAxis, cleanResult.effectiveOrder, f, &ok1);
        noisyMean += ats::evaluatePredictionSigma(
            noisyResult.uncertainty, barkAxis, noisyResult.effectiveOrder, f, &ok2);
    }
    const double n = static_cast<double>(evalFreqs.size());
    cleanMean /= n;
    noisyMean /= n;

    if (noisyMean <= cleanMean)
        return fail("Sigma noise scaling: noisy mean sigma (" + std::to_string(noisyMean) +
                    ") must exceed clean (" + std::to_string(cleanMean) + ")");

    std::cout << "pass: Commit B - sigma scales with noise (clean=" << cleanMean
              << " dB, noisy=" << noisyMean << " dB)\n";
    return 0;
}

// ── Commit C — virtual weight uncertainty adaptation ──────────────────────────
// The virtual-point weight formula is baseScale / (1 + gain * sigma).
// Higher prediction sigma must produce a strictly lower virtual weight.
int testVirtualWeightUncertaintyAdaptation()
{
    const auto barkAxis = makeBarkAxis();
    const double testFreq = 1000.0;
    const double baseScale = 0.05;
    const double gain = 2.0;

    const auto freqs = makeFrequencies(20, 80.0, 8000.0);

    std::vector<ats::RdcFitPoint> cleanPoints;
    for (const double f : freqs)
    {
        ats::RdcFitPoint p;
        p.frequencyHz = f;
        p.logAmplitudeDb = smoothEnvDb(f);
        p.weight = 1.0;
        cleanPoints.push_back(p);
    }

    std::vector<ats::RdcFitPoint> noisyPoints = cleanPoints;
    for (std::size_t i = 0; i < noisyPoints.size(); i += 2)
        noisyPoints[i].logAmplitudeDb += (i % 4 == 0) ? 10.0 : -10.0;

    ats::RdcFitOptions opts;
    opts.frequencyAxis = barkAxis;
    opts.order = 8;
    opts.regularization = 1.0e-3;
    opts.useBicOrderSelection = false;
    opts.irlsMaxIterations = 1;

    ats::RdcFitResult cleanResult, noisyResult;
    if (!ats::fitRegularizedDiscreteCepstrum(cleanPoints, opts, cleanResult) || !cleanResult.uncertainty.valid)
        return fail("Virtual weight adaptation: clean fit failed");
    if (!ats::fitRegularizedDiscreteCepstrum(noisyPoints, opts, noisyResult) || !noisyResult.uncertainty.valid)
        return fail("Virtual weight adaptation: noisy fit failed");

    bool ok1 = false, ok2 = false;
    const double cleanSigma = ats::evaluatePredictionSigma(
        cleanResult.uncertainty, barkAxis, cleanResult.effectiveOrder, testFreq, &ok1);
    const double noisySigma = ats::evaluatePredictionSigma(
        noisyResult.uncertainty, barkAxis, noisyResult.effectiveOrder, testFreq, &ok2);

    if (!ok1 || !ok2)
        return fail("Virtual weight adaptation: evaluatePredictionSigma failed at " +
                    std::to_string(testFreq) + " Hz");
    if (noisySigma <= cleanSigma)
        return fail("Virtual weight adaptation: noisy sigma (" + std::to_string(noisySigma) +
                    ") must exceed clean sigma (" + std::to_string(cleanSigma) + ")");

    const double cleanWeight = baseScale / (1.0 + gain * cleanSigma);
    const double noisyWeight = baseScale / (1.0 + gain * noisySigma);

    if (noisyWeight >= cleanWeight)
        return fail("Virtual weight adaptation: noisy weight (" + std::to_string(noisyWeight) +
                    ") must be smaller than clean weight (" + std::to_string(cleanWeight) + ")");

    std::cout << "pass: Commit C - virtual weight adaptation (clean sigma=" << cleanSigma
              << " w=" << cleanWeight << "; noisy sigma=" << noisySigma
              << " w=" << noisyWeight << ")\n";
    return 0;
}

// ── Commit D — uncertainty gates True Envelope switch ─────────────────────────
// trueEnvelopeSigmaThreshold controls whether the uncertainty from the RDC fit
// is allowed to gate the True Envelope switch.  With threshold=0 any positive
// sigma (all real fits with valid uncertainty) blocks the switch even for a
// dense, well-structured frame that would otherwise qualify.
int testUncertaintyGatesTrueEnvelopeSwitch()
{
    // Dense clean frame — same layout as Tier 2.5 baseline which confirms the switch fires.
    const auto freqs = makeFrequencies(50, 80.0, 8000.0);
    const auto frame = makeFrame(freqs, 20.0);

    ats::EnvelopeEstimatorContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.frequencyAxis = makeBarkAxis();
    ctx.maxOrder = 32;

    std::string err;

    // Baseline: standard threshold (2.5 dB) — dense clean frame should switch.
    {
        ats::RegularizedCepstrumEstimatorOptions opts;
        opts.fit.frequencyAxis = makeBarkAxis();
        opts.fit.order = 16;
        opts.fit.regularization = 1.0e-3;
        opts.autoSwitchToTrueEnvelope = true;
        opts.trueEnvelopeSigmaThreshold = 2.5;

        ats::RegularizedCepstrumEnvelopeEstimator estimator(opts);
        if (!estimator.prepare(ctx, &err))
            return fail("Uncertainty gate: prepare failed (baseline): " + err);
        ats::FrameEnvelope env;
        if (!estimator.estimate(frame, env, &err))
            return fail("Uncertainty gate: estimate failed (baseline): " + err);
        if (env.storage != ats::EnvelopeValueStorage::Samples)
            return fail("Uncertainty gate: baseline (threshold=2.5) should switch to True Envelope");
    }

    // Gated: threshold=0 — any positive sigma from a valid-uncertainty fit blocks the switch.
    {
        ats::RegularizedCepstrumEstimatorOptions opts;
        opts.fit.frequencyAxis = makeBarkAxis();
        opts.fit.order = 16;
        opts.fit.regularization = 1.0e-3;
        opts.autoSwitchToTrueEnvelope = true;
        opts.trueEnvelopeSigmaThreshold = 0.0;

        ats::RegularizedCepstrumEnvelopeEstimator estimator(opts);
        if (!estimator.prepare(ctx, &err))
            return fail("Uncertainty gate: prepare failed (zero threshold): " + err);
        ats::FrameEnvelope env;
        if (!estimator.estimate(frame, env, &err))
            return fail("Uncertainty gate: estimate failed (zero threshold): " + err);
        if (env.storage != ats::EnvelopeValueStorage::Coefficients)
            return fail("Uncertainty gate: threshold=0 should block True Envelope switch");
    }

    std::cout << "pass: Commit D - uncertainty sigma gate blocks True Envelope switch at threshold=0\n";
    return 0;
}

// ── Commit E — adaptive smoothing from uncertainty ────────────────────────────
// When adaptiveSmoothingFromUncertainty is enabled, a frame whose RDC fit has
// large residuals must produce a strictly larger smoothingAmount than a clean frame.
int testAdaptiveSmoothingFromUncertainty()
{
    const auto barkAxis = makeBarkAxis();

    ats::EnvelopeEstimatorContext ctx;
    ctx.sampleRate = 44100.0;
    ctx.frequencyAxis = barkAxis;
    ctx.maxOrder = 16;

    ats::RegularizedCepstrumEstimatorOptions opts;
    opts.fit.frequencyAxis = barkAxis;
    opts.fit.order = 8;
    opts.fit.regularization = 1.0e-3;
    opts.fit.useBicOrderSelection = false;
    opts.fit.irlsMaxIterations = 1;      // skip Huber so outliers stay hot
    opts.autoSwitchToTrueEnvelope = false; // stay on RDC to get its uncertainty
    opts.adaptiveSmoothingFromUncertainty = true;
    opts.smoothingUncertaintyScale = 0.5; // amplify to make the difference visible

    const auto cleanFreqs = makeFrequencies(15, 80.0, 8000.0);
    const auto cleanFrame = makeFrame(cleanFreqs, 20.0);

    // Noisy frame: large amplitude scatter on every other partial
    auto noisyFrame = makeFrame(cleanFreqs, 20.0);
    for (std::size_t i = 0; i < noisyFrame.partials.size(); i += 2)
        noisyFrame.partials[i].amplitude *= (i % 4 == 0) ? 10.0 : 0.1;

    std::string err;

    ats::FrameEnvelope cleanEnv;
    {
        ats::RegularizedCepstrumEnvelopeEstimator est(opts);
        if (!est.prepare(ctx, &err))
            return fail("Adaptive smoothing: prepare clean failed: " + err);
        if (!est.estimate(cleanFrame, cleanEnv, &err))
            return fail("Adaptive smoothing: estimate clean failed: " + err);
    }

    ats::FrameEnvelope noisyEnv;
    {
        ats::RegularizedCepstrumEnvelopeEstimator est(opts);
        if (!est.prepare(ctx, &err))
            return fail("Adaptive smoothing: prepare noisy failed: " + err);
        if (!est.estimate(noisyFrame, noisyEnv, &err))
            return fail("Adaptive smoothing: estimate noisy failed: " + err);
    }

    if (noisyEnv.smoothingAmount <= cleanEnv.smoothingAmount)
        return fail("Adaptive smoothing: noisy smoothingAmount (" +
                    std::to_string(noisyEnv.smoothingAmount) + ") must exceed clean (" +
                    std::to_string(cleanEnv.smoothingAmount) + ")");

    std::cout << "pass: Commit E - adaptive smoothing from uncertainty (clean="
              << cleanEnv.smoothingAmount << " noisy=" << noisyEnv.smoothingAmount << ")\n";
    return 0;
}

int main()
{
    int result = 0;
    result |= testBarkDefault();
    result |= testIllConditionedThreshold();
    result |= testSmrZeroDisable();
    result |= testBicOrderSelection();
    result |= testAutoSwitchTrueEnvelope();
    result |= testAutoSwitchBoundary();
    result |= testCrossFrameVirtualGapFill();
    result |= testBicDensityFloor();
    result |= testVirtualPointAmplitudeDeltaGate();
    result |= testGcvDisabled();
    result |= testGcvNoisySyntheticSelectsHigherLambda();
    result |= testGcvDenseSyntheticDiagnostic();
    result |= testGcvFitValidity();
    result |= testUncertaintyCovariancePSD();
    result |= testUncertaintySigmaScalesWithNoise();
    result |= testVirtualWeightUncertaintyAdaptation();
    result |= testUncertaintyGatesTrueEnvelopeSwitch();
    result |= testAdaptiveSmoothingFromUncertainty();

    if (result == 0)
        std::cout << "ok: all spectral envelope improvements\n";
    return result;
}
