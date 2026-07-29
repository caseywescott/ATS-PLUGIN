#pragma once

#include "EditorHost.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <vector>

namespace ats::ui
{

// Pure A/B source-drop routing. Native backends only provide a target slot and
// UTF-8 paths; this policy keeps extension handling identical on every host.
enum class SessionDropKind : std::uint8_t { wav, ats };

struct SessionSourceDrop
{
    int slot = 0;
    SessionDropKind kind = SessionDropKind::wav;
    ats::Path path;
};

inline std::optional<SessionSourceDrop> classifySessionSourceDrop(
    int slot, const std::vector<std::string>& paths)
{
    if (slot < 0 || slot > 1 || paths.size() != 1 || paths.front().empty())
        return std::nullopt;

    std::string extension;
    const auto dot = paths.front().find_last_of('.');
    if (dot != std::string::npos)
        extension = paths.front().substr(dot);
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char character) {
                       return static_cast<char>(std::tolower(character));
                   });

    SessionDropKind kind {};
    if (extension == ".wav" || extension == ".wave" || extension == ".aif" ||
        extension == ".aiff")
        kind = SessionDropKind::wav;
    else if (extension == ".ats")
        kind = SessionDropKind::ats;
    else
        return std::nullopt;

    return SessionSourceDrop { slot, kind, ats::Path { paths.front() } };
}

} // namespace ats::ui
