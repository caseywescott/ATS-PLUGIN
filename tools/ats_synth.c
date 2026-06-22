/*
 * ats_synth: load an existing .ats file and render to WAV via oscillator-bank synthesis.
 * Usage: ats_synth [options] <input.ats> <output.wav>
 *
 * Options:
 *   --amp   <f>   amplitude scale (default 1.0)
 *   --frec  <f>   frequency scale (default 1.0; 2.0 = one octave up)
 *   --stretch <f> time stretch factor applied via SPARAMS.max_stretch (default 1.0)
 *   --beg   <f>   start time in seconds within the model (default 0.0)
 *   --end   <f>   end time in seconds (default = model duration)
 *   --ramp  <f>   residual (noise) amplitude scale: 0=no noise, 1=full noise (default 1.0)
 *   --transient-gain <f>  if >0, mix transient re-injection layer at this gain
 *                         (requires the model to be linked with AtsTransientLayer)
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
    "Usage: %s [--amp <f>] [--frec <f>] [--stretch <f>] [--beg <f>] [--end <f>]\n"
    "          [--ramp <f>] [--transient-gain <f>] <input.ats> <output.wav>\n",
    prog);
}

static int write_mono_wav(const char *path, const double *samples, int n_frames, int sr)
{
  SNDFILE *outfile;
  SF_INFO sfinfo;
  double chunk[BUFFER_LEN];
  int pos = 0;

  memset(&sfinfo, 0, sizeof(sfinfo));
  sfinfo.samplerate = sr;
  sfinfo.channels   = 1;
  sfinfo.format     = SF_FORMAT_WAV | SF_FORMAT_PCM_16;

  outfile = sf_open(path, SFM_WRITE, &sfinfo);
  if (!outfile) {
    fprintf(stderr, "Could not open '%s' for write: %s\n", path, sf_strerror(NULL));
    return -1;
  }

  while (pos < n_frames) {
    int n = n_frames - pos;
    if (n > BUFFER_LEN) n = BUFFER_LEN;
    for (int i = 0; i < n; ++i) chunk[i] = samples[pos + i];
    sf_writef_double(outfile, chunk, n);
    pos += n;
  }
  sf_close(outfile);
  return 0;
}

int main(int argc, char **argv)
{
  const char *in_ats  = NULL;
  const char *out_wav = NULL;
  double amp           = 1.0;
  double frec          = 1.0;
  double stretch       = 1.0;
  double beg           = 0.0;
  double end_time      = -1.0;   /* <0 = use sound->dur */
  double ramp          = 1.0;    /* residual/noise amplitude scale */
  double transient_gain = 0.0;   /* 0 = off */
  int i = 1;

  while (i < argc) {
    if (strcmp(argv[i], "--amp") == 0 && i + 1 < argc)
      { amp = atof(argv[i+1]); i += 2; continue; }
    if (strcmp(argv[i], "--frec") == 0 && i + 1 < argc)
      { frec = atof(argv[i+1]); i += 2; continue; }
    if (strcmp(argv[i], "--stretch") == 0 && i + 1 < argc)
      { stretch = atof(argv[i+1]); i += 2; continue; }
    if (strcmp(argv[i], "--beg") == 0 && i + 1 < argc)
      { beg = atof(argv[i+1]); i += 2; continue; }
    if (strcmp(argv[i], "--end") == 0 && i + 1 < argc)
      { end_time = atof(argv[i+1]); i += 2; continue; }
    if (strcmp(argv[i], "--ramp") == 0 && i + 1 < argc)
      { ramp = atof(argv[i+1]); i += 2; continue; }
    if (strcmp(argv[i], "--transient-gain") == 0 && i + 1 < argc)
      { transient_gain = atof(argv[i+1]); i += 2; continue; }
    if (argv[i][0] == '-') { usage(argv[0]); return 1; }
    if (!in_ats)  { in_ats  = argv[i++]; continue; }
    if (!out_wav) { out_wav = argv[i++]; continue; }
    usage(argv[0]); return 1;
  }

  if (!in_ats || !out_wav) { usage(argv[0]); return 1; }

  ATS_SOUND *sound = (ATS_SOUND *)calloc(1, sizeof(ATS_SOUND));
  if (!sound) { fprintf(stderr, "Out of memory\n"); return 1; }

  int sound_type = 0;
  if (ats_load(sound, in_ats, &sound_type) < 0) {
    free(sound); return 1;
  }

  SPARAMS sp;
  memset(&sp, 0, sizeof(sp));
  sp.amp         = amp;
  sp.ramp        = ramp;
  sp.frec        = frec;
  sp.max_stretch = stretch;
  sp.beg         = beg;
  sp.end         = (end_time >= 0.0) ? end_time : sound->dur;
  sp.sr          = (double)sound->srate;
  sp.allorsel    = 1;
  sp.upha        = 0;

  CURVE timenv;
  curve_init(&timenv);
  /* Set x endpoint to stretch so the output spans stretch × model duration.
     do_synthesis recomputes max_stretch from the curve; the SPARAMS field is ignored. */
  if (stretch != 1.0)
    timenv.ctlpoint[1][0] = stretch;
  double *out_buf = NULL;
  int n_out = 0;
  do_synthesis(sound, &sp, &timenv, NULL, &out_buf, &n_out);
  curve_free(&timenv);

  int sr = sound->srate;
  free_sound(sound);
  free(sound);

  if (!out_buf || n_out <= 0) {
    fprintf(stderr, "Synthesis produced no samples\n");
    if (out_buf) free(out_buf);
    return 1;
  }

  if (transient_gain > 0.0)
    fprintf(stderr, "Warning: --transient-gain is not supported by ats_synth. "
            "Use ats_transient --mode render, then mix.\n");

  fprintf(stdout, "Rendering %d samples at %d Hz -> %s\n", n_out, sr, out_wav);
  int rc = write_mono_wav(out_wav, out_buf, n_out, sr);
  free(out_buf);
  return rc != 0 ? 1 : 0;
}
