/* Main screen, laid out like a race dash:
 *
 *   - a 40-segment bar around 270 deg. Segments are coloured by zone (rich,
 *     safe, lean for the current mode's limits) and lit from the target to
 *     the reading. A white arc marks the target window, a white tick the
 *     lean limit.
 *   - the mode chip (STREET / RACE), the big number, and a delta bar that
 *     grows right when leaner than the target and left when richer.
 *   - peak rich / peak lean boxes, and a log and target status line.
 *   - a lean warning turns the whole screen red, a rich one amber.
 *   - after a lean-out that came back to safe, a red ring round the edge
 *     for 5 minutes (alarms.h).
 *
 * Double-tap: street / race mode. Long press: reset peaks and the ring.
 * When no number is valid the centre shows the state in words. Pure black
 * background: on AMOLED that is pixels off. */
#include "ui_internal.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "alarms.h"
#include "gauge_state.h"

#define SEG_N        40
#define SEG_GAP      0.9f   /* degrees each side of a segment */
#define BAR_START    135.0f /* LVGL degrees: 0 at 3 o'clock, clockwise */
#define BAR_SPAN     270.0f
#define BAR_R        220    /* outer radius; 194-220 */
#define BAR_W        26
#define BAND_R       186
#define TICK_R_IN    223.0f
#define TICK_R_OUT   232.0f
#define RING_R       233
#define RING_W       7
#define NUM_BASELINE 252
#define DELTA_Y      268    /* delta bar track */
#define DELTA_HALF   140    /* px for DELTA_FULL */
#define DELTA_FULL   0.10f  /* lambda at the end of the delta bar */
#define NUM_MS       100    /* digits at 10 Hz */
#define BAR_MS       33     /* bar and delta at 30 Hz */
#define SHIFT_MS     (5u * 60u * 1000u)
#define DIM_AFTER_MS (2u * 60u * 1000u)
#define DIM_PERCENT  10

/* Race-dash palette. */
#define C_OK_ON      0x34E07A
#define C_OK_OFF     0x0F2E1C
#define C_RICH_ON    0x2F8CFF
#define C_RICH_OFF   0x0F2440
#define C_LEAN_ON    0xFF4D1A
#define C_LEAN_OFF   0x3A1608
#define C_RING       0xFF1E1E
#define C_ACCENT     0xFFB000   /* race mode */
#define C_LABEL      0x8A8A8A
#define C_SUBTLE     0x9A9A9A
#define C_CELL       0x2A2A2A
#define C_TRACK      0x1C1C1C
#define C_PROBLEM    0xFFB000

typedef enum { THEME_NORMAL, THEME_LEAN, THEME_RICH } theme_t;

typedef struct {
  uint32_t bg, seg_on, seg_off, track, cell, label, text2;
} warn_theme_t;

static const warn_theme_t k_warn[3] = {
  [THEME_NORMAL] = { 0x000000, 0, 0, C_TRACK, C_CELL, C_LABEL, C_SUBTLE },
  [THEME_LEAN]   = { 0xC8102E, 0xFFFFFF, 0x900B21, 0x8C0B20, 0xE48797, 0xFFFFFF, 0xFFFFFF },
  [THEME_RICH]   = { 0x9E5A00, 0xFFFFFF, 0x6E3F00, 0x6E3F00, 0xCFAD80, 0xFFFFFF, 0xFFFFFF },
};

static struct {
  lv_obj_t *root;
  lv_obj_t *bar;
  lv_obj_t *chip, *chip_lbl;
  lv_obj_t *num;
  lv_obj_t *track, *fill, *zero;
  lv_obj_t *drow, *pill, *pill_lbl, *delta, *second;
  lv_obj_t *centre, *centre_sub;
  lv_obj_t *cell[2], *cell_val[2], *cell_lbl[2];
  lv_obj_t *dot, *log_lbl, *status;

  /* Scale and limits for the current mode. */
  float     lo, hi, target, lean, rich;
  uint32_t  seg_col[SEG_N];
  bool      ring;
  theme_t   theme;

  int       shown_state;
  uint32_t  last_num_ms, last_bar_ms;
  uint32_t  last_shift_ms;
  unsigned  shift_idx;
  int       brightness;
} m;

/* Fixed-point text without float printf. */
static void fmt_fixed(char *buf, size_t n, float v, int decimals)
{
  if (decimals <= 0) {
    snprintf(buf, n, "%ld", lroundf(v));
    return;
  }
  int scale = decimals == 1 ? 10 : decimals == 2 ? 100 : 1000;
  long q = lroundf(fabsf(v) * (float)scale);
  const char *sign = v < 0 ? "-" : "";
  if (decimals == 1) snprintf(buf, n, "%s%ld.%01ld", sign, q / scale, q % scale);
  else if (decimals == 2) snprintf(buf, n, "%s%ld.%02ld", sign, q / scale, q % scale);
  else snprintf(buf, n, "%s%ld.%03ld", sign, q / scale, q % scale);
}

static void show(lv_obj_t *o, bool on)
{
  if (on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static void set_text(lv_obj_t *l, const char *t)
{
  if (strcmp(t, lv_label_get_text(l)) != 0) lv_label_set_text(l, t);
}

static void set_color(lv_obj_t *l, uint32_t c)
{
  lv_obj_set_style_text_color(l, lv_color_hex(c), 0);
}

/* ---------------------------------------------------------------- Bar */

static float bar_angle(float lambda)
{
  float f = (lambda - m.lo) / (m.hi - m.lo);
  f = f < 0.0f ? 0.0f : f > 1.0f ? 1.0f : f;
  return BAR_START + BAR_SPAN * f;
}

static float seg_a0(int i) { return BAR_START + BAR_SPAN * (float)i / SEG_N + SEG_GAP; }
static float seg_a1(int i) { return BAR_START + BAR_SPAN * (float)(i + 1) / SEG_N - SEG_GAP; }

static float wrap360(float a) { return a >= 360.0f ? a - 360.0f : a; }

static void polar(float deg, float r, float *x, float *y)
{
  float rad = deg * (float)M_PI / 180.0f;
  *x = (float)UI_CX + r * cosf(rad);
  *y = (float)UI_CY + r * sinf(rad);
}

static void bar_draw_cb(lv_event_t *e)
{
  lv_obj_t *obj = lv_event_get_current_target_obj(e);
  lv_layer_t *layer = lv_event_get_layer(e);
  lv_area_t c;
  lv_obj_get_coords(obj, &c);

  lv_draw_arc_dsc_t d;
  lv_draw_arc_dsc_init(&d);
  d.center.x = c.x1 + UI_CX;
  d.center.y = c.y1 + UI_CY;

  if (m.ring) {
    d.radius = RING_R;
    d.width = RING_W;
    d.start_angle = 0;
    d.end_angle = 360;
    d.color = lv_color_hex(C_RING);
    lv_draw_arc(layer, &d);
  }

  d.radius = BAR_R;
  d.width = BAR_W;
  d.rounded = 0;
  for (int i = 0; i < SEG_N; i++) {
    d.start_angle = (lv_value_precise_t)wrap360(seg_a0(i));
    d.end_angle = (lv_value_precise_t)wrap360(seg_a1(i));
    d.color = lv_color_hex(m.seg_col[i]);
    lv_draw_arc(layer, &d);
  }

  /* Target window. */
  d.radius = BAND_R;
  d.width = 4;
  d.rounded = 1;
  d.start_angle = (lv_value_precise_t)wrap360(bar_angle(m.target - TARGET_WINDOW));
  d.end_angle = (lv_value_precise_t)wrap360(bar_angle(m.target + TARGET_WINDOW));
  d.color = lv_color_hex(COL_TEXT);
  lv_draw_arc(layer, &d);

  /* Lean limit. */
  lv_draw_line_dsc_t l;
  lv_draw_line_dsc_init(&l);
  l.width = 4;
  l.round_start = l.round_end = 1;
  l.color = lv_color_hex(COL_TEXT);
  float x, y, a = bar_angle(m.lean);
  polar(a, TICK_R_IN, &x, &y);
  l.p1.x = (lv_value_precise_t)(c.x1 + x);
  l.p1.y = (lv_value_precise_t)(c.y1 + y);
  polar(a, TICK_R_OUT, &x, &y);
  l.p2.x = (lv_value_precise_t)(c.x1 + x);
  l.p2.y = (lv_value_precise_t)(c.y1 + y);
  lv_draw_line(layer, &l);
}

/* Redraw only the segments whose colour changed. */
static void seg_invalidate(int i)
{
  lv_area_t c;
  lv_obj_get_coords(m.bar, &c);
  float xs[5], ys[5];
  float a0 = seg_a0(i), a1 = seg_a1(i);
  polar(a0, BAR_R, &xs[0], &ys[0]);
  polar(a1, BAR_R, &xs[1], &ys[1]);
  polar(a0, BAR_R - BAR_W, &xs[2], &ys[2]);
  polar(a1, BAR_R - BAR_W, &xs[3], &ys[3]);
  polar((a0 + a1) / 2, BAR_R, &xs[4], &ys[4]);
  float x0 = xs[0], x1 = xs[0], y0 = ys[0], y1 = ys[0];
  for (int k = 1; k < 5; k++) {
    x0 = fminf(x0, xs[k]);
    x1 = fmaxf(x1, xs[k]);
    y0 = fminf(y0, ys[k]);
    y1 = fmaxf(y1, ys[k]);
  }
  lv_area_t a = {
    .x1 = c.x1 + (int32_t)floorf(x0) - 2, .y1 = c.y1 + (int32_t)floorf(y0) - 2,
    .x2 = c.x1 + (int32_t)ceilf(x1) + 2, .y2 = c.y1 + (int32_t)ceilf(y1) + 2,
  };
  lv_obj_invalidate_area(m.bar, &a);
}

static uint32_t zone_colour(float lam, bool on)
{
  if (lam < m.rich) return on ? C_RICH_ON : C_RICH_OFF;
  if (lam > m.lean) return on ? C_LEAN_ON : C_LEAN_OFF;
  return on ? C_OK_ON : C_OK_OFF;
}

/* lambda NAN: nothing lit. progress >= 0: light that fraction (calibrating). */
static void bar_update(float lambda, int progress)
{
  const warn_theme_t *w = &k_warn[m.theme];
  float aT = bar_angle(m.target), aV = isnan(lambda) ? aT : bar_angle(lambda);
  float lo = fminf(aT, aV), hi = fmaxf(aT, aV);
  for (int i = 0; i < SEG_N; i++) {
    float a0 = seg_a0(i), a1 = seg_a1(i);
    float lam = m.lo + (m.hi - m.lo) * ((float)i + 0.5f) / SEG_N;
    bool on;
    if (progress >= 0) on = i * 100 < progress * SEG_N;
    else on = !isnan(lambda) && ((a1 > lo && a0 < hi) || (aV >= a0 - SEG_GAP && aV <= a1 + SEG_GAP));
    uint32_t col;
    if (m.theme != THEME_NORMAL) col = on ? w->seg_on : w->seg_off;
    else if (progress >= 0) col = on ? C_RICH_ON : C_RICH_OFF;
    else col = zone_colour(lam, on);
    if (col != m.seg_col[i]) {
      m.seg_col[i] = col;
      seg_invalidate(i);
    }
  }
}

/* ---------------------------------------------------------------- Theme */

static void chip_style(void)
{
  bool race = g_ui.s.race;
  uint32_t warn_fg = k_warn[m.theme].bg;
  uint32_t bg = m.theme != THEME_NORMAL ? COL_TEXT : race ? C_ACCENT : COL_BG;
  uint32_t border = m.theme != THEME_NORMAL ? COL_TEXT : race ? C_ACCENT : 0x555555;
  uint32_t fg = m.theme != THEME_NORMAL ? warn_fg : race ? COL_BG : 0xBDBDBD;
  lv_obj_set_style_bg_color(m.chip, lv_color_hex(bg), 0);
  lv_obj_set_style_bg_opa(m.chip, m.theme == THEME_NORMAL && !race ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(m.chip, lv_color_hex(border), 0);
  set_color(m.chip_lbl, fg);
  set_text(m.chip_lbl, race ? "RACE" : "STREET");
}

static void apply_theme(theme_t t)
{
  m.theme = t;
  const warn_theme_t *w = &k_warn[t];
  lv_obj_set_style_bg_color(g_ui.scr_main, lv_color_hex(w->bg), 0);
  chip_style();
  lv_obj_set_style_bg_color(m.track, lv_color_hex(w->track), 0);
  for (int i = 0; i < 2; i++) {
    lv_obj_set_style_border_color(m.cell[i], lv_color_hex(w->cell), 0);
    set_color(m.cell_lbl[i], w->label);
  }
  set_color(m.second, w->text2);
  set_color(m.log_lbl, w->label);
  show(m.pill, t != THEME_NORMAL);
  if (t != THEME_NORMAL) {
    set_text(m.pill_lbl, t == THEME_LEAN ? "LEAN" : "RICH");
    set_color(m.pill_lbl, w->bg);
  }
  show(m.second, t == THEME_NORMAL && g_ui.s.show_afr);
  for (int i = 0; i < SEG_N; i++) m.seg_col[i] = 0xFFFFFFFFu; /* repaint */
  lv_obj_invalidate(m.bar);
}

/* ---------------------------------------------------------------- Input */

static void tap_cb(lv_event_t *e)
{
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_DOUBLE_CLICKED) {
    g_ui.s.race = !g_ui.s.race;
    ui_notify_settings();
  } else if (code == LV_EVENT_LONG_PRESSED) {
    if (g_ui.hooks.reset_peaks) g_ui.hooks.reset_peaks();
  }
}

/* ---------------------------------------------------------------- Build */

static lv_obj_t *box(lv_obj_t *parent)
{
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
  return o;
}

static lv_obj_t *centred(lv_obj_t *parent, const lv_font_t *font, uint32_t color, int w, int y)
{
  lv_obj_t *l = ui_label(parent, font, color, "");
  lv_obj_set_width(l, w);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_pos(l, UI_CX - w / 2, y);
  return l;
}

static void make_cell(int i, const char *label, int x)
{
  lv_obj_t *c = m.cell[i] = box(m.root);
  lv_obj_set_size(c, 96, 50);
  lv_obj_set_pos(c, x, 336);
  lv_obj_set_style_radius(c, 6, 0);
  lv_obj_set_style_border_width(c, 1, 0);
  lv_obj_set_style_border_color(c, lv_color_hex(C_CELL), 0);
  m.cell_lbl[i] = ui_label(c, &afr_font_12, C_LABEL, label);
  lv_obj_set_style_text_letter_space(m.cell_lbl[i], 2, 0);
  lv_obj_align(m.cell_lbl[i], LV_ALIGN_TOP_MID, 0, 5);
  m.cell_val[i] = ui_label(c, &afr_font_c22, COL_TEXT, "--");
  lv_obj_align(m.cell_val[i], LV_ALIGN_BOTTOM_MID, 0, -3);
}

void ui_main_create(void)
{
  lv_obj_t *scr = g_ui.scr_main = ui_screen_new();
  lv_obj_add_event_cb(scr, tap_cb, LV_EVENT_DOUBLE_CLICKED, NULL);
  lv_obj_add_event_cb(scr, tap_cb, LV_EVENT_LONG_PRESSED, NULL);

  m.root = box(scr);
  lv_obj_set_size(m.root, UI_W, UI_H);

  m.bar = box(m.root);
  lv_obj_set_size(m.bar, UI_W, UI_H);
  lv_obj_add_event_cb(m.bar, bar_draw_cb, LV_EVENT_DRAW_MAIN, NULL);

  /* Mode chip. */
  m.chip = box(m.root);
  lv_obj_set_size(m.chip, LV_SIZE_CONTENT, 30);
  lv_obj_set_style_pad_hor(m.chip, 16, 0);
  lv_obj_set_style_radius(m.chip, 4, 0);
  lv_obj_set_style_border_width(m.chip, 2, 0);
  lv_obj_align(m.chip, LV_ALIGN_TOP_MID, 0, 74);
  m.chip_lbl = ui_label(m.chip, &afr_font_c18b, COL_TEXT, "");
  lv_obj_set_style_text_letter_space(m.chip_lbl, 5, 0);
  lv_obj_center(m.chip_lbl);

  /* Main number: fixed width so a change only redraws this box. */
  m.num = centred(m.root, &afr_font_num, COL_TEXT, 420,
                  NUM_BASELINE - (afr_font_num.line_height - afr_font_num.base_line));
  lv_label_set_long_mode(m.num, LV_LABEL_LONG_CLIP);

  /* Delta bar: centre line is the target. */
  m.track = box(m.root);
  lv_obj_set_size(m.track, 2 * DELTA_HALF, 12);
  lv_obj_set_pos(m.track, UI_CX - DELTA_HALF, DELTA_Y);
  lv_obj_set_style_radius(m.track, 6, 0);
  lv_obj_set_style_bg_opa(m.track, LV_OPA_COVER, 0);
  m.fill = box(m.root);
  lv_obj_set_size(m.fill, 4, 12);
  lv_obj_set_pos(m.fill, UI_CX, DELTA_Y);
  lv_obj_set_style_radius(m.fill, 6, 0);
  lv_obj_set_style_bg_opa(m.fill, LV_OPA_COVER, 0);
  m.zero = box(m.root);
  lv_obj_set_size(m.zero, 2, 28);
  lv_obj_set_pos(m.zero, UI_CX - 1, DELTA_Y - 8);
  lv_obj_set_style_bg_color(m.zero, lv_color_hex(COL_TEXT), 0);
  lv_obj_set_style_bg_opa(m.zero, LV_OPA_COVER, 0);

  /* Delta and the other unit; the warning pill replaces the unit. */
  m.drow = box(m.root);
  lv_obj_set_size(m.drow, UI_W, 36);
  lv_obj_set_pos(m.drow, 0, 292);
  lv_obj_set_flex_flow(m.drow, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(m.drow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(m.drow, 12, 0);
  m.pill = box(m.drow);
  lv_obj_set_size(m.pill, LV_SIZE_CONTENT, 32);
  lv_obj_set_style_pad_hor(m.pill, 16, 0);
  lv_obj_set_style_radius(m.pill, 4, 0);
  lv_obj_set_style_bg_color(m.pill, lv_color_hex(COL_TEXT), 0);
  lv_obj_set_style_bg_opa(m.pill, LV_OPA_COVER, 0);
  m.pill_lbl = ui_label(m.pill, &afr_font_c24b, COL_TEXT, "LEAN");
  lv_obj_set_style_text_letter_space(m.pill_lbl, 6, 0);
  lv_obj_center(m.pill_lbl);
  m.delta = ui_label(m.drow, &afr_font_c26, COL_TEXT, "");
  m.second = ui_label(m.drow, &afr_font_c20, C_SUBTLE, "");

  /* State words when there is no number. */
  m.centre = centred(m.root, &afr_font_c40, COL_TEXT, 340, 140);
  lv_label_set_long_mode(m.centre, LV_LABEL_LONG_WRAP);
  m.centre_sub = centred(m.root, &afr_font_c20, C_SUBTLE, 320, 250);

  make_cell(0, "PEAK RICH", UI_CX - 101);
  make_cell(1, "PEAK LEAN", UI_CX + 5);

  /* Status: log state, then a problem or the target. */
  lv_obj_t *st = box(m.root);
  lv_obj_set_size(st, UI_W, 20);
  lv_obj_set_pos(st, 0, 410);
  lv_obj_set_flex_flow(st, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(st, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(st, 8, 0);
  m.dot = box(st);
  lv_obj_set_size(m.dot, 8, 8);
  lv_obj_set_style_radius(m.dot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(m.dot, LV_OPA_COVER, 0);
  m.log_lbl = ui_label(st, &afr_font_12, C_LABEL, "");
  lv_obj_set_style_text_letter_space(m.log_lbl, 1, 0);
  m.status = ui_label(st, &afr_font_12, C_LABEL, "");
  lv_obj_set_style_text_letter_space(m.status, 1, 0);
  lv_obj_set_style_margin_left(m.status, 10, 0);

  m.shown_state = -1;
  m.brightness = -1;
  apply_theme(THEME_NORMAL);
}

/* Settings, mode or the Spartan's range changed. */
void ui_main_apply(void)
{
  alarm_cfg_t a = settings_alarm_cfg(&g_ui.s);
  m.lo = 0.70f;
  m.hi = g_ui.s.race ? 1.05f : 1.30f;
  if (m.lo < g_ui.range.lam0) m.lo = g_ui.range.lam0;
  if (m.hi > g_ui.range.lam5) m.hi = g_ui.range.lam5;
  m.target = settings_target(&g_ui.s);
  m.lean = a.lean;
  m.rich = a.rich;
  chip_style();
  show(m.second, m.theme == THEME_NORMAL && g_ui.s.show_afr);
  for (int i = 0; i < SEG_N; i++) m.seg_col[i] = 0xFFFFFFFFu;
  lv_obj_invalidate(m.bar);
  m.shown_state = -1; /* redraw everything on the next tick */
  m.last_num_ms = m.last_bar_ms = 0;
}

/* ---------------------------------------------------------------- Update */

static bool has_number(gauge_state_t s)
{
  return s == ST_READY || s == ST_RICH_RAIL || s == ST_LEAN_RAIL;
}

/* Visibility and fixed texts for a state; runs on state change only. */
static void enter_state(gauge_state_t s)
{
  bool number = has_number(s);
  show(m.num, number);
  show(m.track, number);
  show(m.fill, number);
  show(m.zero, number);
  show(m.drow, number);
  show(m.centre, !number);
  show(m.centre_sub, !number);
  set_color(m.centre, s == ST_TOO_HOT ? C_LEAN_ON : COL_TEXT);
  lv_obj_set_style_text_font(m.centre, s == ST_WAIT_EXHAUST ? &afr_font_c26 : &afr_font_c40, 0);

  const char *t = "";
  switch (s) {
  case ST_NO_SPARTAN: t = "ENGINE OFF"; break;
  case ST_SEQUENCER: t = "CALIBRATING"; break;
  case ST_WAIT_EXHAUST: t = "WAITING FOR EXHAUST HEAT"; break;
  case ST_WARMING: t = "WARMING UP"; break;
  case ST_TOO_HOT: t = "SENSOR HOT"; break;
  case ST_FAULT: t = "FAULT"; break;
  default: break;
  }
  lv_label_set_text(m.centre, t);
  lv_label_set_text(m.centre_sub, "");
}

static void update_delta(float lam)
{
  float d = lam - m.target;
  float px = fminf(fabsf(d) / DELTA_FULL, 1.0f) * DELTA_HALF;
  int32_t w = px < 4.0f ? 4 : (int32_t)lroundf(px);
  lv_obj_set_width(m.fill, w);
  lv_obj_set_x(m.fill, d >= 0.0f ? UI_CX : UI_CX - w);
  uint32_t col = m.theme != THEME_NORMAL ? COL_TEXT :
                 fabsf(d) < TARGET_WINDOW ? C_OK_ON : d > 0.0f ? C_LEAN_ON : C_RICH_ON;
  lv_obj_set_style_bg_color(m.fill, lv_color_hex(col), 0);
  set_color(m.delta, col);
}

static void update_number(const reading_t *r)
{
  char buf[24], v[16];
  float stoich = settings_stoich(&g_ui.s);
  if (r->state == ST_LEAN_RAIL || r->state == ST_RICH_RAIL) {
    bool lean = r->state == ST_LEAN_RAIL;
    float end = lean ? g_ui.range.lam5 : g_ui.range.lam0;
    if (g_ui.s.primary_unit == UNIT_AFR) fmt_fixed(buf, sizeof buf, end * stoich, 1);
    else fmt_fixed(buf, sizeof buf, end, 2);
    set_text(m.num, buf);
    set_text(m.delta, lean ? "FULL LEAN" : "FULL RICH");
    set_text(m.second, "");
    return;
  }
  float lam = r->lambda_disp;
  if (g_ui.s.primary_unit == UNIT_AFR) fmt_fixed(buf, sizeof buf, lam * stoich, 1);
  else fmt_fixed(buf, sizeof buf, lam, 2);
  set_text(m.num, buf);

  float d = lam - m.target;
  fmt_fixed(v, sizeof v, fabsf(d), 2);
  snprintf(buf, sizeof buf, "%s%s", d >= 0.0f ? "+" : "-", v);
  set_text(m.delta, buf);

  if (g_ui.s.primary_unit == UNIT_AFR) {
    fmt_fixed(v, sizeof v, lam, 2);
    snprintf(buf, sizeof buf, "\xCE\xBB %s", v);
  } else {
    fmt_fixed(v, sizeof v, lam * stoich, 1);
    snprintf(buf, sizeof buf, "%s AFR", v);
  }
  set_text(m.second, buf);
}

static void update_cell(int i, float v, bool rail)
{
  char buf[16];
  if (isnan(v)) snprintf(buf, sizeof buf, rail ? "RAIL" : "--");
  else {
    fmt_fixed(buf, sizeof buf, v, 2);
    if (rail) strlcat(buf, "*", sizeof buf);
  }
  set_text(m.cell_val[i], buf);
  set_color(m.cell_val[i], COL_TEXT);
}

static void update_texts(const reading_t *r)
{
  char buf[64], a[16];
  gauge_state_t s = r->state;

  if (has_number(s)) {
    update_number(r);
  } else if (s == ST_WARMING) {
    uint32_t secs = (r->t_ms - r->state_since_ms) / 1000u;
    snprintf(buf, sizeof buf, "%lu s", (unsigned long)secs);
    set_text(m.centre_sub, buf);
  } else if (s == ST_FAULT) {
    set_text(m.centre_sub, gs_fault_text((fault_reason_t)r->fault));
  } else if (s == ST_SEQUENCER) {
    snprintf(buf, sizeof buf, "%u %%", (unsigned)r->seq_progress);
    set_text(m.centre_sub, buf);
  }

  update_cell(0, r->peak_rich, (r->flags & FLAG_RICH_RAIL_SEEN) != 0);
  update_cell(1, r->peak_lean, false);

  /* Status line. */
  const ui_status_t *st = &g_ui.st;
  const warn_theme_t *w = &k_warn[m.theme];
  bool log_ok = st->log_state == 1; /* LOG_STATE_OK */
  set_text(m.log_lbl, log_ok ? "LOG" : "NO LOG");
  uint32_t dot = m.theme != THEME_NORMAL ? COL_TEXT : log_ok ? C_OK_ON : C_PROBLEM;
  lv_obj_set_style_bg_color(m.dot, lv_color_hex(dot), 0);

  const char *problem = NULL;
  if (st->sensor_type > 0) problem = "SENSOR NOT LSU 4.9";
  else if (r->flags & FLAG_CAL_REJECTED) problem = "CAL REJECTED";
  else if (r->flags & FLAG_CAL_NOMINAL) problem = "NOT CALIBRATED";
  if (problem) {
    set_text(m.status, problem);
    set_color(m.status, m.theme != THEME_NORMAL ? COL_TEXT : C_PROBLEM);
  } else {
    fmt_fixed(a, sizeof a, m.target, 2);
    snprintf(buf, sizeof buf, "TARGET %s", a);
    set_text(m.status, buf);
    set_color(m.status, w->label);
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
    m.last_num_ms = m.last_bar_ms = 0;
  }

  /* A warning shows at once: whole screen, drawn now rather than on the
   * next refresh. */
  theme_t t = r->alarm == ALARM_LEAN ? THEME_LEAN : r->alarm == ALARM_RICH ? THEME_RICH : THEME_NORMAL;
  bool urgent = false;
  if (t != m.theme) {
    apply_theme(t);
    m.last_num_ms = m.last_bar_ms = 0;
    urgent = t != THEME_NORMAL;
  }
  bool ring = (r->flags & FLAG_LEAN_RING) != 0;
  if (ring != m.ring) {
    m.ring = ring;
    lv_obj_invalidate(m.bar);
  }

  if (now - m.last_bar_ms >= BAR_MS || m.last_bar_ms == 0) {
    m.last_bar_ms = now;
    if (r->state == ST_SEQUENCER) bar_update(NAN, r->seq_progress);
    else if (has_number(r->state)) {
      float lam = r->state == ST_READY ? r->lambda_disp : r->lambda;
      bar_update(lam, -1);
      if (r->state == ST_READY && !isnan(lam)) update_delta(lam);
      else update_delta(r->state == ST_LEAN_RAIL ? m.target + DELTA_FULL : m.target - DELTA_FULL);
    } else bar_update(NAN, -1);
  }

  if (now - m.last_num_ms >= NUM_MS || m.last_num_ms == 0) {
    m.last_num_ms = now;
    update_texts(r);
    update_brightness(r);
  }

  if (urgent) lv_refr_now(NULL);

  /* Burn-in care: shift the layout by up to +/-3 px every 5 minutes. */
  if (now - m.last_shift_ms >= SHIFT_MS) {
    static const int8_t dx[] = { 0, 3, 3, 0, -3, -3, -3, 0, 3 };
    static const int8_t dy[] = { 0, 0, 3, 3, 3, 0, -3, -3, -3 };
    m.last_shift_ms = now;
    m.shift_idx = (m.shift_idx + 1) % sizeof dx;
    lv_obj_set_pos(m.root, dx[m.shift_idx], dy[m.shift_idx]);
  }
}
