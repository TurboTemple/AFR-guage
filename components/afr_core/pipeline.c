#include "pipeline.h"

#include <math.h>
#include <string.h>

#define SEQ_NOMINAL_MS 10000u /* 5 s + 5 s, for the progress ring */

void afr_pipeline_init(afr_pipeline_t *p, const afr_config_t *cfg, const cal_coeffs_t *stored,
                       bool supply_fitted, uint16_t sample_ms, uint32_t t_ms)
{
  memset(p, 0, sizeof(*p));
  p->cfg = *cfg;
  p->supply_fitted = supply_fitted;
  if (stored && cal_plausible(stored)) {
    p->cal = *stored;
    p->cal_src = CAL_SRC_STORED;
  } else {
    p->cal = cal_nominal();
    p->cal_src = CAL_SRC_NOMINAL;
  }
  p->last_fit.a = p->last_fit.b = NAN;
  p->c1 = p->c2 = NAN;
  led_decoder_init(&p->led);
  cal_detector_init(&p->det, sample_ms);
  gs_init(&p->sm, t_ms);
  peaks_reset(&p->peaks);
  alarms_reset(&p->alarms);
  smoother_reset(&p->smooth);
  p->supply_v = NAN;
  p->tps = NAN;
  p->tps_v = NAN;
  p->led_pattern = LED_UNKNOWN;
}

void afr_pipeline_start(afr_pipeline_t *p, uint32_t t_ms)
{
  gs_init_complete(&p->sm, t_ms);
}

void afr_pipeline_fault(afr_pipeline_t *p, fault_reason_t reason, uint32_t t_ms)
{
  gs_force_fault(&p->sm, reason, t_ms);
}

void afr_pipeline_set_config(afr_pipeline_t *p, const afr_config_t *cfg)
{
  p->cfg = *cfg;
  p->tps = conv_tps_pct(p->tps_v, cfg->tps_closed_v, cfg->tps_wot_v);
}

void afr_pipeline_reset_peaks(afr_pipeline_t *p)
{
  peaks_reset(&p->peaks);
}

void afr_pipeline_ack_alarm(afr_pipeline_t *p)
{
  alarms_ack(&p->alarms);
}

uint8_t afr_pipeline_take_events(afr_pipeline_t *p)
{
  uint8_t e = p->events;
  p->events = 0;
  return e;
}

void afr_pipeline_led_sample(afr_pipeline_t *p, uint32_t t_ms, int16_t code)
{
  p->raw_mode = true;
  p->led_pattern = led_decoder_feed(&p->led, t_ms, conv_code_to_adc_volts(code));
}

void afr_pipeline_tps_sample(afr_pipeline_t *p, bool ok, int16_t code)
{
  p->tps_v = ok ? conv_code_to_volts(CAL_A_NOMINAL, CAL_B_NOMINAL, code) : NAN;
  p->tps = conv_tps_pct(p->tps_v, p->cfg.tps_closed_v, p->cfg.tps_wot_v);
}

void afr_pipeline_supply_sample(afr_pipeline_t *p, bool ok, int16_t code)
{
  p->supply_v = ok ? conv_code_to_supply_v(code) : NAN;
}

static void on_capture(afr_pipeline_t *p)
{
  p->cal_done = true;
  if (!p->raw_mode) return;
  float m1, m2;
  if (!cal_detector_captured(&p->det, &m1, &m2)) return;
  /* The detector ran on nominal volts, so codes are volts / a_nominal. */
  p->c1 = m1 / CAL_A_NOMINAL;
  p->c2 = m2 / CAL_A_NOMINAL;
  if (cal_fit(p->c1, p->c2, &p->last_fit)) {
    p->cal = p->last_fit;
    p->cal_src = CAL_SRC_FRESH;
    p->cal_rejected = false;
    p->events |= AFR_EVT_CAL_ACCEPTED;
  } else {
    p->cal_rejected = true;
    p->events |= AFR_EVT_CAL_REJECTED;
  }
}

static uint16_t flags_of(const afr_pipeline_t *p, bool event)
{
  uint16_t f = p->cal_src == CAL_SRC_FRESH ? FLAG_CAL_FRESH :
               p->cal_src == CAL_SRC_STORED ? FLAG_CAL_STORED : FLAG_CAL_NOMINAL;
  if (p->cal_rejected) f |= FLAG_CAL_REJECTED;
  if (p->peaks.rich_rail) f |= FLAG_RICH_RAIL_SEEN;
  if (p->peaks.lean_rail) f |= FLAG_LEAN_RAIL_SEEN;
  bool learnt = conv_tps_learnt(p->cfg.tps_closed_v, p->cfg.tps_wot_v);
  if (!isnan(p->tps)) f |= FLAG_TPS_OK;
  else if (learnt) f |= FLAG_TPS_FAULT;
  if (alarms_under_load(&p->alarms)) f |= FLAG_UNDER_LOAD;
  if (event) f |= FLAG_EVENT_PENDING;
  return f;
}

/* v_cal: calibrated volts. v_det: volts for the plateau detector (nominal
 * conversion). */
static void step(afr_pipeline_t *p, uint32_t t, bool adc_ok, float v_cal, float v_det,
                 reading_t *out)
{
  if (adc_ok) p->v_out = v_cal;

  if (p->sm.state == ST_SEQUENCER && adc_ok && !p->cal_done) {
    if (cal_detector_feed(&p->det, t, v_det)) {
      on_capture(p);
    } else if (t - p->sm.since_ms >= GS_SEQ_TIMEOUT_MS && cal_detector_finish(&p->det)) {
      on_capture(p);
    }
  }

  gauge_state_t before = p->sm.state;
  gs_input_t gi = {
    .t_ms = t,
    .adc_ok = adc_ok,
    .v_out = p->v_out,
    .led = p->led_pattern,
    .supply_v = p->supply_fitted ? p->supply_v : NAN,
    .cal_complete = p->cal_done,
  };
  gauge_state_t st = gs_update(&p->sm, &gi);
  if (st != before) {
    if (st == ST_SEQUENCER) {
      cal_detector_reset(&p->det);
      p->cal_done = false;
      p->events |= AFR_EVT_SPARTAN_UP;
    } else if (st == ST_NO_SPARTAN && before != ST_BOOT) {
      p->events |= AFR_EVT_SPARTAN_DOWN;
    }
  }

  float lambda = NAN;
  if (st == ST_RICH_RAIL) lambda = p->cfg.range.lam0;
  else if (st == ST_LEAN_RAIL) lambda = p->cfg.range.lam5;
  else if (st == ST_READY) lambda = conv_lambda(&p->cfg.range, p->v_out);

  alarm_t a = alarms_update(&p->alarms, &p->cfg.alarm, st, lambda, p->tps, t);
  if (a > p->last_alarm) p->events |= AFR_EVT_ALARM;
  p->last_alarm = a;

  /* Peaks under load when the throttle is known; otherwise all of READY. */
  bool counts = isnan(p->tps) ? true : alarms_under_load(&p->alarms);
  peaks_update(&p->peaks, st, lambda, counts);

  alarm_event_t ev;
  bool has_ev = alarms_event(&p->alarms, &ev);

  out->t_ms = t;
  out->v_out = p->v_out;
  out->lambda = lambda;
  out->afr = lambda * p->cfg.stoich;
  out->lambda_disp = smoother_feed(&p->smooth, lambda, t, p->cfg.smooth_ms);
  out->state = st;
  out->led_pattern = (uint8_t)p->led_pattern;
  out->supply_v = p->supply_fitted ? p->supply_v : NAN;
  out->flags = flags_of(p, has_ev);
  out->alarm = (uint8_t)a;
  out->fault = (uint8_t)p->sm.fault;
  out->state_since_ms = p->sm.since_ms;
  if (st == ST_SEQUENCER) {
    uint32_t el = t - p->sm.since_ms;
    out->seq_progress = p->cal_done ? 100 : (uint8_t)(el >= SEQ_NOMINAL_MS ? 99 : el * 100u / SEQ_NOMINAL_MS);
  } else {
    out->seq_progress = 0;
  }
  out->peak_rich = p->peaks.rich;
  out->peak_lean = p->peaks.lean;
  out->tps = p->tps;
  out->tps_v = p->tps_v;
  if (has_ev) out->event = ev;
  else memset(&out->event, 0, sizeof(out->event));
}

void afr_pipeline_step_raw(afr_pipeline_t *p, uint32_t t_ms, bool ok, float code, reading_t *out)
{
  p->raw_mode = true;
  float v_cal = p->cal.a * code + p->cal.b;
  float v_det = CAL_A_NOMINAL * code + CAL_B_NOMINAL;
  step(p, t_ms, ok, v_cal, v_det, out);
}

void afr_pipeline_step_volts(afr_pipeline_t *p, uint32_t t_ms, float v_out, led_pattern_t led,
                             float supply_v, float tps, reading_t *out)
{
  p->raw_mode = false;
  p->led_pattern = led;
  p->supply_v = supply_v;
  p->tps = tps;
  step(p, t_ms, !isnan(v_out), v_out, v_out, out);
}
