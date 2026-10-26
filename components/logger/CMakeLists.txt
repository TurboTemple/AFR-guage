idf_component_register(SRCS "logger.c"
                       INCLUDE_DIRS "."
                       REQUIRES afr_core freertos
                       PRIV_REQUIRES nvs_flash esp_timer vfs fatfs)
