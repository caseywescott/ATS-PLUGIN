#include "../src/ui/SpectrumRenderer.h"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace
{
using namespace ats::ui;

std::string number(double value)
{
    std::ostringstream stream;
    stream << std::setprecision(9) << value;
    return stream.str();
}

void point(std::ostream& out, UiPoint value)
{
    out << "[" << number(value.x) << "," << number(value.y) << "]";
}

void writeSnapshot(const SpectralTraceSnapshot& snapshot, const std::filesystem::path& path)
{
    std::ofstream out(path);
    out << "{\"schema\":1,\"sourceRevision\":" << snapshot.sourceRevision
        << ",\"displayShapeRevision\":" << snapshot.displayShapeRevision
        << ",\"partialCount\":" << snapshot.partialCount
        << ",\"frameCount\":" << snapshot.frameCount
        << ",\"durationSeconds\":" << number(snapshot.durationSeconds)
        << ",\"minFrequencyHz\":" << number(snapshot.minFrequencyHz)
        << ",\"maxFrequencyHz\":" << number(snapshot.maxFrequencyHz)
        << ",\"partials\":[";
    for (std::size_t i = 0; i < snapshot.partials.size(); ++i)
    {
        if (i != 0) out << ',';
        const auto& trace = snapshot.partials[i];
        out << "{\"partialIndex\":" << trace.partialIndex
            << ",\"frequencyShapeRank\":" << trace.frequencyShapeRank
            << ",\"peakAmpNorm\":" << number(trace.peakAmpNorm)
            << ",\"meanFrequencyHz\":" << number(trace.meanFrequencyHz)
            << ",\"sourceMeanFrequencyHz\":" << number(trace.sourceMeanFrequencyHz)
            << ",\"points\":[";
        for (std::size_t j = 0; j < trace.points.size(); ++j)
        {
            if (j != 0) out << ',';
            const auto& p = trace.points[j];
            out << "{\"frameIndex\":" << p.frameIndex
                << ",\"timeNorm\":" << number(p.timeNorm)
                << ",\"freqNorm\":" << number(p.freqNorm)
                << ",\"ampNorm\":" << number(p.ampNorm)
                << ",\"frequencyHz\":" << number(p.frequencyHz)
                << ",\"sourceFrequencyHz\":" << number(p.sourceFrequencyHz)
                << ",\"amplitude\":" << number(p.amplitude)
                << ",\"active\":" << (p.active ? "true" : "false") << '}';
        }
        out << "]}";
    }
    out << "],\"residualBands\":[";
    for (std::size_t i = 0; i < snapshot.residualBands.size(); ++i)
    {
        if (i != 0) out << ',';
        const auto& band = snapshot.residualBands[i];
        out << "{\"bandIndex\":" << band.bandIndex
            << ",\"lowHz\":" << number(band.lowHz)
            << ",\"highHz\":" << number(band.highHz) << ",\"energyNorm\":[";
        for (std::size_t j = 0; j < band.energyNorm.size(); ++j)
            out << (j == 0 ? "" : ",") << number(band.energyNorm[j]);
        out << "]}";
    }
    out << "],\"onsets\":[";
    for (std::size_t i = 0; i < snapshot.onsets.size(); ++i)
    {
        if (i != 0) out << ',';
        const auto& onset = snapshot.onsets[i];
        out << "{\"norm\":" << number(onset.norm)
            << ",\"preNorm\":" << number(onset.preNorm)
            << ",\"peakNorm\":" << number(onset.peakNorm)
            << ",\"strength\":" << number(onset.strength) << '}';
    }
    out << "]}\n";
}

void writeRender(const std::vector<SpectrumDrawCommand>& commands,
                 const std::filesystem::path& path)
{
    std::ofstream out(path);
    out << "{\"schema\":1,\"commands\":[";
    for (std::size_t i = 0; i < commands.size(); ++i)
    {
        if (i != 0) out << ',';
        const auto& command = commands[i];
        out << "{\"kind\":" << static_cast<int>(command.kind)
            << ",\"index\":" << command.index
            << ",\"bounds\":[" << number(command.bounds.x) << ','
            << number(command.bounds.y) << ',' << number(command.bounds.width) << ','
            << number(command.bounds.height) << "]";
        if (!command.label.empty()) out << ",\"label\":\"" << command.label << "\"";
        if (!command.vertices.empty())
        {
            out << ",\"vertices\":[";
            for (std::size_t j = 0; j < command.vertices.size(); ++j)
            {
                if (j != 0) out << ',';
                point(out, command.vertices[j]);
            }
            out << ']';
        }
        out << '}';
    }
    out << "]}\n";
}

SpectralTraceSnapshot makeFixture()
{
    SpectralTraceInput input;
    input.source = SpectralTraceSource::current;
    input.sourceRevision = 1;
    input.displayShapeRevision = 1;
    input.durationSeconds = 0.37;
    input.partialCount = 5;
    input.frameCount = 17;
    input.nyquistHz = 24000.0;
    input.displayAmpPeak = 1.0;
    input.displayResidualPeak = 1.0;
    input.onsets = { { 0.78, 0.9, 0.70, 0.82 }, { 0.22, 0.4, 0.18, 0.25 } };
    input.smearnovDetectionCurve = { 0.0, 0.2, 0.4, 0.3, 0.1 };
    input.smearnovThresholdCurve = { 0.1, 0.1, 0.15, 0.2, 0.2 };
    for (int partial = 0; partial < 5; ++partial)
    {
        SpectralPartialInput trace;
        trace.partialIndex = partial;
        trace.frequencyShapeRank = partial + 1;
        for (int frame = 0; frame < 17; ++frame)
        {
            const auto frequency = 220.0 + partial * 180.0 + frame * (partial + 1) * 0.7;
            const auto amplitude = 0.08 + 0.12 * partial + 0.02 * (frame % 4);
            trace.frames.push_back({ frequency, amplitude });
        }
        if (partial == 4)
        {
            trace.frames[0] = { 0.0, 0.0 };
            trace.frames[1] = { 0.0, 0.0 };
            trace.frames[15] = { 0.0, 0.0 };
            trace.frames[16] = { 0.0, 0.0 };
        }
        input.partials.push_back(std::move(trace));
    }
    for (int band = 0; band < 3; ++band)
    {
        SpectralResidualBandInput trace;
        trace.bandIndex = band;
        trace.lowHz = 100.0 + band * 100.0;
        trace.highHz = trace.lowHz + 100.0;
        for (int frame = 0; frame < 17; ++frame)
            trace.energy.push_back(0.01 * (band + 1) * (1 + frame % 5));
        input.residualBands.push_back(std::move(trace));
    }
    return SpectralTraceBuilder::build(input, SpectralTraceMode::combined);
}
}

int spectrumCandidateExportMain(int argc, char** argv)
{
    if (argc != 2)
    {
        std::cerr << "usage: spectrum_candidate_export <output-directory>\n";
        return 2;
    }
    const std::filesystem::path output = argv[1];
    std::filesystem::create_directories(output);
    const auto snapshot = makeFixture();
    const auto render = SpectrumRenderer::render(snapshot, { 0.0f, 0.0f, 800.0f, 500.0f });
    writeSnapshot(snapshot, output / "snapshot.spx");
    writeRender(render, output / "render.spx");
    std::ofstream(output / "model.spx")
        << "{\"schema\":1,\"fixture\":\"synthetic-sparse-decay\",\"partialCount\":"
        << snapshot.partialCount << ",\"frameCount\":" << snapshot.frameCount << "}\n";
    std::ofstream(output / "envelope.spx")
        << "{\"schema\":1,\"envelopeNorm\":[0.0,0.25,0.5,0.25,0.0]}\n";
    std::ofstream(output / "navigation.jsonl")
        << "{\"event\":\"initial\",\"timeCenter\":0.5,\"timeZoom\":1,\"freqCenter\":0.5,\"freqZoom\":1}\n";
    return 0;
}

#ifndef SPECTRUM_EXPORTER_NO_MAIN
int main(int argc, char** argv) { return spectrumCandidateExportMain(argc, argv); }
#endif
