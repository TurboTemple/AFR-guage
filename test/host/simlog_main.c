/* afr_simlog: writes the log the gauge would write for a simulated session.
 *
 * Runs the firmware's path (lambda ADC at 860 SPS with chain gain/offset
 * errors and noise, through the decimator and afr_pipeline; LED at 50 Hz;
 * throttle at 200 Hz) and logs every sample with the firmware's log_format,
 * so tools/replay.py can be checked end to end.
 *
 *   afr_simlog [--seed N] > session.csv */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "log_format.h"
#include "pipeline.h"
#include "sim.h"

typedef struct {
  uint32_t ms;
  float    lambda;  /* NAN: hold the given volts instead */
  float    volts;
  uint32_t led_period, led_on;
  float    tps;     /* % */
} seg_t;

#define TPS_CLOSED_V 0.60f
#define TPS_WOT_V    4.40f

/* Engine off, Spartan power-up, warm-up, idle, then two passes: a clean one
 * and one that leans out at the top end, each followed by the lift and
 * fuel cut, and a rich moment at the line. */
static const seg_t k_session[] = {
  { 3000, NAN, 0.0f, 0, 0, 0 },
  { 5000, NAN, 1.666f, 2000, 250, 0 },
  { 5000, NAN, 3.333f, 2000, 250, 0 },
  { 20000, NAN, 1.2f, 2000, 250, 0 },
  { 20000, 1.00f, 0, 0, 1, 0 },
  { 1500, 0.70f, 0, 0, 1, 12 },  /* staging: rich on the transbrake */
  { 9500, 0.82f, 0, 0, 1, 100 }, /* clean pass */
  { 6000, NAN, 5.0f, 0, 1, 0 },  /* lift: fuel cut */
  { 30000, 1.00f, 0, 0, 1, 0 },
  { 6000, 0.82f, 0, 0, 1, 100 }, /* second pass... */
  { 400, 0.96f, 0, 0, 1, 100 },  /* ...leans out */
  { 2000, 0.83f, 0, 0, 1, 100 },
  { 5000, NAN, 5.0f, 0, 1, 0 },
  { 10000, 1.00f, 0, 0, 1, 0 },
};

int main(int argc, char **argv)
{
  uint32_t seed = 4242;
  for (int i = 1; i + 1 < argc; i += 2) {
    if (!strcmp(argv[i], "--seed")) seed = (uint32_t)strtoul(argv[i + 1], NULL, 10);
  }
  sim_srand(seed);

  afr_config_t cfg = {
    .range = conv_range_default(),
    .stoich = 14.7f,
    .smooth_ms = 50,
    .alarm = alarms_default_cfg(),
    .tps_closed_v = TPS_CLOSED_V,
    .tps_wot_v = TPS_WOT_V,
  };
  static afr_pipeline_t p;
  afr_pipeline_init(&p, &cfg, NULL, false, AFR_SAMPLE_MS, 0);
  afr_pipeline_start(&p, 0);
  decim_t dec;
  decim_init(&dec, SPIKE_CODES);

  const float a = CAL_A_NOMINAL * 1.006f, b = 0.008f;
  printf("# firmware=simlog\n# fuel=Petrol 14.70\n# sample_hz=215\n# cal=NOMINAL\n");
  const alarm_cfg_t *ac = &cfg.alarm;
  printf("# lambda_end_points=0.680,1.360\n# lean_load=%.2f,%lu\n# lean_part=%.2f,%lu\n",
         (double)ac->lean_load, (unsigned long)ac->lean_load_ms, (double)ac->lean_part,
         (unsigned long)ac->lean_part_ms);
  printf("# rich=%.2f,%lu\n# load_pct=%.0f\n# closed_pct=%.0f\n# smooth_ms=50\n# supply_sense=no\n",
         (double)ac->rich, (unsigned long)ac->rich_ms, (double)ac->load_pct, (double)ac->closed_pct);
  printf("%s\n", LOG_COLUMNS);

  uint64_t t_us = 0, next_led = 0, next_tps = 0;
  for (size_t s = 0; s < sizeof k_session / sizeof k_session[0]; s++) {
    const seg_t *g = &k_session[s];
    float v = isnan(g->lambda) ? g->volts : sim_v_for_lambda(g->lambda);
    uint64_t seg_start = t_us, seg_end = t_us + (uint64_t)g->ms * 1000u;
    for (; t_us < seg_end; t_us += 1163) {
      uint32_t t_ms = (uint32_t)(t_us / 1000u);
      if (t_us >= next_led) {
        next_led += 20000;
        uint32_t rel = (uint32_t)((t_us - seg_start) / 1000u);
        afr_pipeline_led_sample(&p, t_ms, sim_adc_code(sim_blink(rel, g->led_period, g->led_on) ? 3.0f : 0.0f));
      }
      if (t_us >= next_tps) {
        next_tps += 5000;
        float tv = TPS_CLOSED_V + (TPS_WOT_V - TPS_CLOSED_V) * g->tps / 100.0f;
        afr_pipeline_tps_sample(&p, true, sim_code(tv, CAL_A_NOMINAL, 0.0f, 0.002f));
      }
      float mean;
      uint64_t t_mid;
      if (!decim_feed(&dec, sim_code(v, a, b, 0.0015f), t_us, &mean, &t_mid)) continue;
      reading_t r;
      afr_pipeline_step_raw(&p, (uint32_t)(t_mid / 1000u), true, mean, &r);
      uint8_t ev = afr_pipeline_take_events(&p);
      if (ev & AFR_EVT_CAL_ACCEPTED) printf("# cal=FRESH a=%.6e b=%.5f\n", (double)p.cal.a, (double)p.cal.b);
      char line[LOG_ROW_MAX];
      if (log_format_row(line, sizeof line, &r, 0)) fputs(line, stdout);
    }
  }
  return 0;
}
