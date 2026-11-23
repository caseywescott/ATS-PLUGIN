#pragma once

#include "../core/AtsModel.h"
#include "../core/AtsPrimitives.h"

#include <string>
#include <vector>

namespace ats::engine
{

// Decoded source data deliberately stays outside the UI and host ABI layers.
// WAV/AIFF sources retain their mono analysis samples; native ATS sources own
// the loaded ATSA model directly.
enum class SourceImportKind : std::uint8_t { audio, ats };

struct SourceImportRequest
{
    ats::Path path;
    int slot = 0;
    SourceImportKind kind = SourceImportKind::audio;
};

struct ImportedSource
{
    ats::Path path;
    int slot = 0;
    SourceImportKind kind = SourceImportKind::audio;
    std::string name;
    int sampleRate = 0;
    int atsType = 0;
    std::vector<double> monoSamples;
    std::vector<double> residual;
    ats::AtsSoundPtr sound;
    // ATSA frame times count from the analysis start sample. Tools that read
    // monoSamples at a model frame's time (residual clamp, transientness)
    // need that offset, and must not run once a time edit or reverse has
    // broken the frame <-> audio correspondence.
    std::size_t analysisStartSample = 0;
    bool modelAlignedToAudio = false;
};

class SourceImportService
{
public:
    // This is a worker-domain operation. The caller owns the returned source
    // and publishes it only after a successful, non-cancelled import.
    [[nodiscard]] static ats::JobResult<ImportedSource> import(
        ats::JobGeneration generation, const ats::CancellationToken& cancellation,
        const SourceImportRequest& request);
};

} // namespace ats::engine
