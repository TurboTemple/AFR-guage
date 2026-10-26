#include "power.h"

#include "esp_check.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "power";

#define AXP_ADDR          0x34
#define AXP_STATUS1       0x00  /* bit5 VBUS good, bit3 battery present */
#define AXP_COMMON_CFG    0x10  /* bit0 power off */
#define AXP_INTEN2        0x41  /* bit6 VBUS remove, bit7 VBUS insert */
#define AXP_INTSTS2       0x49
#define AXP_ICC_CHG_SET   0x62  /* bits 4:0 constant charge current */
#define AXP_CHG_200MA     8     /* 25 mA per step: 4 is 100 mA, 8 is 200 mA */

#define EXP_PIN_PWR       4
#define EXP_PIN_AXP_IRQ   5

static i2c_master_dev_handle_t s_axp;
static power_hooks_t s_hooks;
static volatile power_status_t s_status;

static esp_err_t rd(uint8_t reg, uint8_t *v)
{
  return i2c_master_transmit_receive(s_axp, &reg, 1, v, 1, 20);
}

static esp_err_t wr(uint8_t reg, uint8_t v)
{
  uint8_t b[2] = { reg, v };
  return i2c_master_transmit(s_axp, b, 2, 20);
}

esp_err_t power_init(i2c_master_bus_handle_t bus)
{
  i2c_device_config_t dc = {
    .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    .device_address = AXP_ADDR,
    .scl_speed_hz = 400000,
  };
  ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &dc, &s_axp), TAG, "add");
  uint8_t st;
  ESP_RETURN_ON_ERROR(rd(AXP_STATUS1, &st), TAG, "status");
  s_status.vbus = st & (1u << 5);
  s_status.battery = st & (1u << 3);
  ESP_LOGI(TAG, "VBUS %s, battery %s", s_status.vbus ? "present" : "absent",
           s_status.battery ? "connected" : "none");
  if (s_status.battery) {
    /* Conservative: a cabin LiPo is charged at no more than 200 mA. */
    uint8_t v;
    if (rd(AXP_ICC_CHG_SET, &v) == ESP_OK) wr(AXP_ICC_CHG_SET, (uint8_t)((v & 0xE0) | AXP_CHG_200MA));
  }
  uint8_t en;
  if (rd(AXP_INTEN2, &en) == ESP_OK) wr(AXP_INTEN2, (uint8_t)(en | (1u << 6)));
  wr(AXP_INTSTS2, 0xFF);
  return ESP_OK;
}

void power_off(void)
{
  uint8_t v;
  if (rd(AXP_COMMON_CFG, &v) == ESP_OK) wr(AXP_COMMON_CFG, (uint8_t)(v | 1u));
}

power_status_t power_status(void)
{
  power_status_t s = { s_status.vbus, s_status.battery, s_status.pwr_pressed };
  return s;
}

static void task(void *arg)
{
  (void)arg;
  bool lost = false;
  TickType_t lost_at = 0;
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(POWER_POLL_MS));
    if (s_hooks.read_expander_pin) {
      s_status.pwr_pressed = s_hooks.read_expander_pin(EXP_PIN_PWR);
      /* The AXP IRQ line is active low: acknowledge it. */
      if (!s_hooks.read_expander_pin(EXP_PIN_AXP_IRQ)) wr(AXP_INTSTS2, 0xFF);
    }
    uint8_t st;
    if (rd(AXP_STATUS1, &st) != ESP_OK) continue;
    s_status.vbus = st & (1u << 5);
    s_status.battery = st & (1u << 3);

    if (!s_status.vbus && !lost) {
      lost = true;
      lost_at = xTaskGetTickCount();
      ESP_LOGW(TAG, "VBUS lost");
      if (s_hooks.on_vbus_lost) s_hooks.on_vbus_lost();
    } else if (s_status.vbus && lost) {
      lost = false;
      ESP_LOGI(TAG, "VBUS back (cranking dip)");
    }
    if (lost && xTaskGetTickCount() - lost_at >= pdMS_TO_TICKS(POWER_OFF_CONFIRM_MS)) {
      ESP_LOGW(TAG, "ignition off: closing log and powering off");
      if (s_hooks.on_shutdown) s_hooks.on_shutdown();
      power_off();
      /* If power-off did not take (VBUS came back meanwhile), carry on. */
      vTaskDelay(pdMS_TO_TICKS(500));
      esp_restart();
    }
  }
}

esp_err_t power_task_start(const power_hooks_t *hooks, int core, int prio)
{
  s_hooks = *hooks;
  return xTaskCreatePinnedToCore(task, "power", 3072, NULL, prio, NULL, core) == pdPASS
           ? ESP_OK : ESP_ERR_NO_MEM;
}
