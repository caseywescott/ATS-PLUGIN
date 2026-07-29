#include "SourceImportService.h"

#include "../platform/FileService.h"

#include <sndfile.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <limits>

namespace ats::engine
{
namespace
{
constexpr std::size_t kMaximumDecodedFrames = 16u * 1024u * 1024u;

ats::JobResult<ImportedSource> failure(ats::JobGeneration generation,
                                       const char* code, const char* text)
{
    ats::JobResult<ImportedSource> result;
    result.generation = generation;
    result.status = ats::JobResult<ImportedSource>::Status::failed;
    result.error = { code, text };
    return result;
}

ats::JobResult<ImportedSource> cancelled(ats::JobGeneration generation)
{
    auto result = failure(generation, "cancelled", "source import cancelled");
    result.status = ats::JobResult<ImportedSource>::Status::cancelled;
    return result;
}

std::string lowerExtension(const ats::Path& path)
{
    auto extension = std::filesystem::u8path(path.utf8).extension().u8string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return extension;
}

std::string displayName(const ats::Path& path)
{
    const auto stem = std::filesystem::u8path(path.utf8).stem().u8string();
    return stem.empty() ? path.utf8 : stem;
}

bool isSupportedAudioExtension(const std::string& extension)
{
    return extension == ".wav" || extension == ".wave" || extension == ".aif" || extension == ".aiff";
}

SNDFILE* openAudioFile(const ats::Path& path, SF_INFO& info)
{
#if defined(_WIN32)
    const auto native = std::filesystem::u8path(path.utf8).wstring();
    return sf_wchar_open(native.c_str(), SFM_READ, &info);
#else
    return sf_open(path.utf8.c_str(), SFM_READ, &info);
#endif
}
}

ats::JobResult<ImportedSource> SourceImportService::import(
    ats::JobGeneration generation, const ats::CancellationToken& cancellation,
    const SourceImportRequest& request)
{
    if (cancellation.isCancelled())
        return cancelled(generation);
    if (request.slot < 0 || request.slot > 1)
        return failure(generation, "invalid_slot", "source import slot must be A or B");
    if (request.path.empty())
        return failure(generation, "invalid_path", "source import path is empty");

    const auto extension = lowerExtension(request.path);
    if ((request.kind == SourceImportKind::audio && !isSupportedAudioExtension(extension)) ||
        (request.kind == SourceImportKind::ats && extension != ".ats"))
        return failure(generation, "unsupported_extension", "path does not match the requested source format");

    // FileService provides portable path, size, permission, and cancellation
    // handling before a format decoder is allowed to open the path itself.
    ats::platform::FileService files;
    const auto preflight = files.read(request.path, &cancellation);
    if (preflight.cancelled())
        return cancelled(generation);
    if (!preflight.succeeded())
    {
        ats::JobResult<ImportedSource> result;
        result.generation = generation;
        result.status = ats::JobResult<ImportedSource>::Status::failed;
        result.error = preflight.error;
        return result;
    }
    if (cancellation.isCancelled())
        return cancelled(generation);

    ImportedSource source;
    source.path = request.path;
    source.slot = request.slot;
    source.kind = request.kind;
    source.name = displayName(request.path);

    if (request.kind == SourceImportKind::ats)
    {
        auto* raw = static_cast<ATS_SOUND*>(std::calloc(1, sizeof(ATS_SOUND)));
        if (raw == nullptr)
            return failure(generation, "out_of_memory", "could not allocate ATS model");
        int type = 0;
        if (ats_load(raw, request.path.utf8.c_str(), &type) < 0)
        {
            std::free(raw);
            return failure(generation, "ats_load_failed", "file is not a valid ATS model");
        }
        if (cancellation.isCancelled())
        {
            free_sound(raw);
            std::free(raw);
            return cancelled(generation);
        }
        source.sampleRate = raw->srate;
        source.atsType = type;
        source.sound.reset(raw);
    }
    else
    {
        SF_INFO info {};
        SNDFILE* file = openAudioFile(request.path, info);
        if (file == nullptr)
            return failure(generation, "audio_open_failed", "libsndfile could not open the audio source");

        const auto closeFile = [&file] { if (file != nullptr) sf_close(file); };
        if (info.frames <= 0 || info.channels <= 0 || info.samplerate <= 0 ||
            static_cast<std::uint64_t>(info.frames) > kMaximumDecodedFrames ||
            static_cast<std::uint64_t>(info.frames) >
                std::numeric_limits<std::size_t>::max() / static_cast<std::uint64_t>(info.channels))
        {
            closeFile();
            return failure(generation, "invalid_audio", "audio source has invalid or unsupported dimensions");
        }

        const auto frames = static_cast<std::size_t>(info.frames);
        const auto channels = static_cast<std::size_t>(info.channels);
        std::vector<double> interleaved(frames * channels);
        const auto read = sf_readf_double(file, interleaved.data(), info.frames);
        closeFile();
        if (read != info.frames)
            return failure(generation, "audio_read_failed", "audio source could not be fully decoded");
        if (cancellation.isCancelled())
            return cancelled(generation);

        source.sampleRate = info.samplerate;
        source.monoSamples.resize(frames);
        for (std::size_t frame = 0; frame < frames; ++frame)
        {
            double sum = 0.0;
            for (std::size_t channel = 0; channel < channels; ++channel)
                sum += interleaved[frame * channels + channel];
            source.monoSamples[frame] = sum / static_cast<double>(channels);
        }
    }

    ats::JobResult<ImportedSource> result;
    result.generation = generation;
    result.status = ats::JobResult<ImportedSource>::Status::success;
    result.value = std::move(source);
    return result;
}

} // namespace ats::engine
