/*
 * Verify ats_save / ats_load round-trip against in-memory synthesis (Phase 4, TASKS commit 26).
 * Self-contained: generates a short mono sine, runs tracker, synthesizes, re-synthesizes via file I/O.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "atsa.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static void default_anargs(ANARGS *a)
{
  memset(a, 0, sizeof(*a));
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
  a->type = 1; /* amp + freq only for stable test */
}

static void default_sparams(SPARAMS *s, int sr, double sound_dur)
{
  memset(s, 0, sizeof(*s));
  s->amp = 1.0;
  s->ramp = 1.0;
  s->frec = 1.0;
  s->max_stretch = 1.0;
  s->beg = 0.0;
  s->end = sound_dur;
  s->sr = (double)sr;
  s->allorsel = 1;
  s->upha = 0;
}

static void free_residual(double ***res)
{
  if (!res || !*res)
    return;
  if ((*res)[0])
    free((*res)[0]);
  if ((*res)[1])
    free((*res)[1]);
  free(*res);
  *res = NULL;
}

static int synth_to_buffer(ATS_SOUND *sound, int sr, double **out_buf, int *out_n)
{
  SPARAMS sp;
  default_sparams(&sp, sr, sound->dur);
  CURVE timenv;
  curve_init(&timenv);
  double *raw = NULL;
  int n = 0;
  do_synthesis(sound, &sp, &timenv, NULL, &raw, &n);
  curve_free(&timenv);
  *out_buf = raw;
  *out_n = n;
  return (raw && n > 0) ? 0 : -1;
}

static double max_abs_diff(const double *a, const double *b, int n)
{
  double m = 0.0;
  int i;
  for (i = 0; i < n; ++i) {
    double d = fabs(a[i] - b[i]);
    if (d > m)
      m = d;
  }
  return m;
}

int main(void)
{
  const int sr = 48000;
  const int n_frames = (int)(0.6 * (double)sr);
  double *samples = (double *)calloc((size_t)n_frames, sizeof(double));
  int i;

  if (!samples)
    return 1;

  for (i = 0; i < n_frames; ++i)
    samples[i] = 0.35 * sin(2.0 * M_PI * 440.0 * (double)i / (double)sr);

  ANARGS an;
  default_anargs(&an);

  double **residual = NULL;
  int res_len = 0;
  ATS_SOUND *sound = tracker(&an, samples, sr, n_frames, &residual, &res_len);
  free(samples);
  free_residual(&residual);

  if (!sound) {
    fprintf(stderr, "tracker failed\n");
    return 1;
  }

  double *direct = NULL;
  int n_direct = 0;
  if (synth_to_buffer(sound, sr, &direct, &n_direct) != 0) {
    fprintf(stderr, "direct synth failed\n");
    free_sound(sound);
    free(sound);
    return 1;
  }

  const char *tmp_path = "ats_roundtrip_test.ats";
  if (ats_save(sound, tmp_path, an.SMR_thres, (int)an.type) < 0) {
    fprintf(stderr, "ats_save failed\n");
    free(direct);
    free_sound(sound);
    free(sound);
    return 1;
  }

  free_sound(sound);
  free(sound);
  sound = NULL;

  ATS_SOUND *loaded = (ATS_SOUND *)calloc(1, sizeof(ATS_SOUND));
  if (!loaded) {
    fprintf(stderr, "alloc failed\n");
    free(direct);
    remove(tmp_path);
    return 1;
  }

  int st = 0;
  if (ats_load(loaded, tmp_path, &st) < 0) {
    fprintf(stderr, "ats_load failed st=%d\n", st);
    free(direct);
    free(loaded);
    remove(tmp_path);
    return 1;
  }

  remove(tmp_path);

  double *viafile = NULL;
  int n_via = 0;
  if (synth_to_buffer(loaded, sr, &viafile, &n_via) != 0) {
    fprintf(stderr, "synth after load failed\n");
    free(direct);
    free_sound(loaded);
    free(loaded);
    return 1;
  }

  free_sound(loaded);
  free(loaded);

  if (n_via != n_direct) {
    fprintf(stderr, "length mismatch: direct=%d viafile=%d\n", n_direct, n_via);
    free(direct);
    free(viafile);
    return 1;
  }

  const double tol = 1e-5;
  const double mx = max_abs_diff(direct, viafile, n_direct);
  free(direct);
  free(viafile);

  if (mx > tol) {
    fprintf(stderr, "sample max diff too large: %.3e (tol %.3e)\n", mx, tol);
    return 1;
  }

  printf("ats_roundtrip ok: frames=%d max_diff=%.3e\n", n_direct, mx);
  return 0;
}
