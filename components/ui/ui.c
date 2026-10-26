#include "ui_internal.h"

#include <string.h>

ui_ctx_t g_ui;

static void gesture_cb(lv_event_t *e)
{
  lv_obj_t *scr = lv_event_get_current_target_obj(e);
  lv_indev_t *indev = lv_indev_active();
  if (!indev) return;
  lv_dir_t dir = lv_indev_get_gesture_dir(indev);
  lv_obj_t *to = NULL;
  bool from_right = true;

  if (scr == g_ui.scr_main) {
    if (dir == LV_DIR_LEFT) to = g_ui.scr_chart;
    else if (dir == LV_DIR_RIGHT) { to = g_ui.scr_settings; from_right = false; }
  } else if (scr == g_ui.scr_chart) {
    if (dir == LV_DIR_RIGHT) { to = g_ui.scr_main; from_right = false; }
  } else if (scr == g_ui.scr_settings) {
    if (dir == LV_DIR_LEFT) to = g_ui.scr_main;
  } else if (scr == g_ui.scr_service || scr == g_ui.scr_clock) {
    if (dir == LV_DIR_LEFT || dir == LV_DIR_RIGHT) to = g_ui.scr_settings;
  }
  if (to) {
    lv_indev_wait_release(indev); /* no click after a swipe */
    ui_go(to, from_right);
  }
}

lv_obj_t *ui_screen_new(void)
{
  lv_obj_t *scr = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr, lv_color_hex(COL_BG), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_set_style_text_color(scr, lv_color_hex(COL_TEXT), 0);
  lv_obj_set_style_text_font(scr, &afr_font_24, 0);
  lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(scr, gesture_cb, LV_EVENT_GESTURE, NULL);
  return scr;
}

lv_obj_t *ui_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *text)
{
  lv_obj_t *l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
  lv_label_set_text(l, text);
  return l;
}

lv_obj_t *ui_button(lv_obj_t *parent, const char *text, int w, int h, lv_event_cb_t cb, void *user)
{
  lv_obj_t *b = lv_button_create(parent);
  lv_obj_remove_style_all(b);
  lv_obj_add_style(b, &g_ui.btn_style, 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(0x505050), LV_STATE_PRESSED);
  lv_obj_set_size(b, w, h);
  if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);
  lv_obj_t *l = ui_label(b, &afr_font_24, COL_TEXT, text);
  lv_obj_center(l);
  return b;
}

void ui_go(lv_obj_t *scr, bool from_right)
{
  if (scr == g_ui.scr_settings) ui_settings_refresh();
  if (scr == g_ui.scr_service) ui_service_refresh();
  if (scr == g_ui.scr_clock) ui_clock_load();
  if (scr == g_ui.scr_settings && g_ui.hooks.spartan_get_modes) g_ui.hooks.spartan_get_modes();
  lv_screen_load_anim(scr, from_right ? LV_SCREEN_LOAD_ANIM_MOVE_LEFT : LV_SCREEN_LOAD_ANIM_MOVE_RIGHT,
                      200, 0, false);
}

void ui_notify_settings(void)
{
  settings_validate(&g_ui.s);
  if (g_ui.hooks.settings_changed) g_ui.hooks.settings_changed(&g_ui.s);
  ui_main_apply();
}

float ui_frac(float lambda)
{
  float f = (lambda - g_ui.range.lam0) / (g_ui.range.lam5 - g_ui.range.lam0);
  return f < 0.0f ? 0.0f : f > 1.0f ? 1.0f : f;
}

float ui_angle(float lambda)
{
  return 135.0f + 270.0f * ui_frac(lambda);
}

static void tick_cb(lv_timer_t *t)
{
  (void)t;
  uint32_t now = lv_tick_get();
  if (g_ui.hooks.get_reading && g_ui.hooks.get_reading(&g_ui.r)) g_ui.have_r = true;
  if (g_ui.hooks.get_status) g_ui.hooks.get_status(&g_ui.st);
  ui_main_tick(now);

  static uint32_t last_chart, last_refresh;
  if (now - last_chart >= 100) {
    last_chart = now;
    if (g_ui.have_r) ui_chart_push(&g_ui.r);
  }
  if (now - last_refresh >= 500) {
    last_refresh = now;
    lv_obj_t *act = lv_screen_active();
    if (act == g_ui.scr_service) ui_service_refresh();
    if (act == g_ui.scr_settings) ui_settings_refresh();
  }
}

void ui_splash(const ui_hooks_t *hooks, const char *version)
{
  hooks->lock();
  lv_obj_t *scr = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr, lv_color_hex(COL_BG), 0);
  lv_obj_t *t = ui_label(scr, &afr_font_40, COL_TEXT, "WIDEBAND");
  lv_obj_align(t, LV_ALIGN_CENTER, 0, -30);
  lv_obj_t *v = ui_label(scr, &afr_font_20, COL_GREY, version);
  lv_obj_align(v, LV_ALIGN_CENTER, 0, 25);
  lv_screen_load(scr);
  hooks->unlock();
}

void ui_start(const ui_hooks_t *hooks, const afr_settings_t *s, lambda_range_t range)
{
  hooks->lock();
  lv_obj_t *splash = lv_screen_active();
  g_ui.hooks = *hooks;
  g_ui.s = *s;
  g_ui.range = range;
  memset(&g_ui.st, 0, sizeof g_ui.st);
  g_ui.st.sensor_type = g_ui.st.spartan_perf = g_ui.st.spartan_slowheat = -1;

  lv_style_init(&g_ui.btn_style);
  lv_style_set_bg_color(&g_ui.btn_style, lv_color_hex(COL_BTN));
  lv_style_set_bg_opa(&g_ui.btn_style, LV_OPA_COVER);
  lv_style_set_radius(&g_ui.btn_style, 12);
  lv_style_set_text_color(&g_ui.btn_style, lv_color_hex(COL_TEXT));

  lv_indev_t *indev = lv_indev_get_next(NULL);
  if (indev) lv_indev_set_long_press_time(indev, 1000);

  ui_main_create();
  ui_chart_create();
  ui_settings_create();
  ui_service_create();
  ui_clock_create();
  ui_main_apply();
  ui_chart_apply();

  lv_screen_load(g_ui.scr_main);
  if (splash) lv_obj_delete(splash);
  lv_timer_create(tick_cb, 33, NULL); /* arc marker at 30 Hz */
  hooks->unlock();
}

void ui_set_settings(const afr_settings_t *s)
{
  g_ui.hooks.lock();
  g_ui.s = *s;
  ui_main_apply();
  if (lv_screen_active() == g_ui.scr_settings) ui_settings_refresh();
  g_ui.hooks.unlock();
}

void ui_set_range(lambda_range_t range)
{
  g_ui.hooks.lock();
  g_ui.range = range;
  ui_main_apply();
  ui_chart_apply();
  g_ui.hooks.unlock();
}
