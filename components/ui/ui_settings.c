/* Settings, Service and Clock screens. */
#include "ui_internal.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

#include "calibration.h"
#include "gauge_state.h"
#include "led_decoder.h"
#include "spartan_proto.h"

/* ---------------------------------------------------------------- Settings */

typedef struct {
  const char *name;
  void (*fmt)(char *buf, size_t n);
  void (*step)(int dir);
} row_def_t;

static void fx(char *b, size_t n, float v, int d)
{
  int scale = d == 1 ? 10 : d == 2 ? 100 : 1000;
  long q = lroundf(v * (float)scale);
  snprintf(b, n, "%ld.%0*ld", q / scale, d, q % scale);
}

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static int wrap(int v, int n) { return (v % n + n) % n; }

static void f_fuel(char *b, size_t n) { snprintf(b, n, "%s", conv_fuel_name((fuel_t)g_ui.s.fuel)); }
static void s_fuel(int d) { g_ui.s.fuel = (uint8_t)wrap(g_ui.s.fuel + d, FUEL_COUNT); }
static void f_custom(char *b, size_t n) { fx(b, n, g_ui.s.fuel_custom, 2); }
static void s_custom(int d) { g_ui.s.fuel_custom = clampf(g_ui.s.fuel_custom + 0.05f * (float)d, 5.0f, 20.0f); }
static void f_unit(char *b, size_t n) { snprintf(b, n, "%s", g_ui.s.primary_unit == UNIT_AFR ? "AFR" : "\xCE\xBB"); }
static void s_unit(int d) { (void)d; g_ui.s.primary_unit = g_ui.s.primary_unit == UNIT_AFR ? UNIT_LAMBDA : UNIT_AFR; }
static void f_show(char *b, size_t n) { snprintf(b, n, "%s", g_ui.s.show_afr ? "On" : "Off"); }
static void s_show(int d) { (void)d; g_ui.s.show_afr = !g_ui.s.show_afr; }
static void f_lean(char *b, size_t n) { fx(b, n, g_ui.s.warn_lean, 2); }
static void s_lean(int d) { g_ui.s.warn_lean = clampf(g_ui.s.warn_lean + 0.01f * (float)d, 1.00f, 1.30f); }
static void f_lean_t(char *b, size_t n) { fx(b, n, (float)g_ui.s.warn_lean_ms / 1000.0f, 1); strlcat(b, " s", n); }
static void s_lean_t(int d) { g_ui.s.warn_lean_ms = (uint16_t)clampi(g_ui.s.warn_lean_ms + 100 * d, 200, 5000); }
static void f_rich(char *b, size_t n) { fx(b, n, g_ui.s.warn_rich, 2); }
static void s_rich(int d) { g_ui.s.warn_rich = clampf(g_ui.s.warn_rich + 0.01f * (float)d, 0.68f, 0.95f); }
static void f_rich_t(char *b, size_t n) { fx(b, n, (float)g_ui.s.warn_rich_ms / 1000.0f, 1); strlcat(b, " s", n); }
static void s_rich_t(int d) { g_ui.s.warn_rich_ms = (uint16_t)clampi(g_ui.s.warn_rich_ms + 100 * d, 200, 5000); }
static void f_smooth(char *b, size_t n) { snprintf(b, n, "%u ms", g_ui.s.smooth_ms); }
static void s_smooth(int d) { g_ui.s.smooth_ms = (uint16_t)clampi(g_ui.s.smooth_ms + 25 * d, 0, 500); }
static void f_rate(char *b, size_t n) { snprintf(b, n, "%u Hz", g_ui.s.log_rate_hz); }
static void s_rate(int d)
{
  static const uint8_t r[] = { 10, 20, 50 };
  int i = g_ui.s.log_rate_hz == 10 ? 0 : g_ui.s.log_rate_hz == 20 ? 1 : 2;
  g_ui.s.log_rate_hz = r[clampi(i + d, 0, 2)];
}
static void f_day(char *b, size_t n) { snprintf(b, n, "%u %%", g_ui.s.bright_day); }
static void s_day(int d) { g_ui.s.bright_day = (uint8_t)clampi(g_ui.s.bright_day + 5 * d, 5, 100); }
static void f_night(char *b, size_t n) { snprintf(b, n, "%u %%", g_ui.s.bright_night); }
static void s_night(int d) { g_ui.s.bright_night = (uint8_t)clampi(g_ui.s.bright_night + 5 * d, 5, 100); }

/* Spartan modes live in the Spartan, not in NVS. */
static void f_perf(char *b, size_t n)
{
  static const char *const t[] = { "20 ms", "10 ms", "Lean" };
  int p = g_ui.st.spartan_perf;
  snprintf(b, n, "%s", p >= 0 && p <= 2 ? t[p] : "?");
}
static void s_perf(int d)
{
  int p = g_ui.st.spartan_perf < 0 ? 0 : g_ui.st.spartan_perf;
  if (g_ui.hooks.spartan_set_perf) g_ui.hooks.spartan_set_perf(wrap(p + d, 3));
}
static void f_heat(char *b, size_t n)
{
  int h = g_ui.st.spartan_slowheat;
  snprintf(b, n, "%s", h == SP_SLOWHEAT_WAIT ? "Wait" : h == SP_SLOWHEAT_OFF ? "Off" : "?");
}
static void s_heat(int d)
{
  (void)d;
  int h = g_ui.st.spartan_slowheat == SP_SLOWHEAT_WAIT ? SP_SLOWHEAT_OFF : SP_SLOWHEAT_WAIT;
  if (g_ui.hooks.spartan_set_slowheat) g_ui.hooks.spartan_set_slowheat(h);
}

static const row_def_t k_rows[] = {
  { "Fuel", f_fuel, s_fuel },
  { "Custom stoich", f_custom, s_custom },
  { "Main unit", f_unit, s_unit },
  { "AFR line", f_show, s_show },
  { "Lean warn \xCE\xBB", f_lean, s_lean },
  { "Lean warn time", f_lean_t, s_lean_t },
  { "Rich warn \xCE\xBB", f_rich, s_rich },
  { "Rich warn time", f_rich_t, s_rich_t },
  { "Smoothing", f_smooth, s_smooth },
  { "Log rate", f_rate, s_rate },
  { "Day bright", f_day, s_day },
  { "Night bright", f_night, s_night },
  { "Spartan resp.", f_perf, s_perf },
  { "Spartan heat-up", f_heat, s_heat },
};
#define N_ROWS (sizeof k_rows / sizeof k_rows[0])
#define ROW_SPARTAN_FIRST 12

static lv_obj_t *s_values[N_ROWS];

static void step_cb(lv_event_t *e)
{
  intptr_t code = (intptr_t)lv_event_get_user_data(e);
  int row = (int)(code >> 1);
  int dir = (code & 1) ? 1 : -1;
  k_rows[row].step(dir);
  if (row < ROW_SPARTAN_FIRST) ui_notify_settings();
  ui_settings_refresh();
}

static void go_cb(lv_event_t *e)
{
  lv_obj_t **to = lv_event_get_user_data(e);
  if (*to) ui_go(*to, true);
}

static void back_main_cb(lv_event_t *e)
{
  (void)e;
  ui_go(g_ui.scr_main, true);
}

static lv_obj_t *column(lv_obj_t *scr)
{
  /* A scrolling column inside the round screen. */
  lv_obj_t *col = lv_obj_create(scr);
  lv_obj_remove_style_all(col);
  lv_obj_set_size(col, 360, 400);
  lv_obj_center(col);
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(col, 8, 0);
  lv_obj_set_style_pad_top(col, 40, 0);
  lv_obj_set_style_pad_bottom(col, 140, 0); /* the last rows can reach the middle */
  lv_obj_set_scroll_dir(col, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(col, LV_SCROLLBAR_MODE_OFF);
  return col;
}

void ui_settings_create(void)
{
  lv_obj_t *scr = g_ui.scr_settings = ui_screen_new();
  lv_obj_t *col = column(scr);
  ui_label(col, &afr_font_28, COL_TEXT, "Settings");

  for (unsigned i = 0; i < N_ROWS; i++) {
    lv_obj_t *row = lv_obj_create(col);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, 300, 52);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(row, LV_OBJ_FLAG_EVENT_BUBBLE | LV_OBJ_FLAG_GESTURE_BUBBLE);

    lv_obj_t *name = ui_label(row, &afr_font_20, COL_GREY, k_rows[i].name);
    lv_obj_align(name, LV_ALIGN_LEFT_MID, 0, -12);
    s_values[i] = ui_label(row, &afr_font_24, COL_TEXT, "");
    lv_obj_align(s_values[i], LV_ALIGN_LEFT_MID, 0, 12);

    lv_obj_t *minus = ui_button(row, "-", 56, 44, step_cb, (void *)(intptr_t)(i << 1));
    lv_obj_align(minus, LV_ALIGN_RIGHT_MID, -64, 0);
    lv_obj_t *plus = ui_button(row, "+", 56, 44, step_cb, (void *)(intptr_t)((i << 1) | 1));
    lv_obj_align(plus, LV_ALIGN_RIGHT_MID, 0, 0);
  }

  lv_obj_t *clock = ui_button(col, "Clock", 200, 48, NULL, NULL);
  lv_obj_t *svc = ui_button(col, "Service", 200, 48, NULL, NULL);
  ui_button(col, "Back", 200, 48, back_main_cb, NULL);
  /* Those screens are created after this one: pass where they will be. */
  lv_obj_add_event_cb(clock, go_cb, LV_EVENT_CLICKED, &g_ui.scr_clock);
  lv_obj_add_event_cb(svc, go_cb, LV_EVENT_CLICKED, &g_ui.scr_service);
}

void ui_settings_refresh(void)
{
  char b[32];
  for (unsigned i = 0; i < N_ROWS; i++) {
    k_rows[i].fmt(b, sizeof b);
    if (strcmp(b, lv_label_get_text(s_values[i])) != 0) lv_label_set_text(s_values[i], b);
  }
}

/* ----------------------------------------------------------------- Service */

static lv_obj_t *s_info;
static lv_obj_t *s_check_btn[3];
static int s_armed = -1;
static uint32_t s_armed_at;
static const float k_check_v[3] = { 1.0f, 2.5f, 4.0f };

static void back_settings_cb(lv_event_t *e)
{
  (void)e;
  ui_go(g_ui.scr_settings, false);
}

static void check_cb(lv_event_t *e)
{
  int i = (int)(intptr_t)lv_event_get_user_data(e);
  uint32_t now = lv_tick_get();
  char b[32];
  if (s_armed == i && now - s_armed_at < 3000) {
    /* Second tap: send. The Spartan holds it until it reboots. */
    if (g_ui.hooks.spartan_set_linout) g_ui.hooks.spartan_set_linout(k_check_v[i]);
    s_armed = -1;
    fx(b, sizeof b, k_check_v[i], 1);
    strlcat(b, " V sent", sizeof b);
  } else {
    s_armed = i;
    s_armed_at = now;
    snprintf(b, sizeof b, "Tap again");
  }
  lv_label_set_text(lv_obj_get_child(s_check_btn[i], 0), b);
}

void ui_service_create(void)
{
  lv_obj_t *scr = g_ui.scr_service = ui_screen_new();
  lv_obj_t *col = column(scr);
  ui_label(col, &afr_font_28, COL_TEXT, "Service");

  s_info = ui_label(col, &afr_font_20, COL_TEXT, "");
  lv_obj_set_width(s_info, 330);
  lv_label_set_long_mode(s_info, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(s_info, LV_TEXT_ALIGN_CENTER, 0);

  lv_obj_t *note = ui_label(col, &afr_font_20, COL_LEAN,
                            "ADC check: forces the Spartan output until it reboots. "
                            "Compare v_out with a multimeter on the green wire.");
  lv_obj_set_width(note, 300);
  lv_label_set_long_mode(note, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(note, LV_TEXT_ALIGN_CENTER, 0);
  for (int i = 0; i < 3; i++) {
    char b[16];
    fx(b, sizeof b, k_check_v[i], 1);
    strlcat(b, " V", sizeof b);
    s_check_btn[i] = ui_button(col, b, 200, 48, check_cb, (void *)(intptr_t)i);
  }
  ui_button(col, "Back", 200, 48, back_settings_cb, NULL);
}

void ui_service_refresh(void)
{
  const ui_status_t *st = &g_ui.st;
  const reading_t *r = &g_ui.r;
  static const char *const src[] = { "NOMINAL", "STORED", "FRESH" };
  static const char *const logs[] = { "off", "OK", "no card", "error" };
  char v[16], lam[16], a[16], b[16], l0[16], l5[16], sup[16];

  fx(v, sizeof v, r->v_out, 3);
  if (isnan(r->lambda)) snprintf(lam, sizeof lam, "-");
  else fx(lam, sizeof lam, r->lambda, 3);
  fx(a, sizeof a, st->cal_a * 1e6f, 2);
  fx(b, sizeof b, st->cal_b * 1000.0f, 1);
  fx(l0, sizeof l0, g_ui.range.lam0, 3);
  fx(l5, sizeof l5, g_ui.range.lam5, 3);
  if (isnan(r->supply_v)) snprintf(sup, sizeof sup, "not fitted");
  else { fx(sup, sizeof sup, r->supply_v, 1); strlcat(sup, " V", sizeof sup); }

  char t[640];
  snprintf(t, sizeof t,
           "Gauge %s\n"
           "Spartan FW %s  HW %s\n"
           "Sensor %s\n"
           "\xCE\xBB" " at 0 V %s, 5 V %s\n"
           "Cal %s%s\na %s uV/code, b %s mV\n"
           "State %s  LED %s\n"
           "v_out %s V  \xCE\xBB %s\n"
           "Supply %s\n"
           "SD %s %s\n"
           "Clock %s  Battery %s",
           st->gauge_fw,
           st->spartan_seen ? st->spartan_fw : "-", st->spartan_seen ? st->spartan_hw : "-",
           st->sensor_type == 0 ? "LSU 4.9" : st->sensor_type == 1 ? "LSU ADV (check!)" : "unknown",
           l0, l5,
           st->cal_src < 3 ? src[st->cal_src] : "?", st->cal_rejected ? " (last fit rejected)" : "",
           a, b,
           gs_state_name(r->state), led_pattern_name((led_pattern_t)r->led_pattern),
           v, lam, sup,
           st->log_state < 4 ? logs[st->log_state] : "?", st->log_name,
           st->rtc_valid ? "set" : "NOT SET", st->battery ? "yes" : "no");
  if (strcmp(t, lv_label_get_text(s_info)) != 0) lv_label_set_text(s_info, t);

  if (s_armed >= 0 && lv_tick_get() - s_armed_at >= 3000) {
    char lbl[16];
    fx(lbl, sizeof lbl, k_check_v[s_armed], 1);
    strlcat(lbl, " V", sizeof lbl);
    lv_label_set_text(lv_obj_get_child(s_check_btn[s_armed], 0), lbl);
    s_armed = -1;
  }
}

/* ------------------------------------------------------------------- Clock */

#define YEAR0 2025
static lv_obj_t *s_roll[5]; /* year, month, day, hour, minute */
static lv_obj_t *s_clock_msg;

static lv_obj_t *roller(lv_obj_t *parent, int from, int to, bool pad2)
{
  static char opts[5][400];
  static int n;
  char *o = opts[n++ % 5];
  o[0] = '\0';
  for (int i = from; i <= to; i++) {
    char b[8];
    snprintf(b, sizeof b, pad2 ? "%02d%s" : "%d%s", i, i < to ? "\n" : "");
    strlcat(o, b, 400);
  }
  lv_obj_t *r = lv_roller_create(parent);
  lv_roller_set_options(r, o, LV_ROLLER_MODE_NORMAL);
  lv_roller_set_visible_row_count(r, 3);
  lv_obj_set_style_text_font(r, &afr_font_24, 0);
  lv_obj_set_style_bg_color(r, lv_color_hex(COL_BTN), 0);
  lv_obj_set_style_text_color(r, lv_color_hex(COL_TEXT), 0);
  lv_obj_set_style_border_width(r, 0, 0);
  lv_obj_set_style_bg_color(r, lv_color_hex(0x505050), LV_PART_SELECTED);
  return r;
}

static void set_clock_cb(lv_event_t *e)
{
  (void)e;
  struct tm tm = { 0 };
  tm.tm_year = YEAR0 + (int)lv_roller_get_selected(s_roll[0]) - 1900;
  tm.tm_mon = (int)lv_roller_get_selected(s_roll[1]);
  tm.tm_mday = (int)lv_roller_get_selected(s_roll[2]) + 1;
  tm.tm_hour = (int)lv_roller_get_selected(s_roll[3]);
  tm.tm_min = (int)lv_roller_get_selected(s_roll[4]);
  struct tm chk = tm;
  time_t tt = mktime(&chk); /* normalises 31 February and fills the weekday */
  localtime_r(&tt, &tm);
  if (g_ui.hooks.set_clock) g_ui.hooks.set_clock(&tm);
  lv_label_set_text(s_clock_msg, "Clock set");
}

void ui_clock_create(void)
{
  lv_obj_t *scr = g_ui.scr_clock = ui_screen_new();
  lv_obj_t *t = ui_label(scr, &afr_font_28, COL_TEXT, "Clock");
  lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 60);
  lv_obj_t *h = ui_label(scr, &afr_font_20, COL_GREY, "year  month  day    hour  min");
  lv_obj_align(h, LV_ALIGN_TOP_MID, 0, 110);

  lv_obj_t *row = lv_obj_create(scr);
  lv_obj_remove_style_all(row);
  lv_obj_set_size(row, 400, 140);
  lv_obj_align(row, LV_ALIGN_CENTER, 0, -10);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(row, 6, 0);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

  s_roll[0] = roller(row, YEAR0, YEAR0 + 24, false);
  s_roll[1] = roller(row, 1, 12, true);
  s_roll[2] = roller(row, 1, 31, true);
  s_roll[3] = roller(row, 0, 23, true);
  s_roll[4] = roller(row, 0, 59, true);

  lv_obj_t *set = ui_button(scr, "Set", 160, 50, set_clock_cb, NULL);
  lv_obj_align(set, LV_ALIGN_CENTER, -90, 110);
  lv_obj_t *back = ui_button(scr, "Back", 160, 50, back_settings_cb, NULL);
  lv_obj_align(back, LV_ALIGN_CENTER, 90, 110);
  s_clock_msg = ui_label(scr, &afr_font_20, COL_GREY, "");
  lv_obj_align(s_clock_msg, LV_ALIGN_BOTTOM_MID, 0, -60);
}

void ui_clock_load(void)
{
  time_t now = time(NULL);
  struct tm tm;
  localtime_r(&now, &tm);
  if (!g_ui.st.rtc_valid || tm.tm_year + 1900 < YEAR0) {
    memset(&tm, 0, sizeof tm);
    tm.tm_year = 2026 - 1900;
    tm.tm_mday = 1;
  }
  int y = clampi(tm.tm_year + 1900 - YEAR0, 0, 24);
  lv_roller_set_selected(s_roll[0], (uint32_t)y, LV_ANIM_OFF);
  lv_roller_set_selected(s_roll[1], (uint32_t)tm.tm_mon, LV_ANIM_OFF);
  lv_roller_set_selected(s_roll[2], (uint32_t)(tm.tm_mday - 1), LV_ANIM_OFF);
  lv_roller_set_selected(s_roll[3], (uint32_t)tm.tm_hour, LV_ANIM_OFF);
  lv_roller_set_selected(s_roll[4], (uint32_t)tm.tm_min, LV_ANIM_OFF);
  lv_label_set_text(s_clock_msg, g_ui.st.rtc_valid ? "" : "Clock not set");
}
