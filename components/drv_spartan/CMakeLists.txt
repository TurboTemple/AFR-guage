idf_component_register(SRCS "spartan.c"
                       INCLUDE_DIRS "."
                       REQUIRES afr_core
                       PRIV_REQUIRES esp_driver_uart freertos)
