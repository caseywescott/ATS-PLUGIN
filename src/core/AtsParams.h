#pragma once

#include <cstring>

extern "C" {
#include "atsa.h"
}

namespace ats
{

enum class FrequencyShapeMode
{
    Linear = 0,
    Stretch,
    Compress,
    Harmonic
};

struct FrequencyShapeSettings
{
    FrequencyShapeMode mode = FrequencyShapeMode::Linear;
    double amount = 1.0;
    double pivotHz = 0.0;
    double resolvedPivotHz = 0.0;
};

/** Mirrors Qatsh / ATSA ANARGS defaults (see tools/ats_offline.c). */
struct AnalysisParams
{
    double start = ATSA_START;
    double duration = ATSA_DUR;
    double lowest_freq = ATSA_LFREQ;
    double highest_freq = ATSA_HFREQ;
    double freq_dev = ATSA_FREQDEV;
    int win_cycles = ATSA_WCYCLES;
    int win_type = ATSA_WTYPE;
    int win_size = ATSA_WSIZE;
    double hop_size = ATSA_HSIZE;
    double lowest_mag = ATSA_LMAG;
    int track_len = ATSA_TRKLEN;
    int min_seg_len = ATSA_MSEGLEN;
    int min_gap_len = ATSA_MGAPLEN;
    double SMR_thres = ATSA_SMRTHRES;
    double min_seg_SMR = ATSA_MSEGSMR;
    double last_peak_cont = ATSA_LPKCONT;
    double SMR_cont = ATSA_SMRCONT;
    int type = ATSA_TYPE;
    /** Opt-in multi-resolution low-band analysis (ANARGS.multi_res). Default off is bit-identical. */
    bool multiRes = false;
    double mrWinMult = ATSA_MRWINMULT;
    double mrCrossover = ATSA_MRCROSS;

    void toAnargs(ANARGS& out) const;
    static AnalysisParams fromAnargs(const ANARGS& a);
};

/** Mirrors Qatsh ATSSynthesis / SPARAMS (defaults match ATSSynthesis constructor). */
struct SynthParams
{
    double startTime = 0.0;
    double duration = 0.0;
    bool useAllPartials = true;
    bool usePartialsPhase = false;
    double partialsGain = 1.0;
    double residualGain = 1.0;
    double frequencyFactor = 1.0;
    double timeFactor = 1.0;
    double sampleRate = 44100.0;
    /** Opt-in overlap-add IFFT synthesis (SPARAMS.use_ifft). Default off preserves oscillator path. */
    bool useIfftSynth = false;
    FrequencyShapeSettings frequencyShape;

    void toSparams(SPARAMS& out, double soundDurationSec) const;
    static SynthParams fromSparams(const SPARAMS& s, double soundDurationSec, bool useAll, bool usePhase);
};

} // namespace ats
