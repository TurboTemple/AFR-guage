#include "ads1115.h"

#include "esp_check.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ads1115";

#define REG_CONVERSION 0x00
#define REG_CONFIG     0x01
#define REG_LO_THRESH  0x02
#define REG_HI_THRESH  0x03

#define CFG_OS          0x8000 /* start a conversion / conversion done */
#define CFG_MUX_DIFF01  (0u << 12)
#define CFG_MUX_AIN(n)  ((uint16_t)((4u + (n)) << 12))
#define CFG_PGA_4V096   (1u << 9)
#define CFG_MODE_SHOT   (1u << 8)
#define CFG_MODE_CONT   (0u << 8)
#define CFG_DR_860      (7u << 5)
#define CFG_COMP_OFF    0x0003
#define CFG_COMP_1CONV  0x0000 /* ALERT after one conversion; with the
                                  thresholds below, conversion ready */

#define XFER_TIMEOUT_MS 5

esp_err_t ads1115_bus_create(i2c_port_num_t port, gpio_num_t sda, gpio_num_t scl,
                             i2c_master_bus_handle_t *bus)
{
  i2c_master_bus_config_t bc = {
    .i2c_port = port,
    .sda_io_num = sda,
    .scl_io_num = scl,
    .clk_source = I2C_CLK_SRC_DEFAULT,
    .glitch_ignore_cnt = 7,
    .flags.enable_internal_pullup = true,
  };
  return i2c_new_master_bus(&bc, bus);
}

esp_err_t ads1115_attach(i2c_master_bus_handle_t bus, uint8_t addr, ads1115_t *a)
{
  if (i2c_master_probe(bus, addr, 50) != ESP_OK) {
    ESP_LOGE(TAG, "no ADS1115 at 0x%02x", addr);
    return ESP_ERR_NOT_FOUND;
  }
  i2c_device_config_t dc = {
    .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    .device_address = addr,
    .scl_speed_hz = ADS1115_I2C_HZ,
  };
  return i2c_master_bus_add_device(bus, &dc, &a->dev);
}

static esp_err_t write_reg(ads1115_t *a, uint8_t reg, uint16_t v)
{
  uint8_t tx[3] = { reg, (uint8_t)(v >> 8), (uint8_t)v };
  return i2c_master_transmit(a->dev, tx, sizeof tx, XFER_TIMEOUT_MS);
}

static esp_err_t read_reg(ads1115_t *a, uint8_t reg, uint16_t *v)
{
  uint8_t rx[2];
  ESP_RETURN_ON_ERROR(i2c_master_transmit_receive(a->dev, &reg, 1, rx, 2, XFER_TIMEOUT_MS), TAG, "rd");
  *v = (uint16_t)((rx[0] << 8) | rx[1]);
  return ESP_OK;
}

esp_err_t ads1115_read(ads1115_t *a, uint8_t channel, int16_t *code)
{
  uint16_t cfg = CFG_OS | CFG_MUX_AIN(channel & 3u) | CFG_PGA_4V096 | CFG_MODE_SHOT |
                 CFG_DR_860 | CFG_COMP_OFF;
  ESP_RETURN_ON_ERROR(write_reg(a, REG_CONFIG, cfg), TAG, "start");

  /* 860 SPS is 1.16 ms (+/-10 %). Two ticks at 1 kHz is 1-2 ms; then poll. */
  vTaskDelay(2);
  uint16_t st = 0;
  for (int i = 0; i < 8; i++) {
    ESP_RETURN_ON_ERROR(read_reg(a, REG_CONFIG, &st), TAG, "poll");
    if (st & CFG_OS) break;
    esp_rom_delay_us(100);
  }
  if (!(st & CFG_OS)) return ESP_ERR_TIMEOUT;

  uint16_t raw;
  ESP_RETURN_ON_ERROR(read_reg(a, REG_CONVERSION, &raw), TAG, "conv");
  *code = (int16_t)raw;
  return ESP_OK;
}

esp_err_t ads1115_start_continuous_diff01(ads1115_t *a)
{
  /* Hi_thresh MSB 1 and Lo_thresh MSB 0 turn ALERT/RDY into conversion
   * ready. */
  ESP_RETURN_ON_ERROR(write_reg(a, REG_LO_THRESH, 0x0000), TAG, "lo");
  ESP_RETURN_ON_ERROR(write_reg(a, REG_HI_THRESH, 0x8000), TAG, "hi");
  uint16_t cfg = CFG_MUX_DIFF01 | CFG_PGA_4V096 | CFG_MODE_CONT | CFG_DR_860 | CFG_COMP_1CONV;
  ESP_RETURN_ON_ERROR(write_reg(a, REG_CONFIG, cfg), TAG, "cfg");
  uint8_t ptr = REG_CONVERSION;
  return i2c_master_transmit(a->dev, &ptr, 1, XFER_TIMEOUT_MS);
}

esp_err_t ads1115_read_latest(ads1115_t *a, int16_t *code)
{
  uint8_t rx[2];
  ESP_RETURN_ON_ERROR(i2c_master_receive(a->dev, rx, 2, XFER_TIMEOUT_MS), TAG, "latest");
  *code = (int16_t)((rx[0] << 8) | rx[1]);
  return ESP_OK;
}
