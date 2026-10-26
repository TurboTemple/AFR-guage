/* afr_replay: runs logged readings back through afr_core.
 *
 * stdin:  t_ms,v_out,led,supply_v,tps   (one reading per line; supply and
 *                                        tps blank if unknown)
 * stdout: t_ms,state,lambda,alarm       (recomputed)
 *         # peaks rich=<λ> lean=<λ>     (last line)
 *
 * tools/replay.py drives this from a gauge log and compares the result with
 * what the gauge logged. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "log_format.h"
#include "pipeline.h"

static void usage(void)
{
  fprintf(stderr,
          "usage: afr_replay [--period-ms N] [--lam0 L] [--lam5 L] [--stoich S] [--smooth-ms N]\n"
          "                  [--lean-load L,MS] [--lean-part L,MS] [--rich L,MS]\n"
          "                  [--load-pct P] [--closed-pct P] [--supply]\n");
  exit(2);
}

static void parse_limit(const char *s, float *lambda, uint32_t *ms)
{
  char *end;
  *lambda = strtof(s, &end);
  if (*end != ',') usage();
  *ms = (uint32_t)strtoul(end + 1, NULL, 10);
}

int main(int argc, char **argv)
{
  afr_config_t cfg = {
    .range = conv_range_default(),
    .stoich = 14.7f,
    .smooth_ms = 50,
    .alarm = alarms_default_cfg(),
    .tps_closed_v = NAN,
    .tps_wot_v = NAN,
  };
  unsigned period = 50;
  bool supply = false;

  for (int i = 1; i < argc; i++) {
    const char *a = argv[i];
    const char *v = i + 1 < argc ? argv[i + 1] : NULL;
    if (!strcmp(a, "--supply")) { supply = true; continue; }
    if (!v) usage();
    if (!strcmp(a, "--period-ms")) period = (unsigned)strtoul(v, NULL, 10);
    else if (!strcmp(a, "--lam0")) cfg.range.lam0 = strtof(v, NULL);
    else if (!strcmp(a, "--lam5")) cfg.range.lam5 = strtof(v, NULL);
    else if (!strcmp(a, "--stoich")) cfg.stoich = strtof(v, NULL);
    else if (!strcmp(a, "--smooth-ms")) cfg.smooth_ms = (uint16_t)strtoul(v, NULL, 10);
    else if (!strcmp(a, "--lean-load")) parse_limit(v, &cfg.alarm.lean_load, &cfg.alarm.lean_load_ms);
    else if (!strcmp(a, "--lean-part")) parse_limit(v, &cfg.alarm.lean_part, &cfg.alarm.lean_part_ms);
    else if (!strcmp(a, "--rich")) parse_limit(v, &cfg.alarm.rich, &cfg.alarm.rich_ms);
    else if (!strcmp(a, "--load-pct")) cfg.alarm.load_pct = strtof(v, NULL);
    else if (!strcmp(a, "--closed-pct")) cfg.alarm.closed_pct = strtof(v, NULL);
    else usage();
    i++;
  }
  if (!conv_range_valid(cfg.range.lam0, cfg.range.lam5) || period == 0) usage();

  static afr_pipeline_t p;
  afr_pipeline_init(&p, &cfg, NULL, supply, (uint16_t)period, 0);
  bool started = false;
  reading_t r;
  memset(&r, 0, sizeof r);
  r.peak_rich = r.peak_lean = NAN;

  char line[256];
  while (fgets(line, sizeof line, stdin)) {
    char *f[5] = { 0 };
    int n = 0;
    char *s = line;
    while (n < 5) {
      f[n++] = s;
      char *c = strpbrk(s, ",\r\n");
      if (!c) break;
      bool last = *c != ',';
      *c = '\0';
      if (last) break;
      s = c + 1;
    }
    if (n < 3 || !f[0][0]) continue;
    uint32_t t = (uint32_t)strtoul(f[0], NULL, 10);
    float v = f[1][0] ? strtof(f[1], NULL) : NAN;
    led_pattern_t led = led_pattern_parse(f[2]);
    float sv = (supply && n >= 4 && f[3][0]) ? strtof(f[3], NULL) : NAN;
    float tps = (n >= 5 && f[4][0]) ? strtof(f[4], NULL) : NAN;
    if (!started) {
      afr_pipeline_start(&p, t);
      started = true;
    }
    afr_pipeline_step_volts(&p, t, v, led, sv, tps, &r);
    char lam[16];
    log_fmt_fixed(lam, sizeof lam, r.lambda, 4);
    printf("%lu,%s,%s,%c\n", (unsigned long)r.t_ms, gs_state_name(r.state), lam,
           alarm_char((alarm_t)r.alarm));
  }
  char pr[16], pl[16];
  log_fmt_fixed(pr, sizeof pr, r.peak_rich, 4);
  log_fmt_fixed(pl, sizeof pl, r.peak_lean, 4);
  printf("# peaks rich=%s lean=%s\n", pr, pl);
  return 0;
}
