#pragma once

#include "../platform/FileService.h"

#include <array>

namespace ats::state
{

struct FactoryPreset
{
    std::string name;
    std::string description;
};

struct PresetEntry
{
    std::string name;
    ats::Path path;
    bool factory = false;
    std::int32_t factoryIndex = -1;
};

class PresetService
{
public:
    static constexpr std::string_view kExtension = ".atspreset";

    explicit PresetService(ats::platform::FileService& files, ats::Path directory)
        : files_(files), directory_(std::move(directory)) {}

    [[nodiscard]] const ats::Path& directory() const noexcept { return directory_; }
    [[nodiscard]] static const std::array<FactoryPreset, 12>& factoryPresets();
    [[nodiscard]] static std::string withExtension(std::string name);
    [[nodiscard]] std::vector<PresetEntry> entries(const ats::CancellationToken* cancellation = nullptr) const;
    [[nodiscard]] ats::platform::FileResult load(const ats::Path& path,
                                                 const ats::CancellationToken* cancellation = nullptr) const;
    [[nodiscard]] ats::platform::FileResult save(std::string name,
                                                 const std::vector<std::byte>& payload,
                                                 bool overwrite,
                                                 const ats::CancellationToken* cancellation = nullptr) const;

private:
    ats::platform::FileService& files_;
    ats::Path directory_;
};

} // namespace ats::state
