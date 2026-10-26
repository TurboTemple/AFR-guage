/* PCF85063 real-time clock on the onboard I2C bus (0x51).
 *
 * Its backup supply comes through the board's power circuit, so without the
 * LiPo it may lose time at every ignition-off. The oscillator-stop flag
 * then marks the time invalid. */
#pragma once

#include <stdbool.h>
#include <time.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t pcf85063_init(i2c_master_bus_handle_t bus);
/* Fills tm; *valid false if the oscillator stopped since the last set. */
esp_err_t pcf85063_read(struct tm *tm, bool *valid);
/* Sets the time and clears the oscillator-stop flag. */
esp_err_t pcf85063_write(const struct tm *tm);

#ifdef __cplusplus
}
#endif
