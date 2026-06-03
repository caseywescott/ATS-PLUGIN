/*
 * Residual synthesis regression coverage for the vendored ATSA engine.
 *
 * This catches three failure modes that made residual previews unreliable:
 *  - residual-only synthesis of type 1/2 ATS data must not dereference NULL band energy,
 *  - type 3/4 residual renders must be deterministic for preview caches/tests,
 *  - classic combined synthesis is not equivalent to summing separately rendered layers.
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "atsa.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static void default_anargs(ANARGS *a, int type)
{
  memset(a, 0, sizeof(*a));
  a->start = ATSA_START;
  a->duration = ATSA_DUR;
  a->lowest_freq = 55.0;
  a->highest_freq = 8000.0;
  a->freq_dev = 0.16;
  a->win_cycles = 4;
  a->win_type = BLACKMAN_H;
  a->win_size = ATSA_WSIZE;
  a->hop_size = 0.25;
  a->lowest_mag = -76.0;
  a->track_len = 3;
  a->min_seg_len = 2;
  a->min_gap_len = 2;
  a->SMR_thres = 24.0;
  a->min_seg_SMR = 46.0;
  a->last_peak_cont = 0.2;
  a->SMR_cont = 0.5;
  a->type = type;
}

static void default_sparams(SPARAMS *s, int sr, double sound_dur, double amp, double ramp)
{
  memset(s, 0, sizeof(*s));
  s->amp = amp;
  s->ramp = ramp;
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

static void free_ats_sound(ATS_SOUND *sound)
{
  if (!sound)
    return;
  free_sound(sound);
  free(sound);
}

static double next_noise(uint32_t *state)
{
  *state = (*state * 1664525u) + 1013904223u;
  return ((double)((*state >> 8) & 0x00ffffffu) / (double)0x00ffffffu) * 2.0 - 1.0;
}

static double *make_source(int sr, int n)
{
  double *samples = (double *)calloc((size_t)n, sizeof(double));
  uint32_t noise_state = 0x12345678u;
  int i;
  if (!samples)
    return NULL;

  for (i = 0; i < n; ++i) {
    const double t = (double)i / (double)sr;
    const double fade_in = fmin(1.0, (double)i / 512.0);
    const double fade_out = fmin(1.0, (double)(n - i) / 512.0);
    const double env = fade_in * fade_out;
    const double harmonic = 0.34 * sin(2.0 * M_PI * 220.0 * t)
                            + 0.14 * sin(2.0 * M_PI * 440.0 * t + 0.2)
                            + 0.07 * sin(2.0 * M_PI * 1320.0 * t + 0.7);
    const double noisy = 0.065 * next_noise(&noise_state)
                         + 0.035 * sin(2.0 * M_PI * 3077.0 * t);
    samples[i] = env * (harmonic + noisy);
  }

  return samples;
}

static ATS_SOUND *analyze_source(const double *samples, int sr, int n, int type)
{
  ANARGS an;
  default_anargs(&an, type);

  double *working = (double *)malloc((size_t)n * sizeof(double));
  double **residual = NULL;
  int res_len = 0;
  ATS_SOUND *sound;
  if (!working)
    return NULL;

  memcpy(working, samples, (size_t)n * sizeof(double));
  sound = tracker(&an, working, sr, n, &residual, &res_len);
  free(working);
  free_residual(&residual);
  return sound;
}

static int synth_layer(ATS_SOUND *sound, int sr, double amp, double ramp, double **out, int *out_n)
{
  SPARAMS sp;
  CURVE timenv;
  double *raw = NULL;
  int n = 0;

  default_sparams(&sp, sr, sound->dur, amp, ramp);
  curve_init(&timenv);
  do_synthesis(sound, &sp, &timenv, NULL, &raw, &n);
  curve_free(&timenv);

  *out = raw;
  *out_n = n;
  return (raw && n > 0) ? 0 : -1;
}

static double rms(const double *samples, int n)
{
  double sum = 0.0;
  int i;
  if (!samples || n <= 0)
    return 0.0;
  for (i = 0; i < n; ++i)
    sum += samples[i] * samples[i];
  return sqrt(sum / (double)n);
}

static double max_abs_diff(const double *a, const double *b, int n)
{
  double m = 0.0;
  int i;
  for (i = 0; i < n; ++i) {
    const double d = fabs(a[i] - b[i]);
    if (d > m)
      m = d;
  }
  return m;
}

static int all_finite(const double *samples, int n)
{
  int i;
  for (i = 0; i < n; ++i) {
    if (!isfinite(samples[i]))
      return 0;
  }
  return 1;
}

int main(void)
{
  const int sr = 44100;
  const int n = (int)(0.45 * (double)sr);
  double *samples = make_source(sr, n);
  ATS_SOUND *type1 = NULL;
  ATS_SOUND *type4 = NULL;
  ATS_SOUND *loaded = NULL;
  double *type1_res = NULL;
  double *res_a = NULL;
  double *res_b = NULL;
  double *partials = NULL;
  double *combined = NULL;
  double *loaded_res = NULL;
  int n_type1_res = 0;
  int n_res_a = 0;
  int n_res_b = 0;
  int n_partials = 0;
  int n_combined = 0;
  int n_loaded_res = 0;
  int n_compare;
  double res_rms;
  double loaded_res_rms;
  double deterministic_diff;
  double classic_sum_diff = 0.0;
  const char *tmp_path = "atsa_residual_save_load_test.ats";
  int i;

  if (!samples) {
    fprintf(stderr, "source allocation failed\n");
    return 1;
  }

  type1 = analyze_source(samples, sr, n, 1);
  if (!type1) {
    fprintf(stderr, "type 1 tracker failed\n");
    free(samples);
    return 1;
  }
  if (type1->band_energy != NULL) {
    fprintf(stderr, "type 1 unexpectedly has residual band energy\n");
    free(samples);
    free_ats_sound(type1);
    return 1;
  }
  if (synth_layer(type1, sr, 0.0, 1.0, &type1_res, &n_type1_res) != 0
      || !all_finite(type1_res, n_type1_res)
      || rms(type1_res, n_type1_res) > 1.0e-10) {
    fprintf(stderr, "type 1 residual-only synth should be finite silence\n");
    free(samples);
    free(type1_res);
    free_ats_sound(type1);
    return 1;
  }
  free(type1_res);
  type1_res = NULL;
  free_ats_sound(type1);
  type1 = NULL;

  type4 = analyze_source(samples, sr, n, 4);
  free(samples);
  samples = NULL;
  if (!type4) {
    fprintf(stderr, "type 4 tracker failed\n");
    return 1;
  }
  if (type4->band_energy == NULL) {
    fprintf(stderr, "type 4 should have residual band energy\n");
    free_ats_sound(type4);
    return 1;
  }

  if (synth_layer(type4, sr, 0.0, 1.0, &res_a, &n_res_a) != 0
      || synth_layer(type4, sr, 0.0, 1.0, &res_b, &n_res_b) != 0
      || synth_layer(type4, sr, 1.0, 0.0, &partials, &n_partials) != 0
      || synth_layer(type4, sr, 1.0, 1.0, &combined, &n_combined) != 0) {
    fprintf(stderr, "type 4 synthesis failed\n");
    free(res_a);
    free(res_b);
    free(partials);
    free(combined);
    free_ats_sound(type4);
    return 1;
  }

  if (n_res_a != n_res_b) {
    fprintf(stderr, "residual render length mismatch: %d vs %d\n", n_res_a, n_res_b);
    free(res_a);
    free(res_b);
    free(partials);
    free(combined);
    free_ats_sound(type4);
    return 1;
  }

  res_rms = rms(res_a, n_res_a);
  deterministic_diff = max_abs_diff(res_a, res_b, n_res_a);
  if (res_rms <= 1.0e-8 || deterministic_diff > 1.0e-12 || !all_finite(res_a, n_res_a)) {
    fprintf(stderr, "residual render invalid: rms=%.3e diff=%.3e\n", res_rms, deterministic_diff);
    free(res_a);
    free(res_b);
    free(partials);
    free(combined);
    free_ats_sound(type4);
    return 1;
  }

  n_compare = n_combined;
  if (n_partials < n_compare)
    n_compare = n_partials;
  if (n_res_a < n_compare)
    n_compare = n_res_a;
  for (i = 0; i < n_compare; ++i) {
    const double d = fabs(combined[i] - (partials[i] + res_a[i]));
    if (d > classic_sum_diff)
      classic_sum_diff = d;
  }
  if (classic_sum_diff <= 1.0e-9) {
    fprintf(stderr, "classic combined render unexpectedly matched separate layer sum\n");
    free(res_a);
    free(res_b);
    free(partials);
    free(combined);
    free_ats_sound(type4);
    return 1;
  }

  if (ats_save(type4, tmp_path, 24.0, 4) < 0) {
    fprintf(stderr, "ats_save failed for type 4 residual model\n");
    free(res_a);
    free(res_b);
    free(partials);
    free(combined);
    free_ats_sound(type4);
    return 1;
  }

  loaded = (ATS_SOUND *)calloc(1, sizeof(ATS_SOUND));
  if (!loaded) {
    fprintf(stderr, "loaded model allocation failed\n");
    remove(tmp_path);
    free(res_a);
    free(res_b);
    free(partials);
    free(combined);
    free_ats_sound(type4);
    return 1;
  }

  {
    int loaded_type = 0;
    if (ats_load(loaded, tmp_path, &loaded_type) < 0 || loaded_type != 4 || loaded->band_energy == NULL) {
      fprintf(stderr, "ats_load did not preserve type 4 residual band energy\n");
      remove(tmp_path);
      free(res_a);
      free(res_b);
      free(partials);
      free(combined);
      free_ats_sound(loaded);
      free_ats_sound(type4);
      return 1;
    }
  }
  remove(tmp_path);

  if (synth_layer(loaded, sr, 0.0, 1.0, &loaded_res, &n_loaded_res) != 0
      || !all_finite(loaded_res, n_loaded_res)) {
    fprintf(stderr, "loaded type 4 residual-only synth failed\n");
    free(loaded_res);
    free(res_a);
    free(res_b);
    free(partials);
    free(combined);
    free_ats_sound(loaded);
    free_ats_sound(type4);
    return 1;
  }
  loaded_res_rms = rms(loaded_res, n_loaded_res);
  if (loaded_res_rms <= 1.0e-8) {
    fprintf(stderr, "loaded type 4 residual-only synth was silent\n");
    free(loaded_res);
    free(res_a);
    free(res_b);
    free(partials);
    free(combined);
    free_ats_sound(loaded);
    free_ats_sound(type4);
    return 1;
  }

  printf("atsa_residual ok: residual_rms=%.3e loaded_residual_rms=%.3e deterministic_diff=%.3e classic_sum_diff=%.3e\n",
         res_rms,
         loaded_res_rms,
         deterministic_diff,
         classic_sum_diff);

  free(loaded_res);
  free(res_a);
  free(res_b);
  free(partials);
  free(combined);
  free_ats_sound(loaded);
  free_ats_sound(type4);
  return 0;
}
