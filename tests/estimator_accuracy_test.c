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
    double amp;     /* linear, window-norm scaled (current parabolic height) */
    double amp_wg;  /* linear, window-gain corrected amplitude (Phase 1 QIFFT prototype) */
    double pha;     /* radians, current linear phase_interp */
    double pha_bin; /* radians, candidate: complex phase at the peak bin */
    double frq_re;  /* Hz, derivative-window spectral reassignment (Phase 2 prototype) */
} EstResult;

/* Analytic main-lobe gain of the analysis window at a fractional-bin offset
 * delta (in FFT bins), normalized so G(0) = 1. For a stationary sinusoid the
 * peak-bin magnitude is the on-bin magnitude scaled by exactly this factor, so
 * dividing the raw bin magnitude by G(delta) recovers the true amplitude with
 * no parabolic approximation. |W(delta)| = |sum_n w[n] exp(-j 2pi delta n /
 * Nfft)|; the circular window centering only adds linear phase and drops out of
 * the magnitude. */
static double window_gain(const double* window, int win_size, double delta, int fft_size)
{
    double re = 0.0, im = 0.0, dc = 0.0;
    const double w = 2.0 * M_PI * delta / (double) fft_size;
    for (int n = 0; n < win_size; ++n)
    {
        re += window[n] * cos(w * (double) n);
        im -= window[n] * sin(w * (double) n);
        dc += window[n];
    }
    return sqrt(re * re + im * im) / dc;
}

/* Build a windowed cosine A*cos(2*pi*f*n/sr + phi), n=0..win_size-1, lay it into
 * the FFT buffer exactly as tracker.c does (window centered at M_2), run the
 * forward FFT, detect peaks, and return the peak nearest f_true. */
static EstResult estimate_tone(double f_true, double amp_true, double phi,
                               int win_size, int sample_rate, double pad_factor)
{
    EstResult out = { 0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 };

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

        /* Phase 1 prototype: window-gain amplitude correction. Use the
         * reassigned frequency for the sub-bin offset, then divide the raw
         * peak-bin magnitude by the analytic window main-lobe gain. Exact for a
         * stationary sinusoid regardless of zero-pad, unlike the dB-parabolic
         * height which carries a window-dependent bias. */
        const double delta = out.frq_re / fft_mag - (double) b;
        const double g = window_gain(window, win_size, delta, fft_size);
        out.amp_wg = (g > 0.0) ? norm * sqrt(fr * fr + fi * fi) / g : out.amp;
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

/* Order-2 Distribution Derivative Method (Betser 2009) on a windowed segment.
 * Models the underlying signal over sample-offset tau (centered at the window
 * midpoint) as s(tau) = exp(b0 + b1*tau + b2*tau^2), b_k complex, then recovers
 * the within-frame slopes the stationary sinusoid model cannot represent:
 *   - chirp (FM) rate  c  [Hz/s]      = Im(b2) * sr^2 / pi
 *   - amplitude log-slope mu [nepers/s] = Re(b1) * sr
 * The estimator uses the integration-by-parts identity <s', phi> = -<s, phi'>
 * with the demodulated atoms phi = h e^{-j w0 tau} and tau*phi (h = analysis
 * window, w0 = peak angular freq), giving a 2x2 complex system in (b1, b2):
 *   [P0  2P1][b1]   [ -D  + j w0 P0      ]
 *   [P1  2P2][b2] = [ -P0 - DT + j w0 P1 ]
 * where P_m = <s, tau^m h e^{-j w0 tau}>, D = <s, h' e^{-j w0 tau}>,
 * DT = <s, tau h' e^{-j w0 tau}>. Inner products are formed directly in time
 * (textbook -j convention; independent of qatsh's +j fft). */
typedef struct
{
    int found;
    double f_center;    /* Hz, instantaneous freq at the window center (Im b1) */
    double chirp_hz_s;  /* Hz/s, frequency slope (2*Im b2 in rad/sample^2)      */
    double amp_slope_s; /* nepers/s, amplitude log-slope (Re b1)                */
} DdmResult;

static DdmResult estimate_chirp_ddm(const double* s, int win_size, int sample_rate,
                                    double f0_peak)
{
    DdmResult out = { 0, 0.0, 0.0, 0.0 };

    double* window = make_window(BLACKMAN_H, win_size);
    double* dwindow = (double*) malloc((size_t) win_size * sizeof(double));
    for (int k = 0; k < win_size; ++k)
    {
        const double hp = window[k + 1 < win_size ? k + 1 : k];
        const double hm = window[k - 1 >= 0 ? k - 1 : k];
        const double span = (k + 1 < win_size && k - 1 >= 0) ? 0.5 : 1.0;
        dwindow[k] = (hp - hm) * span;
    }

    const double n_c = floor(((double) win_size - 1) / 2.0);
    const double w0 = 2.0 * M_PI * f0_peak / (double) sample_rate;

    /* Accumulate the five complex inner products (re/im pairs). */
    double p0r = 0, p0i = 0, p1r = 0, p1i = 0, p2r = 0, p2i = 0;
    double dr = 0, di = 0, dtr = 0, dti = 0;
    for (int n = 0; n < win_size; ++n)
    {
        const double tau = (double) n - n_c;
        const double ph = -w0 * tau;
        const double c = cos(ph), sn = sin(ph); /* e^{-j w0 tau} = c + j sn */
        const double sh = s[n] * window[n];     /* s * h */
        const double sdh = s[n] * dwindow[n];   /* s * h' */
        /* P0 += sh * e^{-j w0 tau} */
        p0r += sh * c;       p0i += sh * sn;
        p1r += sh * tau * c; p1i += sh * tau * sn;
        p2r += sh * tau * tau * c; p2i += sh * tau * tau * sn;
        dr  += sdh * c;      di  += sdh * sn;
        dtr += sdh * tau * c; dti += sdh * tau * sn;
    }

    /* 2x2 complex system: [a b; c d] [b1; b2] = [e; f]
       a=P0, b=2P1, c=P1, d=2P2, e=-D + j w0 P0, f=-P0 - DT + j w0 P1 */
    const double ar = p0r, ai = p0i;
    const double br = 2 * p1r, bi = 2 * p1i;
    const double cr = p1r, ci = p1i;
    const double dr2 = 2 * p2r, di2 = 2 * p2i;
    const double er = -dr - w0 * p0i, ei = -di + w0 * p0r;          /* -D + j w0 P0 */
    const double fr = -p0r - dtr - w0 * p1i, fi = -p0i - dti + w0 * p1r; /* -P0 - DT + j w0 P1 */

    /* det = a*d - b*c (complex) */
    const double detr = (ar * dr2 - ai * di2) - (br * cr - bi * ci);
    const double deti = (ar * di2 + ai * dr2) - (br * ci + bi * cr);
    const double detm = detr * detr + deti * deti;
    free(dwindow);
    free(window);
    if (detm <= 0.0)
        return out;

    /* b1 = (e*d - b*f)/det ; b2 = (a*f - e*c)/det  (Cramer) */
    const double edr = er * dr2 - ei * di2, edi = er * di2 + ei * dr2; /* e*d */
    const double bfr = br * fr - bi * fi, bfi = br * fi + bi * fr;     /* b*f */
    const double n1r = edr - bfr, n1i = edi - bfi;                     /* e*d - b*f */
    const double afr = ar * fr - ai * fi, afi = ar * fi + ai * fr;     /* a*f */
    const double ecr = er * cr - ei * ci, eci = er * ci + ei * cr;     /* e*c */
    const double n2r = afr - ecr, n2i = afi - eci;                     /* a*f - e*c */

    /* divide by det: q = n / det = n * conj(det) / |det|^2 */
    const double b1i = (n1i * detr - n1r * deti) / detm; /* Im(b1) */
    const double b1r = (n1r * detr + n1i * deti) / detm; /* Re(b1) */
    const double b2i = (n2i * detr - n2r * deti) / detm; /* Im(b2) */

    const double sr = (double) sample_rate;
    out.found = 1;
    out.f_center = b1i * sr / (2.0 * M_PI);          /* Im(b1) rad/sample -> Hz */
    out.chirp_hz_s = b2i * sr * sr / M_PI;           /* 2*Im(b2)/(2pi) * sr^2  */
    out.amp_slope_s = b1r * sr;                      /* Re(b1) 1/sample -> 1/s */
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
    double worst_amp_wg_db[2] = { 0.0, 0.0 };
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
                    const double ae_wg = fabs(db(r.amp_wg / amps[ai]));
                    const double ce_re = fabs(cents(r.frq_re, f_true));
                    if (ce > worst_cents[pi])
                        worst_cents[pi] = ce;
                    if (ae > worst_amp_db[pi])
                        worst_amp_db[pi] = ae;
                    if (ae_wg > worst_amp_wg_db[pi])
                        worst_amp_wg_db[pi] = ae_wg;
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
               "reassigned |freq err| = %.4f cents, |amp err| = %.4f dB, "
               "window-gain |amp err| = %.4f dB\n",
               pads[pi], worst_cents[pi], worst_cents_re[pi], worst_amp_db[pi],
               worst_amp_wg_db[pi]);
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

    /* Phase 1 numerical gate (QIFFT): window-gain amplitude correction must beat
     * the dB-parabolic height on the unpadded regime, where the parabolic
     * amplitude bias is largest. The padded live regime is already near-exact,
     * so the payoff is concentrated unpadded -- this gate is what justifies
     * porting the correction into the vendored core. */
    if (!(worst_amp_wg_db[1] < 0.5 * worst_amp_db[1]))
        return fprintf(stderr,
                       "FAIL: window-gain amp (%.4f dB) did not beat parabolic amp (%.4f dB) by 2x\n",
                       worst_amp_wg_db[1], worst_amp_db[1]),
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

    /* --- Within-frame slopes: DDM chirp + AM recovery (Phase 3 gate) -------- */
    {
        const int fft_size = next_pow2(2 * win_size);
        const int M_2c = (int) floor(((double) win_size - 1) / 2);
        const int first_point = fft_size - M_2c;
        double* window = make_window(BLACKMAN_H, win_size);
        const double norm = window_norm(window, win_size);
        double* s = (double*) malloc((size_t) win_size * sizeof(double));

        /* helper: lay a time signal into the FFT buffer and return the
         * parabolic peak frequency nearest f_guess (demodulation reference). */
        #define PEAK_OF(buf, fguess) ({                                        \
            ATS_FFT ff; ff.size = fft_size; ff.rate = sr;                      \
            ff.fdr = (double*) calloc((size_t) fft_size, sizeof(double));      \
            ff.fdi = (double*) calloc((size_t) fft_size, sizeof(double));      \
            for (int k = 0; k < win_size; ++k)                                 \
                ff.fdr[(k + first_point) % fft_size] = window[k] * (buf)[k];   \
            fft(ff.fdr, ff.fdi, fft_size, 1);                                  \
            int ps = 0;                                                        \
            ATS_PEAK* pk = peak_detection(&ff, NULL, 2, fft_size/2, -120.0, norm, &ps); \
            double bf = 1e18, fr_ = (fguess);                                  \
            for (int i = 0; i < ps; ++i)                                       \
                if (fabs(pk[i].frq - (fguess)) < bf) { bf = fabs(pk[i].frq - (fguess)); fr_ = pk[i].frq; } \
            free(pk); free(ff.fdr); free(ff.fdi); fr_; })

        const double sr_d = (double) sr;
        const double f_c = 1500.0;       /* inst freq at window center (Hz) */
        const double chirp = 2000.0;     /* true frequency slope (Hz/s) */

        /* Linear chirp, center inst freq f_c, chirp rate `chirp`. */
        for (int n = 0; n < win_size; ++n)
        {
            const double t = ((double) n - (double) M_2c) / sr_d;
            const double ph = 2.0 * M_PI * (f_c * t + 0.5 * chirp * t * t);
            s[n] = cos(ph);
        }
        double f0 = PEAK_OF(s, f_c);
        DdmResult dc = estimate_chirp_ddm(s, win_size, sr, f0);

        /* Stationary tone at f_c (chirp rate must come out ~0). */
        for (int n = 0; n < win_size; ++n)
        {
            const double t = ((double) n - (double) M_2c) / sr_d;
            s[n] = cos(2.0 * M_PI * f_c * t);
        }
        double f0s = PEAK_OF(s, f_c);
        DdmResult ds = estimate_chirp_ddm(s, win_size, sr, f0s);

        /* Exponential AM, constant freq f_c, amplitude A=exp(mu t). */
        const double mu = 40.0; /* nepers/s */
        for (int n = 0; n < win_size; ++n)
        {
            const double t = ((double) n - (double) M_2c) / sr_d;
            s[n] = exp(mu * t) * cos(2.0 * M_PI * f_c * t);
        }
        double f0a = PEAK_OF(s, f_c);
        DdmResult da = estimate_chirp_ddm(s, win_size, sr, f0a);

        free(s);
        free(window);

        const double chirp_rel = fabs(dc.chirp_hz_s - chirp) / chirp;
        const double fc_err = fabs(dc.f_center - f_c);
        const double amp_rel = fabs(da.amp_slope_s - mu) / mu;
        printf("DDM chirp: true %.0f Hz/s -> est %.1f Hz/s (rel err %.4f), "
               "f_center %.2f Hz (err %.3f Hz); stationary chirp est %.2f Hz/s\n",
               chirp, dc.chirp_hz_s, chirp_rel, dc.f_center, fc_err, ds.chirp_hz_s);
        printf("DDM AM: true %.0f nep/s -> est %.2f nep/s (rel err %.4f)\n",
               mu, da.amp_slope_s, amp_rel);

        /* Phase 3 numerical gate: DDM must recover the within-frame chirp rate
         * the stationary model omits entirely. Require <2% chirp-rate error and
         * an accurate center frequency, and confirm a true stationary tone reads
         * near-zero chirp (so the slope is signal-driven, not an artifact). This
         * is what justifies porting DDM into the vendored core. */
        if (!(chirp_rel < 0.02))
            return fprintf(stderr, "FAIL: DDM chirp rel err %.4f exceeds 0.02\n", chirp_rel), 1;
        if (!(fc_err < 1.0))
            return fprintf(stderr, "FAIL: DDM center freq err %.3f Hz exceeds 1.0\n", fc_err), 1;
        if (!(fabs(ds.chirp_hz_s) < 0.05 * chirp))
            return fprintf(stderr, "FAIL: stationary tone DDM chirp %.2f Hz/s not ~0\n", ds.chirp_hz_s), 1;
        if (!(amp_rel < 0.05))
            return fprintf(stderr, "FAIL: DDM amp-slope rel err %.4f exceeds 0.05\n", amp_rel), 1;
        #undef PEAK_OF
    }

    printf("ok: estimator accuracy baseline recorded\n");
    return 0;
}
