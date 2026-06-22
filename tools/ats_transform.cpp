/*
 * ats_transform: apply frequency/amplitude/noise transforms to an ATS model.
 *
 * Usage: ats_transform [options] <input.ats> <output.ats>
 *
 * Options:
 *   --freq-mode     Linear|Stretch|Compress|Harmonic  (default: Linear)
 *   --freq-amount   <f>   mode-specific warp amount   (default: 0.0 = identity)
 *   --freq-pivot-hz <f>   pivot frequency for Stretch/Compress/Harmonic (default: 440.0)
 *   --amp-scale     <f>   multiply all partial amplitudes (default: 1.0)
 *   --noise-scale   <f>   multiply all band energies      (default: 1.0)
 *   --type          <1-4> save as different ATS type       (default: keep source)
 */

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

extern "C" {
#include "atsa.h"
}
#include "AtsFrequencyShape.h"
#include "AtsParams.h"

static void usage(const char* prog)
{
    std::fprintf(stderr,
        "Usage: %s [--freq-mode Linear|Stretch|Compress|Harmonic]\n"
        "          [--freq-amount <f>] [--freq-pivot-hz <f>]\n"
        "          [--amp-scale <f>] [--noise-scale <f>] [--type <1-4>]\n"
        "          <input.ats> <output.ats>\n", prog);
}

static ats::FrequencyShapeMode parseMode(const char* s)
{
    if (std::strcmp(s, "Stretch")  == 0) return ats::FrequencyShapeMode::Stretch;
    if (std::strcmp(s, "Compress") == 0) return ats::FrequencyShapeMode::Compress;
    if (std::strcmp(s, "Harmonic") == 0) return ats::FrequencyShapeMode::Harmonic;
    return ats::FrequencyShapeMode::Linear;
}

int main(int argc, char** argv)
{
    const char* in_ats   = nullptr;
    const char* out_ats  = nullptr;
    std::string freq_mode_str = "Linear";
    double freq_amount   = 0.0;
    double freq_pivot_hz = 440.0;
    double amp_scale     = 1.0;
    double noise_scale   = 1.0;
    int out_type         = -1;

    for (int i = 1; i < argc; ) {
        auto arg = std::string(argv[i]);
        if (arg == "--freq-mode"    && i+1 < argc) { freq_mode_str = argv[++i]; ++i; continue; }
        if (arg == "--freq-amount"  && i+1 < argc) { freq_amount   = std::atof(argv[++i]); ++i; continue; }
        if (arg == "--freq-pivot-hz"&& i+1 < argc) { freq_pivot_hz = std::atof(argv[++i]); ++i; continue; }
        if (arg == "--amp-scale"    && i+1 < argc) { amp_scale     = std::atof(argv[++i]); ++i; continue; }
        if (arg == "--noise-scale"  && i+1 < argc) { noise_scale   = std::atof(argv[++i]); ++i; continue; }
        if (arg == "--type"         && i+1 < argc) { out_type      = std::atoi(argv[++i]); ++i; continue; }
        if (arg[0] == '-') { usage(argv[0]); return 1; }
        if (!in_ats)  { in_ats  = argv[i++]; continue; }
        if (!out_ats) { out_ats = argv[i++]; continue; }
        usage(argv[0]); return 1;
    }

    if (!in_ats || !out_ats) { usage(argv[0]); return 1; }
    if (out_type != -1 && (out_type < 1 || out_type > 4)) {
        std::fprintf(stderr, "Invalid type %d\n", out_type); return 1;
    }

    ATS_SOUND* sound = static_cast<ATS_SOUND*>(std::calloc(1, sizeof(ATS_SOUND)));
    if (!sound) { std::fprintf(stderr, "Out of memory\n"); return 1; }

    int sound_type = 0;
    if (ats_load(sound, in_ats, &sound_type) < 0) { std::free(sound); return 1; }

    // Apply frequency shape
    ats::FrequencyShapeSettings fs;
    fs.mode    = parseMode(freq_mode_str.c_str());
    fs.amount  = freq_amount;
    fs.pivotHz = freq_pivot_hz;
    fs.resolvedPivotHz = ats::resolveFrequencyShapePivotHz(sound, fs);

    if (ats::isFrequencyShapeActive(fs))
        ats::applyFrequencyShapeToSound(sound, fs);

    // Apply amplitude scale
    if (std::fabs(amp_scale - 1.0) > 1e-9) {
        double new_max = 0.0;
        for (int p = 0; p < sound->partials; ++p) {
            for (int f = 0; f < sound->frames; ++f) {
                sound->amp[p][f] *= amp_scale;
                if (sound->amp[p][f] > new_max) new_max = sound->amp[p][f];
            }
        }
        sound->ampmax = new_max;
    }

    // Apply noise scale
    if (std::fabs(noise_scale - 1.0) > 1e-9 && sound->band_energy) {
        for (int b = 0; b < ATSA_CRITICAL_BANDS; ++b)
            for (int f = 0; f < sound->frames; ++f)
                sound->band_energy[b][f] *= noise_scale;
    }

    // Recompute averages
    set_av(sound);

    int save_type = (out_type == -1) ? sound_type : out_type;
    int saved = ats_save(sound, out_ats, 0.0, save_type);
    free_sound(sound);
    std::free(sound);

    if (saved < 0) { std::fprintf(stderr, "ats_save failed\n"); return 1; }
    std::fprintf(stdout, "Transformed model saved to '%s' (%d bytes)\n", out_ats, saved);
    return 0;
}
