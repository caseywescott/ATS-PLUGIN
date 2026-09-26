#pragma once

#include "AtsPrimitives.h"

#include <optional>

namespace ats
{

struct StateHeader
{
    static constexpr std::uint32_t kMagic = 0x41545350u; // ATSP
    static constexpr std::uint32_t kCurrentVersion = 18;
    static constexpr std::uint32_t kMinimumReadableVersion = 2;

    std::uint32_t version = kCurrentVersion;
};

class AtsStateCodec
{
public:
    [[nodiscard]] static std::vector<std::byte> encodeHeader(StateHeader header = {})
    {
        ByteWriter writer;
        writer.writeU32(StateHeader::kMagic);
        writer.writeU32(header.version);
        return std::move(writer).take();
    }

    [[nodiscard]] static std::optional<StateHeader> decodeHeader(const void* data, std::size_t size) noexcept
    {
        if (data == nullptr || size < 8) return std::nullopt;
        ByteReader reader(static_cast<const std::byte*>(data), size);
        std::uint32_t magic = 0, version = 0;
        if (!reader.readU32(magic) || !reader.readU32(version) || magic != StateHeader::kMagic ||
            version < StateHeader::kMinimumReadableVersion || version > StateHeader::kCurrentVersion)
            return std::nullopt;
        return StateHeader { version };
    }
};

} // namespace ats
