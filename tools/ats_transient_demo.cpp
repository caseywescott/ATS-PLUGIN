/*
 * A/B demo for the explicit transient layer (DSP fidelity Commit 8).
 * Loads a mono WAV, analyzes it, and writes two resyntheses:
 *   <out>_base.wav      -- oscillator-bank + residual (baseline)
 *   <out>_transient.wav -- baseline + re-injected transient layer
 * so attacks can be auditioned A/B.
 */

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <sndfile.h>

extern "C" {
#include "atsa.h"
}

#include "AtsTransientLayer.h"
#include "AtsTransientSmear.h"

namespace
{
constexpr int kBufferLen = 1024;

bool loadMonoWav(const char* path, std::vector<double>& out, int& sr)
{
    SF_INFO sfinfo;
    std::memset(&sfinfo, 0, sizeof(sfinfo));
    SNDFILE* infile = sf_open(path, SFM_READ, &sfinfo);
    if (!infile)
    {
        std::fprintf(stderr, "Could not open '%s': %s\n", path, sf_strerror(nullptr));
        return false;
    }
    if (sfinfo.channels != 1)
    {
        std::fprintf(stderr, "Expected mono WAV, got %d channels.\n", sfinfo.channels);
        sf_close(infile);
        return false;
    }
    out.assign((size_t) sfinfo.frames, 0.0);
    double buf[kBufferLen];
    sf_count_t total = 0;
    while (true)
    {
        sf_count_t n = sf_readf_double(infile, buf, kBufferLen);
        if (n <= 0)
            break;
        for (sf_count_t i = 0; i < n; ++i)
            out[(size_t) (total + i)] = buf[i];
        total += n;
    }
    sf_close(infile);
    sr = sfinfo.samplerate;
    return true;
}

bool writeMonoWav(const std::string& path, const std::vector<double>& samples, int sr)
{
    SF_INFO sfinfo;
    std::memset(&sfinfo, 0, sizeof(sfinfo));
    sfinfo.samplerate = sr;
    sfinfo.channels = 1;
    sfinfo.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;
    SNDFILE* outfile = sf_open(path.c_str(), SFM_WRITE, &sfinfo);
    if (!outfile)
    {
        std::fprintf(stderr, "Could not open '%s' for write: %s\n", path.c_str(), sf_strerror(nullptr));
        return false;
    }
    sf_writef_double(outfile, samples.data(), (sf_count_t) samples.size());
    sf_close(outfile);
    return true;
}

void defaultAnargs(ANARGS* a)
{
    std::memset(a, 0, sizeof(*a));
    a->start = ATSA_START;
    a->duration = ATSA_DUR;
    a->lowest_freq = ATSA_LFREQ;
    a->highest_freq = ATSA_HFREQ;
    a->freq_dev = ATSA_FREQDEV;
    a->win_cycles = ATSA_WCYCLES;
    a->win_type = ATSA_WTYPE;
    a->win_size = ATSA_WSIZE;
    a->hop_size = ATSA_HSIZE;
    a->lowest_mag = ATSA_LMAG;
    a->track_len = ATSA_TRKLEN;
    a->min_seg_len = ATSA_MSEGLEN;
    a->min_gap_len = ATSA_MGAPLEN;
    a->SMR_thres = ATSA_SMRTHRES;
    a->min_seg_SMR = ATSA_MSEGSMR;
    a->last_peak_cont = ATSA_LPKCONT;
    a->SMR_cont = ATSA_SMRCONT;
    a->type = ATSA_TYPE;
}

void defaultSparams(SPARAMS* s, int sr, double soundDur)
{
    std::memset(s, 0, sizeof(*s));
    s->amp = 1.0;
    s->ramp = 1.0;
    s->frec = 1.0;
    s->max_stretch = 1.0;
    s->beg = 0.0;
    s->end = soundDur;
    s->sr = (double) sr;
    s->allorsel = 1;
    s->upha = 0;
}

void freeResidual(double*** res)
{
    if (!res || !*res)
        return;
    if ((*res)[0]) free((*res)[0]);
    if ((*res)[1]) free((*res)[1]);
    free(*res);
    *res = nullptr;
}
} // namespace

int main(int argc, char** argv)
{
    double gain = 1.0;
    const char* inPath = nullptr;
    const char* outStem = nullptr;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--gain") == 0 && i + 1 < argc)
        {
            gain = std::atof(argv[++i]);
            continue;
        }
        if (!inPath)
            inPath = argv[i];
        else if (!outStem)
            outStem = argv[i];
    }
    if (!inPath || !outStem)
    {
        std::fprintf(stderr, "Usage: %s [--gain g] <input.wav> <output-stem>\n", argv[0]);
        return 1;
    }

    std::vector<double> samples;
    int sr = 0;
    if (!loadMonoWav(inPath, samples, sr))
        return 1;

    ANARGS an;
    defaultAnargs(&an);
    double** residual = nullptr;
    int resLen = 0;
    ATS_SOUND* sound = tracker(&an, samples.data(), sr, (int) samples.size(), &residual, &resLen);
    freeResidual(&residual);
    if (!sound)
    {
        std::fprintf(stderr, "Analysis failed.\n");
        return 1;
    }

    SPARAMS sp;
    defaultSparams(&sp, sr, sound->dur);
    CURVE timenv;
    curve_init(&timenv);
    double* synth = nullptr;
    int nOut = 0;
    do_synthesis(sound, &sp, &timenv, nullptr, &synth, &nOut);
    curve_free(&timenv);
    if (!synth || nOut <= 0)
    {
        std::fprintf(stderr, "Synthesis produced no samples.\n");
        free_sound(sound);
        free(sound);
        return 1;
    }

    std::vector<double> baseline(synth, synth + nOut);
    free(synth);

    const ats::SmearnovDetectParams dparams;
    const auto onsets = ats::detectOnsets(sound, dparams);
    const auto events = ats::extractTransientLayer(sound, onsets);

    ats::TransientLayerParams tparams;
    tparams.gain = gain;
    std::vector<double> layer((size_t) nOut, 0.0);
    (void) ats::renderTransientLayer(events, tparams, sr, layer.data(), nOut);

    std::vector<double> withTransient(baseline);
    double peak = 0.0;
    for (int n = 0; n < nOut; ++n)
    {
        withTransient[(size_t) n] += layer[(size_t) n];
        peak = std::max(peak, std::abs(withTransient[(size_t) n]));
    }
    // Match the baseline's peak so the A/B is a fair loudness comparison.
    double basePeak = 0.0;
    for (double v : baseline)
        basePeak = std::max(basePeak, std::abs(v));
    if (peak > basePeak && peak > 0.0)
    {
        const double g = basePeak / peak;
        for (auto& v : withTransient)
            v *= g;
    }

    free_sound(sound);
    free(sound);

    const std::string stem = outStem;
    const bool okA = writeMonoWav(stem + "_base.wav", baseline, sr);
    const bool okB = writeMonoWav(stem + "_transient.wav", withTransient, sr);
    std::fprintf(stderr, "onsets=%zu events=%zu wrote %s_base.wav, %s_transient.wav\n",
                 onsets.size(), events.size(), stem.c_str(), stem.c_str());
    return (okA && okB) ? 0 : 1;
}
