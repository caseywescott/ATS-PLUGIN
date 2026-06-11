/* Multi-resolution analysis front-end numerical gate (DSP Fidelity PRD 6.4,
 * Commit 7). Two checks, both driving the REAL qatsh front-end:
 *
 *   Gate A (single-window resolution, fully controlled win_size): two equal,
 *   closely-spaced low tones. A short analysis window cannot resolve them (one
 *   merged peak); a longer window does (two peaks). This is the time/frequency
 *   compromise the multi-resolution front-end exploits -- long windows for the
 *   low band, short for the highs.
 *
 *   Gate B (end-to-end tracker, multi_res off vs on): a buffer with two close
 *   low partials plus one high partial. Single-resolution merges the low pair;
 *   multi-resolution resolves both AND keeps the high partial (the highs ride
 *   the unchanged default window, so they are not smeared). multi_res == 0 is
 *   the legacy path and stays bit-identical (covered by ats_roundtrip).
 */
#include "atsa.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

/* Lay a windowed two-tone buffer into the FFT grid (tracker.c circular
 * placement), run peak_detection, and count peaks falling in [lo, hi] Hz. */
static int count_peaks_in_band(double f1, double a1, double f2, double a2,
                               int win_size, int sample_rate, double lo, double hi)
{
    const int fft_size = next_pow2(2 * win_size);
    const int M_2 = (int) floor(((double) win_size - 1) / 2);
    const int first_point = fft_size - M_2;
    double* window = make_window(BLACKMAN_H, win_size);
    const double norm = window_norm(window, win_size);

    ATS_FFT ff;
    ff.size = fft_size;
    ff.rate = sample_rate;
    ff.fdr = (double*) calloc((size_t) fft_size, sizeof(double));
    ff.fdi = (double*) calloc((size_t) fft_size, sizeof(double));
    for (int k = 0; k < win_size; ++k)
    {
        const double s = a1 * cos(2.0 * M_PI * f1 * (double) k / sample_rate)
                       + a2 * cos(2.0 * M_PI * f2 * (double) k / sample_rate + 0.6);
        ff.fdr[(k + first_point) % fft_size] = window[k] * s;
    }
    fft(ff.fdr, ff.fdi, fft_size, 1);

    const double fft_mag = (double) sample_rate / (double) fft_size;
    int ps = 0;
    ATS_PEAK* pk = peak_detection(&ff, NULL, 2, fft_size / 2, -90.0, norm, &ps, NULL, NULL);
    int n = 0;
    for (int i = 0; i < ps; ++i)
        if (pk[i].frq >= lo && pk[i].frq <= hi)
            ++n;
    (void) fft_mag;
    free(pk);
    free(ff.fdr);
    free(ff.fdi);
    free(window);
    return n;
}

static void default_anargs(ANARGS* a)
{
    memset(a, 0, sizeof(*a));
    a->start = ATSA_START;
    a->duration = ATSA_DUR;
    a->lowest_freq = 100.0;     /* drives a long-ish default window (4 cycles) */
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
    a->multi_res = 0;
    a->mr_win_mult = 0.0;
    a->mr_crossover = 0.0;
}

/* True if some *sustained* partial (active across >= min_cov of all frames, mean
 * amplitude above amp_floor) has a time-averaged frequency within tol_hz of
 * `target`. The coverage gate rejects the short spurious tracks that sidelobes
 * and tracking churn spawn; the tolerance gate rejects the blended-centroid
 * track that an unresolved tone pair leaves between the two true frequencies. */
static int has_clean_tone(ATS_SOUND* s, double target, double tol_hz,
                          double amp_floor, double min_cov)
{
    for (int p = 0; p < s->partials; ++p)
    {
        double fsum = 0.0, asum = 0.0;
        int cnt = 0;
        for (int f = 0; f < s->frames; ++f)
        {
            if (s->amp[p][f] > 0.0)
            {
                fsum += s->frq[p][f];
                asum += s->amp[p][f];
                ++cnt;
            }
        }
        if (cnt == 0)
            continue;
        const double fmean = fsum / cnt;
        const double amean = asum / cnt;
        const double cov = (double) cnt / (double) s->frames;
        if (fabs(fmean - target) <= tol_hz && amean >= amp_floor && cov >= min_cov)
            return 1;
    }
    return 0;
}

static ATS_SOUND* analyze_two_low_plus_high(int multi_res, int sr, int N,
                                            double fl1, double fl2, double fh)
{
    double* buf = (double*) malloc((size_t) N * sizeof(double));
    for (int n = 0; n < N; ++n)
    {
        const double t = (double) n / (double) sr;
        buf[n] = 1.00 * cos(2.0 * M_PI * fl1 * t)
               + 0.90 * cos(2.0 * M_PI * fl2 * t + 0.6)
               + 0.40 * cos(2.0 * M_PI * fh * t + 1.1);
    }
    ANARGS an;
    default_anargs(&an);
    an.multi_res = multi_res;
    if (multi_res)
    {
        an.mr_win_mult = 3.0;
        an.mr_crossover = 500.0;
    }
    double** res = NULL;
    int res_len = 0;
    ATS_SOUND* s = tracker(&an, buf, sr, N, &res, &res_len);
    free(buf);
    if (res)
    {
        if (res[0]) free(res[0]);
        if (res[1]) free(res[1]);
        free(res);
    }
    return s;
}

int main(void)
{
    const int sr = 44100;

    /* ---- Gate A: single-window resolution improves with window length ------ */
    {
        const double f1 = 200.0, f2 = 245.0; /* 45 Hz apart, equal amplitude */
        const int short_win = 1765;          /* ~4 cycles of 100 Hz @ 44.1k   */
        const int long_win = 3 * short_win;  /* multi-res low-band window      */
        const int n_short = count_peaks_in_band(f1, 1.0, f2, 1.0, short_win, sr, 120.0, 360.0);
        const int n_long = count_peaks_in_band(f1, 1.0, f2, 1.0, long_win, sr, 120.0, 360.0);
        printf("Gate A: two tones %.0f Hz apart -> short-window peaks=%d, long-window peaks=%d\n",
               f2 - f1, n_short, n_long);
        if (!(n_long >= 2))
            return fprintf(stderr, "FAIL: long window did not resolve two low tones (got %d)\n", n_long), 1;
        if (!(n_long > n_short))
            return fprintf(stderr, "FAIL: long window (%d) did not out-resolve short window (%d)\n",
                           n_long, n_short), 1;
    }

    /* ---- Gate B: end-to-end tracker, multi_res off vs on ------------------- */
    {
        const int N = (int) (0.5 * sr);
        const double fl1 = 200.0, fl2 = 245.0, fh = 3000.0;
        ATS_SOUND* s_off = analyze_two_low_plus_high(0, sr, N, fl1, fl2, fh);
        ATS_SOUND* s_on = analyze_two_low_plus_high(1, sr, N, fl1, fl2, fh);
        if (!s_off || !s_on)
            return fprintf(stderr, "FAIL: analysis returned NULL\n"), 1;

        /* Each true low tone must be carried by a sustained partial (cov >= 0.9)
           whose mean frequency is within 8 Hz of it. The unresolved single-res
           blob settles into a blended-centroid track between 200 and 245, so it
           cannot satisfy both at once; the long low-band window can. */
        const int on_lo1 = has_clean_tone(s_on, fl1, 8.0, 0.05, 0.9);
        const int on_lo2 = has_clean_tone(s_on, fl2, 8.0, 0.05, 0.9);
        const int off_lo1 = has_clean_tone(s_off, fl1, 8.0, 0.05, 0.9);
        const int off_lo2 = has_clean_tone(s_off, fl2, 8.0, 0.05, 0.9);
        const int clean_on = on_lo1 + on_lo2;
        const int clean_off = off_lo1 + off_lo2;
        /* The high partial rides the unchanged default window in both modes. */
        const int high_on = has_clean_tone(s_on, fh, 20.0, 0.02, 0.5);
        const int high_off = has_clean_tone(s_off, fh, 20.0, 0.02, 0.5);
        printf("Gate B: clean low tones recovered off=%d on=%d ; high tone off=%d on=%d\n",
               clean_off, clean_on, high_off, high_on);

        if (!(clean_on >= 2))
            return fprintf(stderr, "FAIL: multi-res did not cleanly recover both low tones (got %d)\n",
                           clean_on), 1;
        if (!(clean_on > clean_off))
            return fprintf(stderr, "FAIL: multi-res (%d clean low tones) did not beat single-res (%d)\n",
                           clean_on, clean_off), 1;
        if (!(high_on >= 1))
            return fprintf(stderr, "FAIL: multi-res lost the high partial\n"), 1;

        free_sound(s_off);
        free(s_off);
        free_sound(s_on);
        free(s_on);
    }

    printf("ok: multi-resolution front-end resolves closely-spaced lows without losing highs\n");
    return 0;
}
