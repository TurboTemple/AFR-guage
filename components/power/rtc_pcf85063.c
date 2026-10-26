#include "rtc_pcf85063.h"

#include "esp_check.h"

static const char *TAG = "rtc";

#define RTC_ADDR     0x51
#define REG_SECONDS  0x04 /* bit7: oscillator stopped */

static i2c_master_dev_handle_t s_dev;

static uint8_t bcd2bin(uint8_t v) { return (uint8_t)((v >> 4) * 10 + (v & 0x0F)); }
static uint8_t bin2bcd(int v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

esp_err_t rtc_init(i2c_master_bus_handle_t bus)
{
  i2c_device_config_t dc = {
    .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    .device_address = RTC_ADDR,
    .scl_speed_hz = 400000,
  };
  return i2c_master_bus_add_device(bus, &dc, &s_dev);
}

esp_err_t rtc_read(struct tm *tm, bool *valid)
{
  uint8_t reg = REG_SECONDS, r[7];
  ESP_RETURN_ON_ERROR(i2c_master_transmit_receive(s_dev, &reg, 1, r, sizeof r, 20), TAG, "read");
  *valid = !(r[0] & 0x80);
  tm->tm_sec = bcd2bin(r[0] & 0x7F);
  tm->tm_min = bcd2bin(r[1] & 0x7F);
  tm->tm_hour = bcd2bin(r[2] & 0x3F);
  tm->tm_mday = bcd2bin(r[3] & 0x3F);
  tm->tm_wday = r[4] & 0x07;
  tm->tm_mon = bcd2bin(r[5] & 0x1F) - 1;
  tm->tm_year = bcd2bin(r[6]) + 100;
  tm->tm_isdst = 0;
  if (tm->tm_mon < 0 || tm->tm_mon > 11 || tm->tm_mday < 1 || tm->tm_hour > 23) *valid = false;
  return ESP_OK;
}

esp_err_t rtc_write(const struct tm *tm)
{
  uint8_t b[8] = {
    REG_SECONDS,
    bin2bcd(tm->tm_sec),  /* OS flag cleared */
    bin2bcd(tm->tm_min),
    bin2bcd(tm->tm_hour),
    bin2bcd(tm->tm_mday),
    (uint8_t)(tm->tm_wday & 7),
    bin2bcd(tm->tm_mon + 1),
    bin2bcd(tm->tm_year % 100),
  };
  return i2c_master_transmit(s_dev, b, sizeof b, 20);
}
