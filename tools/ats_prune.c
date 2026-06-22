/*
 * ats_prune: load an ATS file, drop weak partials, save the result.
 *
 * Usage: ats_prune [--smr-threshold <f>] [--min-active-frames <n>] [--type <1-4>]
 *                  <input.ats> <output.ats>
 *
 * --smr-threshold   partials with mean SMR below this are removed (default 0.0)
 * --min-active-frames partials with fewer active frames than this are removed (default 1)
 * --type            save as different ATS type (default = keep original type)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "atsa.h"

static void usage(const char *prog)
{
  fprintf(stderr,
    "Usage: %s [--smr-threshold <f>] [--min-active-frames <n>] [--type <1-4>]\n"
    "          <input.ats> <output.ats>\n",
    prog);
}

int main(int argc, char **argv)
{
  const char *in_ats = NULL;
  const char *out_ats = NULL;
  double smr_threshold = 0.0;
  int min_active_frames = 1;
  int out_type = -1;  /* -1 = keep source type */
  int i = 1;

  while (i < argc) {
    if (strcmp(argv[i], "--smr-threshold") == 0 && i + 1 < argc) {
      smr_threshold = atof(argv[i + 1]); i += 2; continue;
    }
    if (strcmp(argv[i], "--min-active-frames") == 0 && i + 1 < argc) {
      min_active_frames = atoi(argv[i + 1]); i += 2; continue;
    }
    if (strcmp(argv[i], "--type") == 0 && i + 1 < argc) {
      out_type = atoi(argv[i + 1]); i += 2; continue;
    }
    if (argv[i][0] == '-') { usage(argv[0]); return 1; }
    if (!in_ats)  { in_ats  = argv[i++]; continue; }
    if (!out_ats) { out_ats = argv[i++]; continue; }
    usage(argv[0]); return 1;
  }

  if (!in_ats || !out_ats) { usage(argv[0]); return 1; }
  if (out_type != -1 && (out_type < 1 || out_type > 4)) {
    fprintf(stderr, "Invalid type %d; must be 1-4\n", out_type);
    return 1;
  }

  ATS_SOUND *sound = (ATS_SOUND *)calloc(1, sizeof(ATS_SOUND));
  if (!sound) { fprintf(stderr, "Out of memory\n"); return 1; }

  int sound_type = 0;
  if (ats_load(sound, in_ats, &sound_type) < 0) {
    free(sound); return 1;
  }

  /* Zero out partials with fewer than min_active_frames active amplitude frames */
  if (min_active_frames > 1) {
    int par, frm;
    for (par = 0; par < sound->partials; par++) {
      int active = 0;
      for (frm = 0; frm < sound->frames; frm++) {
        if (sound->amp[par][frm] > 0.0)
          active++;
      }
      if (active < min_active_frames) {
        for (frm = 0; frm < sound->frames; frm++)
          sound->amp[par][frm] = 0.0;
      }
    }
  }

  /* optimize_sound computes av[], removes partials below SMR threshold */
  ANARGS an;
  memset(&an, 0, sizeof(an));
  an.SMR_thres = smr_threshold;
  an.type = (out_type == -1) ? sound_type : out_type;
  optimize_sound(&an, sound);

  int save_type = (out_type == -1) ? sound_type : out_type;
  int saved = ats_save(sound, out_ats, smr_threshold, save_type);
  free_sound(sound);
  free(sound);

  if (saved < 0) {
    fprintf(stderr, "ats_save failed\n"); return 1;
  }
  fprintf(stdout, "Pruned model saved to '%s' (%d bytes)\n", out_ats, saved);
  return 0;
}
