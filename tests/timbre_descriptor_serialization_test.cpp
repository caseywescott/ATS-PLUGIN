#include "TimbreDescriptorSerialization.h"
#include "TimbreDescriptors.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace
{

int fail(const std::string& message)
{
    std::cerr << "timbre_descriptor_serialization_test: " << message << '\n';
    return 1;
}

ats::TimbreDescriptorValue makeAvailable(
    const double value,
    const ats::TimbreDescriptorDomain domain,
    const double confidence = 1.0)
{
    ats::TimbreDescriptorValue out;
    out.value = value;
    out.domain = domain;
    out.status = ats::TimbreDescriptorStatus::Available;
    out.confidence = confidence;
    out.coverage.activePointCount = 4;
    out.coverage.supportedFrequencySpanHz = 8000.0;
    out.coverage.representedEnergyRatio = 1.0;
    return out;
}

ats::TimbreDescriptorFrame makeFilledFrame()
{
    ats::TimbreDescriptorFrame frame;
    frame.sourceModelId = "fixture-source";
    frame.frameIndex = 5;
    frame.partialEnergy = makeAvailable(0.75, ats::TimbreDescriptorDomain::Partials);
    frame.residualEnergy = makeAvailable(0.25, ats::TimbreDescriptorDomain::ResidualBands);
    frame.combinedEnergy = makeAvailable(1.0, ats::TimbreDescriptorDomain::Combined);
    frame.noisiness = makeAvailable(0.25, ats::TimbreDescriptorDomain::Combined);
    frame.combinedCentroidHz = makeAvailable(2000.0, ats::TimbreDescriptorDomain::Combined, 0.9);
    frame.combinedSpreadHz = makeAvailable(800.0, ats::TimbreDescriptorDomain::Combined);
    frame.rolloff85Hz = makeAvailable(3500.0, ats::TimbreDescriptorDomain::Combined);
    frame.flatness = makeAvailable(0.4, ats::TimbreDescriptorDomain::Combined);
    frame.crest = makeAvailable(2.1, ats::TimbreDescriptorDomain::Combined);
    frame.dominantPartialFraction = makeAvailable(0.6, ats::TimbreDescriptorDomain::Partials);

    // Vector descriptor
    frame.bandwiseNoisiness.values = { 0.1, 0.2, 0.3, 0.4 };
    frame.bandwiseNoisiness.domain = ats::TimbreDescriptorDomain::Combined;
    frame.bandwiseNoisiness.status = ats::TimbreDescriptorStatus::Available;
    frame.bandwiseNoisiness.confidence = 1.0;
    frame.bandwiseNoisiness.coverage.activePointCount = 4;
    frame.bandwiseNoisiness.coverage.supportedFrequencySpanHz = 8000.0;
    frame.bandwiseNoisiness.coverage.representedEnergyRatio = 1.0;

    // Resonance peaks
    frame.resonancePeaks = {
        { 800.0,  0.2, 12.0, 0.05 },
        { 3200.0, 0.7, 10.0, 0.10 },
    };
    frame.resonancePeaksStatus = ats::TimbreDescriptorStatus::Available;
    frame.resonancePeaksConfidence = 0.9;

    return frame;
}

ats::TimbreDescriptorSeries makeFilledSeries()
{
    ats::TimbreDescriptorSeries series;
    series.sourceModelId = "fixture-source";
    series.frameHopSeconds = 0.025;
    series.temporalCentroidSeconds = makeAvailable(0.4, ats::TimbreDescriptorDomain::TemporalSeries);
    series.logAttackTime = makeAvailable(-1.0, ats::TimbreDescriptorDomain::TemporalSeries);
    series.attackSlopeDbPerSecond = makeAvailable(240.0, ats::TimbreDescriptorDomain::TemporalSeries);
    series.decaySlopeDbPerSecond = makeAvailable(-120.0, ats::TimbreDescriptorDomain::TemporalSeries);
    series.effectiveDurationSeconds = makeAvailable(0.5, ats::TimbreDescriptorDomain::TemporalSeries);
    series.positiveFlux = {
        makeAvailable(0.1, ats::TimbreDescriptorDomain::TemporalSeries),
        makeAvailable(0.2, ats::TimbreDescriptorDomain::TemporalSeries),
        makeAvailable(0.3, ats::TimbreDescriptorDomain::TemporalSeries),
    };
    series.spectralVariation = {
        makeAvailable(0.05, ats::TimbreDescriptorDomain::TemporalSeries),
        makeAvailable(0.07, ats::TimbreDescriptorDomain::TemporalSeries),
    };
    return series;
}

ats::TimbreDescriptorReport makeFilledReport()
{
    ats::TimbreDescriptorReport report;
    report.sourceModelId = "fixture-source";
    report.weighting = "energy";
    report.comparisonAxis.type = ats::EnvelopeFrequencyAxis::Bark;
    report.comparisonAxis.minFrequencyHz = 20.0;
    report.comparisonAxis.maxFrequencyHz = 20000.0;
    report.comparisonAxis.sampleRate = 48000.0;
    report.comparisonGridSize = 64;
    report.frames = { makeFilledFrame() };
    auto second = makeFilledFrame();
    second.frameIndex = 6;
    second.combinedCentroidHz.value = 2100.0;
    report.frames.push_back(second);
    report.series = makeFilledSeries();
    return report;
}

bool runPythonChecker(const std::string& jsonPath)
{
    const char* python = std::getenv("PYTHON3");
    if (python == nullptr)
        python = "python3";

    std::ostringstream command;
    command << python << " "
            << "\"" << CHECKER_PATH << "\""
            << " \"" << jsonPath << "\"";
    return std::system(command.str().c_str()) == 0;
}

} // namespace

int main()
{
    const ats::TimbreDescriptorReport report = makeFilledReport();
    const std::string first = ats::serializeTimbreDescriptorReport(report);
    const std::string second = ats::serializeTimbreDescriptorReport(report);
    if (first != second)
        return fail("serialization is not deterministic across two runs");

    if (first.find("\"schema_version\": 1") == std::string::npos)
        return fail("schema_version key missing");
    if (first.find("\"formula_version\": 1") == std::string::npos)
        return fail("formula_version key missing");
    if (first.find("\"comparison_axis\": \"bark\"") == std::string::npos)
        return fail("comparison_axis must serialize as 'bark'");
    if (first.find("\"resonance_peaks\"") == std::string::npos)
        return fail("resonance_peaks key missing");

    // Frame-level serialization helper produces the same per-frame substring.
    ats::TimbreDescriptorOptions options;
    const std::string frameOnly =
        ats::serializeTimbreDescriptorFrame(report.frames.front(), options);
    if (first.find(frameOnly) == std::string::npos)
        return fail("frame serializer output not embedded in report serializer output");

    // Stable order: re-ordering descriptor populating cannot change output.
    // We approximate this by writing the same report twice from rebuilt fixtures.
    const std::string rebuilt = ats::serializeTimbreDescriptorReport(makeFilledReport());
    if (rebuilt != first)
        return fail("rebuilt report does not match original");

    // Round-trip: write to a temp file and run the Python checker over it.
    const std::string filePath = std::string(CMAKE_BINARY_DIR_VALUE)
        + "/timbre_descriptor_serialization_report.json";
    std::string errorMessage;
    if (!ats::writeTimbreDescriptorReport(report, filePath, &errorMessage))
        return fail("writeTimbreDescriptorReport failed: " + errorMessage);

    if (!runPythonChecker(filePath))
        return fail("python report checker rejected the serialized output");

    // Status-other-than-Available emits null value but keeps confidence and
    // formula_version present.
    ats::TimbreDescriptorReport blankReport = report;
    blankReport.frames.front().rolloff85Hz.status = ats::TimbreDescriptorStatus::MissingResidual;
    blankReport.frames.front().rolloff85Hz.value = 0.0;
    const std::string blankText = ats::serializeTimbreDescriptorReport(blankReport);
    if (blankText.find("\"status\": \"missing_residual\"") == std::string::npos)
        return fail("missing_residual status must appear in serialized output");

    std::cout << "timbre_descriptor_serialization_test: ok\n";
    return 0;
}
