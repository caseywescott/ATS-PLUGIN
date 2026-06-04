/* Sinusoidal estimator accuracy harness (DSP Fidelity PRD section 8, Phase 0).
 *
 * Drives the REAL qatsh analysis front-end (make_window -> fft -> peak_detection)
 * on synthetic signals with exactly known parameters and records the current
 * estimator's frequency / amplitude / phase error. This is the regression
 * baseline that Phase 1 (QIFFT bias correction) and Phase 2 (reassignment /
 * true phase) must measurably improve. The window-into-FFT layout below mirrors
 * tracker.c so peak_detection sees realistic input.
 */
#include "atsa.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int next_pow2(int n)
{
    int p = 1;
    while (p < n)
        p <<= 1;
    return p;
}

/* Result of estimating the single dominant partial of a windowed sinusoid. */
typedef struct
{
    int found;
    double frq;     /* Hz, current parabolic-in-dB estimator (peak_detection) */
    double amp;     /* linear, window-norm scaled */
    double pha;     /* radians, current linear phase_interp */
    double pha_bin; /* radians, candidate: complex phase at the peak bin */
    double frq_re;  /* Hz, derivative-window spectral reassignment (Phase 2 prototype) */
} EstResult;

/* Build a windowed cosine A*cos(2*pi*f*n/sr + phi), n=0..win_size-1, lay it into
 * the FFT buffer exactly as tracker.c does (window centered at M_2), run the
 * forward FFT, detect peaks, and return the peak nearest f_true. */
static EstResult estimate_tone(double f_true, double amp_true, double phi,
                               int win_size, int sample_rate, double pad_factor)
{
    EstResult out = { 0, 0.0, 0.0, 0.0, 0.0, 0.0 };

    const int fft_size = next_pow2((int) (pad_factor * (double) win_size));
    const int M_2 = (int) floor(((double) win_size - 1) / 2);
    const int first_point = fft_size - M_2;

    double* window = make_window(BLACKMAN_H, win_size);
    const double norm = window_norm(window, win_size);

    /* Central-difference derivative of the analysis window (dimensionless,
     * per-sample). Reassignment folds the 1/T into the sr/(2*pi) scale below. */
    double* dwindow = (double*) malloc((size_t) win_size * sizeof(double));
    for (int k = 0; k < win_size; ++k)
    {
        const double hp = window[k + 1 < win_size ? k + 1 : k];
        const double hm = window[k - 1 >= 0 ? k - 1 : k];
        const double span = (k + 1 < win_size && k - 1 >= 0) ? 0.5 : 1.0;
        dwindow[k] = (hp - hm) * span;
    }

    ATS_FFT fft_struct;
    fft_struct.size = fft_size;
    fft_struct.rate = sample_rate;
    fft_struct.fdr = (double*) calloc((size_t) fft_size, sizeof(double));
    fft_struct.fdi = (double*) calloc((size_t) fft_size, sizeof(double));

    /* Second FFT with the derivative window, same circular placement so the
     * window-centering phase ramp cancels in the X_dh/X_h ratio. */
    double* gdr = (double*) calloc((size_t) fft_size, sizeof(double));
    double* gdi = (double*) calloc((size_t) fft_size, sizeof(double));

    for (int k = 0; k < win_size; ++k)
    {
        const double s = amp_true * cos(2.0 * M_PI * f_true * (double) k / (double) sample_rate + phi);
        const int idx = (k + first_point) % fft_size;
        fft_struct.fdr[idx] = window[k] * s;
        gdr[idx] = dwindow[k] * s;
    }

    fft(fft_struct.fdr, fft_struct.fdi, fft_size, 1);
    fft(gdr, gdi, fft_size, 1);

    const double fft_mag = (double) sample_rate / (double) fft_size;
    const int lowest_bin = 2;
    const int highest_bin = fft_size / 2;
    int peaks_size = 0;
    ATS_PEAK* peaks = peak_detection(&fft_struct, NULL, lowest_bin, highest_bin, -120.0, norm, &peaks_size);

    double best_df = 1.0e18;
    for (int i = 0; i < peaks_size; ++i)
    {
        const double df = fabs(peaks[i].frq - f_true);
        if (df < best_df)
        {
            best_df = df;
            out.found = 1;
            out.frq = peaks[i].frq;
            out.amp = peaks[i].amp;
            out.pha = peaks[i].pha;
        }
    }

    if (out.found)
    {
        /* Reassign frequency at the integer bin nearest the detected peak:
         *   f_hat = f_bin - (sr/2pi) * Im( X_Dh / X_h )      (Auger-Flandrin)
         * qatsh's fft(...,1) uses the +j convention (see to_polar's
         * atan2(-y,x)), i.e. it returns conj of the -j-convention spectrum, so
         * Im( X_Dh/X_h ) is negated relative to the textbook derivation and the
         * correction sign flips to +. */
        const int b = (int) (out.frq / fft_mag + 0.5);
        const double fr = fft_struct.fdr[b], fi = fft_struct.fdi[b];
        const double gr = gdr[b], gi = gdi[b];
        const double denom = fr * fr + fi * fi;
        if (denom > 0.0)
        {
            const double im_ratio = (gi * fr - gr * fi) / denom;
            out.frq_re = (double) b * fft_mag + (sample_rate / (2.0 * M_PI)) * im_ratio;
        }
        else
        {
            out.frq_re = out.frq;
        }
        /* Candidate phase estimator: complex phase at the peak bin, same
         * convention as to_polar (atan2(-y, x)). */
        out.pha_bin = atan2(-fi, fr);
    }

    free(peaks);
    free(fft_struct.fdr);
    free(fft_struct.fdi);
    free(gdr);
    free(gdi);
    free(dwindow);
    free(window);
    return out;
}

static double cents(double f_meas, double f_true)
{
    return 1200.0 * log2(f_meas / f_true);
}

static double db(double ratio)
{
    return 20.0 * log10(ratio);
}

int main(void)
{
    const int sr = 44100;
    const int win_size = 2048;

    const double base_freqs[] = { 220.0, 1000.0, 4000.0 };
    const double amps[] = { 1.0, 0.25, 0.03 };

    /* Sweep the peak across a full bin (parabolic-in-dB bias peaks near the
     * half-bin offset) at two zero-padding regimes:
     *   pad 2.0 = what the live analyzer uses (ppp2(2*win_size)); generous
     *             padding largely hides parabolic bias on isolated tones.
     *   pad 1.0 = no padding; exposes the raw estimator bias that QIFFT
     *             (Phase 1) is meant to remove. Diagnostic only. */
    const double pads[] = { 2.0, 1.0 };
    double worst_cents[2] = { 0.0, 0.0 };
    double worst_amp_db[2] = { 0.0, 0.0 };
    double worst_cents_re[2] = { 0.0, 0.0 };
    double worst_pha_cur[2] = { 0.0, 0.0 };
    double worst_pha_bin[2] = { 0.0, 0.0 };
    const int M_2 = (int) floor(((double) win_size - 1) / 2);

    for (size_t pi = 0; pi < 2; ++pi)
    {
        const double fft_mag = (double) sr / (double) next_pow2((int) (pads[pi] * win_size));
        for (size_t bi = 0; bi < sizeof(base_freqs) / sizeof(base_freqs[0]); ++bi)
        {
            for (size_t ai = 0; ai < sizeof(amps) / sizeof(amps[0]); ++ai)
            {
                for (int step = 0; step <= 10; ++step)
                {
                    const double frac = 0.05 * (double) step; /* 0 .. 0.5 bin */
                    const double f_true = base_freqs[bi] + frac * fft_mag;
                    const EstResult r = estimate_tone(f_true, amps[ai], 0.37, win_size, sr, pads[pi]);
                    if (!r.found || !(r.amp > 0.0))
                    {
                        fprintf(stderr, "FAIL: no peak found for f=%.3f amp=%.3f pad=%.1f\n",
                                f_true, amps[ai], pads[pi]);
                        return 1;
                    }
                    const double ce = fabs(cents(r.frq, f_true));
                    const double ae = fabs(db(r.amp / amps[ai]));
                    const double ce_re = fabs(cents(r.frq_re, f_true));
                    if (ce > worst_cents[pi])
                        worst_cents[pi] = ce;
                    if (ae > worst_amp_db[pi])
                        worst_amp_db[pi] = ae;
                    if (ce_re > worst_cents_re[pi])
                        worst_cents_re[pi] = ce_re;

                    /* Ground-truth phase at the window center (FFT time origin,
                     * sample M_2): wrap(2*pi*f*M_2/sr + phi). */
                    const double theta = 2.0 * M_PI * f_true * (double) M_2 / (double) sr + 0.37;
                    const double pe_cur = fabs(remainder(r.pha - theta, 2.0 * M_PI));
                    const double pe_bin = fabs(remainder(r.pha_bin - theta, 2.0 * M_PI));
                    if (pe_cur > worst_pha_cur[pi])
                        worst_pha_cur[pi] = pe_cur;
                    if (pe_bin > worst_pha_bin[pi])
                        worst_pha_bin[pi] = pe_bin;
                }
            }
        }
        printf("single-tone sweep (pad %.1fx): parabolic |freq err| = %.4f cents, "
               "reassigned |freq err| = %.4f cents, |amp err| = %.4f dB\n",
               pads[pi], worst_cents[pi], worst_cents_re[pi], worst_amp_db[pi]);
        printf("  phase (pad %.1fx): linear phase_interp |err| = %.4f rad, "
               "peak-bin phase |err| = %.4f rad\n",
               pads[pi], worst_pha_cur[pi], worst_pha_bin[pi]);
    }

    const double fft_mag = (double) sr / (double) next_pow2(2 * win_size);

    /* Regression guard on the live analyzer regime (pad 2.0). Phase 1 must keep
     * these green while also shrinking the pad-1.0 diagnostic numbers above. */
    if (worst_cents[0] > 0.5)
        return fprintf(stderr, "FAIL: freq error %.4f cents exceeds baseline 0.5\n", worst_cents[0]), 1;
    if (worst_amp_db[0] > 0.05)
        return fprintf(stderr, "FAIL: amp error %.4f dB exceeds baseline 0.05\n", worst_amp_db[0]), 1;

    /* Phase 2 numerical gate: derivative-window reassignment must beat the
     * parabolic estimator on the unpadded regime, where parabolic bias is
     * largest. Reassignment is exact for stationary tones, so this is a wide
     * margin -- if it is not met, the qatsh integration is not worth doing. */
    if (!(worst_cents_re[1] < 0.25 * worst_cents[1]))
        return fprintf(stderr,
                       "FAIL: reassignment (%.4f cents) did not beat parabolic (%.4f cents) by 4x\n",
                       worst_cents_re[1], worst_cents[1]),
               1;

    /* --- Two closely-spaced tones (resolution reference, documented) -------- */
    {
        const double f1 = 1000.0;
        const double f2 = f1 + 1.5 * fft_mag; /* ~1.5 bins apart */
        /* Sum both into one buffer by superposition via two estimate passes is
         * not valid; instead measure how the estimator localizes the louder of
         * a closely spaced pair built directly. */
        const int fft_size = next_pow2(2 * win_size);
        const int M_2 = (int) floor(((double) win_size - 1) / 2);
        const int first_point = fft_size - M_2;
        double* window = make_window(BLACKMAN_H, win_size);
        const double norm = window_norm(window, win_size);
        ATS_FFT ff;
        ff.size = fft_size;
        ff.rate = sr;
        ff.fdr = (double*) calloc((size_t) fft_size, sizeof(double));
        ff.fdi = (double*) calloc((size_t) fft_size, sizeof(double));
        for (int k = 0; k < win_size; ++k)
        {
            const double s = 1.0 * cos(2.0 * M_PI * f1 * (double) k / sr)
                             + 0.7 * cos(2.0 * M_PI * f2 * (double) k / sr + 0.9);
            ff.fdr[(k + first_point) % fft_size] = window[k] * s;
        }
        fft(ff.fdr, ff.fdi, fft_size, 1);
        int ps = 0;
        ATS_PEAK* pk = peak_detection(&ff, NULL, 2, fft_size / 2, -120.0, norm, &ps);
        int near = 0;
        for (int i = 0; i < ps; ++i)
            if (pk[i].frq > f1 - 2.0 * fft_mag && pk[i].frq < f2 + 2.0 * fft_mag)
                ++near;
        printf("two-tone (%.0f Hz apart, ~1.5 bins): peaks resolved in band = %d\n", f2 - f1, near);
        free(pk);
        free(ff.fdr);
        free(ff.fdi);
        free(window);
    }

    printf("ok: estimator accuracy baseline recorded\n");
    return 0;
}
