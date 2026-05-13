#include "AtsAnalysisPipeline.h"
#include "AtsModernAcceptance.h"
#include "AtsModernSynth.h"
#include "AtsParams.h"

#include <sndfile.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace
{

void usage(const char* prog)
{
    std::cerr << "Usage: " << prog
              << " [--analysis-mode modern_ats|experimental] [--metadata <analysis.json>]"
              << " <input.wav> <output.wav>\n";
}

std::string jsonString(const std::string& value)
{
    std::ostringstream out;
    out << '"';
    for (const char c : value)
    {
        switch (c)
        {
            case '\\': out << "\\\\"; break;
            case '"': out << "\\\""; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default: out << c; break;
        }
    }
    out << '"';
    return out.str();
}

bool loadMonoWav(const std::string& path,
                 std::vector<double>& samples,
                 int& sampleRate,
                 std::int64_t& inputFrames)
{
    SF_INFO info {};
    SNDFILE* file = sf_open(path.c_str(), SFM_READ, &info);
    if (file == nullptr)
    {
        std::cerr << "Could not open '" << path << "': " << sf_strerror(nullptr) << '\n';
        return false;
    }
    if (info.channels != 1)
    {
        std::cerr << "Expected mono WAV, got " << info.channels << " channels.\n";
        sf_close(file);
        return false;
    }

    samples.assign(static_cast<std::size_t>(std::max<sf_count_t>(0, info.frames)), 0.0);
    const sf_count_t read = sf_readf_double(file, samples.data(), info.frames);
    sf_close(file);

    if (read != info.frames)
    {
        std::cerr << "Warning: short read (" << read << " vs " << info.frames << " frames)\n";
        samples.resize(static_cast<std::size_t>(std::max<sf_count_t>(0, read)));
    }

    sampleRate = info.samplerate;
    inputFrames = static_cast<std::int64_t>(read);
    return true;
}

bool writeMonoWav(const std::string& path, const std::vector<float>& samples, int sampleRate)
{
    SF_INFO info {};
    info.samplerate = sampleRate;
    info.channels = 1;
    info.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;

    SNDFILE* file = sf_open(path.c_str(), SFM_WRITE, &info);
    if (file == nullptr)
    {
        std::cerr << "Could not open '" << path << "' for write: " << sf_strerror(nullptr) << '\n';
        return false;
    }

    const auto frames = static_cast<sf_count_t>(samples.size());
    const sf_count_t written = sf_writef_float(file, samples.data(), frames);
    sf_close(file);
    if (written != frames)
    {
        std::cerr << "Could not write all output frames (" << written << " vs " << frames << ").\n";
        return false;
    }
    return true;
}

bool writeMetadataJson(const std::string& path,
                       const ats::ModernAtsModel& model,
                       const ats::ModernAtsRenderStats& renderStats,
                       const ats::ModernAtsContinuityMetrics& continuity,
                       int identityErrors,
                       const ats::ModernAtsResidualMetrics& residual,
                       const ats::ModernAtsTransientMetrics& transient,
                       std::int64_t inputFrames)
{
    std::ofstream out(path);
    if (!out)
    {
        std::cerr << "Could not open metadata path '" << path << "'.\n";
        return false;
    }

    out << std::setprecision(17);
    out << "{\n";
    out << "  \"schema_version\": 1,\n";
    out << "  \"analysis_mode\": " << jsonString(ats::toString(model.header.analysisMode)) << ",\n";
    out << "  \"tracker_mode\": " << jsonString(ats::toString(model.header.trackerMode)) << ",\n";
    out << "  \"source_hash\": " << jsonString(model.header.sourceHash) << ",\n";
    out << "  \"parameter_hash\": " << jsonString(model.header.parameterHash) << ",\n";
    out << "  \"input_sample_rate\": " << model.header.sampleRate << ",\n";
    out << "  \"input_frames\": " << inputFrames << ",\n";
    out << "  \"rendered_frames\": " << renderStats.totalSamples << ",\n";
    out << "  \"continuity_score\": " << continuity.continuityScore << ",\n";
    out << "  \"identity_errors\": " << identityErrors << ",\n";
    out << "  \"partial_noise_energy\": " << residual.partialNoiseEnergy << ",\n";
    out << "  \"orphan_residual_energy\": " << residual.orphanResidualEnergy << ",\n";
    out << "  \"total_residual_energy\": " << residual.totalResidualEnergy << ",\n";
    out << "  \"orphan_residual_ratio\": " << residual.orphanResidualRatio << ",\n";
    out << "  \"attack_sharpness\": " << transient.attackSharpness << ",\n";
    out << "  \"max_buffer_step\": " << transient.maxBufferStep << ",\n";
    out << "  \"model\": {\n";
    out << "    \"sample_rate\": " << model.header.sampleRate << ",\n";
    out << "    \"hop_size_samples\": " << model.header.hopSizeSamples << ",\n";
    out << "    \"window_size_samples\": " << model.header.windowSizeSamples << ",\n";
    out << "    \"partials\": " << model.partials.size() << ",\n";
    out << "    \"frames\": " << model.frames.size() << ",\n";
    out << "    \"transients\": " << model.transients.size() << ",\n";
    out << "    \"transient_buffers\": " << model.transientBuffers.size() << ",\n";
    out << "    \"orphan_residual_energy\": " << residual.orphanResidualEnergy << "\n";
    out << "  }\n";
    out << "}\n";

    if (!out)
    {
        std::cerr << "Could not finish writing metadata path '" << path << "'.\n";
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    std::string inputPath;
    std::string outputPath;
    std::string metadataPath;
    std::string analysisMode = "modern_ats";

    for (int i = 1; i < argc;)
    {
        const std::string arg = argv[i];
        if (arg == "--metadata")
        {
            if (i + 1 >= argc)
            {
                usage(argv[0]);
                return 1;
            }
            metadataPath = argv[i + 1];
            i += 2;
            continue;
        }
        if (arg == "--analysis-mode")
        {
            if (i + 1 >= argc)
            {
                usage(argv[0]);
                return 1;
            }
            analysisMode = argv[i + 1];
            i += 2;
            continue;
        }
        if (!arg.empty() && arg[0] == '-')
        {
            usage(argv[0]);
            return 1;
        }
        if (inputPath.empty())
            inputPath = arg;
        else if (outputPath.empty())
            outputPath = arg;
        else
        {
            usage(argv[0]);
            return 1;
        }
        ++i;
    }

    if (inputPath.empty() || outputPath.empty())
    {
        usage(argv[0]);
        return 1;
    }

    ats::ModernAtsAnalysisConfig analysisConfig;
    if (analysisMode == "modern" || analysisMode == "modern_ats")
    {
        analysisConfig.analysisMode = ats::ModernAtsAnalysisMode::ModernAts;
        analysisConfig.trackerMode = ats::ModernAtsTrackerMode::ModernAssignment;
    }
    else if (analysisMode == "experimental")
    {
        analysisConfig.analysisMode = ats::ModernAtsAnalysisMode::Experimental;
        analysisConfig.trackerMode = ats::ModernAtsTrackerMode::Experimental;
        analysisConfig.useExperimentalPeakEstimator = true;
    }
    else
    {
        std::cerr << "Unsupported modern offline analysis mode '" << analysisMode << "'.\n";
        return 1;
    }

    std::vector<double> samples;
    int sampleRate = 0;
    std::int64_t inputFrames = 0;
    if (!loadMonoWav(inputPath, samples, sampleRate, inputFrames))
        return 1;

    ats::AnalysisParams params;
    params.type = 2; // Preserve phase so the modern renderer can use stored oscillator starts.
    const auto analysis = ats::runModernAtsAnalysis(params, samples, sampleRate, analysisConfig);
    if (!analysis.errorMessage.empty())
    {
        std::cerr << "Modern analysis failed: " << analysis.errorMessage << '\n';
        return 1;
    }

    ats::ModernAtsSynthConfig synthConfig;
    synthConfig.outputSampleRate = sampleRate;
    synthConfig.orphanResidualGain = 1.0;
    synthConfig.transientGain = 1.0;

    ats::ModernAtsRenderStats renderStats;
    const auto rendered = ats::renderModernAtsModel(analysis.model, synthConfig, &renderStats);
    if (rendered.empty())
    {
        std::cerr << "Modern synthesis produced no samples.\n";
        return 1;
    }

    if (!writeMonoWav(outputPath, rendered, sampleRate))
        return 1;

    if (!metadataPath.empty())
    {
        const auto continuity = ats::computeModernAtsContinuityMetrics(analysis.model);
        const int identityErrors = ats::estimateModernAtsIdentityErrors(analysis.model);
        const auto residual = ats::computeModernAtsResidualMetrics(analysis.model);
        const auto transient = ats::computeModernAtsTransientMetrics(analysis.model);
        if (!writeMetadataJson(metadataPath,
                               analysis.model,
                               renderStats,
                               continuity,
                               identityErrors,
                               residual,
                               transient,
                               inputFrames))
        {
            return 1;
        }
    }

    return 0;
}
