#pragma once

#include "../core/AtsPrimitives.h"
#include "../core/ParameterRegistry.h"

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace ats::state
{

class ParameterPresetCodec
{
public:
    static constexpr std::uint32_t kMagic = 0x50535441u; // ATSP parameter preset
    static constexpr std::uint32_t kVersion = 1;

    [[nodiscard]] static std::vector<std::byte> encode(const ParameterStore& parameters)
    {
        ByteWriter writer;
        const auto& descriptors = ParameterRegistry::descriptors();
        writer.writeU32(kMagic);
        writer.writeU32(kVersion);
        writer.writeU32(static_cast<std::uint32_t>(descriptors.size()));
        for (const auto& descriptor : descriptors)
        {
            writer.writeU16(static_cast<std::uint16_t>(descriptor.id.size()));
            writer.writeBytes(descriptor.id.data(), descriptor.id.size());
            writer.writeF32(parameters.read(descriptor.denseIndex));
        }
        return std::move(writer).take();
    }

    [[nodiscard]] static bool decode(const std::vector<std::byte>& payload,
                                     ParameterStore& parameters, std::string& error)
    {
        ByteReader reader(payload.data(), payload.size());
        std::uint32_t magic = 0, version = 0, count = 0;
        if (!reader.readU32(magic) || !reader.readU32(version) || !reader.readU32(count) ||
            magic != kMagic || version != kVersion || count > 4096)
        {
            error = "invalid_preset_header";
            return false;
        }

        auto values = parameters.snapshot();
        for (std::uint32_t index = 0; index < count; ++index)
        {
            std::uint16_t idSize = 0;
            if (!reader.readU16(idSize) || idSize == 0 || idSize > 256 || idSize > reader.remaining())
            {
                error = "invalid_preset_entry";
                return false;
            }
            std::string id(idSize, '\0');
            float value = 0.0f;
            if (!reader.readBytes(id.data(), idSize) || !reader.readF32(value) || !std::isfinite(value))
            {
                error = "invalid_preset_value";
                return false;
            }
            if (const auto* descriptor = ParameterRegistry::find(id))
                values[descriptor->denseIndex] = descriptor->snap(value);
        }
        if (reader.remaining() != 0)
        {
            error = "trailing_preset_data";
            return false;
        }
        for (std::size_t index = 0; index < values.size(); ++index)
            (void) parameters.write(index, values[index]);
        error.clear();
        return true;
    }
};

} // namespace ats::state
