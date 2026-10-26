#pragma once

#include "lvgl.h"

#include "fonts/afr_fonts.h"
#include "ui.h"

#define UI_W   466
#define UI_H   466
#define UI_CX  233
#define UI_CY  233

#define COL_BG      0x000000
#define COL_TEXT    0xFFFFFF
#define COL_GREY    0x808080   /* static labels at 50 % grey (burn-in) */
#define COL_DIM     0x404040
#define COL_RICH    0x00C8FF   /* cyan */
#define COL_STOICH  0x30D050   /* green */
#define COL_LEAN    0xFFA000   /* amber */
#define COL_WARN    0xC00000   /* warning flash */
#define COL_BTN     0x262626

typedef struct {
  ui_hooks_t     hooks;
  afr_settings_t s;
  lambda_range_t range;
  reading_t      r;
  bool           have_r;
  ui_status_t    st;
  lv_obj_t      *scr_main, *scr_chart, *scr_settings, *scr_service, *scr_clock;
  lv_style_t     btn_style;
} ui_ctx_t;

extern ui_ctx_t g_ui;

/* ui.c */
lv_obj_t *ui_screen_new(void);
lv_obj_t *ui_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *text);
lv_obj_t *ui_button(lv_obj_t *parent, const char *text, int w, int h, lv_event_cb_t cb, void *user);
void ui_go(lv_obj_t *scr, bool from_right);
void ui_notify_settings(void);
float ui_frac(float lambda);     /* 0 at lam0, 1 at lam5, clamped */
float ui_angle(float lambda);    /* degrees, LVGL convention, 135..405 */

/* ui_main.c */
void ui_main_create(void);
void ui_main_tick(uint32_t now_ms);
void ui_main_apply(void);        /* settings or range changed */

/* ui_chart.c */
void ui_chart_create(void);
void ui_chart_push(const reading_t *r);
void ui_chart_apply(void);

/* ui_settings.c */
void ui_settings_create(void);
void ui_settings_refresh(void);
void ui_service_create(void);
void ui_service_refresh(void);
void ui_clock_create(void);
void ui_clock_load(void);
