#pragma once

#include "../core/ParameterRegistry.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace ats::parameters
{

struct ParameterSnapshot
{
    static constexpr std::size_t kCapacity = 128;
    std::array<float, kCapacity> values {};
    std::uint16_t count = 0;
    std::uint64_t revision = 0;

    [[nodiscard]] float read(std::size_t index) const noexcept
    { return index < count ? values[index] : 0.0f; }

    [[nodiscard]] static ParameterSnapshot capture(const ats::ParameterStore& store,
                                                   std::uint64_t revision = 0) noexcept
    {
        ParameterSnapshot snapshot;
        snapshot.count = static_cast<std::uint16_t>(std::min(store.size(), kCapacity));
        snapshot.revision = revision;
        for (std::size_t i = 0; i < snapshot.count; ++i)
            snapshot.values[i] = store.read(i);
        return snapshot;
    }
};

} // namespace ats::parameters
