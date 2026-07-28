#include "FileService.h"

#include <algorithm>
#include <chrono>
#include <fstream>

namespace ats::platform
{
namespace
{
bool isCancelled(const ats::CancellationToken* token) noexcept
{
    return token != nullptr && token->isCancelled();
}
}

std::filesystem::path FileService::native(const ats::Path& path)
{
    return std::filesystem::u8path(path.utf8);
}

FileResult FileService::failure(FileStatus status, const ats::Path& path,
                                std::string code, std::string text)
{
    return { status, path, {}, { std::move(code), std::move(text) } };
}

FileResult FileService::read(const ats::Path& path, const ats::CancellationToken* cancellation) const
{
    if (path.empty()) return failure(FileStatus::invalidPath, path, "invalid_path", "path is empty");
    if (isCancelled(cancellation)) return failure(FileStatus::cancelled, path, "cancelled", "file operation cancelled");

    const auto file = native(path);
    std::error_code ec;
    if (!std::filesystem::exists(file, ec))
        return failure(FileStatus::notFound, path, "not_found", "file does not exist");
    if (ec || !std::filesystem::is_regular_file(file, ec))
        return failure(FileStatus::invalidPath, path, "invalid_path", "path is not a regular file");

    const auto size = std::filesystem::file_size(file, ec);
    if (ec) return failure(FileStatus::ioError, path, "stat_failed", ec.message());
    if (size > maxBytes_)
        return failure(FileStatus::tooLarge, path, "too_large", "file exceeds the configured size limit");

    std::ifstream stream(file, std::ios::binary);
    if (!stream) return failure(FileStatus::permissionDenied, path, "open_failed", "file could not be opened");
    FileResult result { FileStatus::success, path, std::vector<std::byte>(static_cast<std::size_t>(size)), {} };
    if (size > 0)
        stream.read(reinterpret_cast<char*>(result.bytes.data()), static_cast<std::streamsize>(size));
    if (!stream || isCancelled(cancellation))
        return isCancelled(cancellation) ? failure(FileStatus::cancelled, path, "cancelled", "file operation cancelled")
                                         : failure(FileStatus::ioError, path, "read_failed", "file could not be read");
    return result;
}

FileResult FileService::writeAtomic(const ats::Path& path, const std::vector<std::byte>& bytes,
                                    bool overwrite, const ats::CancellationToken* cancellation) const
{
    if (path.empty()) return failure(FileStatus::invalidPath, path, "invalid_path", "path is empty");
    if (bytes.size() > maxBytes_)
        return failure(FileStatus::tooLarge, path, "too_large", "payload exceeds the configured size limit");
    if (isCancelled(cancellation)) return failure(FileStatus::cancelled, path, "cancelled", "file operation cancelled");

    const auto file = native(path);
    std::error_code ec;
    const auto parent = file.parent_path();
    if (parent.empty() || !std::filesystem::exists(parent, ec))
        return failure(FileStatus::invalidPath, path, "invalid_parent", "parent directory does not exist");
    if (std::filesystem::exists(file, ec) && !overwrite)
        return failure(FileStatus::alreadyExists, path, "already_exists", "target exists and overwrite is disabled");

    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto temporary = file.string() + ".tmp." + std::to_string(stamp);
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream)
            return failure(FileStatus::permissionDenied, path, "open_failed", "temporary file could not be opened");
        if (!bytes.empty())
            stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        stream.flush();
        if (!stream || isCancelled(cancellation))
        {
            std::filesystem::remove(temporary, ec);
            return isCancelled(cancellation) ? failure(FileStatus::cancelled, path, "cancelled", "file operation cancelled")
                                             : failure(FileStatus::ioError, path, "write_failed", "temporary file could not be written");
        }
    }

    if (isCancelled(cancellation))
    {
        std::filesystem::remove(temporary, ec);
        return failure(FileStatus::cancelled, path, "cancelled", "file operation cancelled");
    }
    if (overwrite && std::filesystem::exists(file, ec))
    {
        std::filesystem::remove(file, ec);
        if (ec)
        {
            std::filesystem::remove(temporary, ec);
            return failure(FileStatus::permissionDenied, path, "remove_failed", "existing target could not be replaced");
        }
    }
    std::filesystem::rename(temporary, file, ec);
    if (ec)
    {
        std::filesystem::remove(temporary, ec);
        return failure(FileStatus::ioError, path, "rename_failed", ec.message());
    }
    return { FileStatus::success, path, {}, {} };
}

FileResult FileService::list(const ats::Path& directory, std::string_view extension,
                             const ats::CancellationToken* cancellation) const
{
    if (directory.empty()) return failure(FileStatus::invalidPath, directory, "invalid_path", "path is empty");
    if (isCancelled(cancellation)) return failure(FileStatus::cancelled, directory, "cancelled", "file operation cancelled");

    std::error_code ec;
    const auto folder = native(directory);
    if (!std::filesystem::is_directory(folder, ec))
        return failure(FileStatus::notFound, directory, "not_found", "directory does not exist");

    std::vector<std::string> names;
    for (const auto& entry : std::filesystem::directory_iterator(folder, ec))
    {
        if (isCancelled(cancellation)) return failure(FileStatus::cancelled, directory, "cancelled", "file operation cancelled");
        if (ec) return failure(FileStatus::ioError, directory, "list_failed", ec.message());
        if (!entry.is_regular_file(ec) || ec) continue;
        const auto name = entry.path().filename().u8string();
        if (extension.empty() || entry.path().extension().u8string() == extension)
            names.push_back(name);
    }
    if (ec) return failure(FileStatus::ioError, directory, "list_failed", ec.message());
    std::sort(names.begin(), names.end());
    ats::ByteWriter writer;
    writer.writeU32(static_cast<std::uint32_t>(names.size()));
    for (const auto& name : names)
    {
        writer.writeU32(static_cast<std::uint32_t>(name.size()));
        writer.writeBytes(name.data(), name.size());
    }
    return { FileStatus::success, directory, std::move(writer).take(), {} };
}

} // namespace ats::platform
