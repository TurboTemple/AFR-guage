/* Strip chart: the last 30 s of lambda at 10 points/s (300 points). */
#include "ui_internal.h"

#include <math.h>
#include <stdio.h>

#include "gauge_state.h"

#define POINTS   300
#define CHART_W  340
#define CHART_H  220
#define CHART_X  (UI_CX - CHART_W / 2)
#define CHART_Y  (UI_CY - CHART_H / 2)

static lv_obj_t *s_chart;
static lv_chart_series_t *s_ser;
static lv_obj_t *s_stoich_line;
static lv_point_precise_t s_stoich_pts[2];
static lv_obj_t *s_top, *s_bottom, *s_one, *s_now;

void ui_chart_create(void)
{
  lv_obj_t *scr = g_ui.scr_chart = ui_screen_new();

  lv_obj_t *title = ui_label(scr, &afr_font_22, COL_GREY, "\xCE\xBB  last 30 s");
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 70);

  s_chart = lv_chart_create(scr);
  lv_obj_set_size(s_chart, CHART_W, CHART_H);
  lv_obj_set_pos(s_chart, CHART_X, CHART_Y);
  lv_obj_set_style_bg_opa(s_chart, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(s_chart, 0, 0);
  lv_obj_set_style_pad_all(s_chart, 0, 0);
  lv_obj_set_style_line_color(s_chart, lv_color_hex(COL_DIM), LV_PART_MAIN);
  lv_obj_set_style_line_width(s_chart, 3, LV_PART_ITEMS);
  lv_obj_set_style_size(s_chart, 0, 0, LV_PART_INDICATOR); /* no point dots */
  lv_obj_remove_flag(s_chart, LV_OBJ_FLAG_CLICKABLE);
  lv_chart_set_type(s_chart, LV_CHART_TYPE_LINE);
  lv_chart_set_update_mode(s_chart, LV_CHART_UPDATE_MODE_SHIFT);
  lv_chart_set_point_count(s_chart, POINTS);
  lv_chart_set_div_line_count(s_chart, 5, 0);
  s_ser = lv_chart_add_series(s_chart, lv_color_hex(COL_STOICH), LV_CHART_AXIS_PRIMARY_Y);
  lv_chart_set_all_values(s_chart, s_ser, LV_CHART_POINT_NONE);

  s_stoich_line = lv_line_create(scr);
  lv_obj_set_style_line_width(s_stoich_line, 2, 0);
  lv_obj_set_style_line_color(s_stoich_line, lv_color_hex(COL_GREY), 0);
  lv_obj_set_style_line_dash_width(s_stoich_line, 6, 0);
  lv_obj_set_style_line_dash_gap(s_stoich_line, 6, 0);

  s_top = ui_label(scr, &afr_font_20, COL_GREY, "");
  s_bottom = ui_label(scr, &afr_font_20, COL_GREY, "");
  s_one = ui_label(scr, &afr_font_20, COL_GREY, "1.00");
  s_now = ui_label(scr, &afr_font_28, COL_TEXT, "");
  lv_obj_align(s_now, LV_ALIGN_BOTTOM_MID, 0, -78);
}

static int32_t y_of(float lambda)
{
  return CHART_Y + (int32_t)lroundf((float)CHART_H * (1.0f - ui_frac(lambda)));
}

void ui_chart_apply(void)
{
  int32_t lo = (int32_t)lroundf(g_ui.range.lam0 * 1000.0f);
  int32_t hi = (int32_t)lroundf(g_ui.range.lam5 * 1000.0f);
  lv_chart_set_axis_range(s_chart, LV_CHART_AXIS_PRIMARY_Y, lo, hi);

  char b[16];
  snprintf(b, sizeof b, "%ld.%02ld", (long)(hi / 1000), (long)((hi % 1000) / 10));
  lv_label_set_text(s_top, b);
  lv_obj_set_pos(s_top, CHART_X + CHART_W - 50, CHART_Y - 26);
  snprintf(b, sizeof b, "%ld.%02ld", (long)(lo / 1000), (long)((lo % 1000) / 10));
  lv_label_set_text(s_bottom, b);
  lv_obj_set_pos(s_bottom, CHART_X + CHART_W - 50, CHART_Y + CHART_H + 4);

  bool one_in = g_ui.range.lam0 < 1.0f && g_ui.range.lam5 > 1.0f;
  if (one_in) {
    int32_t y = y_of(1.0f);
    s_stoich_pts[0].x = 0;
    s_stoich_pts[0].y = 1;
    s_stoich_pts[1].x = CHART_W;
    s_stoich_pts[1].y = 1;
    lv_obj_set_pos(s_stoich_line, CHART_X, y - 1);
    lv_line_set_points(s_stoich_line, s_stoich_pts, 2);
    lv_obj_set_pos(s_one, CHART_X - 46, y - 12);
    lv_obj_remove_flag(s_stoich_line, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_one, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(s_stoich_line, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_one, LV_OBJ_FLAG_HIDDEN);
  }
}

void ui_chart_push(const reading_t *r)
{
  int32_t v = LV_CHART_POINT_NONE;
  float lam = r->state == ST_READY ? r->lambda_disp : r->lambda;
  if (gs_has_lambda(r->state) && !isnan(lam)) v = (int32_t)lroundf(lam * 1000.0f);
  lv_chart_set_next_value(s_chart, s_ser, v);

  if (lv_screen_active() == g_ui.scr_chart) {
    char b[24];
    if (v == LV_CHART_POINT_NONE) snprintf(b, sizeof b, "%s", gs_state_name(r->state));
    else snprintf(b, sizeof b, "\xCE\xBB %ld.%02ld", (long)(v / 1000), (long)((v % 1000) / 10));
    lv_label_set_text(s_now, b);
  }
}
