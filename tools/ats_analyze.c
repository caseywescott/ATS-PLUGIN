/*
 * ats_analyze: load a mono WAV, run ATSA peak-tracking, save an .ats file.
 *
 * Usage: ats_analyze [options] <input.wav> <output.ats>
 *
 * Options (all optional; defaults match ATSA classic):
 *   --type <1-4>          ATS file type (default 4 = amp+freq+phase+noise)
 *   --no-slopes           Skip DDM slope computation (legacy LIN3 baseline)
 *   --multi-res           Enable multi-resolution analysis front-end
 *
 *   Time range:
 *   --start <s>           Start time in seconds (default 0.0)
 *   --duration <s>        Duration to analyze in seconds (default 0.0 = whole file)
 *
 *   Frequency range:
 *   --lowest-freq <hz>    Lowest frequency to track in Hz (default 20.0)
 *   --highest-freq <hz>   Highest frequency to track in Hz (default 20000.0)
 *
 *   Windowing:
 *   --win-cycles <n>      Window size in cycles (default 4; overrides --win-size)
 *   --win-type <n>        Window type: 0=Blackman 1=Blackman-Harris 2=Hanning (default 1)
 *   --win-size <n>        Window size in samples (default 1024; ignored if --win-cycles set)
 *   --hop-size <f>        Hop size as fraction of window size (default 0.25)
 *
 *   Peak detection:
 *   --lowest-mag <db>     Lowest magnitude threshold in dB (default -60.0)
 *   --freq-dev <f>        Frequency deviation tolerance for tracking (default 0.1)
 *
 *   Tracking:
 *   --track-len <n>       Tracking history length in frames (default 3)
 *   --min-seg-len <n>     Minimum segment length in frames (default 3)
 *   --min-gap-len <n>     Minimum gap length in frames (default 3)
 *   --last-peak-cont <f>  Last peak continuation factor (default 0.0)
 *   --smr-cont <f>        SMR continuation factor (default 0.0)
 *   --smr-threshold <db>  SMR threshold in dB (default 30.0)
 *   --min-seg-smr <db>    Minimum segment SMR in dB (default 60.0)
 *
 *   Multi-resolution (only used with --multi-res):
 *   --mr-win-mult <f>     Window multiplier for low-frequency band (default 2.0)
 *   --mr-crossover <hz>   Crossover frequency in Hz (default 500.0)
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
    "Usage: %s [options] <input.wav> <output.ats>\n"
    "  --type <1-4>           ATS type: 1=amp+freq 2=+phase 3=+noise 4=all (default %d)\n"
    "  --no-slopes            Skip DDM slope computation\n"
    "  --multi-res            Enable multi-resolution front-end\n"
    "  --start <s>            Analysis start time in seconds (default %.1f)\n"
    "  --duration <s>         Duration to analyze in seconds (default %.1f = whole file)\n"
    "  --lowest-freq <hz>     Lowest tracked frequency (default %.1f)\n"
    "  --highest-freq <hz>    Highest tracked frequency (default %.1f)\n"
    "  --win-cycles <n>       Window size in cycles (default %d)\n"
    "  --win-type <n>         Window: 0=Blackman 1=Blackman-H 2=Hanning (default %d)\n"
    "  --win-size <n>         Window size in samples (default %d)\n"
    "  --hop-size <f>         Hop size fraction of window (default %.2f)\n"
    "  --lowest-mag <db>      Lowest magnitude in dB (default %.1f)\n"
    "  --freq-dev <f>         Frequency deviation tolerance (default %.1f)\n"
    "  --track-len <n>        Tracking history frames (default %d)\n"
    "  --min-seg-len <n>      Minimum segment length frames (default %d)\n"
    "  --min-gap-len <n>      Minimum gap length frames (default %d)\n"
    "  --last-peak-cont <f>   Last peak continuation (default %.1f)\n"
    "  --smr-cont <f>         SMR continuation (default %.1f)\n"
    "  --smr-threshold <db>   SMR threshold in dB (default %.1f)\n"
    "  --min-seg-smr <db>     Minimum segment SMR in dB (default %.1f)\n"
    "  --mr-win-mult <f>      Multi-res window multiplier (default %.1f)\n"
    "  --mr-crossover <hz>    Multi-res crossover Hz (default %.1f)\n",
    prog,
    ATSA_TYPE,
    ATSA_START, ATSA_DUR,
    ATSA_LFREQ, ATSA_HFREQ,
    ATSA_WCYCLES, ATSA_WTYPE, ATSA_WSIZE, ATSA_HSIZE,
    ATSA_LMAG, ATSA_FREQDEV,
    ATSA_TRKLEN, ATSA_MSEGLEN, ATSA_MGAPLEN,
    ATSA_LPKCONT, ATSA_SMRCONT, ATSA_SMRTHRES, ATSA_MSEGSMR,
    ATSA_MRWINMULT, ATSA_MRCROSS);
}

static int load_mono_wav(const char *path, double **out_samples,
                         sf_count_t *out_frames, int *out_sr)
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
  if (!buf) { sf_close(infile); return -1; }

  while (1) {
    sf_count_t n = sf_readf_double(infile, data, BUFFER_LEN);
    if (n <= 0) break;
    for (sf_count_t i = 0; i < n; ++i)
      buf[total_read + i] = data[i];
    total_read += n;
  }
  sf_close(infile);

  *out_samples = buf;
  *out_frames  = sfinfo.frames;
  *out_sr      = sfinfo.samplerate;
  return 0;
}

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

static void free_residual(double ***res)
{
  if (!res || !*res) return;
  if ((*res)[0]) free((*res)[0]);
  if ((*res)[1]) free((*res)[1]);
  free(*res);
  *res = NULL;
}

/* Return 1 if argv[i] matches flag and i+1 < argc (has value), else 0. */
#define FLAGVAL(flag) (strcmp(argv[i], (flag)) == 0 && i + 1 < argc)

int main(int argc, char **argv)
{
  const char *in_wav  = NULL;
  const char *out_ats = NULL;
  int no_slopes = 0;
  int i = 1;

  ANARGS an;
  default_anargs(&an);

  while (i < argc) {
    if (FLAGVAL("--type"))          { an.type          = atoi(argv[++i]); i++; continue; }
    if (FLAGVAL("--start"))         { an.start         = atof(argv[++i]); i++; continue; }
    if (FLAGVAL("--duration"))      { an.duration      = atof(argv[++i]); i++; continue; }
    if (FLAGVAL("--lowest-freq"))   { an.lowest_freq   = atof(argv[++i]); i++; continue; }
    if (FLAGVAL("--highest-freq"))  { an.highest_freq  = atof(argv[++i]); i++; continue; }
    if (FLAGVAL("--win-cycles"))    { an.win_cycles    = atoi(argv[++i]); i++; continue; }
    if (FLAGVAL("--win-type"))      { an.win_type      = atoi(argv[++i]); i++; continue; }
    if (FLAGVAL("--win-size"))      { an.win_size      = atoi(argv[++i]); i++; continue; }
    if (FLAGVAL("--hop-size"))      { an.hop_size      = atof(argv[++i]); i++; continue; }
    if (FLAGVAL("--lowest-mag"))    { an.lowest_mag    = atof(argv[++i]); i++; continue; }
    if (FLAGVAL("--freq-dev"))      { an.freq_dev      = atof(argv[++i]); i++; continue; }
    if (FLAGVAL("--track-len"))     { an.track_len     = atoi(argv[++i]); i++; continue; }
    if (FLAGVAL("--min-seg-len"))   { an.min_seg_len   = atoi(argv[++i]); i++; continue; }
    if (FLAGVAL("--min-gap-len"))   { an.min_gap_len   = atoi(argv[++i]); i++; continue; }
    if (FLAGVAL("--last-peak-cont")){ an.last_peak_cont= atof(argv[++i]); i++; continue; }
    if (FLAGVAL("--smr-cont"))      { an.SMR_cont      = atof(argv[++i]); i++; continue; }
    if (FLAGVAL("--smr-threshold")) { an.SMR_thres     = atof(argv[++i]); i++; continue; }
    if (FLAGVAL("--min-seg-smr"))   { an.min_seg_SMR   = atof(argv[++i]); i++; continue; }
    if (FLAGVAL("--mr-win-mult"))   { an.mr_win_mult   = atof(argv[++i]); i++; continue; }
    if (FLAGVAL("--mr-crossover"))  { an.mr_crossover  = atof(argv[++i]); i++; continue; }
    if (strcmp(argv[i], "--no-slopes") == 0) { no_slopes = 1; i++; continue; }
    if (strcmp(argv[i], "--multi-res") == 0) {
      an.multi_res    = 1;
      an.mr_win_mult  = ATSA_MRWINMULT;
      an.mr_crossover = ATSA_MRCROSS;
      i++; continue;
    }
    if (argv[i][0] == '-') { usage(argv[0]); return 1; }
    if (!in_wav)  { in_wav  = argv[i++]; continue; }
    if (!out_ats) { out_ats = argv[i++]; continue; }
    usage(argv[0]); return 1;
  }

  if (!in_wav || !out_ats) { usage(argv[0]); return 1; }

  if (an.type < 1 || an.type > 4) {
    fprintf(stderr, "Invalid type %d; must be 1-4\n", an.type);
    return 1;
  }

  double *samples = NULL;
  sf_count_t frames = 0;
  int sr = 0;
  if (load_mono_wav(in_wav, &samples, &frames, &sr) != 0)
    return 1;

  double **residual = NULL;
  int res_len = 0;
  ATS_SOUND *sound = tracker(&an, samples, sr, (int)frames, &residual, &res_len);
  free(samples);
  free_residual(&residual);

  if (!sound) {
    fprintf(stderr, "Analysis failed.\n");
    return 1;
  }

  if (no_slopes)
    sound->has_slopes = 0;

  int saved = ats_save(sound, out_ats, an.SMR_thres, an.type);
  free_sound(sound);
  free(sound);

  if (saved < 0) {
    fprintf(stderr, "ats_save failed.\n");
    return 1;
  }

  fprintf(stdout, "Saved %d bytes to '%s'\n", saved, out_ats);
  return 0;
}
