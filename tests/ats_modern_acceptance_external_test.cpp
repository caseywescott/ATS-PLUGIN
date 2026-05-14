// ats_modern_acceptance_external_test.cpp
//
// Unit tests for the external fixture gate evaluation logic
// (evaluateExternalFixtureGates, parseExternalFixtureGate,
// loadExternalFixtureReferenceMetrics).
//
// This binary runs with SYNTHETIC data so it is always deterministic and
// never requires external audio assets or real ATSA reference files.
// It is always built and registered as a CTest regardless of the
// ATS_ENABLE_EXTERNAL_FIXTURES option.
//
// When ATS_ENABLE_EXTERNAL_FIXTURES=ON, the separate Python CTest
// run_external_acceptance.py adds real-audio comparisons on top of these gates.

#include "AtsModernAcceptance.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace
{

int failures = 0;

void expect(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

bool hasCheck(const ats::ModernAtsAcceptanceReport& report, const std::string& prefix)
{
    for (const auto& check : report.checks)
    {
        if (check.id.find(prefix) == 0)
            return true;
    }
    return false;
}

bool checkPassed(const ats::ModernAtsAcceptanceReport& report, const std::string& prefix)
{
    for (const auto& check : report.checks)
    {
        if (check.id.find(prefix) == 0)
            return check.passed;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Gate name parsing
// ---------------------------------------------------------------------------

void testParseExternalFixtureGate()
{
    ats::ExternalFixtureGate gate;
    expect(ats::parseExternalFixtureGate("classic_parity", gate)
               && gate == ats::ExternalFixtureGate::ClassicParity,
           "parseExternalFixtureGate: classic_parity");
    expect(ats::parseExternalFixtureGate("tracker_continuity", gate)
               && gate == ats::ExternalFixtureGate::TrackerContinuity,
           "parseExternalFixtureGate: tracker_continuity");
    expect(ats::parseExternalFixtureGate("residual_energy_preservation", gate)
               && gate == ats::ExternalFixtureGate::ResidualEnergyPreservation,
           "parseExternalFixtureGate: residual_energy_preservation");
    expect(ats::parseExternalFixtureGate("transient_preservation", gate)
               && gate == ats::ExternalFixtureGate::TransientPreservation,
           "parseExternalFixtureGate: transient_preservation");
    expect(!ats::parseExternalFixtureGate("unknown_gate", gate),
           "parseExternalFixtureGate: unknown name returns false");
}

// ---------------------------------------------------------------------------
// Classic parity gate
// ---------------------------------------------------------------------------

void testClassicParityGatePasses()
{
    ats::ExternalFixtureReferenceMetrics ref;
    ref.fixtureId = "test_fixture";
    ref.normalizedRmsError = 0.0005;
    ref.spectralDistanceDb = 0.2;

    ats::ExternalFixtureReferenceMetrics our;
    our.fixtureId = "test_fixture";
    our.normalizedRmsError = 0.0006;  // within allowed regression threshold
    our.spectralDistanceDb = 0.25;    // within allowed regression threshold

    const auto report = ats::evaluateExternalFixtureGates(
        our, ref, { ats::ExternalFixtureGate::ClassicParity });

    expect(report.passed(), "classic parity: small delta should pass");
    expect(hasCheck(report, "test_fixture.classic_parity.normalized_rms_error"),
           "classic parity: check id includes fixture id and metric name");
    expect(hasCheck(report, "test_fixture.classic_parity.spectral_distance_db"),
           "classic parity: spectral distance check present");
}

void testClassicParityAllowsCandidateImprovement()
{
    ats::ExternalFixtureReferenceMetrics ref;
    ref.fixtureId = "better_fixture";
    ref.normalizedRmsError = 0.75;
    ref.spectralDistanceDb = 12.0;

    ats::ExternalFixtureReferenceMetrics our;
    our.fixtureId = "better_fixture";
    our.normalizedRmsError = 0.25;
    our.spectralDistanceDb = 4.0;

    const auto report = ats::evaluateExternalFixtureGates(
        our, ref, { ats::ExternalFixtureGate::ClassicParity });

    expect(report.passed(), "classic parity: candidate improvements should pass");
}

void testClassicParityGateFails()
{
    ats::ExternalFixtureReferenceMetrics ref;
    ref.fixtureId = "flute_a4";
    ref.normalizedRmsError = 0.001;
    ref.spectralDistanceDb = 0.1;

    ats::ExternalFixtureReferenceMetrics our;
    our.fixtureId = "flute_a4";
    our.normalizedRmsError = 0.005;  // worse than reference plus threshold
    our.spectralDistanceDb = 1.5;    // worse than reference plus threshold

    const auto report = ats::evaluateExternalFixtureGates(
        our, ref, { ats::ExternalFixtureGate::ClassicParity });

    expect(!report.passed(), "classic parity: large delta should fail");
    expect(!report.failureMessages().empty(), "classic parity: failures have messages");
    const auto failures_msgs = report.failureMessages();
    // Failure messages must include the fixture id for diagnosis.
    bool hasFixtureId = false;
    for (const auto& msg : failures_msgs)
    {
        if (msg.find("flute_a4") != std::string::npos)
            hasFixtureId = true;
    }
    expect(hasFixtureId, "classic parity failure message includes fixture id");
}

// ---------------------------------------------------------------------------
// Tracker continuity gate
// ---------------------------------------------------------------------------

void testTrackerContinuityGatePasses()
{
    ats::ExternalFixtureReferenceMetrics ref;
    ref.fixtureId = "sustained_tone";
    ref.continuityScore = 0.92;
    ref.identityErrors = 2;

    ats::ExternalFixtureReferenceMetrics our;
    our.fixtureId = "sustained_tone";
    our.continuityScore = 0.90;   // regression = 0.02 < 0.05 threshold
    our.identityErrors = 3;       // increase = 1 < 2 threshold

    const auto report = ats::evaluateExternalFixtureGates(
        our, ref, { ats::ExternalFixtureGate::TrackerContinuity });

    expect(report.passed(), "tracker continuity: small regression should pass");
    expect(hasCheck(report, "sustained_tone.tracker_continuity.continuity_score"),
           "tracker continuity: continuity_score check present");
    expect(hasCheck(report, "sustained_tone.tracker_continuity.identity_errors"),
           "tracker continuity: identity_errors check present");
}

void testTrackerContinuityGateFails()
{
    ats::ExternalFixtureReferenceMetrics ref;
    ref.fixtureId = "crossing";
    ref.continuityScore = 0.90;
    ref.identityErrors = 0;

    ats::ExternalFixtureReferenceMetrics our;
    our.fixtureId = "crossing";
    our.continuityScore = 0.75;  // regression = 0.15 > 0.05 threshold
    our.identityErrors = 5;      // increase = 5 > 2 threshold

    const auto report = ats::evaluateExternalFixtureGates(
        our, ref, { ats::ExternalFixtureGate::TrackerContinuity });

    expect(!report.passed(), "tracker continuity: large regression should fail");
}

void testTrackerContinuitySkipsUnavailableFields()
{
    // Fields at -1 mean "not available from reference artifact".
    ats::ExternalFixtureReferenceMetrics ref;
    ref.fixtureId = "fixture_no_continuity";
    ref.continuityScore = -1.0;  // not available
    ref.identityErrors = -1;     // not available

    ats::ExternalFixtureReferenceMetrics our;
    our.fixtureId = "fixture_no_continuity";
    our.continuityScore = 0.85;
    our.identityErrors = 1;

    const auto report = ats::evaluateExternalFixtureGates(
        our, ref, { ats::ExternalFixtureGate::TrackerContinuity });

    // No checks emitted when reference fields are unavailable.
    expect(report.checks.empty(),
           "tracker continuity: no checks when reference fields are unavailable");
}

// ---------------------------------------------------------------------------
// Residual energy preservation gate
// ---------------------------------------------------------------------------

void testResidualEnergyGatePasses()
{
    ats::ExternalFixtureReferenceMetrics ref;
    ref.fixtureId = "flute_breathy";
    ref.residualHighpassRatio = 0.35; // ATSA can differ from source highpass character.

    ats::ExternalFixtureReferenceMetrics our;
    our.fixtureId = "flute_breathy";
    our.sourceResidualHighpassRatio = 0.20;
    our.residualHighpassRatio = 0.204;  // source delta = 0.004 < 0.005 threshold

    const auto report = ats::evaluateExternalFixtureGates(
        our, ref, { ats::ExternalFixtureGate::ResidualEnergyPreservation });

    expect(report.passed(), "residual energy: small source-ratio delta should pass");
    expect(hasCheck(report, "flute_breathy.residual_energy_preservation"),
           "residual energy: check id present");
}

void testResidualEnergyGateFails()
{
    ats::ExternalFixtureReferenceMetrics ref;
    ref.fixtureId = "noisy_voice";
    ref.orphanResidualRatio = 0.35;

    ats::ExternalFixtureReferenceMetrics our;
    our.fixtureId = "noisy_voice";
    our.orphanResidualRatio = 0.34;  // delta = 0.01 > 0.005 threshold

    const auto report = ats::evaluateExternalFixtureGates(
        our, ref, { ats::ExternalFixtureGate::ResidualEnergyPreservation });

    expect(!report.passed(), "residual energy: ratio delta above tightened threshold should fail");
}

// ---------------------------------------------------------------------------
// Transient preservation gate
// ---------------------------------------------------------------------------

void testTransientPreservationGatePasses()
{
    ats::ExternalFixtureReferenceMetrics ref;
    ref.fixtureId = "plucked_guitar";
    ref.attackSharpness = 30.0; // ATSA can over-sharpen by under-rendering RMS.

    ats::ExternalFixtureReferenceMetrics our;
    our.fixtureId = "plucked_guitar";
    our.sourceAttackSharpness = 3.5;
    our.attackSharpness = 3.4;  // ratio = 3.4/3.5 = 0.971 >= 0.95 threshold

    const auto report = ats::evaluateExternalFixtureGates(
        our, ref, { ats::ExternalFixtureGate::TransientPreservation });

    expect(report.passed(), "transient preservation: source-ratio above 0.95 should pass");
}

void testTransientPreservationGateFails()
{
    ats::ExternalFixtureReferenceMetrics ref;
    ref.fixtureId = "drum_hit";
    ref.attackSharpness = 5.0;

    ats::ExternalFixtureReferenceMetrics our;
    our.fixtureId = "drum_hit";
    our.attackSharpness = 2.0;  // ratio = 0.40 < 0.95 threshold

    const auto report = ats::evaluateExternalFixtureGates(
        our, ref, { ats::ExternalFixtureGate::TransientPreservation });

    expect(!report.passed(), "transient preservation: ratio below 0.95 should fail");
}

// ---------------------------------------------------------------------------
// Multiple gates
// ---------------------------------------------------------------------------

void testMultipleGatesAllPass()
{
    ats::ExternalFixtureReferenceMetrics ref;
    ref.fixtureId = "sung_vowel";
    ref.normalizedRmsError = 0.001;
    ref.spectralDistanceDb = 0.3;
    ref.continuityScore = 0.88;
    ref.identityErrors = 1;
    ref.orphanResidualRatio = 0.18;

    ats::ExternalFixtureReferenceMetrics our = ref;  // identical = perfect match

    const auto report = ats::evaluateExternalFixtureGates(
        our, ref,
        { ats::ExternalFixtureGate::ClassicParity,
          ats::ExternalFixtureGate::TrackerContinuity,
          ats::ExternalFixtureGate::ResidualEnergyPreservation });

    expect(report.passed(), "multiple gates: identical metrics should all pass");
    expect(report.checks.size() >= 4, "multiple gates: all checks emitted");
}

void testMultipleGatesMixedResults()
{
    ats::ExternalFixtureReferenceMetrics ref;
    ref.fixtureId = "mixed_fixture";
    ref.normalizedRmsError = 0.001;
    ref.spectralDistanceDb = 0.2;
    ref.continuityScore = 0.90;
    ref.orphanResidualRatio = 0.20;

    ats::ExternalFixtureReferenceMetrics our;
    our.fixtureId = "mixed_fixture";
    our.normalizedRmsError = 0.001;   // delta = 0 -> pass
    our.spectralDistanceDb = 0.21;    // delta = 0.01 -> pass
    our.continuityScore = 0.70;       // regression = 0.20 -> FAIL
    our.orphanResidualRatio = 0.21;   // delta = 0.01 -> pass

    const auto report = ats::evaluateExternalFixtureGates(
        our, ref,
        { ats::ExternalFixtureGate::ClassicParity,
          ats::ExternalFixtureGate::TrackerContinuity,
          ats::ExternalFixtureGate::ResidualEnergyPreservation });

    expect(!report.passed(), "mixed results: overall report should fail");
    // Classic parity checks should pass.
    expect(checkPassed(report, "mixed_fixture.classic_parity.normalized_rms_error"),
           "mixed results: nrms check should pass");
    // Continuity should fail.
    expect(!checkPassed(report, "mixed_fixture.tracker_continuity.continuity_score"),
           "mixed results: continuity check should fail");
}

// ---------------------------------------------------------------------------
// Threshold overrides
// ---------------------------------------------------------------------------

void testThresholdOverrides()
{
    ats::ExternalFixtureReferenceMetrics ref;
    ref.fixtureId = "fixture_strict";
    ref.normalizedRmsError = 0.001;
    ref.spectralDistanceDb = 0.5;

    ats::ExternalFixtureReferenceMetrics our;
    our.fixtureId = "fixture_strict";
    our.normalizedRmsError = 0.0012;  // delta = 0.0002
    our.spectralDistanceDb = 0.55;    // delta = 0.05

    // With defaults (5e-4 nrms, 0.5 spec), delta 0.0002 passes but we test overrides.
    ats::ExternalFixtureThresholds strict;
    strict.maxNormalizedRmsErrorDelta = 1.0e-5;  // very tight: 0.0002 > 1e-5 -> FAIL

    const auto report = ats::evaluateExternalFixtureGates(
        our, ref, { ats::ExternalFixtureGate::ClassicParity }, strict);

    expect(!report.passed(), "threshold override: tighter threshold should fail what defaults pass");

    ats::ExternalFixtureThresholds loose;
    loose.maxNormalizedRmsErrorDelta = 1.0;  // very loose -> pass
    loose.maxSpectralDistanceDbDelta = 1.0;

    const auto report2 = ats::evaluateExternalFixtureGates(
        our, ref, { ats::ExternalFixtureGate::ClassicParity }, loose);

    expect(report2.passed(), "threshold override: looser threshold should pass");
}

// ---------------------------------------------------------------------------
// JSON loading
// ---------------------------------------------------------------------------

void testLoadExternalFixtureReferenceMetrics()
{
    // Write a temporary summary JSON and load it back.
    const std::string tmpPath = "/tmp/ats_external_test_summary.json";
    {
        std::ofstream out(tmpPath);
        out << "{\n"
            << "  \"schema_version\": 1,\n"
            << "  \"fixture_id\": \"test_sustained\",\n"
            << "  \"normalized_rms_error\": 0.00082,\n"
            << "  \"spectral_distance_db\": 0.042,\n"
            << "  \"continuity_score\": 0.94,\n"
            << "  \"identity_errors\": 0,\n"
            << "  \"orphan_residual_ratio\": 0.15,\n"
            << "  \"source_residual_highpass_ratio\": 0.11,\n"
            << "  \"source_attack_sharpness\": 1.7,\n"
            << "  \"attack_sharpness\": 1.8,\n"
            << "  \"max_buffer_step\": 0.12\n"
            << "}\n";
    }

    ats::ExternalFixtureReferenceMetrics loaded;
    std::string err;
    const bool ok = ats::loadExternalFixtureReferenceMetrics(tmpPath, loaded, err);
    expect(ok, "loadExternalFixtureReferenceMetrics: should succeed on valid JSON");
    expect(loaded.fixtureId == "test_sustained",
           "loadExternalFixtureReferenceMetrics: fixture_id parsed");
    expect(loaded.normalizedRmsError > 0.0008 && loaded.normalizedRmsError < 0.0009,
           "loadExternalFixtureReferenceMetrics: normalized_rms_error parsed");
    expect(loaded.continuityScore > 0.93 && loaded.continuityScore < 0.95,
           "loadExternalFixtureReferenceMetrics: continuity_score parsed");
    expect(loaded.identityErrors == 0,
           "loadExternalFixtureReferenceMetrics: identity_errors parsed as zero");
    expect(loaded.orphanResidualRatio > 0.14 && loaded.orphanResidualRatio < 0.16,
           "loadExternalFixtureReferenceMetrics: orphan_residual_ratio parsed");
    expect(loaded.sourceResidualHighpassRatio > 0.10 && loaded.sourceResidualHighpassRatio < 0.12,
           "loadExternalFixtureReferenceMetrics: source_residual_highpass_ratio parsed");
    expect(loaded.attackSharpness > 1.79 && loaded.attackSharpness < 1.81,
           "loadExternalFixtureReferenceMetrics: attack_sharpness parsed");
    expect(loaded.sourceAttackSharpness > 1.69 && loaded.sourceAttackSharpness < 1.71,
           "loadExternalFixtureReferenceMetrics: source_attack_sharpness parsed");
}

void testLoadExternalFixtureReferenceMetricsMissingFile()
{
    ats::ExternalFixtureReferenceMetrics loaded;
    std::string err;
    const bool ok =
        ats::loadExternalFixtureReferenceMetrics("/nonexistent/path.json", loaded, err);
    expect(!ok, "loadExternalFixtureReferenceMetrics: missing file returns false");
    expect(!err.empty(), "loadExternalFixtureReferenceMetrics: missing file sets error message");
}

void testLoadExternalFixtureReferenceMetricsMissingSchemaVersion()
{
    const std::string tmpPath = "/tmp/ats_external_test_no_schema.json";
    {
        std::ofstream out(tmpPath);
        out << "{ \"fixture_id\": \"bad_fixture\", \"normalized_rms_error\": 0.001 }\n";
    }
    ats::ExternalFixtureReferenceMetrics loaded;
    std::string err;
    const bool ok = ats::loadExternalFixtureReferenceMetrics(tmpPath, loaded, err);
    expect(!ok, "loadExternalFixtureReferenceMetrics: missing schema_version returns false");
    expect(!err.empty(), "loadExternalFixtureReferenceMetrics: schema error is described");
}

// ---------------------------------------------------------------------------
// Round-trip: load JSON then evaluate gates
// ---------------------------------------------------------------------------

void testLoadAndEvaluate()
{
    const std::string refPath = "/tmp/ats_external_ref_roundtrip.json";
    const std::string ourPath = "/tmp/ats_external_our_roundtrip.json";

    auto writeMetrics = [](const std::string& path, double nrms, double spec,
                           double cont, int ie, double orr) {
        std::ofstream out(path);
        out << "{\n"
            << "  \"schema_version\": 1,\n"
            << "  \"fixture_id\": \"roundtrip\",\n"
            << "  \"normalized_rms_error\": " << nrms << ",\n"
            << "  \"spectral_distance_db\": " << spec << ",\n"
            << "  \"continuity_score\": " << cont << ",\n"
            << "  \"identity_errors\": " << ie << ",\n"
            << "  \"orphan_residual_ratio\": " << orr << ",\n"
            << "  \"attack_sharpness\": -1.0,\n"
            << "  \"max_buffer_step\": -1.0\n"
            << "}\n";
    };

    writeMetrics(refPath, 0.001, 0.3, 0.90, 1, 0.18);
    writeMetrics(ourPath, 0.001, 0.3, 0.90, 1, 0.18);  // identical = all pass

    ats::ExternalFixtureReferenceMetrics ref, our;
    std::string err;
    expect(ats::loadExternalFixtureReferenceMetrics(refPath, ref, err), "round-trip: load ref");
    expect(ats::loadExternalFixtureReferenceMetrics(ourPath, our, err), "round-trip: load our");

    const auto report = ats::evaluateExternalFixtureGates(
        our, ref,
        { ats::ExternalFixtureGate::ClassicParity,
          ats::ExternalFixtureGate::TrackerContinuity,
          ats::ExternalFixtureGate::ResidualEnergyPreservation });

    expect(report.passed(), "round-trip: identical loaded metrics should pass all gates");
    expect(report.checks.size() >= 4, "round-trip: all gate checks emitted");
}

} // namespace

int main()
{
    testParseExternalFixtureGate();

    testClassicParityGatePasses();
    testClassicParityAllowsCandidateImprovement();
    testClassicParityGateFails();

    testTrackerContinuityGatePasses();
    testTrackerContinuityGateFails();
    testTrackerContinuitySkipsUnavailableFields();

    testResidualEnergyGatePasses();
    testResidualEnergyGateFails();

    testTransientPreservationGatePasses();
    testTransientPreservationGateFails();

    testMultipleGatesAllPass();
    testMultipleGatesMixedResults();

    testThresholdOverrides();

    testLoadExternalFixtureReferenceMetrics();
    testLoadExternalFixtureReferenceMetricsMissingFile();
    testLoadExternalFixtureReferenceMetricsMissingSchemaVersion();

    testLoadAndEvaluate();

    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
