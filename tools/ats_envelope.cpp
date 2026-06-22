/*
 * ats_envelope: compute True-Envelope spectral envelopes from an ATS model.
 *
 * Modes:
 *   envelope  (default): output per-frame Bark-gridded envelope + formant peaks as JSON
 *   compare:  formant-aligned comparison between two ATS files (outputs JSON)
 *
 * Usage (envelope mode):
 *   ats_envelope [--mode envelope] [--frames <start>-[<end>]] [--stride <n>] <input.ats>
 *
 * Usage (compare mode):
 *   ats_envelope --mode compare [--frame-a <n>] [--frame-b <n>] <a.ats> <b.ats>
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

extern "C" {
#include "atsa.h"
}
#include "AtsSpectralEnvelope.h"

// ── minimal JSON helpers ──────────────────────────────────────────────────────

static std::string jd(double v)   { std::ostringstream o; o << v; return o.str(); }
static std::string ji(int v)       { return std::to_string(v); }
static std::string jbool(bool v)   { return v ? "true" : "false"; }

static std::string jsonDoubleArray(const std::vector<double>& v)
{
    std::ostringstream o;
    o << "[";
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) o << ",";
        o << v[i];
    }
    o << "]";
    return o.str();
}

// ── build envelope points from one frame ─────────────────────────────────────

static std::vector<ats::SpectralEnvelopePoint> framePoints(const ATS_SOUND* s, int frm)
{
    std::vector<ats::SpectralEnvelopePoint> pts;
    for (int p = 0; p < s->partials; ++p) {
        double a = s->amp[p][frm];
        double f = s->frq[p][frm];
        if (a > 0.0 && f > 0.0)
            pts.push_back({f, a});
    }
    return pts;
}

// ── envelope mode ─────────────────────────────────────────────────────────────

static int modeEnvelope(const ATS_SOUND* sound, int fra_start, int fra_end, int stride)
{
    std::ostringstream out;
    out << "{\"frames\":[";
    bool first = true;
    for (int f = fra_start; f < fra_end; f += stride) {
        auto pts = framePoints(sound, f);
        ats::BarkSpectralEnvelope env;
        env.build(pts);
        auto peaks = env.isValid() ? env.findPeakBarks() : std::vector<double>{};

        // Convert peak Bark positions to Hz
        if (!first) out << ",";
        first = false;
        out << "{";
        out << "\"frame\":" << ji(f) << ",";
        out << "\"time_s\":" << jd(sound->time[0][f]) << ",";
        out << "\"valid\":" << jbool(env.isValid()) << ",";
        if (env.isValid()) {
            // Sample envelope at 64 Bark-evenly-spaced points
            int n_bins = env.getBinCount();
            double max_bark = env.getMaxBark();
            std::vector<double> bins(n_bins);
            for (int b = 0; b < n_bins; ++b)
                bins[b] = env.sampleAtBark(b * max_bark / (n_bins - 1));
            out << "\"bark_bins\":" << jsonDoubleArray(bins) << ",";
        } else {
            out << "\"bark_bins\":[],";
        }
        out << "\"formant_peaks\":[";
        for (size_t pi = 0; pi < peaks.size(); ++pi) {
            double bark = peaks[pi];
            // Bark -> approx Hz using inverse Zwicker: hz = 1960*(bark+0.53)/(26.28-bark)
            double hz = 1960.0 * (bark + 0.53) / (26.28 - bark);
            double amp_at_peak = env.isValid() ? env.sampleAtBark(bark) : 0.0;
            if (pi) out << ",";
            out << "{\"bark\":" << jd(bark)
                << ",\"hz\":"  << jd(hz)
                << ",\"amp\":" << jd(amp_at_peak) << "}";
        }
        out << "]}";
    }
    out << "]}";
    std::printf("%s\n", out.str().c_str());
    return 0;
}

// ── compare mode ─────────────────────────────────────────────────────────────

static int modeCompare(const ATS_SOUND* sa, int fa, const ATS_SOUND* sb, int fb)
{
    auto pts_a = framePoints(sa, fa);
    auto pts_b = framePoints(sb, fb);

    ats::BarkSpectralEnvelope env_a, env_b;
    env_a.build(pts_a);
    env_b.build(pts_b);

    ats::FormantAlignedEnvelopeMorph morph;
    morph.build(env_a, env_b);

    // Serialize formant warp anchors
    std::ostringstream out;
    out << "{";
    out << "\"frame_a\":" << ji(fa) << ",";
    out << "\"frame_b\":" << ji(fb) << ",";
    out << "\"valid_a\":" << jbool(env_a.isValid()) << ",";
    out << "\"valid_b\":" << jbool(env_b.isValid()) << ",";
    out << "\"morph_valid\":" << jbool(morph.isValid()) << ",";
    out << "\"matched_formants\":" << ji(morph.getMatchedFormantCount()) << ",";

    // Sample both envelopes at 64 points for comparison
    int n_bins = ats::BarkSpectralEnvelope::kDefaultBins;
    double max_bark = ats::BarkSpectralEnvelope::kDefaultMaxBark;
    std::vector<double> ea(n_bins), eb(n_bins);
    for (int b = 0; b < n_bins; ++b) {
        double bk = b * max_bark / (n_bins - 1);
        ea[b] = env_a.isValid() ? env_a.sampleAtBark(bk) : 0.0;
        eb[b] = env_b.isValid() ? env_b.sampleAtBark(bk) : 0.0;
    }
    out << "\"envelope_a\":" << jsonDoubleArray(ea) << ",";
    out << "\"envelope_b\":" << jsonDoubleArray(eb);
    out << "}";
    std::printf("%s\n", out.str().c_str());
    return 0;
}

// ── main ──────────────────────────────────────────────────────────────────────

int main(int argc, char** argv)
{
    std::string mode = "envelope";
    int fra_start = 0, fra_end = -1, stride = 1;
    int frame_a = 0, frame_b = 0;
    const char* path_a = nullptr;
    const char* path_b = nullptr;  // compare mode only

    for (int i = 1; i < argc; ) {
        auto arg = std::string(argv[i]);
        if (arg == "--mode"    && i+1 < argc) { mode = argv[++i]; ++i; continue; }
        if (arg == "--stride"  && i+1 < argc) { stride = std::atoi(argv[++i]); ++i; continue; }
        if (arg == "--frame-a" && i+1 < argc) { frame_a = std::atoi(argv[++i]); ++i; continue; }
        if (arg == "--frame-b" && i+1 < argc) { frame_b = std::atoi(argv[++i]); ++i; continue; }
        if (arg == "--frames"  && i+1 < argc) {
            // format: "start-[end]" e.g. "0-50" or "10-"
            std::string spec = argv[++i]; ++i;
            auto dash = spec.find('-');
            fra_start = std::atoi(spec.substr(0, dash).c_str());
            if (dash + 1 < spec.size())
                fra_end = std::atoi(spec.substr(dash + 1).c_str());
            continue;
        }
        if (arg[0] == '-') {
            std::fprintf(stderr, "Unknown option: %s\n", argv[i]); return 1;
        }
        if (!path_a) { path_a = argv[i++]; continue; }
        if (!path_b) { path_b = argv[i++]; continue; }
        ++i;
    }

    if (!path_a) {
        std::fprintf(stderr, "Usage: ats_envelope [--mode envelope|compare] [options] <input.ats> [<b.ats>]\n");
        return 1;
    }

    ATS_SOUND* sa = static_cast<ATS_SOUND*>(std::calloc(1, sizeof(ATS_SOUND)));
    int st_a = 0;
    if (ats_load(sa, path_a, &st_a) < 0) { std::free(sa); return 1; }

    if (fra_end < 0 || fra_end > sa->frames) fra_end = sa->frames;
    if (stride < 1) stride = 1;

    if (mode == "compare") {
        if (!path_b) {
            std::fprintf(stderr, "compare mode requires two ATS files\n");
            free_sound(sa); std::free(sa); return 1;
        }
        ATS_SOUND* sb = static_cast<ATS_SOUND*>(std::calloc(1, sizeof(ATS_SOUND)));
        int st_b = 0;
        if (ats_load(sb, path_b, &st_b) < 0) {
            free_sound(sa); std::free(sa); std::free(sb); return 1;
        }
        // clamp frames
        if (frame_a >= sa->frames) frame_a = sa->frames - 1;
        if (frame_b >= sb->frames) frame_b = sb->frames - 1;
        int rc = modeCompare(sa, frame_a, sb, frame_b);
        free_sound(sa); std::free(sa);
        free_sound(sb); std::free(sb);
        return rc;
    }

    // envelope mode
    int rc = modeEnvelope(sa, fra_start, fra_end, stride);
    free_sound(sa);
    std::free(sa);
    return rc;
}
