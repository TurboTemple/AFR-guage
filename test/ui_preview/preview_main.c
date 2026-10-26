/* Renders every gauge screen on the PC (no hardware) to PNG-ready raw
 * frames: ui_preview <out_dir>, then tools/ui_frames_to_png.py <out_dir>. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "lvgl.h"
#include "ui_internal.h"
#include "alarms.h"
#include "gauge_state.h"

static uint16_t fb[466 * 466];
static reading_t R;
static ui_status_t S;

static void flush(lv_display_t *d, const lv_area_t *a, uint8_t *px)
{
  int w = lv_area_get_width(a);
  for (int y = a->y1; y <= a->y2; y++)
    memcpy(&fb[y * 466 + a->x1], px + (size_t)(y - a->y1) * w * 2, (size_t)w * 2);
  lv_display_flush_ready(d);
}
static void lk(void) {}
static bool get_r(reading_t *o) { *o = R; return true; }
static void get_s(ui_status_t *o) { *o = S; }
static void bright(int p) { printf("brightness %d\n", p); }

static void run(int ms) { for (int t = 0; t < ms; t += 10) { lv_tick_inc(10); R.t_ms += 10; lv_timer_handler(); } }
static const char *s_out = ".";
static void dump(const char *name)
{
  char path[256];
  snprintf(path, sizeof path, "%s/%s.raw", s_out, name);
  run(200);
  lv_obj_invalidate(lv_screen_active());
  lv_refr_now(NULL);
  FILE *f = fopen(path, "wb"); fwrite(fb, 2, 466 * 466, f); fclose(f);
}

int main(int argc, char **argv)
{
  if (argc > 1) s_out = argv[1];
  lv_init();
  lv_display_t *d = lv_display_create(466, 466);
  static uint8_t buf[466 * 466 * 2];
  lv_display_set_buffers(d, buf, NULL, sizeof buf, LV_DISPLAY_RENDER_MODE_FULL);
  lv_display_set_flush_cb(d, flush);

  ui_hooks_t h = { .lock = lk, .unlock = lk, .get_reading = get_r, .get_status = get_s, .set_brightness = bright };
  afr_settings_t s; settings_defaults(&s); s.show_afr = true; s.cal_valid = false;
  lambda_range_t rg = conv_range_default();
  ui_splash(&h, "v1.0.0"); dump("splash");

  S.log_state = 1; strcpy(S.log_name, "20261026_090507.csv"); strcpy(S.gauge_fw, "1.0.0");
  S.spartan_seen = true; strcpy(S.spartan_fw, "1.03"); strcpy(S.spartan_hw, "2"); S.sensor_type = 0;
  S.spartan_perf = 0; S.spartan_slowheat = 0; S.cal_a = 208.6e-6f; S.cal_b = 0.0042f; S.cal_src = 2; S.rtc_valid = true;

  memset(&R, 0, sizeof R);
  R.t_ms = 100000; R.state = ST_READY; R.lambda = R.lambda_disp = 0.87f; R.afr = 0.87f * 14.7f; R.v_out = 1.397f;
  R.flags = FLAG_CAL_FRESH | FLAG_TPS_OK | FLAG_UNDER_LOAD; R.peak_rich = 0.80f; R.peak_lean = 0.86f;
  R.led_pattern = LED_STEADY; R.supply_v = NAN; R.tps = 100.0f; R.tps_v = 4.40f;
  ui_start(&h, &s, rg);
  run(500); dump("ready");

  R.lambda = R.lambda_disp = 1.004f; run(300); dump("ready_stoich");

  s.primary_unit = UNIT_AFR; s.fuel = FUEL_E10; s.show_afr = false; ui_set_settings(&s);
  R.lambda = R.lambda_disp = 0.912f; run(300); dump("ready_afr");
  s.primary_unit = UNIT_LAMBDA; s.fuel = FUEL_PETROL; s.show_afr = false; ui_set_settings(&s);

  R.state = ST_WARMING; R.state_since_ms = R.t_ms - 23000; R.lambda = R.lambda_disp = NAN; run(300); dump("warming");
  R.state = ST_SEQUENCER; R.seq_progress = 40; S.rtc_valid = false; run(300); dump("calibrating");
  R.state = ST_WAIT_EXHAUST; run(300); dump("wait");
  R.state = ST_LEAN_RAIL; R.lambda = 1.36f; R.lambda_disp = 1.36f; run(300); dump("lean_rail");
  R.state = ST_FAULT; R.fault = FAULT_ADC_NOT_FOUND; run(300); dump("fault");
  R.state = ST_READY; R.lambda = R.lambda_disp = 0.95f; R.alarm = ALARM_LEAN_LOAD; S.rtc_valid = true;
  R.flags |= FLAG_EVENT_PENDING; R.event = (alarm_event_t){ ALARM_LEAN_LOAD, 0.96f, 100.0f, 0, 400 };
  run(250); dump("warn"); run(125); dump("alarm_danger");
  R.alarm = ALARM_NONE; R.lambda = R.lambda_disp = 1.0f; R.tps = 0.0f; R.flags &= ~FLAG_UNDER_LOAD;
  run(300); dump("event_latched");
  R.flags &= ~FLAG_EVENT_PENDING; R.flags &= ~FLAG_TPS_OK; R.tps = NAN; run(300); dump("no_tps");
  R.flags |= FLAG_TPS_OK; R.tps = 100.0f; R.flags |= FLAG_UNDER_LOAD;

  for (int i = 0; i < 300; i++) {
    R.lambda = R.lambda_disp = 1.0f + 0.12f * sinf((float)i / 15.0f) - (i > 200 ? 0.15f : 0.0f);
    ui_chart_push(&R);
  }
  lv_screen_load(g_ui.scr_chart); run(100); dump("chart");
  ui_settings_refresh(); lv_screen_load(g_ui.scr_settings); dump("settings");
  ui_service_refresh(); lv_screen_load(g_ui.scr_service); dump("service");
  ui_clock_load(); lv_screen_load(g_ui.scr_clock); dump("clock");
  return 0;
}
