/* afr_simlog: writes the log the gauge would write for a simulated drive.
 *
 * Runs the firmware's full 200 Hz path (ADC codes through afr_pipeline,
 * with chain gain/offset errors and noise) and formats rows at the log rate
 * with the firmware's log_format, so tools/replay.py can be checked end to
 * end: replaying the decimated log must reproduce the logged states,
 * warnings and peaks.
 *
 *   afr_simlog [--rate HZ] [--seed N] > drive.csv */
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
} seg_t;

/* Engine off, Spartan power-up (sequencer), warm-up, then a short drive
 * with rich acceleration, cruise, overrun fuel cut and a lean stumble. */
static const seg_t k_drive[] = {
  { 3000, NAN, 0.0f, 0, 0 },
  { 5000, NAN, 1.666f, 2000, 250 },
  { 5000, NAN, 3.333f, 2000, 250 },
  { 20000, NAN, 1.2f, 2000, 250 },
  { 30000, 1.00f, 0, 0, 1 },
  { 4000, 0.82f, 0, 0, 1 },
  { 15000, 1.00f, 0, 0, 1 },
  { 6000, NAN, 5.0f, 0, 1 },
  { 3000, 1.15f, 0, 0, 1 },
  { 10000, 0.99f, 0, 0, 1 },
  { 3000, 0.72f, 0, 0, 1 },
  { 5000, 1.00f, 0, 0, 1 },
};

int main(int argc, char **argv)
{
  unsigned rate = 20;
  uint32_t seed = 4242;
  for (int i = 1; i + 1 < argc; i += 2) {
    if (!strcmp(argv[i], "--rate")) rate = (unsigned)atoi(argv[i + 1]);
    else if (!strcmp(argv[i], "--seed")) seed = (uint32_t)strtoul(argv[i + 1], NULL, 10);
  }
  if (rate != 10 && rate != 20 && rate != 50) rate = 20;
  sim_srand(seed);

  afr_config_t cfg = {
    .range = conv_range_default(),
    .stoich = 14.7f,
    .smooth_ms = 100,
    .lean = { 1.10f, 1000 },
    .rich = { 0.75f, 2000 },
  };
  static afr_pipeline_t p;
  afr_pipeline_init(&p, &cfg, NULL, false, AFR_SAMPLE_MS, 0);
  afr_pipeline_start(&p, 0);

  const float a = CAL_A_NOMINAL * 1.006f, b = 0.008f;
  printf("# firmware=simlog\n# fuel=Petrol 14.70\n# log_rate_hz=%u\n", rate);
  printf("# cal=NOMINAL\n# lambda_end_points=0.680,1.360\n# warn_lean=1.10,1000\n");
  printf("# warn_rich=0.75,2000\n# smooth_ms=100\n# supply_sense=no\n%s\n", LOG_COLUMNS);

  uint32_t t = 0, tick = 0, decim = 1000u / AFR_SAMPLE_MS / rate;
  for (size_t s = 0; s < sizeof k_drive / sizeof k_drive[0]; s++) {
    const seg_t *g = &k_drive[s];
    float v = isnan(g->lambda) ? g->volts : sim_v_for_lambda(g->lambda);
    uint32_t seg_start = t;
    for (; t < seg_start + g->ms; t += AFR_SAMPLE_MS, tick++) {
      afr_raw_t in = { .t_ms = t, .lam_ok = true };
      in.lam_code = sim_code(v, a, b, 0.001f);
      if (tick % 4 == 0) {
        in.led_new = true;
        in.led_code = sim_adc_code(sim_blink(t - seg_start, g->led_period, g->led_on) ? 3.0f : 0.0f);
      }
      reading_t r;
      afr_pipeline_step_raw(&p, &in, &r);
      uint8_t ev = afr_pipeline_take_events(&p);
      if (ev & AFR_EVT_CAL_ACCEPTED) printf("# cal=FRESH a=%.6e b=%.5f\n", (double)p.cal.a, (double)p.cal.b);
      if (tick % decim == 0) {
        char line[LOG_ROW_MAX];
        if (log_format_row(line, sizeof line, &r, 0)) fputs(line, stdout);
      }
    }
  }
  return 0;
}
