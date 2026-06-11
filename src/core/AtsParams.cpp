#include "AtsParams.h"

namespace ats
{

void AnalysisParams::toAnargs(ANARGS& out) const
{
    std::memset(&out, 0, sizeof(out));
    out.start = start;
    out.duration = duration;
    out.lowest_freq = lowest_freq;
    out.highest_freq = highest_freq;
    out.freq_dev = freq_dev;
    out.win_cycles = win_cycles;
    out.win_type = win_type;
    out.win_size = win_size;
    out.hop_size = hop_size;
    out.lowest_mag = lowest_mag;
    out.track_len = track_len;
    out.min_seg_len = min_seg_len;
    out.min_gap_len = min_gap_len;
    out.SMR_thres = SMR_thres;
    out.min_seg_SMR = min_seg_SMR;
    out.last_peak_cont = last_peak_cont;
    out.SMR_cont = SMR_cont;
    out.type = type;
}

AnalysisParams AnalysisParams::fromAnargs(const ANARGS& a)
{
    AnalysisParams p;
    p.start = a.start;
    p.duration = a.duration;
    p.lowest_freq = a.lowest_freq;
    p.highest_freq = a.highest_freq;
    p.freq_dev = a.freq_dev;
    p.win_cycles = a.win_cycles;
    p.win_type = a.win_type;
    p.win_size = a.win_size;
    p.hop_size = a.hop_size;
    p.lowest_mag = a.lowest_mag;
    p.track_len = a.track_len;
    p.min_seg_len = a.min_seg_len;
    p.min_gap_len = a.min_gap_len;
    p.SMR_thres = a.SMR_thres;
    p.min_seg_SMR = a.min_seg_SMR;
    p.last_peak_cont = a.last_peak_cont;
    p.SMR_cont = a.SMR_cont;
    p.type = a.type;
    return p;
}

void SynthParams::toSparams(SPARAMS& out, double soundDurationSec) const
{
    std::memset(&out, 0, sizeof(out));
    out.amp = partialsGain;
    out.ramp = residualGain;
    out.frec = frequencyFactor;
    out.max_stretch = timeFactor;
    out.beg = startTime;
    const double dur = (duration <= 0.0 ? soundDurationSec : duration);
    out.end = out.beg + dur;
    out.sr = sampleRate;
    out.allorsel = useAllPartials ? static_cast<short>(1) : static_cast<short>(0);
    out.upha = usePartialsPhase ? static_cast<short>(1) : static_cast<short>(0);
    out.use_ifft = useIfftSynth ? static_cast<short>(1) : static_cast<short>(0);
}

SynthParams SynthParams::fromSparams(const SPARAMS& s, double soundDurationSec, bool useAll, bool usePhase)
{
    (void)soundDurationSec;
    SynthParams p;
    p.partialsGain = s.amp;
    p.residualGain = s.ramp;
    p.frequencyFactor = s.frec;
    p.timeFactor = s.max_stretch;
    p.startTime = s.beg;
    p.duration = s.end - s.beg;
    p.sampleRate = s.sr;
    p.useAllPartials = useAll;
    p.usePartialsPhase = usePhase;
    p.useIfftSynth = s.use_ifft != 0;
    return p;
}

} // namespace ats
