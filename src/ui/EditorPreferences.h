#pragma once

#include "ThemeTokens.h"

#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

namespace ats::ui
{

// Per-user editor preferences (ADR-0004 §5): the theme is editor state, not an
// automatable parameter, and is not part of the project state chunk (whose
// v18 decoder rejects trailing bytes). It is kept in a small key=value file so
// it survives editor close/reopen and host relaunch in every wrapper.
struct EditorPreferences
{
    ThemeId theme = ThemeId::ember;

    [[nodiscard]] static std::string_view themeName(ThemeId theme) noexcept
    { return theme == ThemeId::porcelain ? "porcelain" : "ember"; }

    [[nodiscard]] static std::optional<ThemeId> themeFromName(std::string_view name) noexcept
    {
        if (name == "ember") return ThemeId::ember;
        if (name == "porcelain") return ThemeId::porcelain;
        return std::nullopt;
    }

    [[nodiscard]] std::string serialize() const
    {
        std::string out = "# ATS Resynth editor preferences\n";
        out += "theme=";
        out += themeName(theme);
        out += '\n';
        return out;
    }

    // Unknown keys and malformed lines are ignored so older and newer builds
    // can share one file.
    [[nodiscard]] static EditorPreferences parse(std::string_view text)
    {
        EditorPreferences preferences;
        std::istringstream lines { std::string(text) };
        std::string line;
        while (std::getline(lines, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            const auto equals = line.find('=');
            if (line.empty() || line.front() == '#' || equals == std::string::npos)
                continue;
            const std::string_view key(line.data(), equals);
            const std::string_view value(line.data() + equals + 1, line.size() - equals - 1);
            if (key == "theme")
                if (const auto theme = themeFromName(value))
                    preferences.theme = *theme;
        }
        return preferences;
    }

    [[nodiscard]] static EditorPreferences load(const std::string& path)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in)
            return {};
        std::ostringstream buffer;
        buffer << in.rdbuf();
        return parse(buffer.str());
    }

    bool save(const std::string& path) const
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out)
            return false;
        out << serialize();
        return static_cast<bool>(out);
    }
};

} // namespace ats::ui
