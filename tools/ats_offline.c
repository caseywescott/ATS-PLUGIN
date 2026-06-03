/*
 * Offline spike: load mono WAV -> ATS analysis -> resynthesis -> mono WAV.
 * Phase 0 deliverable (see PRD.md / TASKS.md).
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sndfile.h>

#include "atsa.h"

#define BUFFER_LEN 1024

static void usage(const char *prog)
{
  fprintf(stderr,
          "Usage: %s [--type <1-4>] <input.wav> <output.wav>\n"
          "  Analysis type matches ATSA/Qatsh (default %d).\n",
          prog, ATSA_TYPE);
}

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
    fprintf(stderr, "Warning: short read (%lld vs %lld frames)\n", (long long)total_read,
            (long long)sfinfo.frames);
  }

  *out_samples = buf;
  *out_frames = sfinfo.frames;
  *out_sr = sfinfo.samplerate;
  return 0;
}

static int write_mono_wav(const char *path, const double *samples, int n_frames, int sr)
{
  SNDFILE *outfile;
  SF_INFO sfinfo;

  memset(&sfinfo, 0, sizeof(sfinfo));
  sfinfo.samplerate = sr;
  sfinfo.channels = 1;
  sfinfo.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;

  outfile = sf_open(path, SFM_WRITE, &sfinfo);
  if (!outfile) {
    fprintf(stderr, "Could not open '%s' for write: %s\n", path, sf_strerror(NULL));
    return -1;
  }

  double chunk[BUFFER_LEN];
  int pos = 0;
  while (pos < n_frames) {
    int n = n_frames - pos;
    if (n > BUFFER_LEN)
      n = BUFFER_LEN;
    for (int i = 0; i < n; ++i)
      chunk[i] = samples[pos + i];
    sf_writef_double(outfile, chunk, n);
    pos += n;
  }
  sf_close(outfile);
  return 0;
}

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
  a->type = ATSA_TYPE;
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

int main(int argc, char **argv)
{
  const char *in_path = NULL;
  const char *out_path = NULL;
  int analysis_type = ATSA_TYPE;
  int i = 1;

  while (i < argc) {
    if (strcmp(argv[i], "--type") == 0) {
      if (i + 1 >= argc) {
        usage(argv[0]);
        return 1;
      }
      analysis_type = atoi(argv[i + 1]);
      i += 2;
      continue;
    }
    if (argv[i][0] == '-') {
      usage(argv[0]);
      return 1;
    }
    if (!in_path)
      in_path = argv[i++];
    else if (!out_path)
      out_path = argv[i++];
    else {
      usage(argv[0]);
      return 1;
    }
  }

  if (!in_path || !out_path) {
    usage(argv[0]);
    return 1;
  }

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

  free_sound(sound);
  free(sound);

  if (!out || n_out <= 0) {
    fprintf(stderr, "Synthesis produced no samples.\n");
    if (out)
      free(out);
    return 1;
  }

  int wr = write_mono_wav(out_path, out, n_out, sr);
  free(out);
  return wr != 0 ? 1 : 0;
}
