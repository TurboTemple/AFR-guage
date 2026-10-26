/* ADS1115 on its own I2C controller: single-shot, single-ended,
 * +/-4.096 V full scale (125 uV/LSB), 860 SPS. */
#pragma once

#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ADS1115_ADDR      0x48
#define ADS1115_I2C_HZ    400000

typedef struct {
  i2c_master_bus_handle_t bus;
  i2c_master_dev_handle_t dev;
} ads1115_t;

/* Create the I2C bus and probe the chip. ESP_ERR_NOT_FOUND if it does not
 * answer. */
esp_err_t ads1115_init(ads1115_t *a, i2c_port_num_t port, gpio_num_t sda, gpio_num_t scl,
                       uint8_t addr);
/* One single-shot conversion of AINx against GND. Blocks about 1.5 ms. */
esp_err_t ads1115_read(ads1115_t *a, uint8_t channel, int16_t *code);

#ifdef __cplusplus
}
#endif
