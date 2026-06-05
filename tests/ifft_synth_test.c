/*
 * ATSA_IFFT_SYNTH numerical gate.
 *
 * Validates do_synthesis_ifft (overlap-add inverse-FFT additive synthesis)
 * against a direct time-domain oscillator reference on stationary sinusoids.
 * For steady partials the IFFT path must reconstruct the waveform (amplitude
 * AND phase) to a tight tolerance in the OA-steady interior.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "atsa.h"

#define SR 44100.0
#define HOP 256
#define NFRAMES 64
#define NPART 3

static double wrap_pi(double x)
{
  while (x > M_PI) x -= 2.0 * M_PI;
  while (x < -M_PI) x += 2.0 * M_PI;
  return x;
}

static double **alloc2d(int rows, int cols)
{
  double **m = (double **)malloc((size_t)rows * sizeof(double *));
  for (int r = 0; r < rows; ++r)
    m[r] = (double *)calloc((size_t)cols, sizeof(double));
  return m;
}

static void free2d(double **m, int rows)
{
  for (int r = 0; r < rows; ++r)
    free(m[r]);
  free(m);
}

int main(void)
{
  const double freqs[NPART] = { 440.0, 880.0, 1330.0 };
  const double amps[NPART]  = { 0.50, 0.30, 0.18 };
  const double ph0[NPART]   = { 0.30, 1.10, -0.70 };

  const double hop_sec = (double)HOP / SR;
  const double dur = NFRAMES * hop_sec;

  ATS_SOUND s;
  memset(&s, 0, sizeof(s));
  s.srate = (int)SR;
  s.frame_size = HOP;
  s.window_size = HOP;
  s.partials = NPART;
  s.frames = NFRAMES;
  s.dur = dur;
  s.has_slopes = 0;

  s.time = alloc2d(NPART, NFRAMES); /* only row 0 read */
  s.frq  = alloc2d(NPART, NFRAMES);
  s.amp  = alloc2d(NPART, NFRAMES);
  s.pha  = alloc2d(NPART, NFRAMES);

  for (int f = 0; f < NFRAMES; ++f) {
    int sf = f * HOP; /* grain-center sample (time[f]*sr) */
    s.time[0][f] = (double)f * hop_sec;
    for (int d = 0; d < NPART; ++d) {
      s.frq[d][f] = freqs[d];
      s.amp[d][f] = amps[d];
      /* phase at the frame-center sample sf */
      s.pha[d][f] = wrap_pi(2.0 * M_PI * freqs[d] * (double)sf / SR + ph0[d]);
    }
  }

  SPARAMS sp;
  memset(&sp, 0, sizeof(sp));
  sp.amp = 1.0;
  sp.ramp = 0.0; /* deterministic only */
  sp.frec = 1.0;
  sp.max_stretch = 1.0;
  sp.beg = 0.0;
  sp.end = dur;
  sp.sr = SR;
  sp.allorsel = 0;
  sp.upha = 1;
  sp.use_ifft = 1;

  double *out = NULL;
  int n_out = 0;
  do_synthesis_ifft(&s, &sp, NULL, &out, &n_out);
  if (!out || n_out <= 0) {
    fprintf(stderr, "ifft_synth_test: synthesis produced no samples\n");
    return 1;
  }

  /* Direct oscillator reference. */
  double *ref = (double *)malloc((size_t)n_out * sizeof(double));
  for (int m = 0; m < n_out; ++m) {
    double v = 0.0;
    for (int d = 0; d < NPART; ++d)
      v += amps[d] * cos(2.0 * M_PI * freqs[d] * (double)m / SR + ph0[d]);
    ref[m] = v;
  }

  /* Compare in the OA-steady interior (skip the first/last grain's taper). */
  int N = (int)ppp2((unsigned int)(2 * HOP));
  int lo = N, hi = n_out - N;
  if (hi <= lo) {
    fprintf(stderr, "ifft_synth_test: interior too small\n");
    return 1;
  }

  double num = 0.0, den = 0.0, peak = 0.0;
  for (int m = lo; m < hi; ++m) {
    double e = out[m] - ref[m];
    num += e * e;
    den += ref[m] * ref[m];
    if (fabs(out[m]) > peak) peak = fabs(out[m]);
  }
  double nrmse = sqrt(num / (den + 1e-30));

  printf("ifft_synth_test: n_out=%d N=%d interior=[%d,%d] peak=%.4f nrmse=%.4f\n",
         n_out, N, lo, hi, peak, nrmse);

  int rc = 0;
  if (!(peak > 0.1)) {
    fprintf(stderr, "ifft_synth_test: output is silent (peak=%.4g)\n", peak);
    rc = 1;
  }
  if (!(nrmse < 0.05)) {
    fprintf(stderr, "ifft_synth_test: reconstruction error too high (nrmse=%.4f)\n", nrmse);
    rc = 1;
  }

  free(out);
  free(ref);
  free2d(s.time, NPART);
  free2d(s.frq, NPART);
  free2d(s.amp, NPART);
  free2d(s.pha, NPART);

  if (rc == 0)
    printf("ifft_synth_test: OK\n");
  return rc;
}
