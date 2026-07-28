#pragma once

#include <cstdint>

namespace ats::engine
{

// A deliberately scalar snapshot. It can be copied by a UI reader and is
// published as an immutable slot to the audio thread.
struct PlaybackSnapshot
{
    std::uint64_t modelRevision = 0;
    std::uint64_t renderGeneration = 0;
    std::uint64_t playheadFrame = 0;
    float peak = 0.0f;
    float rms = 0.0f;
    bool playing = false;
    bool rendering = false;
};

} // namespace ats::engine
