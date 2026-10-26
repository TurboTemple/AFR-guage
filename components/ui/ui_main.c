/* Main screen: a large lambda (or AFR) number in the centre and a 270 deg
 * arc showing where the mixture sits between rich and lean. When no number
 * is valid the centre shows the state in words. Pure black background: on
 * AMOLED that is pixels off. */
#include "ui_internal.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "gauge_state.h"

#define ARC_SIZE     450   /* ring outer radius 225 */
#define ARC_W        20    /* ring 205-225 */
#define RING_R       215.0f
#define MARKER       30
#define TICK_R_IN    201.0f
#define TICK_R_OUT   229.0f
#define TICK_W       4
#define NUM_BASELINE 268   /* baseline of the main number */
#define NUM_MS       100   /* digits at 10 Hz */
#define FLASH_MS     250   /* warning flash at 2 Hz */
#define SHIFT_MS     (5u * 60u * 1000u)
#define DIM_AFTER_MS (2u * 60u * 1000u)
#define DIM_PERCENT  10

static struct {
  lv_obj_t *root;
  lv_obj_t *arc_layer;
  lv_obj_t *zone[3];
  lv_obj_t *band;
  lv_obj_t *marker;
  lv_obj_t *tick[2];
  lv_point_precise_t tick_pts[2][2];
  float     tick_lambda[2];
  lv_obj_t *num, *unit, *second;
  lv_obj_t *centre, *centre_sub, *progress;
  lv_obj_t *dot, *log_lbl, *cal_lbl, *hint;
  lv_obj_t *peaks;

  int       shown_state;
  float     marker_angle;
  uint32_t  last_num_ms;
  uint32_t  last_flash_ms;
  bool      flash_on;
  uint32_t  last_shift_ms;
  unsigned  shift_idx;
  int       brightness;
} m;

/* Fixed-point text without float printf. */
static void fmt_fixed(char *buf, size_t n, float v, int decimals)
{
  int scale = decimals == 1 ? 10 : decimals == 2 ? 100 : 1000;
  long q = lroundf(fabsf(v) * (float)scale);
  snprintf(buf, n, "%s%ld.%0*ld", v < 0 ? "-" : "", q / scale, decimals, q % scale);
}

static lv_obj_t *make_arc(lv_obj_t *parent, uint32_t color, lv_opa_t opa)
{
  lv_obj_t *a = lv_arc_create(parent);
  lv_obj_remove_style_all(a);
  lv_obj_set_size(a, ARC_SIZE, ARC_SIZE);
  lv_obj_center(a);
  lv_obj_remove_flag(a, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_arc_width(a, ARC_W, LV_PART_MAIN);
  lv_obj_set_style_arc_color(a, lv_color_hex(color), LV_PART_MAIN);
  lv_obj_set_style_arc_opa(a, opa, LV_PART_MAIN);
  lv_obj_set_style_arc_rounded(a, false, LV_PART_MAIN);
  return a;
}

static void set_arc_span(lv_obj_t *a, float lam_from, float lam_to)
{
  float a0 = ui_angle(lam_from), a1 = ui_angle(lam_to);
  if (a1 - a0 < 0.5f) {
    lv_obj_add_flag(a, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  lv_obj_remove_flag(a, LV_OBJ_FLAG_HIDDEN);
  lv_arc_set_bg_angles(a, (lv_value_precise_t)lroundf(a0), (lv_value_precise_t)lroundf(a1));
}

static void polar(float angle_deg, float r, int32_t *x, int32_t *y)
{
  float rad = angle_deg * (float)M_PI / 180.0f;
  *x = (int32_t)lroundf((float)UI_CX + r * cosf(rad));
  *y = (int32_t)lroundf((float)UI_CY + r * sinf(rad));
}

/* A radial tick as a small line object, so moving it only redraws its own
 * few pixels. */
static void place_tick(int i, float lambda)
{
  lv_obj_t *t = m.tick[i];
  if (isnan(lambda)) {
    lv_obj_add_flag(t, LV_OBJ_FLAG_HIDDEN);
    m.tick_lambda[i] = NAN;
    return;
  }
  if (!isnan(m.tick_lambda[i]) && fabsf(lambda - m.tick_lambda[i]) < 0.002f &&
      !lv_obj_has_flag(t, LV_OBJ_FLAG_HIDDEN)) {
    return;
  }
  m.tick_lambda[i] = lambda;
  float ang = ui_angle(lambda);
  int32_t x1, y1, x2, y2;
  polar(ang, TICK_R_IN, &x1, &y1);
  polar(ang, TICK_R_OUT, &x2, &y2);
  int32_t ox = (x1 < x2 ? x1 : x2) - TICK_W, oy = (y1 < y2 ? y1 : y2) - TICK_W;
  m.tick_pts[i][0].x = x1 - ox;
  m.tick_pts[i][0].y = y1 - oy;
  m.tick_pts[i][1].x = x2 - ox;
  m.tick_pts[i][1].y = y2 - oy;
  lv_obj_set_pos(t, ox, oy);
  lv_obj_set_size(t, LV_ABS(x2 - x1) + 2 * TICK_W, LV_ABS(y2 - y1) + 2 * TICK_W);
  lv_line_set_points(t, m.tick_pts[i], 2);
  lv_obj_remove_flag(t, LV_OBJ_FLAG_HIDDEN);
}

static void place_marker(float angle)
{
  if (fabsf(angle - m.marker_angle) < 0.2f) return;
  m.marker_angle = angle;
  int32_t x, y;
  polar(angle, RING_R, &x, &y);
  lv_obj_set_pos(m.marker, x - MARKER / 2, y - MARKER / 2);
}

static void tap_cb(lv_event_t *e)
{
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_SHORT_CLICKED) {
    g_ui.s.show_afr = !g_ui.s.show_afr;
    ui_notify_settings();
  } else if (code == LV_EVENT_LONG_PRESSED) {
    if (g_ui.hooks.reset_peaks) g_ui.hooks.reset_peaks();
    place_tick(0, NAN);
    place_tick(1, NAN);
  }
}

void ui_main_create(void)
{
  lv_obj_t *scr = g_ui.scr_main = ui_screen_new();
  lv_obj_add_event_cb(scr, tap_cb, LV_EVENT_SHORT_CLICKED, NULL);
  lv_obj_add_event_cb(scr, tap_cb, LV_EVENT_LONG_PRESSED, NULL);

  /* Everything sits in root so the burn-in shift moves it as one. */
  m.root = lv_obj_create(scr);
  lv_obj_remove_style_all(m.root);
  lv_obj_set_size(m.root, UI_W, UI_H);
  lv_obj_remove_flag(m.root, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

  m.arc_layer = lv_obj_create(m.root);
  lv_obj_remove_style_all(m.arc_layer);
  lv_obj_set_size(m.arc_layer, UI_W, UI_H);
  lv_obj_remove_flag(m.arc_layer, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

  m.zone[0] = make_arc(m.arc_layer, COL_RICH, LV_OPA_40);
  m.zone[1] = make_arc(m.arc_layer, COL_STOICH, LV_OPA_40);
  m.zone[2] = make_arc(m.arc_layer, COL_LEAN, LV_OPA_40);
  m.band = make_arc(m.arc_layer, COL_STOICH, LV_OPA_COVER);

  for (int i = 0; i < 2; i++) {
    m.tick[i] = lv_line_create(m.arc_layer);
    lv_obj_set_style_line_width(m.tick[i], TICK_W, 0);
    lv_obj_set_style_line_color(m.tick[i], lv_color_hex(COL_TEXT), 0);
    lv_obj_set_style_line_rounded(m.tick[i], true, 0);
    lv_obj_add_flag(m.tick[i], LV_OBJ_FLAG_HIDDEN);
    m.tick_lambda[i] = NAN;
  }

  m.marker = lv_obj_create(m.arc_layer);
  lv_obj_remove_style_all(m.marker);
  lv_obj_set_size(m.marker, MARKER, MARKER);
  lv_obj_set_style_radius(m.marker, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(m.marker, lv_color_hex(COL_TEXT), 0);
  lv_obj_set_style_bg_opa(m.marker, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(m.marker, lv_color_hex(COL_BG), 0);
  lv_obj_set_style_border_width(m.marker, 4, 0);
  lv_obj_remove_flag(m.marker, LV_OBJ_FLAG_CLICKABLE);
  m.marker_angle = -1000.0f;

  /* Main number: fixed width so a change only redraws this box. */
  m.num = ui_label(m.root, &afr_font_num, COL_TEXT, "");
  lv_obj_set_width(m.num, 380);
  lv_obj_set_style_text_align(m.num, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_pos(m.num, UI_CX - 190,
                 NUM_BASELINE - (afr_font_num.line_height - afr_font_num.base_line));

  m.unit = ui_label(m.root, &afr_font_24, COL_GREY, "");
  lv_obj_set_width(m.unit, 260);
  lv_obj_set_style_text_align(m.unit, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_pos(m.unit, UI_CX - 130, NUM_BASELINE + 14);

  m.second = ui_label(m.root, &afr_font_28, COL_TEXT, "");
  lv_obj_set_width(m.second, 260);
  lv_obj_set_style_text_align(m.second, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_pos(m.second, UI_CX - 130, NUM_BASELINE + 46);

  m.centre = ui_label(m.root, &afr_font_40, COL_TEXT, "");
  lv_obj_set_width(m.centre, 330);
  lv_label_set_long_mode(m.centre, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(m.centre, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(m.centre, LV_ALIGN_CENTER, 0, -20);

  m.centre_sub = ui_label(m.root, &afr_font_24, COL_GREY, "");
  lv_obj_set_width(m.centre_sub, 300);
  lv_obj_set_style_text_align(m.centre_sub, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(m.centre_sub, LV_ALIGN_CENTER, 0, 45);

  /* Sequencer progress runs along the gauge's own track, which is hidden
   * while calibrating. */
  m.progress = lv_arc_create(m.root);
  lv_obj_remove_style_all(m.progress);
  lv_obj_set_size(m.progress, ARC_SIZE, ARC_SIZE);
  lv_obj_center(m.progress);
  lv_obj_remove_flag(m.progress, LV_OBJ_FLAG_CLICKABLE);
  lv_arc_set_rotation(m.progress, 135);
  lv_arc_set_bg_angles(m.progress, 0, 270);
  lv_arc_set_range(m.progress, 0, 100);
  lv_obj_set_style_arc_width(m.progress, ARC_W, LV_PART_MAIN);
  lv_obj_set_style_arc_color(m.progress, lv_color_hex(COL_DIM), LV_PART_MAIN);
  lv_obj_set_style_arc_opa(m.progress, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_arc_rounded(m.progress, false, LV_PART_MAIN);
  lv_obj_set_style_arc_width(m.progress, ARC_W, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(m.progress, lv_color_hex(COL_RICH), LV_PART_INDICATOR);
  lv_obj_set_style_arc_opa(m.progress, LV_OPA_COVER, LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(m.progress, false, LV_PART_INDICATOR);

  /* Top status line: state dot, log state, calibration source. */
  m.dot = lv_obj_create(m.root);
  lv_obj_remove_style_all(m.dot);
  lv_obj_set_size(m.dot, 14, 14);
  lv_obj_set_style_radius(m.dot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(m.dot, LV_OPA_COVER, 0);
  lv_obj_set_pos(m.dot, UI_CX - 104, 69);
  m.log_lbl = ui_label(m.root, &afr_font_20, COL_GREY, "");
  lv_obj_set_pos(m.log_lbl, UI_CX - 82, 62);
  m.cal_lbl = ui_label(m.root, &afr_font_20, COL_GREY, "");
  lv_obj_set_pos(m.cal_lbl, UI_CX + 8, 62);
  m.hint = ui_label(m.root, &afr_font_20, COL_LEAN, "");
  lv_obj_set_width(m.hint, 240);
  lv_obj_set_style_text_align(m.hint, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_pos(m.hint, UI_CX - 120, 90);

  m.peaks = ui_label(m.root, &afr_font_22, COL_GREY, "");
  lv_obj_set_width(m.peaks, 280);
  lv_obj_set_style_text_align(m.peaks, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_pos(m.peaks, UI_CX - 140, 372);

  m.shown_state = -1;
  m.brightness = -1;
}

void ui_main_apply(void)
{
  set_arc_span(m.zone[0], g_ui.range.lam0, 0.85f);
  set_arc_span(m.zone[1], 0.85f, 1.05f);
  set_arc_span(m.zone[2], 1.05f, g_ui.range.lam5);
  set_arc_span(m.band, 0.98f, 1.02f);
  m.marker_angle = -1000.0f;
  float t0 = m.tick_lambda[0], t1 = m.tick_lambda[1];
  m.tick_lambda[0] = m.tick_lambda[1] = NAN;
  place_tick(0, t0);
  place_tick(1, t1);

  char u[32];
  if (g_ui.s.primary_unit == UNIT_AFR) {
    snprintf(u, sizeof u, "AFR \xC2\xB7 %s", conv_fuel_name((fuel_t)g_ui.s.fuel));
  } else {
    snprintf(u, sizeof u, "\xCE\xBB");
  }
  lv_label_set_text(m.unit, u);
  m.shown_state = -1; /* redraw everything on the next tick */
  m.last_num_ms = 0;
}

static uint32_t state_colour(gauge_state_t s)
{
  switch (s) {
  case ST_READY: case ST_RICH_RAIL: case ST_LEAN_RAIL: return COL_STOICH;
  case ST_SEQUENCER: return COL_RICH;
  case ST_WARMING: case ST_WAIT_EXHAUST: case ST_TOO_HOT: return COL_LEAN;
  case ST_FAULT: return COL_WARN;
  default: return COL_GREY;
  }
}

static void show(lv_obj_t *o, bool on)
{
  if (on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

/* Visibility and fixed texts for a state; runs on state change only. */
static void enter_state(gauge_state_t s)
{
  bool number = s == ST_READY;
  bool arc = s == ST_READY || s == ST_RICH_RAIL || s == ST_LEAN_RAIL || s == ST_TOO_HOT;
  show(m.num, number);
  show(m.unit, number);
  show(m.second, number && g_ui.s.show_afr);
  show(m.centre, !number);
  show(m.centre_sub, !number);
  show(m.progress, s == ST_SEQUENCER);
  show(m.arc_layer, arc);
  lv_obj_set_style_opa(m.arc_layer, s == ST_TOO_HOT ? LV_OPA_40 : LV_OPA_COVER, 0);
  lv_obj_set_style_text_color(m.centre, lv_color_hex(s == ST_TOO_HOT ? COL_LEAN : COL_TEXT), 0);
  lv_obj_set_style_text_font(m.centre, s == ST_WAIT_EXHAUST ? &afr_font_28 : &afr_font_40, 0);
  lv_obj_set_style_bg_color(m.dot, lv_color_hex(state_colour(s)), 0);

  const char *t = "";
  switch (s) {
  case ST_BOOT: t = ""; break;
  case ST_NO_SPARTAN: t = "ENGINE OFF"; break;
  case ST_SEQUENCER: t = "CALIBRATING"; break;
  case ST_WAIT_EXHAUST: t = "WAITING FOR EXHAUST HEAT"; break;
  case ST_WARMING: t = "WARMING"; break;
  case ST_RICH_RAIL: t = "RICH"; break;
  case ST_LEAN_RAIL: t = "LEAN"; break;
  case ST_TOO_HOT: t = "SENSOR HOT"; break;
  case ST_FAULT: t = "FAULT"; break;
  default: break;
  }
  lv_label_set_text(m.centre, t);
  lv_label_set_text(m.centre_sub, "");
  m.marker_angle = -1000.0f;
}

static void update_number(const reading_t *r)
{
  char buf[24];
  float stoich = settings_stoich(&g_ui.s);
  float lam = r->lambda_disp;
  if (g_ui.s.primary_unit == UNIT_AFR) fmt_fixed(buf, sizeof buf, lam * stoich, 1);
  else fmt_fixed(buf, sizeof buf, lam, 2);
  if (strcmp(buf, lv_label_get_text(m.num)) != 0) lv_label_set_text(m.num, buf);

  if (g_ui.s.show_afr) {
    char v[16];
    if (g_ui.s.primary_unit == UNIT_AFR) {
      fmt_fixed(v, sizeof v, lam, 2);
      snprintf(buf, sizeof buf, "\xCE\xBB %s", v);
    } else {
      fmt_fixed(v, sizeof v, lam * stoich, 1);
      snprintf(buf, sizeof buf, "%s AFR", v);
    }
    if (strcmp(buf, lv_label_get_text(m.second)) != 0) lv_label_set_text(m.second, buf);
  }
}

static void update_texts(const reading_t *r)
{
  char buf[48], a[16], b[16];
  gauge_state_t s = r->state;

  if (s == ST_READY) {
    update_number(r);
  } else if (s == ST_WARMING) {
    uint32_t secs = (r->t_ms - r->state_since_ms) / 1000u;
    snprintf(buf, sizeof buf, "%lu s", (unsigned long)secs);
    lv_label_set_text(m.centre_sub, buf);
  } else if (s == ST_RICH_RAIL) {
    fmt_fixed(a, sizeof a, g_ui.range.lam0, 2);
    snprintf(buf, sizeof buf, "< \xCE\xBB %s", a);
    lv_label_set_text(m.centre_sub, buf);
  } else if (s == ST_LEAN_RAIL) {
    fmt_fixed(a, sizeof a, g_ui.range.lam5, 2);
    snprintf(buf, sizeof buf, "> \xCE\xBB %s", a);
    lv_label_set_text(m.centre_sub, buf);
  } else if (s == ST_FAULT) {
    lv_label_set_text(m.centre_sub, gs_fault_text((fault_reason_t)r->fault));
  } else if (s == ST_SEQUENCER) {
    lv_arc_set_value(m.progress, r->seq_progress);
  }

  /* Status line. */
  const ui_status_t *st = &g_ui.st;
  bool log_ok = st->log_state == 1; /* LOG_STATE_OK */
  lv_label_set_text(m.log_lbl, log_ok ? "LOG" : "NO LOG");
  lv_obj_set_style_text_color(m.log_lbl, lv_color_hex(log_ok ? COL_GREY : COL_WARN), 0);
  const char *cal = (r->flags & FLAG_CAL_FRESH) ? "FRESH" : (r->flags & FLAG_CAL_STORED) ? "STORED" : "NOMINAL";
  lv_label_set_text(m.cal_lbl, cal);

  const char *hint = "";
  if (st->sensor_type > 0) hint = "SENSOR TYPE NOT LSU 4.9";
  else if (r->flags & FLAG_CAL_REJECTED) hint = "CAL REJECTED";
  else if (!st->rtc_valid) hint = "set clock";
  if (strcmp(hint, lv_label_get_text(m.hint)) != 0) lv_label_set_text(m.hint, hint);

  /* Peaks: numbers from READY only; a rich rail is flagged separately. */
  if (isnan(r->peak_rich)) {
    lv_label_set_text(m.peaks, (r->flags & FLAG_RICH_RAIL_SEEN) ? "R RAIL" : "");
  } else {
    fmt_fixed(a, sizeof a, r->peak_rich, 2);
    fmt_fixed(b, sizeof b, r->peak_lean, 2);
    snprintf(buf, sizeof buf, "R %s%s \xC2\xB7 L %s", a,
             (r->flags & FLAG_RICH_RAIL_SEEN) ? "*" : "", b);
    if (strcmp(buf, lv_label_get_text(m.peaks)) != 0) lv_label_set_text(m.peaks, buf);
  }
}

/* Day or night brightness; after 2 minutes of ENGINE OFF, 10 %. */
static void update_brightness(const reading_t *r)
{
  int want = g_ui.s.night ? g_ui.s.bright_night : g_ui.s.bright_day;
  if (r->state == ST_NO_SPARTAN && r->t_ms - r->state_since_ms >= DIM_AFTER_MS) want = DIM_PERCENT;
  if (want != m.brightness) {
    m.brightness = want;
    if (g_ui.hooks.set_brightness) g_ui.hooks.set_brightness(want);
  }
}

void ui_main_tick(uint32_t now)
{
  if (!g_ui.have_r) return;
  const reading_t *r = &g_ui.r;

  if ((int)r->state != m.shown_state) {
    m.shown_state = (int)r->state;
    enter_state(r->state);
    m.last_num_ms = 0;
  }

  /* Arc marker at 30 Hz; pinned to the end on a rail. */
  if (r->state == ST_READY || r->state == ST_TOO_HOT) {
    if (!isnan(r->lambda_disp)) place_marker(ui_angle(r->lambda_disp));
  } else if (r->state == ST_RICH_RAIL) {
    place_marker(135.0f);
  } else if (r->state == ST_LEAN_RAIL) {
    place_marker(405.0f);
  }
  place_tick(0, r->peak_rich);
  place_tick(1, r->peak_lean);

  if (now - m.last_num_ms >= NUM_MS) {
    m.last_num_ms = now;
    update_texts(r);
    update_brightness(r);
  }

  /* Warning: flash the background red at 2 Hz until it clears. */
  if (r->warn != WARN_NONE) {
    if (now - m.last_flash_ms >= FLASH_MS) {
      m.last_flash_ms = now;
      m.flash_on = !m.flash_on;
      lv_obj_set_style_bg_color(g_ui.scr_main, lv_color_hex(m.flash_on ? COL_WARN : COL_BG), 0);
    }
  } else if (m.flash_on) {
    m.flash_on = false;
    lv_obj_set_style_bg_color(g_ui.scr_main, lv_color_hex(COL_BG), 0);
  }

  /* Burn-in care: shift the layout by up to +/-3 px every 5 minutes. */
  if (now - m.last_shift_ms >= SHIFT_MS) {
    static const int8_t dx[] = { 0, 3, 3, 0, -3, -3, -3, 0, 3 };
    static const int8_t dy[] = { 0, 0, 3, 3, 3, 0, -3, -3, -3 };
    m.last_shift_ms = now;
    m.shift_idx = (m.shift_idx + 1) % sizeof dx;
    lv_obj_set_pos(m.root, dx[m.shift_idx], dy[m.shift_idx]);
  }
}
