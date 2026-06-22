/*
 * ats_transient: onset detection, transient layer extraction, and attack shaping.
 *
 * Modes:
 *   detect   — Smearnov onset detection, output JSON onset list
 *   extract  — detect + extractTransientLayer, output JSON event list
 *   render   — detect + extract + renderTransientLayer, write WAV to --output-wav
 *   smear    — detect + smearAttacks (blur/sharpen/exaggerate), write ATS to --output-ats
 *
 * Usage:
 *   ats_transient [options] <input.ats>
 *
 * Detection options (all modes):
 *   --weight-det <f>       D1 deterministic energy flux weight (default 1.0)
 *   --weight-res <f>       D2 residual band-energy flux weight (default 0.8)
 *   --weight-hfc <f>       D3 residual HFC flux weight (default 0.6)
 *   --weight-birth <f>     D4 partial-birth rate weight (default 0.7)
 *   --weight-phase <f>     D5 phase deviation weight (default 0.4)
 *   --use-phase-dev        Enable D5 (off by default; auto-enabled for type 2/4)
 *   --norm-window <n>      Normalization MAD window radius in frames (default 8)
 *   --peak-window <n>      Peak-picking local-max window radius in frames (default 6)
 *   --threshold-lambda <f> Additive threshold; <=0 → auto from global MAD (default 0)
 *   --threshold-scale <f>  Auto-threshold MAD multiplier (default 1.5)
 *   --onset-floor <f>      Minimum combined strength to accept onset (default 1e-6)
 *   --min-gap <f>          Refractory gap between onsets in seconds (default 0.03)
 *   --pre-margin <f>       Pre-onset margin in seconds (default 0.005)
 *   --max-attack-frames <n> Cap on attack window length in frames (0 = uncapped)
 *
 * Smear mode options (--mode smear only):
 *   --output-ats <path>    Output ATS path (required)
 *   --blur-ms <f>          Gaussian blur of attack envelope in ms (default 0)
 *   --sharpen <f>          Unsharp-mask sharpening amount (default 0)
 *   --exaggerate-db <f>    Exaggerate attack peak by dB (default 0; negative = soften)
 *   --max-db <f>           Clamp on exaggerate-db (default 12)
 *   --mix <f>              Dry/wet blend: 0=dry 1=full effect (default 1.0)
 *   --target <s>           partials | residual | both (default both)
 *
 * Render mode options:
 *   --output-wav <path>    Output WAV path (required)
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

// ── arg helpers ──────────────────────────────────────────────────────────────

static bool matchFlag(const char* arg, const char* flag, int i, int argc, double& out)
{
    if (std::strcmp(arg, flag) == 0 && i + 1 < argc) { out = std::atof(nullptr); return true; }
    return false;
}

// ── main ──────────────────────────────────────────────────────────────────────

int main(int argc, char** argv)
{
    std::string mode    = "detect";
    const char* in_ats  = nullptr;
    const char* out_wav = nullptr;
    const char* out_ats = nullptr;

    // SmearnovDetectParams — all tunable from CLI
    ats::SmearnovDetectParams dp;
    bool forcePhaseDeviation = false;

    // SmearnovParams — smear mode only
    ats::SmearnovParams sp;
    std::string targetStr = "both";

    for (int i = 1; i < argc; ) {
        const std::string arg = argv[i];

        // mode / output
        if (arg == "--mode"       && i+1 < argc) { mode    = argv[++i]; ++i; continue; }
        if (arg == "--output-wav" && i+1 < argc) { out_wav = argv[++i]; ++i; continue; }
        if (arg == "--output-ats" && i+1 < argc) { out_ats = argv[++i]; ++i; continue; }

        // detection weights
        if (arg == "--weight-det"   && i+1 < argc) { dp.weightDetFlux  = std::atof(argv[++i]); ++i; continue; }
        if (arg == "--weight-res"   && i+1 < argc) { dp.weightResFlux  = std::atof(argv[++i]); ++i; continue; }
        if (arg == "--weight-hfc"   && i+1 < argc) { dp.weightHfcFlux  = std::atof(argv[++i]); ++i; continue; }
        if (arg == "--weight-birth" && i+1 < argc) { dp.weightBirth    = std::atof(argv[++i]); ++i; continue; }
        if (arg == "--weight-phase" && i+1 < argc) { dp.weightPhaseDev = std::atof(argv[++i]); ++i; continue; }
        if (arg == "--use-phase-dev") { forcePhaseDeviation = true; ++i; continue; }

        // normalization / peak-picking
        if (arg == "--norm-window"       && i+1 < argc) { dp.normWindowRadiusFrames = std::atoi(argv[++i]); ++i; continue; }
        if (arg == "--peak-window"       && i+1 < argc) { dp.peakWindowRadiusFrames = std::atoi(argv[++i]); ++i; continue; }
        if (arg == "--threshold-lambda"  && i+1 < argc) { dp.thresholdLambda        = std::atof(argv[++i]); ++i; continue; }
        if (arg == "--threshold-scale"   && i+1 < argc) { dp.thresholdAutoScale     = std::atof(argv[++i]); ++i; continue; }
        if (arg == "--onset-floor"       && i+1 < argc) { dp.onsetFloor             = std::atof(argv[++i]); ++i; continue; }
        if (arg == "--min-gap"           && i+1 < argc) { dp.minGapSeconds          = std::atof(argv[++i]); ++i; continue; }
        if (arg == "--pre-margin"        && i+1 < argc) { dp.preMarginSeconds       = std::atof(argv[++i]); ++i; continue; }
        if (arg == "--max-attack-frames" && i+1 < argc) { dp.maxAttackFrames        = std::atoi(argv[++i]); ++i; continue; }

        // smear params
        if (arg == "--blur-ms"      && i+1 < argc) { sp.blurMs          = std::atof(argv[++i]); ++i; continue; }
        if (arg == "--sharpen"      && i+1 < argc) { sp.sharpenAmount   = std::atof(argv[++i]); ++i; continue; }
        if (arg == "--exaggerate-db"&& i+1 < argc) { sp.exaggerateDb    = std::atof(argv[++i]); ++i; continue; }
        if (arg == "--max-db"       && i+1 < argc) { sp.maxDb           = std::atof(argv[++i]); ++i; continue; }
        if (arg == "--mix"          && i+1 < argc) { sp.mix             = std::atof(argv[++i]); ++i; continue; }
        if (arg == "--target"       && i+1 < argc) { targetStr          = argv[++i];             ++i; continue; }

        if (arg[0] == '-') { std::fprintf(stderr, "Unknown option: %s\n", argv[i]); return 1; }
        if (!in_ats) { in_ats = argv[i++]; continue; }
        ++i;
    }

    if (!in_ats) {
        std::fprintf(stderr,
            "Usage: ats_transient [--mode detect|extract|render|smear] [options] <input.ats>\n"
            "  --output-wav <path>   (render mode)\n"
            "  --output-ats <path>   (smear mode)\n"
            "Detection: --weight-det --weight-res --weight-hfc --weight-birth --weight-phase\n"
            "           --use-phase-dev --norm-window --peak-window\n"
            "           --threshold-lambda --threshold-scale --onset-floor\n"
            "           --min-gap --pre-margin --max-attack-frames\n"
            "Smear:     --blur-ms --sharpen --exaggerate-db --max-db --mix\n"
            "           --target partials|residual|both\n");
        return 1;
    }

    // Parse target
    if (targetStr == "partials")       sp.targets = ats::SmearnovTarget::Partials;
    else if (targetStr == "residual")  sp.targets = ats::SmearnovTarget::Residual;
    else                               sp.targets = ats::SmearnovTarget::Both;

    ATS_SOUND* sound = static_cast<ATS_SOUND*>(std::calloc(1, sizeof(ATS_SOUND)));
    int st = 0;
    if (ats_load(sound, in_ats, &st) < 0) { std::free(sound); return 1; }

    dp.usePhaseDeviation = forcePhaseDeviation || FILE_HAS_PHASE(st);
    auto onsets = ats::detectOnsets(sound, dp);

    // ── detect ────────────────────────────────────────────────────────────────
    if (mode == "detect") {
        std::printf("{\"onsets\":%s}\n", jsonOnsets(sound, onsets).c_str());
        free_sound(sound); std::free(sound);
        return 0;
    }

    // ── extract ───────────────────────────────────────────────────────────────
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

    // ── render ────────────────────────────────────────────────────────────────
    if (mode == "render") {
        if (!out_wav) {
            std::fprintf(stderr, "render mode requires --output-wav\n");
            free_sound(sound); std::free(sound); return 1;
        }
        SPARAMS sparams;
        std::memset(&sparams, 0, sizeof(sparams));
        sparams.amp = 1.0; sparams.ramp = 1.0; sparams.frec = 1.0; sparams.max_stretch = 1.0;
        sparams.beg = 0.0; sparams.end = sound->dur; sparams.sr = (double)sr; sparams.allorsel = 1;
        CURVE tc; curve_init(&tc);
        double* base = nullptr; int n_base = 0;
        do_synthesis(sound, &sparams, &tc, nullptr, &base, &n_base);
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

    // ── smear ────────────────────────────────────────────────────────────────
    if (mode == "smear") {
        if (!out_ats) {
            std::fprintf(stderr, "smear mode requires --output-ats\n");
            free_sound(sound); std::free(sound); return 1;
        }
        if (onsets.empty()) {
            std::fprintf(stderr, "smear: no onsets detected — nothing to do\n");
            free_sound(sound); std::free(sound); return 1;
        }

        bool changed = ats::smearAttacks(sound, onsets, sp);

        int saved = ats_save(sound, out_ats, 0.0, st);
        free_sound(sound); std::free(sound);

        if (saved < 0) {
            std::fprintf(stderr, "ats_save failed\n");
            return 1;
        }

        std::printf("{\"onsets_processed\":%d,\"changed\":%s,"
                    "\"blur_ms\":%.4f,\"sharpen_amount\":%.4f,"
                    "\"exaggerate_db\":%.4f,\"mix\":%.4f,"
                    "\"target\":\"%s\",\"output_path\":\"%s\"}\n",
                    (int)onsets.size(),
                    changed ? "true" : "false",
                    sp.blurMs, sp.sharpenAmount, sp.exaggerateDb, sp.mix,
                    targetStr.c_str(), out_ats);
        return 0;
    }

    std::fprintf(stderr, "Unknown mode: %s\n", mode.c_str());
    free_sound(sound); std::free(sound);
    return 1;
}
