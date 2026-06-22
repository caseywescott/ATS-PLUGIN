/*
 * ats_transient: onset detection and transient layer extraction from an ATS model.
 *
 * Modes:
 *   detect   — run Smearnov onset detection, output JSON onset list
 *   extract  — detect + extractTransientLayer, output JSON event list
 *   render   — detect + extract + renderTransientLayer, write WAV to --output-wav
 *
 * Usage:
 *   ats_transient [--mode detect|extract|render] [--output-wav <path>] <input.ats>
 */

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#include <sndfile.h>

extern "C" {
#include "atsa.h"
}
#include "AtsTransientLayer.h"
#include "AtsTransientSmear.h"

// ── minimal JSON helpers ──────────────────────────────────────────────────────

static std::string jd(double v)  { std::ostringstream o; o << v; return o.str(); }
static std::string ji(int v)      { return std::to_string(v); }

static std::string jsonDoubleArray(const double* v, int n)
{
    std::ostringstream o;
    o << "[";
    for (int i = 0; i < n; ++i) {
        if (i) o << ",";
        o << v[i];
    }
    o << "]";
    return o.str();
}

// ── serialize onsets ──────────────────────────────────────────────────────────

static std::string jsonOnsets(const ATS_SOUND* sound,
                              const std::vector<ats::AtsOnset>& onsets)
{
    std::ostringstream o;
    o << "[";
    for (size_t i = 0; i < onsets.size(); ++i) {
        const auto& on = onsets[i];
        if (i) o << ",";
        double t = (on.frame < sound->frames) ? sound->time[0][on.frame] : 0.0;
        o << "{\"frame\":" << ji(on.frame)
          << ",\"pre_frame\":" << ji(on.preFrame)
          << ",\"peak_frame\":" << ji(on.peakFrame)
          << ",\"time_s\":" << jd(t)
          << ",\"strength\":" << jd(on.strength) << "}";
    }
    o << "]";
    return o.str();
}

// ── write WAV ────────────────────────────────────────────────────────────────

static int write_wav(const char* path, const double* buf, int n, int sr)
{
    SF_INFO info;
    std::memset(&info, 0, sizeof(info));
    info.samplerate = sr;
    info.channels   = 1;
    info.format     = SF_FORMAT_WAV | SF_FORMAT_PCM_16;
    SNDFILE* f = sf_open(path, SFM_WRITE, &info);
    if (!f) {
        std::fprintf(stderr, "Cannot open '%s': %s\n", path, sf_strerror(nullptr));
        return -1;
    }
    sf_writef_double(f, buf, n);
    sf_close(f);
    return 0;
}

// ── main ──────────────────────────────────────────────────────────────────────

int main(int argc, char** argv)
{
    std::string mode    = "detect";
    const char* in_ats  = nullptr;
    const char* out_wav = nullptr;

    for (int i = 1; i < argc; ) {
        auto arg = std::string(argv[i]);
        if (arg == "--mode"       && i+1 < argc) { mode    = argv[++i]; ++i; continue; }
        if (arg == "--output-wav" && i+1 < argc) { out_wav = argv[++i]; ++i; continue; }
        if (arg[0] == '-') { std::fprintf(stderr, "Unknown option: %s\n", argv[i]); return 1; }
        if (!in_ats) { in_ats = argv[i++]; continue; }
        ++i;
    }

    if (!in_ats) {
        std::fprintf(stderr,
            "Usage: ats_transient [--mode detect|extract|render] [--output-wav <path>] <input.ats>\n");
        return 1;
    }

    ATS_SOUND* sound = static_cast<ATS_SOUND*>(std::calloc(1, sizeof(ATS_SOUND)));
    int st = 0;
    if (ats_load(sound, in_ats, &st) < 0) { std::free(sound); return 1; }

    ats::SmearnovDetectParams params;
    params.usePhaseDeviation = FILE_HAS_PHASE(st);
    auto onsets = ats::detectOnsets(sound, params);

    if (mode == "detect") {
        std::printf("{\"onsets\":%s}\n", jsonOnsets(sound, onsets).c_str());
        free_sound(sound); std::free(sound);
        return 0;
    }

    // extract
    auto events = ats::extractTransientLayer(sound, onsets);
    int sr = sound->srate;

    if (mode == "extract") {
        std::ostringstream o;
        o << "{\"onsets\":" << jsonOnsets(sound, onsets) << ",\"events\":[";
        for (size_t i = 0; i < events.size(); ++i) {
            const auto& ev = events[i];
            if (i) o << ",";
            o << "{\"frame\":" << ji(ev.frameIndex)
              << ",\"time_s\":" << jd(ev.timeSeconds)
              << ",\"strength\":" << jd(ev.strength)
              << ",\"deterministic_energy\":" << jd(ev.deterministicEnergy)
              << ",\"band_energy\":" << jsonDoubleArray(ev.bandEnergy.data(), ATSA_CRITICAL_BANDS)
              << "}";
        }
        o << "]}";
        std::printf("%s\n", o.str().c_str());
        free_sound(sound); std::free(sound);
        return 0;
    }

    if (mode == "render") {
        if (!out_wav) {
            std::fprintf(stderr, "render mode requires --output-wav\n");
            free_sound(sound); std::free(sound); return 1;
        }
        // Compute total synthesis length from SPARAMS
        SPARAMS sp;
        std::memset(&sp, 0, sizeof(sp));
        sp.amp = 1.0; sp.ramp = 1.0; sp.frec = 1.0; sp.max_stretch = 1.0;
        sp.beg = 0.0; sp.end = sound->dur; sp.sr = (double)sr; sp.allorsel = 1;
        CURVE tc; curve_init(&tc);
        double* base = nullptr; int n_base = 0;
        do_synthesis(sound, &sp, &tc, nullptr, &base, &n_base);
        curve_free(&tc);

        std::vector<double> trans_buf(n_base, 0.0);
        ats::TransientLayerParams tp;
        (void)ats::renderTransientLayer(events, tp, sr, trans_buf.data(), n_base);

        int rc = write_wav(out_wav, trans_buf.data(), n_base, sr);
        std::free(base);
        free_sound(sound); std::free(sound);
        if (rc == 0)
            std::fprintf(stdout, "Transient layer WAV written to '%s'\n", out_wav);
        return rc < 0 ? 1 : 0;
    }

    std::fprintf(stderr, "Unknown mode: %s\n", mode.c_str());
    free_sound(sound); std::free(sound);
    return 1;
}
