idf_component_register(SRCS "app_main.c" "sampler.c"
                       INCLUDE_DIRS "."
                       REQUIRES afr_core drv_ads1115 drv_spartan logger settings power ui
                                nvs_flash esp_timer esp_driver_gpio esp_app_format)
