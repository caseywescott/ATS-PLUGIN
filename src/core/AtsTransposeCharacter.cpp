#include "AtsTransposeCharacter.h"

#include "AtsSpectralEnvelope.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace ats
{

namespace
{
constexpr double kMinRatio = 0.003981071705534973; // -48 dB
constexpr double kMaxRatio = 3.981071705534973;    // +12 dB
constexpr int kMinEnvelopePartials = 3;
} // namespace

bool TransposeCharacterParams::active() const noexcept
{
    if (!std::isfinite(frequencyFactor) || frequencyFactor <= 0.0 || std::abs(frequencyFactor - 1.0) < 1.0e-9)
        return false;
    return preserveEnvelope || std::abs(tiltDbPerOctavePerOctave) > 1.0e-9 || ceilingHz > 0.0;
}

AtsSoundPtr applyTransposeCharacter(const ATS_SOUND* sound, const TransposeCharacterParams& params)
{
    if (sound == nullptr || sound->frames <= 0 || sound->partials <= 0 || !params.active())
        return {};
    auto* out = copy_sound(const_cast<ATS_SOUND*>(sound));
    if (out == nullptr)
        return {};
    AtsSoundPtr result(out);

    const auto factor = params.frequencyFactor;
    const auto octaves = std::log2(factor);
    const auto tiltPerOctave = params.tiltDbPerOctavePerOctave * octaves;
    const auto pivot = params.pivotHz > 0.0 ? params.pivotHz : 1000.0;
    const auto ceiling = params.ceilingHz > 0.0 ? params.ceilingHz
                                                : (sound->srate > 0 ? 0.95 * 0.5 * sound->srate : 20000.0);
    const auto fadeOctaves = std::max(1.0e-6, params.ceilingFadeOctaves);

    std::vector<SpectralEnvelopePoint> points;
    points.reserve(static_cast<std::size_t>(sound->partials));
    BarkSpectralEnvelope envelope;
    double frqmax = 0.0, ampmax = 0.0;

    for (int f = 0; f < sound->frames; ++f)
    {
        bool haveEnvelope = false;
        if (params.preserveEnvelope)
        {
            points.clear();
            for (int p = 0; p < sound->partials; ++p)
                if (sound->amp[p][f] > 0.0 && sound->frq[p][f] > 0.0)
                    points.push_back({sound->frq[p][f], sound->amp[p][f]});
            if (static_cast<int>(points.size()) >= kMinEnvelopePartials)
            {
                envelope.build(points);
                haveEnvelope = envelope.isValid();
            }
        }

        for (int p = 0; p < sound->partials; ++p)
        {
            const auto frq = sound->frq[p][f];
            auto amp = sound->amp[p][f];
            if (out->pha) out->pha[p][f] = 0.0;
            if (!(frq > 0.0) || !(amp > 0.0))
            {
                out->frq[p][f] = frq > 0.0 ? frq * factor : 0.0;
                out->amp[p][f] = 0.0;
                continue;
            }
            const auto newFrq = frq * factor;
            double gain = 1.0;
            if (haveEnvelope)
            {
                const auto eSrc = envelope.sampleAtHz(frq);
                const auto eDst = envelope.sampleAtHz(newFrq);
                if (eSrc > 0.0 && std::isfinite(eSrc) && std::isfinite(eDst))
                    gain *= std::clamp(eDst / eSrc, kMinRatio, kMaxRatio);
            }
            if (std::abs(tiltPerOctave) > 0.0)
                gain *= std::pow(10.0, tiltPerOctave * std::log2(newFrq / pivot) / 20.0);
            if (newFrq > ceiling)
            {
                const auto over = std::log2(newFrq / ceiling) / fadeOctaves;
                gain *= std::clamp(1.0 - over, 0.0, 1.0);
            }
            amp *= gain;
            out->frq[p][f] = newFrq;
            out->amp[p][f] = amp;
            frqmax = std::max(frqmax, newFrq);
            ampmax = std::max(ampmax, amp);
        }
    }

    out->has_slopes = 0;
    for (int p = 0; p < out->partials; ++p)
        for (int f = 0; f < out->frames; ++f)
            out->frq_slope[p][f] = out->amp_slope[p][f] = 0.0;
    out->frqmax = frqmax;
    out->ampmax = ampmax;
    if (out->av != nullptr)
        for (int p = 0; p < out->partials; ++p)
        {
            double sumF = 0.0, sumA = 0.0;
            int n = 0;
            for (int f = 0; f < out->frames; ++f)
                if (out->amp[p][f] > 0.0)
                {
                    sumF += out->frq[p][f];
                    sumA += out->amp[p][f];
                    ++n;
                }
            out->av[p].frq = n > 0 ? sumF / n : out->av[p].frq * factor;
            out->av[p].amp = n > 0 ? sumA / n : 0.0;
        }
    return result;
}

} // namespace ats
