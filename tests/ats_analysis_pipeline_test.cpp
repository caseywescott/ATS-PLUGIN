#include "AtsAnalysisPipeline.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace
{

int failures = 0;

void expect(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << message << '\n';
        ++failures;
    }
}

ats::TrackingPeak peak(int sourceIndex, double frequency, double amplitude = 0.5, double smr = 60.0)
{
    ats::TrackingPeak p;
    p.sourceIndex = sourceIndex;
    p.frequencyHz = frequency;
    p.amplitude = amplitude;
    p.smrDb = smr;
    return p;
}

void testModeRegistry()
{
    const auto& modes = ats::analysisModeRegistry();
    expect(modes.size() == 3, "analysis registry should expose classic, modern, experimental");
    expect(ats::defaultAnalysisModeDescriptor().analysisMode == ats::ModernAtsAnalysisMode::ClassicAts,
           "classic ATS must remain the compatibility default");
    expect(ats::findAnalysisModeDescriptor("classic_ats") != nullptr, "classic mode missing");
    expect(ats::findAnalysisModeDescriptor("modern_ats") != nullptr, "modern mode missing");
    expect(ats::findAnalysisModeDescriptor("experimental") != nullptr, "experimental mode missing");
}

void testPeakEstimatorDiagnostics()
{
    std::vector<ats::SpectrumBin> bins = {
        { 0.0, -120.0, 0.0 },
        { 100.0, -75.0, 0.0 },
        { 200.0, -18.0, 0.1 },
        { 300.0, -24.0, 0.2 },
        { 400.0, -90.0, 0.3 },
        { 500.0, -80.0, 0.4 },
        { 600.0, -55.0, 0.5 },
        { 700.0, -65.0, 0.6 },
        { 800.0, -120.0, 0.7 },
    };

    ats::PeakEstimationConfig config;
    config.lowestFrequencyHz = 150.0;
    config.highestFrequencyHz = 650.0;
    config.magnitudeThresholdDb = -60.0;

    ats::ClassicStftPeakEstimator estimator;
    ats::PeakEstimationDiagnostics diagnostics;
    const auto peaks = estimator.estimatePeaks(bins, config, diagnostics);
    const auto peaksAgain = estimator.estimatePeaks(bins, config, diagnostics);

    expect(peaks.size() == 2, "expected two accepted peaks");
    expect(peaks.size() == peaksAgain.size(), "peak estimation should be deterministic");
    expect(diagnostics.rawPeaks == 2, "raw peak count mismatch");
    expect(diagnostics.acceptedPeaks == 2, "accepted peak count mismatch");
    expect(diagnostics.rejectedPeaks == 0, "unexpected rejected peak count");
    expect(peaks[0].frequencyHz > 200.0 && peaks[0].frequencyHz < 250.0, "first peak frequency mismatch");
    expect(peaks[1].frequencyHz > 600.0 && peaks[1].frequencyHz < 630.0, "second peak frequency mismatch");

    bins[2].magnitudeDb = std::numeric_limits<double>::quiet_NaN();
    const auto noFinitePeaks = estimator.estimatePeaks(bins, config, diagnostics);
    expect(noFinitePeaks.size() == 1, "non-finite peak bins should be ignored");
    expect(noFinitePeaks[0].frequencyHz > 600.0 && noFinitePeaks[0].frequencyHz < 630.0,
           "finite peak should remain after ignoring non-finite bins");
}

void testExperimentalPeakEstimatorAndComparison()
{
    std::vector<ats::SpectrumBin> bins = {
        { 0.0, -120.0, 0.0 },
        { 100.0, -70.0, 0.0 },
        { 200.0, -21.0, 0.0 },
        { 300.0, -16.0, 0.0 },
        { 400.0, -28.0, 0.0 },
        { 500.0, -65.0, 0.0 },
        { 600.0, -18.0, 0.0 },
        { 700.0, -22.0, 0.0 },
        { 800.0, -120.0, 0.0 },
    };

    ats::PeakEstimationConfig config;
    config.lowestFrequencyHz = 80.0;
    config.highestFrequencyHz = 760.0;
    config.magnitudeThresholdDb = -60.0;

    ats::ExperimentalReassignedPeakEstimator expEstimator;
    ats::PeakEstimationDiagnostics expDiag;
    const auto expPeaks = expEstimator.estimatePeaks(bins, config, expDiag);
    expect(!expPeaks.empty(), "experimental estimator should find peaks");
    expect(expDiag.acceptedPeaks == (int) expPeaks.size(), "experimental diagnostics mismatch");

    ats::ClassicStftPeakEstimator classicEstimator;
    const auto comparison =
        ats::comparePeakEstimators(classicEstimator, expEstimator, bins, config, 50.0);
    expect(comparison.baselineAcceptedPeaks > 0, "comparison baseline peaks missing");
    expect(comparison.candidateAcceptedPeaks > 0, "comparison candidate peaks missing");
    expect(comparison.matchedPeakRatio >= 0.0 && comparison.matchedPeakRatio <= 1.0,
           "comparison matched ratio out of range");

    const std::vector<ats::SpectrumBin> symmetricBins = {
        { 0.0, -120.0, 0.0 },
        { 100.0, -40.0, 0.0 },
        { 200.0, -10.0, 0.0 },
        { 300.0, -40.0, 0.0 },
        { 400.0, -120.0, 0.0 },
    };
    const auto centered = expEstimator.estimatePeaks(symmetricBins, config, expDiag);
    expect(centered.size() == 1, "symmetric experimental fixture should have one peak");
    expect(std::abs(centered[0].frequencyHz - 200.0) < 1.0e-9,
           "symmetric experimental peak should stay centered");

    const std::vector<ats::SpectrumBin> offBin = {
        { 0.0, -120.0, 0.0 },
        { 100.0, -18.0, 0.0 },
        { 200.0, -10.0, 0.0 },
        { 300.0, -14.0, 0.0 },
        { 400.0, -120.0, 0.0 },
    };
    const auto shifted = expEstimator.estimatePeaks(offBin, config, expDiag);
    expect(shifted.size() == 1, "off-bin experimental fixture should have one peak");
    expect(std::abs(shifted[0].frequencyHz - 216.6666666667) < 1.0e-6,
           "off-bin experimental peak frequency mismatch");
}

void testLifecycleGapFillAndPruning()
{
    const std::vector<bool> activity = {
        false, true, true, false, true, true, false, false, true, false
    };
    const auto events = ats::normalizeTrackLifecycleFromActivity(3, activity, 1, 2);

    bool sawBirth = false;
    bool sawGapFill = false;
    bool sawDeath = false;
    bool sawPruned = false;
    for (const auto& event : events)
    {
        if (event.kind == ats::ModernAtsLifecycleKind::Birth && event.frameIndex == 1)
            sawBirth = true;
        if (event.kind == ats::ModernAtsLifecycleKind::GapFill && event.frameIndex == 3)
            sawGapFill = true;
        if (event.kind == ats::ModernAtsLifecycleKind::Death && event.frameIndex == 5)
            sawDeath = true;
        if (event.kind == ats::ModernAtsLifecycleKind::Pruned && event.frameIndex == 8)
            sawPruned = true;
    }

    expect(sawBirth, "missing lifecycle birth event");
    expect(sawGapFill, "missing short-gap fill event");
    expect(sawDeath, "missing lifecycle death event");
    expect(sawPruned, "missing short-segment prune event");

    const std::vector<bool> attackActivity = { true, true, false, true, true };
    const auto protectedEvents =
        ats::normalizeTrackLifecycleFromActivity(4, attackActivity, 1, 2, { 2 }, false);
    bool filledAcrossAttack = false;
    bool brokeBeforeAttack = false;
    bool rebirthedAfterAttack = false;
    for (const auto& event : protectedEvents)
    {
        if (event.kind == ats::ModernAtsLifecycleKind::GapFill && event.frameIndex == 2)
            filledAcrossAttack = true;
        if (event.kind == ats::ModernAtsLifecycleKind::Death && event.frameIndex == 1)
            brokeBeforeAttack = true;
        if (event.kind == ats::ModernAtsLifecycleKind::Birth && event.frameIndex == 3)
            rebirthedAfterAttack = true;
    }
    expect(!filledAcrossAttack, "protected transient frame should block short-gap interpolation");
    expect(brokeBeforeAttack, "track should break before protected attack frame");
    expect(rebirthedAfterAttack, "track should be reborn after protected attack frame");
}

void testAssignmentTrackerCrossingPartials()
{
    const std::vector<ats::TrackingPeakFrame> crossing = {
        { peak(0, 400.0), peak(1, 700.0) },
        { peak(0, 500.0), peak(1, 600.0) },
        { peak(0, 500.0), peak(1, 600.0) },
    };

    ats::AssignmentTrackerConfig classicConfig;
    classicConfig.analysisMode = ats::ModernAtsAnalysisMode::ClassicAts;
    classicConfig.useFrequencySlopeContinuity = false;
    const auto classic = ats::runAssignmentTrackerPrototype(crossing, classicConfig);

    ats::AssignmentTrackerConfig modernConfig;
    modernConfig.analysisMode = ats::ModernAtsAnalysisMode::ModernAts;
    modernConfig.useFrequencySlopeContinuity = true;
    const auto modern = ats::runAssignmentTrackerPrototype(crossing, modernConfig);

    expect(classic.partials.size() == 2, "classic crossing tracker should produce two tracks");
    expect(modern.partials.size() == 2, "modern crossing tracker should produce two tracks");
    expect(classic.partials[0].frames[2].frequencyHz == 500.0,
           "classic nearest-frequency tracker expectation changed");
    expect(modern.partials[0].frames[2].frequencyHz == 600.0,
           "modern slope-aware tracker did not preserve crossing identity");
    expect(!modern.assignmentCosts.empty(), "modern tracker should record assignment costs");
}

void testTransientBreakResetsAssignment()
{
    const std::vector<ats::TrackingPeakFrame> pluckedLike = {
        { peak(0, 220.0) },
        { peak(0, 222.0) },
        { peak(0, 440.0, 0.9, 80.0) },
        { peak(0, 442.0, 0.8, 75.0) },
    };

    ats::AssignmentTrackerConfig config;
    config.analysisMode = ats::ModernAtsAnalysisMode::ModernAts;
    config.transientBreakFrameIndices = { 2 };
    const auto result = ats::runAssignmentTrackerPrototype(pluckedLike, config);

    expect(result.partials.size() == 2, "transient break should reset assignment into a new track");
    bool sawTransientDeath = false;
    bool sawAttackBirth = false;
    for (const auto& event : result.trackLifecycle)
    {
        if (event.kind == ats::ModernAtsLifecycleKind::Death && event.frameIndex == 2
            && event.detail == "transient_break")
            sawTransientDeath = true;
        if (event.kind == ats::ModernAtsLifecycleKind::Birth && event.frameIndex == 2)
            sawAttackBirth = true;
    }
    expect(sawTransientDeath, "transient break should emit a death/reset event");
    expect(sawAttackBirth, "transient break should birth attack-side peaks");
}

void testOrphanResidualNoise()
{
    const std::vector<double> total = { 1.0, 0.5, 0.25 };
    const std::vector<double> assigned = { 0.25, 0.5, 0.5 };
    const auto orphan = ats::computeOrphanResidualBandEnergy(total, assigned);
    expect(orphan.size() == 3, "orphan residual size mismatch");
    expect(std::abs(orphan[0] - 0.75) < 1.0e-12, "orphan residual band 0 mismatch");
    expect(orphan[1] == 0.0, "orphan residual band 1 should be zero");
    expect(orphan[2] == 0.0, "orphan residual should clamp over-assigned bands to zero");

    const std::vector<std::vector<double>> frames = { orphan, orphan };
    const auto renderA = ats::renderOrphanResidualBandNoise(frames, 16, 0.5f, 1234u);
    const auto renderB = ats::renderOrphanResidualBandNoise(frames, 16, 0.5f, 1234u);
    expect(renderA.size() == 32, "orphan residual render length mismatch");
    expect(renderA == renderB, "orphan residual render should be deterministic for a fixed seed");

    float peakAbs = 0.0f;
    for (const float sample : renderA)
        peakAbs = std::max(peakAbs, std::abs(sample));
    expect(peakAbs > 0.0f, "orphan residual render should produce non-zero output");

    const std::vector<std::vector<double>> nonFiniteFrames = {
        { std::numeric_limits<double>::quiet_NaN(), 0.25 }
    };
    const auto finiteRender = ats::renderOrphanResidualBandNoise(nonFiniteFrames, 8, 1.0f, 42u);
    expect(finiteRender.size() == 8, "non-finite orphan bands should not abort finite render");
    for (const float sample : finiteRender)
        expect(std::isfinite(sample), "non-finite orphan bands should not produce non-finite output");

    const auto mutedNaNGain = ats::renderOrphanResidualBandNoise(frames, 16,
                                                                std::numeric_limits<float>::quiet_NaN(),
                                                                1234u);
    expect(mutedNaNGain.empty(), "non-finite orphan residual gain should be rejected");
}

void testTransientDetection()
{
    std::vector<ats::TransientFeatureFrame> features;
    for (int i = 0; i < 8; ++i)
    {
        ats::TransientFeatureFrame frame;
        frame.timeSeconds = 0.01 * (double)i;
        frame.energy = i == 3 ? 2.0 : 0.25;
        frame.spectralFlux = i == 3 ? 3.0 : 0.02;
        frame.modelDiscontinuity = i == 3 ? 1.0 : 0.0;
        features.push_back(frame);
    }

    ats::TransientDetectionConfig config;
    config.minimumScore = 0.5;
    config.thresholdStdDevMultiplier = 0.5;
    config.preWindowSeconds = 0.005;
    config.postWindowSeconds = 0.02;

    ats::TransientDetectionDiagnostics diagnostics;
    const auto markers = ats::detectTransientMarkers(features, config, diagnostics);
    expect(markers.size() == 1, "expected one transient marker");
    expect(markers[0].frameIndex == 3, "transient marker frame mismatch");
    expect(markers[0].windowStartSeconds < markers[0].onsetSeconds, "transient start window mismatch");
    expect(markers[0].windowEndSeconds > markers[0].onsetSeconds, "transient end window mismatch");
    expect(diagnostics.threshold >= config.minimumScore, "transient diagnostic threshold missing");
    expect(diagnostics.scores.size() == features.size(), "transient diagnostic scores missing");
}

void testExperimentalTransientLanePrototype()
{
    std::vector<ats::ModernAtsTransientMarker> markers;
    ats::ModernAtsTransientMarker a;
    a.onsetSeconds = 0.05;
    a.windowStartSeconds = 0.04;
    a.windowEndSeconds = 0.07;
    a.gain = 1.0;
    markers.push_back(a);
    ats::ModernAtsTransientMarker b;
    b.onsetSeconds = 0.12;
    b.windowStartSeconds = 0.11;
    b.windowEndSeconds = 0.14;
    b.gain = 0.7;
    markers.push_back(b);

    const auto lane = ats::buildExperimentalTransientLane(markers, 48000, 8);
    expect(lane.atoms.size() == markers.size(), "transient lane atom count mismatch");
    const auto render = ats::synthesizeExperimentalTransientLane(lane);
    expect(!render.empty(), "transient lane render should produce output");

    ats::ModernAtsTransientBuffer preserved;
    preserved.sampleRate = 48000;
    preserved.samples = std::vector<float>(render.begin(), render.begin() + std::min<std::size_t>(render.size(), 256));
    const auto comparison = ats::compareTransientLanes({ preserved }, lane);
    expect(comparison.bufferTransientCount == 1, "transient lane comparison buffer count mismatch");
    expect(comparison.laneTransientCount == 2, "transient lane comparison lane count mismatch");
}

void testClassicAtsAnalysisEmptyInput()
{
    ats::AnalysisParams params;
    const std::vector<double> empty;
    const auto result = ats::runClassicAtsAnalysis(params, empty, 48000);
    expect(!result.errorMessage.empty(), "empty input should produce an error message");
    expect(!result.sound, "empty input should not produce a model");

    const std::vector<double> oneFrame(1, 0.0);
    const auto invalidRate = ats::runClassicAtsAnalysis(params, oneFrame, -1);
    expect(!invalidRate.errorMessage.empty(), "invalid sample rate should produce an error message");
    expect(!invalidRate.sound, "invalid sample rate should not produce a model");
}

void testLifecycleGapFillAllowedAcrossProtectedFrame()
{
    // With allowGapFillAcrossProtectedFrames=true, gap-fill SHOULD bridge the protected frame.
    const std::vector<bool> activity = { true, true, false, true, true };
    const auto events =
        ats::normalizeTrackLifecycleFromActivity(5, activity, 1, 2, { 2 }, true);

    bool filledAcrossAttack = false;
    for (const auto& event : events)
    {
        if (event.kind == ats::ModernAtsLifecycleKind::GapFill && event.frameIndex == 2)
            filledAcrossAttack = true;
    }
    expect(filledAcrossAttack, "gap fill should bridge protected frame when explicitly allowed");
}

void testDdspHelperPolicy()
{
    const auto allowed = ats::evaluateDdspHelperPolicy("ddsp_denoiser",
                                                       { "parameter_estimation", "analysis_denoising" },
                                                       false);
    expect(allowed.keepsClassicModelAsSourceOfTruth, "allowed DDSP helper should keep classical model");
    expect(allowed.recommendation == "allow_offline_assistive_ddsp_helper_only",
           "allowed DDSP helper recommendation mismatch");

    const auto blocked = ats::evaluateDdspHelperPolicy("ddsp_runtime",
                                                       { "runtime_core_representation" },
                                                       true);
    expect(!blocked.keepsClassicModelAsSourceOfTruth, "blocked DDSP helper should not preserve source-of-truth rule");
    expect(blocked.recommendation.find("defer_ddsp_helper") != std::string::npos,
           "blocked DDSP helper should be deferred");
}

} // namespace

int main()
{
    testModeRegistry();
    testPeakEstimatorDiagnostics();
    testExperimentalPeakEstimatorAndComparison();
    testLifecycleGapFillAndPruning();
    testLifecycleGapFillAllowedAcrossProtectedFrame();
    testAssignmentTrackerCrossingPartials();
    testTransientBreakResetsAssignment();
    testOrphanResidualNoise();
    testTransientDetection();
    testExperimentalTransientLanePrototype();
    testClassicAtsAnalysisEmptyInput();
    testDdspHelperPolicy();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
