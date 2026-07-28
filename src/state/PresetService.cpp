#include "PresetService.h"

#include <filesystem>

namespace ats::state
{
namespace
{
const std::array<FactoryPreset, 12> kFactoryPresets {{
    { "Default", "Classic neutral ATS playback defaults." },
    { "Bright partials", "Classic playback with stronger sinusoidal partials and reduced residual energy." },
    { "Residual emphasis", "Classic playback with a quieter partial layer and stronger residual layer." },
    { "Residuals only", "Classic playback focused entirely on the residual/noise layer." },
    { "Inharmonic spread", "Stretch mode opens partial spacing around the auto pivot for clangorous inharmonic color." },
    { "Clustered partials", "Compress mode pulls partial spacing inward around the auto pivot." },
    { "Harmonic bloom", "Harmonic mode gently exaggerates rank-based harmonic spacing from the auto F0 estimate." },
    { "Morph stable default", "Bark grid, frequency-rank matching, log-frequency/power interpolation, oscillator phase, and conservative smoothing for broad source material." },
    { "Morph tonal", "Bark grid with greedy nearest matching, Bark-coordinate frequency motion, relative-cluster phase metadata, and lower residual gain for sustained harmonic material." },
    { "Morph vocal/formant", "Bark grid with greedy nearest matching, dB amplitude interpolation, source-only merge-to-residual handling, and sustained-after-attack protection for vowel/formant material." },
    { "Morph percussive", "Bark grid with quick smoothing, crossfaded attack metadata, and hold/fade unmatched handling for plucked or struck sources." },
    { "Morph noisy texture", "Mel grid with greedy nearest matching, dB amplitude interpolation, merge-to-residual unmatched handling, and a residual-heavy lane balance for breathy or noisy material." },
}};
}

const std::array<FactoryPreset, 12>& PresetService::factoryPresets()
{
    return kFactoryPresets;
}

std::string PresetService::withExtension(std::string name)
{
    if (name.size() < kExtension.size() ||
        name.compare(name.size() - kExtension.size(), kExtension.size(), kExtension) != 0)
        name += kExtension;
    return name;
}

std::vector<PresetEntry> PresetService::entries(const ats::CancellationToken* cancellation) const
{
    std::vector<PresetEntry> result;
    result.reserve(kFactoryPresets.size());
    for (std::size_t i = 0; i < kFactoryPresets.size(); ++i)
        result.push_back({ "Factory: " + kFactoryPresets[i].name, {}, true, static_cast<std::int32_t>(i) });

    const auto listing = files_.list(directory_, kExtension, cancellation);
    if (!listing.succeeded()) return result;
    ats::ByteReader reader(listing.bytes.data(), listing.bytes.size());
    std::uint32_t count = 0;
    if (!reader.readU32(count)) return result;
    std::vector<PresetEntry> user;
    user.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i)
    {
        std::uint32_t size = 0;
        if (!reader.readU32(size) || size > 4096 || size > reader.remaining()) return result;
        std::string name(size, '\0');
        if (!reader.readBytes(name.data(), size)) return result;
        if (name.size() <= kExtension.size()) continue;
        const auto stem = name.substr(0, name.size() - kExtension.size());
        const auto path = (std::filesystem::u8path(directory_.utf8) / name).u8string();
        user.push_back({ "User: " + stem, { path }, false, -1 });
    }
    result.insert(result.end(), user.begin(), user.end());
    return result;
}

ats::platform::FileResult PresetService::load(const ats::Path& path,
                                              const ats::CancellationToken* cancellation) const
{
    return files_.read(path, cancellation);
}

ats::platform::FileResult PresetService::save(std::string name, const std::vector<std::byte>& payload,
                                              bool overwrite, const ats::CancellationToken* cancellation) const
{
    name = withExtension(std::move(name));
    if (name == kExtension || name.find('/') != std::string::npos || name.find('\\') != std::string::npos)
        return { ats::platform::FileStatus::invalidPath, {}, {}, { "invalid_name", "preset name is invalid" } };
    const auto path = ats::Path { (std::filesystem::u8path(directory_.utf8) / name).u8string() };
    return files_.writeAtomic(path, payload, overwrite, cancellation);
}

} // namespace ats::state
