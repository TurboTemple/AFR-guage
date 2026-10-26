/* ADS1115 driver, two chips on one I2C controller (I2C1, GPIO17/18):
 *
 *   0x48 (ADDR to GND)  lambda: continuous AIN0-AIN1 differential, 860 SPS,
 *                       ALERT/RDY pulses at each conversion (to GPIO43)
 *   0x49 (ADDR to VDD)  housekeeping: single-shot AIN0 LED, AIN1 TPS,
 *                       AIN2 supply
 *
 * +/-4.096 V full scale, 125 uV/LSB, on both. */
#pragma once

#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ADS1115_ADDR_LAMBDA 0x48
#define ADS1115_ADDR_AUX    0x49
#define ADS1115_I2C_HZ      400000

typedef struct {
  i2c_master_dev_handle_t dev;
} ads1115_t;

esp_err_t ads1115_bus_create(i2c_port_num_t port, gpio_num_t sda, gpio_num_t scl,
                             i2c_master_bus_handle_t *bus);
/* Probe and add a chip. ESP_ERR_NOT_FOUND if it does not answer. */
esp_err_t ads1115_attach(i2c_master_bus_handle_t bus, uint8_t addr, ads1115_t *a);

/* Single-shot conversion of AINx against GND. Blocks about 1.5 ms. */
esp_err_t ads1115_read(ads1115_t *a, uint8_t channel, int16_t *code);

/* Continuous AIN0-AIN1 at 860 SPS, ALERT/RDY as conversion ready (an 8 us
 * low pulse per conversion). Leaves the register pointer on the conversion
 * register, so ads1115_read_latest() is a single 2-byte read. */
esp_err_t ads1115_start_continuous_diff01(ads1115_t *a);
esp_err_t ads1115_read_latest(ads1115_t *a, int16_t *code);

#ifdef __cplusplus
}
#endif
