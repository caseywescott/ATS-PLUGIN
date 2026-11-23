#pragma once

/*
 * AtsTransientness — soft per-frame transientness index.
 *
 * Method (S. Molla & B. Torrésani, "Determining local transientness of audio
 * signals", IEEE Signal Processing Letters 11(7), 2004): a short block of audio
 * is expanded in two orthonormal bases that are "dual" in sparsity —
 *
 *   (a) a local-cosine basis (here the DCT-II of the windowed block), in which
 *       tonal/stationary content is sparse and clicks are dense, and
 *   (b) an orthogonal wavelet basis (here a full-depth periodic Daubechies-4
 *       DWT), in which transients are sparse and sustained tones are dense.
 *
 * For each expansion the coefficient energies are normalised to unit sum and
 * their logarithmic dimension D = (1/N) Σ log2(e_n) (e_n floored at
 * `floorEnergy`) gives an estimated "size" N̂ = 2^D · N: a flat (dense)
 * expansion yields N̂ = 1, a sparse one a much smaller value. The index is
 *
 *   I_tr = N̂_cosine / (N̂_cosine + N̂_wavelet)          in [0, 1]
 *
 * so a block whose cosine expansion is dense relative to its wavelet
 * expansion (a click) scores near 1, a steady tone near 0 and broadband noise
 * (dense in both) sits in between. I_ton = 1 - I_tr. A block with no energy
 * scores 0. The estimate is level-independent and fully deterministic.
 *
 * One value is returned per analysis frame. The model-driven overload centres
 * each block on ATS_SOUND::time[0][f]; the model-free overload uses a fixed
 * hop grid. Blocks are zero-padded at the signal edges.
 */

#include <cstddef>
#include <vector>

extern "C" {
#include "atsa.h"
}

namespace ats
{

struct TransientnessParams
{
    int blockSize = 0;          // 0 -> model window_size rounded up to a power of 2
    double floorEnergy = 1e-12; // floor applied to normalised coefficient energies
};

struct TransientnessResult
{
    std::vector<double> index; // per frame, 0 tonal .. 1 transient
    double mean = 0.0;         // mean of `index` (0 when there are no frames)
};

/** Transientness index on the frame grid of `sound` (block centred at time[0][f],
 *  block size from params.blockSize or the model's window_size rounded up to a
 *  power of two). `samples` is the mono audio the model was analysed from. */
[[nodiscard]] TransientnessResult transientnessIndex(const double* samples,
                                                     std::size_t sampleCount,
                                                     int sampleRate,
                                                     const ATS_SOUND* sound,
                                                     const TransientnessParams& params = {});

/** Same, over a fixed hop/window grid without a model: frame f is centred at
 *  f * hopSamples for f in [0, ceil(sampleCount / hopSamples)); blockSize is
 *  rounded up to a power of two (params.blockSize is ignored here). */
[[nodiscard]] TransientnessResult transientnessIndex(const double* samples,
                                                     std::size_t sampleCount,
                                                     int hopSamples,
                                                     int blockSize,
                                                     const TransientnessParams& params = {});

} // namespace ats
