#pragma once

#include "../core/PlaybackBuffers.h"

namespace ats::audio
{

// Audio-facing name for the preallocated triple-buffer ownership protocol.
// The implementation remains in core so playback and cache code share one
// ownership rule.
using AudioBufferPool = ats::PlaybackBufferPool;

} // namespace ats::audio
