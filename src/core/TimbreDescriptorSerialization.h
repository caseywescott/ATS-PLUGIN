#pragma once

#include "SpectralEnvelope.h"
#include "TimbreDescriptors.h"

#include <iosfwd>
#include <string>
#include <vector>

namespace ats
{

// Top-level descriptor report that an offline tool or test harness can write
// to disk and later validate with tests/timbre_descriptor_report_check.py.
//
// reportSchemaVersion must equal 1 to match the JSON schema fixture; bumping
// it requires updating tests/fixtures/timbre_descriptor_report_schema.json
// and the Python checker.
struct TimbreDescriptorReport
{
    static constexpr std::uint32_t kSchemaVersion = 1;

    std::string sourceModelId;
    std::string weighting = "energy";
    EnvelopeAxisSpec comparisonAxis{};
    int comparisonGridSize = 64;
    double amplitudeFloor = 1.0e-12;
    double minimumPeakProminenceDb = 3.0;
    double rolloffLowFraction = 0.85;
    double rolloffHighFraction = 0.95;
    bool includeResidualInCombined = true;
    bool enableHarmonicDescriptors = false;
    bool enableWaveformDescriptors = true;

    std::vector<TimbreDescriptorFrame> frames;
    TimbreDescriptorSeries series{};
};

// Serialize a single descriptor frame to JSON. Output is deterministic: keys
// appear in a fixed order so two runs over the same input produce
// byte-identical strings.
std::string serializeTimbreDescriptorFrame(
    const TimbreDescriptorFrame& frame,
    const TimbreDescriptorOptions& options);

// Serialize a series and its frames. Layout matches the schema in
// tests/fixtures/timbre_descriptor_report_schema.json.
std::string serializeTimbreDescriptorReport(const TimbreDescriptorReport& report);

// Offline export entry point. Returns true on success. errorMessage carries a
// human-readable error on failure.
bool writeTimbreDescriptorReport(
    const TimbreDescriptorReport& report,
    const std::string& filePath,
    std::string* errorMessage = nullptr);

bool writeTimbreDescriptorReport(
    const TimbreDescriptorReport& report,
    std::ostream& stream,
    std::string* errorMessage = nullptr);

// Convert EnvelopeFrequencyAxis to the textual form used by the report
// schema (linear_hz / bark / mel / erb / normalized_nyquist / custom_normalized).
const char* toReportAxisString(EnvelopeFrequencyAxis axis);

} // namespace ats
