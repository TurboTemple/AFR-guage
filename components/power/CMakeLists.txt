# Board housekeeping: AXP2101 power path and the PCF85063 clock.
idf_component_register(SRCS "power.c" "rtc_pcf85063.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_driver_i2c
                       PRIV_REQUIRES freertos esp_system)
