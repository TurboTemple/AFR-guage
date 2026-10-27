#include "sampler.h"

#include <math.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "logger.h"

static const char *TAG = "sampler";

#define SAMPLER_CORE    1
#define SAMPLER_PRIO    20
#define AUX_PRIO        18
#define RDY_TIMEOUT_MS  5
#define REARM_MS        50
#define AUX_PERIOD_MS   20  /* LED every tick: 50 Hz */
#define AUX_SUPPLY_EVERY 5  /* 10 Hz */

static ads1115_t *s_lambda, *s_aux;
static afr_pipeline_t s_p;
static decim_t s_dec;
static TaskHandle_t s_task;
static QueueHandle_t s_ui_q;
static EventGroupHandle_t s_events;
static volatile int64_t s_rdy_us;

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static afr_config_t s_pending_cfg;
static bool s_cfg_dirty, s_reset_peaks;
static cal_coeffs_t s_cal_copy;
static cal_source_t s_cal_src_copy;
static bool s_cal_rej_copy;
static sampler_stats_t s_stats;

/* Mailbox from the aux task. */
typedef struct {
  uint32_t t_ms;
  int16_t  code;
} led_sample_t;
static QueueHandle_t s_led_q;
static bool s_sup_new, s_sup_ok;
static int16_t s_sup_code;

static void IRAM_ATTR rdy_isr(void *arg)
{
  (void)arg;
  s_rdy_us = esp_timer_get_time();
  BaseType_t hp = pdFALSE;
  vTaskNotifyGiveFromISR(s_task, &hp);
  portYIELD_FROM_ISR(hp);
}

static uint32_t now_ms(void)
{
  return (uint32_t)(esp_timer_get_time() / 1000);
}

/* Settings and second-ADC values, between samples. */
static void take_inputs(void)
{
  afr_config_t cfg;
  bool dirty, reset, sup_new, sup_ok;
  int16_t sup_code;
  taskENTER_CRITICAL(&s_mux);
  dirty = s_cfg_dirty;
  if (dirty) cfg = s_pending_cfg;
  reset = s_reset_peaks;
  sup_new = s_sup_new;
  sup_ok = s_sup_ok;
  sup_code = s_sup_code;
  s_cfg_dirty = s_reset_peaks = s_sup_new = false;
  taskEXIT_CRITICAL(&s_mux);

  if (dirty) afr_pipeline_set_config(&s_p, &cfg);
  if (reset) afr_pipeline_reset_peaks(&s_p);
  if (sup_new) afr_pipeline_supply_sample(&s_p, sup_ok, sup_code);
  led_sample_t l;
  while (xQueueReceive(s_led_q, &l, 0) == pdTRUE) afr_pipeline_led_sample(&s_p, l.t_ms, l.code);
}

static void publish(const reading_t *r, QueueHandle_t log_q, uint32_t *dropped)
{
  xQueueOverwrite(s_ui_q, r);
  if (log_q) {
    log_row_t row = { .r = *r, .dropped = *dropped };
    if (xQueueSend(log_q, &row, 0) == pdTRUE) *dropped = 0;
    else (*dropped)++;
  }
  uint8_t ev = afr_pipeline_take_events(&s_p);
  if (ev) {
    taskENTER_CRITICAL(&s_mux);
    s_cal_copy = s_p.cal;
    s_cal_src_copy = s_p.cal_src;
    s_cal_rej_copy = s_p.cal_rejected;
    taskEXIT_CRITICAL(&s_mux);
    xEventGroupSetBits(s_events, ev);
  }
}

static void sampler_task(void *arg)
{
  (void)arg;
  esp_task_wdt_add(NULL);
  QueueHandle_t log_q = logger_queue();
  uint32_t dropped = 0, last_fail_ms = 0, last_rearm_ms = 0;
  reading_t r;

  for (;;) {
    uint32_t n = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(s_lambda ? RDY_TIMEOUT_MS : 5));
    esp_task_wdt_reset();
    take_inputs();
    uint32_t t = now_ms();

    if (!s_lambda) {
      /* No lambda ADC: keep the UI and log fed with the FAULT state. */
      afr_pipeline_step_raw(&s_p, t, false, 0.0f, &r);
      publish(&r, log_q, &dropped);
      continue;
    }

    if (n == 0) {
      /* No conversion-ready pulse: count it as a failed sample (about one
       * per sample period) and re-arm the ADC in case it reset. */
      s_stats.rdy_timeouts++;
      decim_reset(&s_dec);
      if (t - last_fail_ms >= AFR_SAMPLE_MS) {
        last_fail_ms = t;
        afr_pipeline_step_raw(&s_p, t, false, 0.0f, &r);
        publish(&r, log_q, &dropped);
      }
      if (t - last_rearm_ms >= REARM_MS) {
        last_rearm_ms = t;
        ads1115_start_continuous_diff01(s_lambda);
      }
      continue;
    }
    if (n > 1) s_stats.missed += n - 1;
    s_stats.raw_conversions++;

    uint64_t t_us = (uint64_t)s_rdy_us;
    int16_t code;
    if (ads1115_read_latest(s_lambda, &code) != ESP_OK) {
      decim_reset(&s_dec);
      afr_pipeline_step_raw(&s_p, t, false, 0.0f, &r);
      publish(&r, log_q, &dropped);
      continue;
    }
    float mean;
    uint64_t t_mid_us;
    if (decim_feed(&s_dec, code, t_us, &mean, &t_mid_us)) {
      s_stats.spikes = s_dec.spikes;
      afr_pipeline_step_raw(&s_p, (uint32_t)(t_mid_us / 1000u), true, mean, &r);
      publish(&r, log_q, &dropped);
    }
  }
}

static void aux_task(void *arg)
{
  (void)arg;
  TickType_t wake = xTaskGetTickCount();
  uint32_t tick = 0;
  for (;;) {
    vTaskDelayUntil(&wake, pdMS_TO_TICKS(AUX_PERIOD_MS));
    led_sample_t l = { .t_ms = now_ms() };
    if (ads1115_read(s_aux, 0, &l.code) == ESP_OK) xQueueSend(s_led_q, &l, 0);
    else s_stats.aux_errors++;

    if (s_p.supply_fitted && tick % AUX_SUPPLY_EVERY == 2) {
      int16_t code;
      bool ok = ads1115_read(s_aux, 2, &code) == ESP_OK;
      if (!ok) s_stats.aux_errors++;
      taskENTER_CRITICAL(&s_mux);
      s_sup_new = true;
      s_sup_ok = ok;
      s_sup_code = code;
      taskEXIT_CRITICAL(&s_mux);
    }
    tick++;
  }
}

esp_err_t sampler_start(ads1115_t *lambda, ads1115_t *aux, const afr_config_t *cfg,
                        const cal_coeffs_t *stored, bool supply_fitted)
{
  s_lambda = lambda;
  s_aux = aux;
  uint32_t now = now_ms();
  afr_pipeline_init(&s_p, cfg, stored, supply_fitted, AFR_SAMPLE_MS, now);
  decim_init(&s_dec, SPIKE_CODES);
  s_cal_copy = s_p.cal;
  s_cal_src_copy = s_p.cal_src;
  if (!lambda) afr_pipeline_fault(&s_p, FAULT_ADC_NOT_FOUND, now);
  else if (!aux) afr_pipeline_fault(&s_p, FAULT_AUX_NOT_FOUND, now);
  afr_pipeline_start(&s_p, now);

  s_ui_q = xQueueCreate(1, sizeof(reading_t));
  s_led_q = xQueueCreate(16, sizeof(led_sample_t));
  s_events = xEventGroupCreate();
  if (!s_ui_q || !s_led_q || !s_events) return ESP_ERR_NO_MEM;

  if (xTaskCreatePinnedToCore(sampler_task, "sampler", 4096, NULL, SAMPLER_PRIO, &s_task,
                              SAMPLER_CORE) != pdPASS) {
    return ESP_ERR_NO_MEM;
  }
  if (aux && xTaskCreatePinnedToCore(aux_task, "aux", 3072, NULL, AUX_PRIO, NULL,
                                     SAMPLER_CORE) != pdPASS) {
    return ESP_ERR_NO_MEM;
  }
  if (!lambda) return ESP_OK;

  /* ALERT/RDY is open drain: pull-up, falling edge. The board's ISR
   * service may already be installed by the BSP. */
  gpio_config_t io = {
    .pin_bit_mask = 1ULL << SAMPLER_RDY_GPIO,
    .mode = GPIO_MODE_INPUT,
    .pull_up_en = GPIO_PULLUP_ENABLE,
    .intr_type = GPIO_INTR_NEGEDGE,
  };
  ESP_ERROR_CHECK(gpio_config(&io));
  esp_err_t err = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
  ESP_ERROR_CHECK(gpio_isr_handler_add(SAMPLER_RDY_GPIO, rdy_isr, NULL));
  ESP_LOGI(TAG, "lambda ADC continuous at %u SPS, decimated by %d; supply sense %s",
           LAMBDA_RAW_HZ, DECIM_N, supply_fitted ? "on" : "off");
  return ads1115_start_continuous_diff01(lambda);
}

void sampler_set_config(const afr_config_t *cfg)
{
  taskENTER_CRITICAL(&s_mux);
  s_pending_cfg = *cfg;
  s_cfg_dirty = true;
  taskEXIT_CRITICAL(&s_mux);
}

void sampler_reset_peaks(void)
{
  taskENTER_CRITICAL(&s_mux);
  s_reset_peaks = true;
  taskEXIT_CRITICAL(&s_mux);
}

bool sampler_latest(reading_t *out)
{
  return s_ui_q && xQueuePeek(s_ui_q, out, 0) == pdTRUE;
}

EventGroupHandle_t sampler_events(void)
{
  return s_events;
}

void sampler_cal(cal_coeffs_t *cal, cal_source_t *src, bool *rejected)
{
  taskENTER_CRITICAL(&s_mux);
  *cal = s_cal_copy;
  *src = s_cal_src_copy;
  *rejected = s_cal_rej_copy;
  taskEXIT_CRITICAL(&s_mux);
}

sampler_stats_t sampler_stats(void)
{
  return s_stats;
}
