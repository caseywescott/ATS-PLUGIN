#pragma once

#include <cstring>

#include "AtsAutomationCurves.h"

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
    /** Serra 1997: analyse the sound backwards so the tracker has locked onto
        the partials before it reaches the attack; the model is flipped back
        afterwards (AtsResidualFixes::reverseFrames). Plugin-side; not an ANARGS field. */
    bool reverseTracking = false;
    /** Serra 1997: after analysis, cap each residual band's energy at the
        source's own band energy for that frame (AtsResidualFixes::clampResidualToSource). */
    bool clampResidual = false;

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
    /** Render-time automation (time warp, pitch, spectral warp). */
    SoundCurves curves;

    // Research-derived render options (docs/STATUS.md, 2026-11-24). All
    // defaults reproduce the legacy render bit for bit.
    /** Sinusoidal carriers per Bark band for the residual (Hanna DAFx-00/01).
        1 = ATSA's own single-carrier band noise; 2..32 use the plugin renderer. */
    int residualDensity = 1;
    /** Shimmer (rms dB) and jitter (rms cents) of low-rate Gaussian micro-modulation
        (Jensen's Timbre Model; Torvmark). 0 = off. */
    double animationShimmerDb = 0.0;
    double animationJitterCents = 0.0;
    double animationRateHz = 4.0;
    /** Vibrato depth (0 removes, 1 keeps, 2 doubles) and rate scale (Herrera & Bonada DAFx-98). */
    double vibratoDepth = 1.0;
    double vibratoRateScale = 1.0;
    /** Character-preserving transposition (Serra & Bonada DAFx-98; O'Leary & Griffith
        DAFx-24): formant-preserving amplitude re-read plus a tilt change per octave. */
    bool transposeCharacter = false;
    double transposeTiltDbPerOct = -3.0;

    void toSparams(SPARAMS& out, double soundDurationSec) const;
    static SynthParams fromSparams(const SPARAMS& s, double soundDurationSec, bool useAll, bool usePhase);
};

} // namespace ats
