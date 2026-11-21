/*
 * Regression coverage for two ATSA engine fixes (pyats review, 2026-11-21):
 *
 *  - ATSA_PARTIAL_FREE_BANDS: combined (partials + residual) oscillator
 *    synthesis must play the residual of critical bands that hold no active
 *    partial.  It used to play only the share carried on partials, so on
 *    crotales.ats the default render held 40% of the residual energy.
 *  - ATSA_TRIM_SEGMENT_AVERAGE: trim_partials must judge each short segment
 *    by its own average SMR, not a running average over earlier segments.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "atsa.h"

/* Defined in utilities.c, not declared in atsa.h. */
void trim_partials(ATS_SOUND *sound, int min_seg_len, double min_seg_smr);

static int failures = 0;

#define CHECK(cond, ...)                                  \
  do {                                                    \
    if (!(cond)) {                                        \
      fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
      fprintf(stderr, __VA_ARGS__);                       \
      fprintf(stderr, "\n");                              \
      ++failures;                                         \
    }                                                     \
  } while (0)

enum { SR = 44100, FRAME = 512, WINDOW = 1024 };

static ATS_SOUND *make_sound(int partials, int frames, int with_noise)
{
  ATS_SOUND *s = (ATS_SOUND *)calloc(1, sizeof(ATS_SOUND));
  int p, f, b;
  init_sound(s, SR, FRAME, WINDOW, frames, (double)frames * FRAME / SR, partials, with_noise);
  for (f = 0; f < frames; ++f) {
    s->time[0][f] = (double)f * FRAME / SR;
    for (p = 0; p < partials; ++p) {
      s->amp[p][f] = s->frq[p][f] = s->pha[p][f] = s->smr[p][f] = s->res[p][f] = 0.0;
    }
    if (with_noise)
      for (b = 0; b < ATSA_CRITICAL_BANDS; ++b)
        s->band_energy[b][f] = 0.0;
  }
  return s;
}

static double render_energy(ATS_SOUND *s, double amp, double ramp)
{
  SPARAMS p;
  CURVE c;
  double *out = NULL, e = 0.0;
  int n = 0, i;
  memset(&p, 0, sizeof(p));
  p.amp = amp;
  p.ramp = ramp;
  p.frec = 1.0;
  p.max_stretch = 1.0;
  p.beg = 0.0;
  p.end = s->dur;
  p.sr = SR;
  p.allorsel = 1;
  curve_init(&c);
  do_synthesis(s, &p, &c, NULL, &out, &n);
  curve_free(&c);
  for (i = 0; i < n; ++i)
    e += out[i] * out[i];
  free(out);
  return e;
}

static int band_of(double hz)
{
  int b;
  for (b = 0; b < ATSA_CRITICAL_BANDS; ++b)
    if (hz >= ATSA_CRITICAL_BAND_EDGES[b] && hz < ATSA_CRITICAL_BAND_EDGES[b + 1])
      return b;
  return -1;
}

static void test_partial_free_bands(void)
{
  const int frames = 80;
  const double partial_hz = 1000.0;
  const int partial_band = band_of(partial_hz);
  const int free_band = 20; /* no partial anywhere near it */
  /* ENG_RMS(E, ws) = sqrt(E / (ws * 0.04)); this gives a 0.05 noise amplitude. */
  const double energy = 0.05 * 0.05 * WINDOW * ATSA_NOISE_VARIANCE;
  ATS_SOUND *s = make_sound(1, frames, 1);
  double bands_only, combined_noise, ratio;
  int f, p, res_untouched = 1;

  for (f = 0; f < frames; ++f) {
    s->amp[0][f] = 0.1;
    s->frq[0][f] = partial_hz;
    s->smr[0][f] = 60.0;
    s->band_energy[partial_band][f] = energy;
    s->band_energy[free_band][f] = energy;
  }

  bands_only = render_energy(s, 0.0, 1.0);
  /* The combined path with the partials effectively muted isolates its noise
     (a full-level partial adds a cross-term that only averages out slowly). */
  combined_noise = render_energy(s, 1.0e-9, 1.0);
  ratio = combined_noise / bands_only;
  CHECK(bands_only > 0.0, "residual-only render is silent");
  /* Half the energy sits in a partial-free band: the legacy path gave ~0.5. */
  CHECK(ratio > 0.85 && ratio < 1.15,
        "combined-path noise is %.0f%% of the residual-only render", 100.0 * ratio);

  /* A render must not write the (possibly shared) sound. */
  for (p = 0; p < s->partials; ++p)
    for (f = 0; f < frames; ++f)
      if (s->res[p][f] != 0.0)
        res_untouched = 0;
  CHECK(res_untouched, "do_synthesis wrote ats_sound->res");

  free_sound(s);
  free(s);
}

static void test_trim_segment_average(void)
{
  const int frames = 20, min_seg_len = 3;
  const double min_seg_smr = 60.0;
  ATS_SOUND *s = make_sound(2, frames, 0);
  int f, kept = 1, trimmed = 1;

  /* Partial 0: a weak short segment (SMR 10) - must be trimmed. */
  for (f = 2; f <= 3; ++f) {
    s->amp[0][f] = 0.1;
    s->frq[0][f] = 440.0;
    s->smr[0][f] = 10.0;
  }
  /* Partial 1: a strong short segment (SMR 90) - must survive.  The legacy
     running average, (10 + 10 + 90 + 90) / 4 = 50 < 60, trimmed it. */
  for (f = 5; f <= 6; ++f) {
    s->amp[1][f] = 0.1;
    s->frq[1][f] = 880.0;
    s->smr[1][f] = 90.0;
  }

  trim_partials(s, min_seg_len, min_seg_smr);

  for (f = 2; f <= 3; ++f)
    if (s->amp[0][f] != 0.0)
      trimmed = 0;
  for (f = 5; f <= 6; ++f)
    if (s->amp[1][f] != 0.1)
      kept = 0;
  CHECK(trimmed, "weak short segment was not trimmed");
  CHECK(kept, "strong short segment was trimmed by an earlier segment's SMR");

  free_sound(s);
  free(s);
}

int main(void)
{
  test_partial_free_bands();
  test_trim_segment_average();
  if (failures == 0)
    printf("atsa_residual_split_test: ok\n");
  return failures == 0 ? 0 : 1;
}
