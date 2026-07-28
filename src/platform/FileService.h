#pragma once

#include "../core/AtsPrimitives.h"

#include <cstddef>
#include <filesystem>

namespace ats::platform
{

enum class FileStatus : std::uint8_t
{
    success,
    cancelled,
    invalidPath,
    notFound,
    alreadyExists,
    permissionDenied,
    tooLarge,
    ioError
};

struct FileResult
{
    FileStatus status = FileStatus::ioError;
    ats::Path path;
    std::vector<std::byte> bytes;
    ats::Error error;

    [[nodiscard]] bool succeeded() const noexcept { return status == FileStatus::success; }
    [[nodiscard]] bool cancelled() const noexcept { return status == FileStatus::cancelled; }
};

class FileService
{
public:
    static constexpr std::size_t kDefaultMaxBytes = 128u * 1024u * 1024u;

    explicit FileService(std::size_t maxBytes = kDefaultMaxBytes) noexcept : maxBytes_(maxBytes) {}

    [[nodiscard]] FileResult read(const ats::Path& path,
                                  const ats::CancellationToken* cancellation = nullptr) const;
    [[nodiscard]] FileResult writeAtomic(const ats::Path& path,
                                         const std::vector<std::byte>& bytes,
                                         bool overwrite,
                                         const ats::CancellationToken* cancellation = nullptr) const;
    [[nodiscard]] FileResult list(const ats::Path& directory,
                                  std::string_view extension,
                                  const ats::CancellationToken* cancellation = nullptr) const;

private:
    [[nodiscard]] static FileResult failure(FileStatus status, const ats::Path& path,
                                            std::string code, std::string text);
    [[nodiscard]] static std::filesystem::path native(const ats::Path& path);

    std::size_t maxBytes_ = kDefaultMaxBytes;
};

} // namespace ats::platform
