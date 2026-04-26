/*
 * Offline spike: load mono WAV -> ATS analysis -> resynthesis -> mono WAV.
 * Phase 0 deliverable (see PRD.md / TASKS.md).
 *
 * Optional --metadata <path.json> emits analysis diagnostics including
 * continuity_score, identity_errors, and orphan_residual_ratio so that
 * external fixture gates can activate automatically.
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sndfile.h>

#include "atsa.h"

#define BUFFER_LEN 1024

/* Frequency deviation threshold (relative) for identity-error detection. */
#define IDENTITY_FREQ_REL_THRESHOLD 0.10

static void usage(const char *prog)
{
  fprintf(stderr,
          "Usage: %s [--type <1-4>] [--analysis-mode classic_ats]"
          " [--metadata <analysis.json>] [--dump-frames <frames.json>]"
          " <input.wav> <output.wav>\n"
          "  Analysis type matches ATSA/Qatsh (default %d).\n",
          prog, ATSA_TYPE);
}

/* ---------------------------------------------------------------------------
 * FNV-1a 64-bit hash helpers (for source and parameter fingerprints)
 * ------------------------------------------------------------------------- */

static void fnv1a64_update(uint64_t *hash, const unsigned char *data, size_t n)
{
  size_t i;
  for (i = 0; i < n; ++i) {
    *hash ^= (uint64_t)data[i];
    *hash *= 1099511628211ULL;
  }
}

static void fnv1a64_to_hex(uint64_t hash, char out_hex[17])
{
  snprintf(out_hex, 17, "%016llx", (unsigned long long)hash);
}

static int fnv1a64_file(const char *path, char out_hex[17])
{
  FILE *f = fopen(path, "rb");
  unsigned char buf[4096];
  uint64_t hash = 1469598103934665603ULL;
  size_t n;

  if (!f) {
    fprintf(stderr, "Could not hash '%s'.\n", path);
    return -1;
  }
  while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
    fnv1a64_update(&hash, buf, n);
  if (ferror(f)) {
    fclose(f);
    fprintf(stderr, "Could not read '%s' while hashing.\n", path);
    return -1;
  }
  fclose(f);
  fnv1a64_to_hex(hash, out_hex);
  return 0;
}

static void parameter_hash_fnv1a64(const ANARGS *a, char out_hex[17])
{
  char buf[1024];
  uint64_t hash = 1469598103934665603ULL;
  int n = snprintf(buf, sizeof(buf),
                   "%.17g|%.17g|%.17g|%.17g|%.17g|%d|%d|%d|%.17g|%.17g"
                   "|%d|%d|%d|%.17g|%.17g|%.17g|%.17g|%d|%d|%d|%d",
                   a->start, a->duration, a->lowest_freq, a->highest_freq,
                   a->freq_dev, a->win_cycles, a->win_type, a->win_size,
                   a->hop_size, a->lowest_mag, a->track_len, a->min_seg_len,
                   a->min_gap_len, a->SMR_thres, a->min_seg_SMR,
                   a->last_peak_cont, a->SMR_cont, a->type,
                   a->hop_smp, a->fft_size, a->frames);
  if (n < 0) n = 0;
  if (n >= (int)sizeof(buf)) n = (int)sizeof(buf) - 1;
  fnv1a64_update(&hash, (const unsigned char *)buf, (size_t)n);
  fnv1a64_to_hex(hash, out_hex);
}

/* ---------------------------------------------------------------------------
 * Tracker diagnostic metrics derived from ATS_SOUND
 * ------------------------------------------------------------------------- */

/* Fraction of (partial × frame) slots where amplitude is non-zero.
 * Returns 1.0 when there are no partials or no frames (degenerate sound). */
static double compute_continuity_score(const ATS_SOUND *sound)
{
  int p, f, active = 0;
  long total;
  if (!sound || sound->partials <= 0 || sound->frames <= 0)
    return 1.0;
  total = (long)sound->partials * (long)sound->frames;
  for (p = 0; p < sound->partials; ++p)
    for (f = 0; f < sound->frames; ++f)
      if (sound->amp[p][f] > 0.0)
        ++active;
  return (double)active / (double)total;
}

/* Count of consecutive active-frame pairs where relative frequency deviation
 * exceeds IDENTITY_FREQ_REL_THRESHOLD. A high count indicates tracking
 * identity swaps or poor continuity. */
static int compute_identity_errors(const ATS_SOUND *sound)
{
  int p, f, errors = 0;
  if (!sound || sound->partials <= 0 || sound->frames <= 0)
    return 0;
  for (p = 0; p < sound->partials; ++p) {
    double prev_frq = 0.0;
    for (f = 0; f < sound->frames; ++f) {
      double a = sound->amp[p][f];
      double frq = sound->frq[p][f];
      if (a > 0.0) {
        if (prev_frq > 0.0) {
          double rel = fabs(frq - prev_frq) / prev_frq;
          if (rel > IDENTITY_FREQ_REL_THRESHOLD)
            ++errors;
        }
        prev_frq = frq;
      } else {
        prev_frq = 0.0;
      }
    }
  }
  return errors;
}

/* Ratio of total critical-band noise energy to total energy (partials + noise).
 * Returns -1.0 for analysis types 1 and 2 which carry no noise data. */
static double compute_orphan_residual_ratio(const ATS_SOUND *sound)
{
  int b, f;
  double band_sum = 0.0, amp_sum = 0.0;
  int p;
  /* band_energy is NULL for analysis types 1 and 2 (no noise data). */
  if (!sound || !sound->band_energy)
    return -1.0;
  for (b = 0; b < ATSA_CRITICAL_BANDS; ++b)
    for (f = 0; f < sound->frames; ++f)
      band_sum += sound->band_energy[b][f];
  for (p = 0; p < sound->partials; ++p)
    for (f = 0; f < sound->frames; ++f) {
      double a = sound->amp[p][f];
      amp_sum += a * a;
    }
  if (band_sum + amp_sum < 1e-30)
    return 0.0;
  return band_sum / (band_sum + amp_sum);
}

/* ---------------------------------------------------------------------------
 * Frame-data JSON dump
 * Writes per-frame partial (freq, amp, smr) arrays for envelope visualization.
 * Only active partials (amp > 0) are emitted to keep the file compact.
 * ------------------------------------------------------------------------- */

static int write_frame_dump_json(const char *path,
                                 const ANARGS *a,
                                 const ATS_SOUND *sound)
{
  FILE *f = fopen(path, "w");
  int frame, p;
  if (!f) {
    fprintf(stderr, "Could not open frame-dump path '%s'.\n", path);
    return -1;
  }

  fprintf(f, "{\n");
  fprintf(f, "  \"schema_version\": 1,\n");
  fprintf(f, "  \"sample_rate\": %d,\n", sound->srate);
  fprintf(f, "  \"frame_size\": %d,\n", sound->frame_size);
  fprintf(f, "  \"window_size\": %d,\n", sound->window_size);
  fprintf(f, "  \"partials\": %d,\n", sound->partials);
  fprintf(f, "  \"frames\": %d,\n", sound->frames);
  fprintf(f, "  \"duration_seconds\": %.17g,\n", sound->dur);
  fprintf(f, "  \"lowest_freq_hz\": %.17g,\n", a->lowest_freq);
  fprintf(f, "  \"highest_freq_hz\": %.17g,\n", a->highest_freq);
  fprintf(f, "  \"analysis_type\": %d,\n", a->type);
  fprintf(f, "  \"frame_data\": [\n");

  for (frame = 0; frame < sound->frames; ++frame) {
    double time_s = (sound->time && sound->time[0]) ? sound->time[0][frame] : 0.0;
    int first_partial = 1;

    fprintf(f, "    {\"index\": %d, \"time_s\": %.6g, \"partials\": [", frame, time_s);

    for (p = 0; p < sound->partials; ++p) {
      double a_val = sound->amp[p][frame];
      double f_val = sound->frq[p][frame];
      double s_val = (sound->smr && sound->smr[p]) ? sound->smr[p][frame] : 0.0;
      if (a_val <= 0.0)
        continue;
      if (!first_partial)
        fprintf(f, ", ");
      fprintf(f, "[%.6g, %.10g, %.4g]", f_val, a_val, s_val);
      first_partial = 0;
    }

    fprintf(f, "]}");
    if (frame + 1 < sound->frames)
      fprintf(f, ",");
    fprintf(f, "\n");
  }

  fprintf(f, "  ]\n");
  fprintf(f, "}\n");

  if (fclose(f) != 0) {
    fprintf(stderr, "Could not finish writing frame-dump path '%s'.\n", path);
    return -1;
  }
  return 0;
}

/* ---------------------------------------------------------------------------
 * Metadata JSON writer
 * ------------------------------------------------------------------------- */

static int write_metadata_json(const char *path,
                               const ANARGS *a,
                               const ATS_SOUND *sound,
                               const char *analysis_mode,
                               const char *source_hash,
                               sf_count_t input_frames,
                               int input_sr,
                               int rendered_frames)
{
  char parameter_hash[17];
  double continuity;
  int identity_errors;
  double orphan_ratio;
  FILE *f;

  parameter_hash_fnv1a64(a, parameter_hash);
  continuity     = compute_continuity_score(sound);
  identity_errors = compute_identity_errors(sound);
  orphan_ratio   = compute_orphan_residual_ratio(sound);

  f = fopen(path, "w");
  if (!f) {
    fprintf(stderr, "Could not open metadata path '%s'.\n", path);
    return -1;
  }

  fprintf(f, "{\n");
  fprintf(f, "  \"schema_version\": 1,\n");
  fprintf(f, "  \"analysis_mode\": \"%s\",\n", analysis_mode);
  fprintf(f, "  \"tracker_mode\": \"classic_recursive_frequency_smr\",\n");
  fprintf(f, "  \"source_hash_fnv1a64\": \"%s\",\n", source_hash);
  fprintf(f, "  \"parameter_hash_fnv1a64\": \"%s\",\n", parameter_hash);
  fprintf(f, "  \"input_sample_rate\": %d,\n", input_sr);
  fprintf(f, "  \"input_frames\": %lld,\n", (long long)input_frames);
  fprintf(f, "  \"rendered_frames\": %d,\n", rendered_frames);
  fprintf(f, "  \"continuity_score\": %.17g,\n", continuity);
  fprintf(f, "  \"identity_errors\": %d,\n", identity_errors);
  if (orphan_ratio < 0.0)
    fprintf(f, "  \"orphan_residual_ratio\": null,\n");
  else
    fprintf(f, "  \"orphan_residual_ratio\": %.17g,\n", orphan_ratio);
  fprintf(f, "  \"model\": {\n");
  fprintf(f, "    \"sample_rate\": %d,\n", sound->srate);
  fprintf(f, "    \"frame_size\": %d,\n", sound->frame_size);
  fprintf(f, "    \"window_size\": %d,\n", sound->window_size);
  fprintf(f, "    \"partials\": %d,\n", sound->partials);
  fprintf(f, "    \"frames\": %d,\n", sound->frames);
  fprintf(f, "    \"duration_seconds\": %.17g\n", sound->dur);
  fprintf(f, "  },\n");
  fprintf(f, "  \"analysis_params\": {\n");
  fprintf(f, "    \"start\": %.17g,\n", a->start);
  fprintf(f, "    \"duration\": %.17g,\n", a->duration);
  fprintf(f, "    \"lowest_freq\": %.17g,\n", a->lowest_freq);
  fprintf(f, "    \"highest_freq\": %.17g,\n", a->highest_freq);
  fprintf(f, "    \"freq_dev\": %.17g,\n", a->freq_dev);
  fprintf(f, "    \"win_cycles\": %d,\n", a->win_cycles);
  fprintf(f, "    \"win_type\": %d,\n", a->win_type);
  fprintf(f, "    \"win_size\": %d,\n", a->win_size);
  fprintf(f, "    \"hop_size\": %.17g,\n", a->hop_size);
  fprintf(f, "    \"lowest_mag\": %.17g,\n", a->lowest_mag);
  fprintf(f, "    \"track_len\": %d,\n", a->track_len);
  fprintf(f, "    \"min_seg_len\": %d,\n", a->min_seg_len);
  fprintf(f, "    \"min_gap_len\": %d,\n", a->min_gap_len);
  fprintf(f, "    \"SMR_thres\": %.17g,\n", a->SMR_thres);
  fprintf(f, "    \"min_seg_SMR\": %.17g,\n", a->min_seg_SMR);
  fprintf(f, "    \"last_peak_cont\": %.17g,\n", a->last_peak_cont);
  fprintf(f, "    \"SMR_cont\": %.17g,\n", a->SMR_cont);
  fprintf(f, "    \"type\": %d,\n", a->type);
  fprintf(f, "    \"computed_hop_smp\": %d,\n", a->hop_smp);
  fprintf(f, "    \"computed_fft_size\": %d,\n", a->fft_size);
  fprintf(f, "    \"computed_frames\": %d\n", a->frames);
  fprintf(f, "  }\n");
  fprintf(f, "}\n");

  if (fclose(f) != 0) {
    fprintf(stderr, "Could not finish writing metadata path '%s'.\n", path);
    return -1;
  }
  return 0;
}

/* ---------------------------------------------------------------------------
 * WAV I/O
 * ------------------------------------------------------------------------- */

static int load_mono_wav(const char *path, double **out_samples, sf_count_t *out_frames, int *out_sr)
{
  SNDFILE *infile;
  SF_INFO sfinfo;
  double data[BUFFER_LEN];
  sf_count_t total_read = 0;
  double *buf;

  memset(&sfinfo, 0, sizeof(sfinfo));
  infile = sf_open(path, SFM_READ, &sfinfo);
  if (!infile) {
    fprintf(stderr, "Could not open '%s': %s\n", path, sf_strerror(NULL));
    return -1;
  }
  if (sfinfo.channels != 1) {
    fprintf(stderr, "Expected mono WAV, got %d channels.\n", sfinfo.channels);
    sf_close(infile);
    return -1;
  }

  buf = (double *)malloc((size_t)sfinfo.frames * sizeof(double));
  if (!buf) {
    sf_close(infile);
    return -1;
  }

  while (1) {
    sf_count_t n = sf_readf_double(infile, data, BUFFER_LEN);
    if (n <= 0)
      break;
    for (sf_count_t i = 0; i < n; ++i)
      buf[total_read + i] = data[i];
    total_read += n;
  }
  sf_close(infile);

  if (total_read != sfinfo.frames) {
    fprintf(stderr, "Warning: short read (%lld vs %lld frames)\n",
            (long long)total_read, (long long)sfinfo.frames);
  }

  *out_samples = buf;
  *out_frames  = sfinfo.frames;
  *out_sr      = sfinfo.samplerate;
  return 0;
}

static int write_mono_wav(const char *path, const double *samples, int n_frames, int sr)
{
  SNDFILE *outfile;
  SF_INFO sfinfo;

  memset(&sfinfo, 0, sizeof(sfinfo));
  sfinfo.samplerate = sr;
  sfinfo.channels   = 1;
  sfinfo.format     = SF_FORMAT_WAV | SF_FORMAT_PCM_16;

  outfile = sf_open(path, SFM_WRITE, &sfinfo);
  if (!outfile) {
    fprintf(stderr, "Could not open '%s' for write: %s\n", path, sf_strerror(NULL));
    return -1;
  }

  double chunk[BUFFER_LEN];
  int pos = 0;
  while (pos < n_frames) {
    int n = n_frames - pos;
    if (n > BUFFER_LEN) n = BUFFER_LEN;
    for (int i = 0; i < n; ++i)
      chunk[i] = samples[pos + i];
    sf_writef_double(outfile, chunk, n);
    pos += n;
  }
  sf_close(outfile);
  return 0;
}

/* ---------------------------------------------------------------------------
 * Analysis defaults
 * ------------------------------------------------------------------------- */

static void default_anargs(ANARGS *a)
{
  memset(a, 0, sizeof(*a));
  a->start          = ATSA_START;
  a->duration       = ATSA_DUR;
  a->lowest_freq    = ATSA_LFREQ;
  a->highest_freq   = ATSA_HFREQ;
  a->freq_dev       = ATSA_FREQDEV;
  a->win_cycles     = ATSA_WCYCLES;
  a->win_type       = ATSA_WTYPE;
  a->win_size       = ATSA_WSIZE;
  a->hop_size       = ATSA_HSIZE;
  a->lowest_mag     = ATSA_LMAG;
  a->track_len      = ATSA_TRKLEN;
  a->min_seg_len    = ATSA_MSEGLEN;
  a->min_gap_len    = ATSA_MGAPLEN;
  a->SMR_thres      = ATSA_SMRTHRES;
  a->min_seg_SMR    = ATSA_MSEGSMR;
  a->last_peak_cont = ATSA_LPKCONT;
  a->SMR_cont       = ATSA_SMRCONT;
  a->type           = ATSA_TYPE;
}

static void default_sparams(SPARAMS *s, int sr, double sound_dur)
{
  memset(s, 0, sizeof(*s));
  s->amp        = 1.0;
  s->ramp       = 1.0;
  s->frec       = 1.0;
  s->max_stretch = 1.0;
  s->beg        = 0.0;
  s->end        = sound_dur;
  s->sr         = (double)sr;
  s->allorsel   = 1;
  s->upha       = 0;
}

static void free_residual(double ***res)
{
  if (!res || !*res)
    return;
  if ((*res)[0]) free((*res)[0]);
  if ((*res)[1]) free((*res)[1]);
  free(*res);
  *res = NULL;
}

/* ---------------------------------------------------------------------------
 * main
 * ------------------------------------------------------------------------- */

int main(int argc, char **argv)
{
  const char *in_path         = NULL;
  const char *out_path        = NULL;
  const char *metadata_path   = NULL;
  const char *frame_dump_path = NULL;
  const char *analysis_mode   = "classic_ats";
  int analysis_type           = ATSA_TYPE;
  int i = 1;

  while (i < argc) {
    if (strcmp(argv[i], "--type") == 0) {
      if (i + 1 >= argc) { usage(argv[0]); return 1; }
      analysis_type = atoi(argv[i + 1]);
      i += 2;
      continue;
    }
    if (strcmp(argv[i], "--metadata") == 0) {
      if (i + 1 >= argc) { usage(argv[0]); return 1; }
      metadata_path = argv[i + 1];
      i += 2;
      continue;
    }
    if (strcmp(argv[i], "--dump-frames") == 0) {
      if (i + 1 >= argc) { usage(argv[0]); return 1; }
      frame_dump_path = argv[i + 1];
      i += 2;
      continue;
    }
    if (strcmp(argv[i], "--analysis-mode") == 0) {
      if (i + 1 >= argc) { usage(argv[0]); return 1; }
      if (strcmp(argv[i + 1], "classic") == 0 ||
          strcmp(argv[i + 1], "classic_ats") == 0) {
        analysis_mode = "classic_ats";
      } else {
        fprintf(stderr,
                "Only analysis mode 'classic_ats' is supported.\n");
        return 1;
      }
      i += 2;
      continue;
    }
    if (argv[i][0] == '-') { usage(argv[0]); return 1; }
    if      (!in_path)  in_path  = argv[i++];
    else if (!out_path) out_path = argv[i++];
    else { usage(argv[0]); return 1; }
  }

  if (!in_path || !out_path) { usage(argv[0]); return 1; }
  if (analysis_type < 1 || analysis_type > 4) { usage(argv[0]); return 1; }

  char source_hash[17] = "0000000000000000";
  if (metadata_path && fnv1a64_file(in_path, source_hash) != 0)
    return 1;

  double *samples = NULL;
  sf_count_t frames = 0;
  int sr = 0;
  if (load_mono_wav(in_path, &samples, &frames, &sr) != 0)
    return 1;

  ANARGS anargs;
  default_anargs(&anargs);
  anargs.type = analysis_type;

  double **residual = NULL;
  int res_len = 0;
  ATS_SOUND *sound = tracker(&anargs, samples, sr, (int)frames, &residual, &res_len);
  free(samples);
  free_residual(&residual);

  if (!sound) {
    fprintf(stderr, "Analysis failed.\n");
    return 1;
  }

  SPARAMS sp;
  default_sparams(&sp, sr, sound->dur);

  CURVE timenv;
  curve_init(&timenv);

  double *out = NULL;
  int n_out = 0;
  do_synthesis(sound, &sp, &timenv, NULL, &out, &n_out);
  curve_free(&timenv);

  if (!out || n_out <= 0) {
    fprintf(stderr, "Synthesis produced no samples.\n");
    if (out) free(out);
    free_sound(sound);
    free(sound);
    return 1;
  }

  int wr = write_mono_wav(out_path, out, n_out, sr);
  if (wr == 0 && metadata_path)
    wr = write_metadata_json(metadata_path, &anargs, sound, analysis_mode,
                             source_hash, frames, sr, n_out);
  if (wr == 0 && frame_dump_path)
    wr = write_frame_dump_json(frame_dump_path, &anargs, sound);

  free(out);
  free_sound(sound);
  free(sound);
  return wr != 0 ? 1 : 0;
}
