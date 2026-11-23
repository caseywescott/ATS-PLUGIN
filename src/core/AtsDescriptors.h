#pragma once

#include "AtsModel.h"

#include <array>
#include <vector>

namespace ats
{

/**
    Timbre descriptors computed directly from an ATS model's partials and
    residual bands (no resynthesis).  Per frame, over the active partials
    (amp > 0, frq > 0) with amplitude a_i and frequency f_i:

    - centroidHz   = sum f_i a_i / sum a_i            (Peeters & Rodet, ICMC 2002)
      centroidBark = the same with f replaced by Bark(f) (Zwicker's
      approximation, see hzToBark).
    - spreadHz     = amplitude-weighted standard deviation of f; skewness and
      kurtosis are the 3rd / 4th standardised central moments (Peeters).
    - tiltDbPerOctave = slope of the weighted least-squares regression of
      20 log10(a_i) on log2(f_i) with weights a_i / sum a (Peeters' spectral
      slope, expressed in dB per octave).
    - f0Hz: Serra & Bonada (DAFx-98, "SMS high level attributes")
      f0 = sum (f_i / h_i) a_i / sum a_i, where h_i = round(f_i / f0_est) >= 1 and
      f0_est is a coarse estimate (describeSound passes the median of the
      lowest active partial over frames; describeFrame falls back to the
      frame's lowest active partial when given 0).
      harmonicDistortionHz = sum |f_i - h_i f0| a_i / sum a_i;
      inharmonicity        = sum (|f_i - h_i f0| / (h_i f0)) a_i / sum a_i.
    - harmonicRankCentroid = sum h_i a_i / sum a_i: pitch-invariant brightness
      (Menon et al. 2002).
    - tristimulus: energy (a^2) shares of h = 1, h in {2, 3, 4}, h >= 5
      (Pollard & Jansson, as used in Jensen's "Timbre Model").
      oddEvenRatio = sum a^2 over odd h / sum a^2 over even h (h >= 2); 0
      when the denominator is 0.
    - noisiness = E_res / (E_sin + E_res) with E_sin = sum a_i^2 and
      E_res = sum_b band_energy[b][f] / (window_size * ATSA_NOISE_VARIANCE):
      the ATS residual stores per-band energies that the synthesiser turns
      into a band RMS amplitude via sqrt(E / (window_size * 0.04)) (ENG_RMS
      in synthesis.c), so this division puts the residual on the same
      RMS^2 scale as the partial amplitudes squared.  0 when the model has
      no residual.
    - subbandCentroidHz[b]: amplitude-weighted centroid of the partials inside
      Bark band b (ATSA_CRITICAL_BAND_EDGES), 0 when the band is empty
      (Paliwal's subband spectral centroids, ICASSP-98).

    Whole-sound: attackCentroidHz is the time-weighted mean centroid over the
    first 300 ms starting at the first frame whose total energy exceeds 10 %
    of the peak frame energy.
*/
struct FrameDescriptors
{
    double centroidHz = 0.0;
    double centroidBark = 0.0;
    double spreadHz = 0.0;
    double skewness = 0.0;
    double kurtosis = 0.0;
    double tiltDbPerOctave = 0.0;
    double f0Hz = 0.0;
    double harmonicDistortionHz = 0.0;
    double inharmonicity = 0.0;
    double harmonicRankCentroid = 0.0;
    double oddEvenRatio = 0.0;
    double noisiness = 0.0;
    std::array<double, 3> tristimulus {};
    std::array<double, 25> subbandCentroidHz {};
    double sinusoidalEnergy = 0.0;
    double residualEnergy = 0.0;
    bool active = false;
};

struct DescriptorSummary
{
    double mean = 0.0;
    double stddev = 0.0;
    double min = 0.0;
    double max = 0.0;
};

struct DescriptorTrack
{
    std::vector<FrameDescriptors> frames;
    double fundamentalHz = 0.0;
    double attackCentroidHz = 0.0;
    DescriptorSummary centroidHz, centroidBark, spreadHz, tiltDbPerOctave,
        harmonicRankCentroid, noisiness, inharmonicity;
};

/** Zwicker's Bark approximation: 13 atan(0.76 f / 1000) + 3.5 atan((f / 7500)^2). */
[[nodiscard]] double hzToBark(double hz);

/** Descriptors of one frame; `fundamentalHz` <= 0 uses the frame's lowest
    active partial as the coarse f0 estimate.  Inactive (all zero) when the
    frame has no active partial or the arguments are out of range. */
[[nodiscard]] FrameDescriptors describeFrame(const ATS_SOUND* sound, int frame, double fundamentalHz);

/** Every frame plus summaries over the active frames.  Empty track for a
    null or empty model. */
[[nodiscard]] DescriptorTrack describeSound(const ATS_SOUND* sound);

} // namespace ats
