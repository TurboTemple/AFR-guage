idf_component_register(SRCS "settings_store.c"
                       INCLUDE_DIRS "."
                       REQUIRES afr_core
                       PRIV_REQUIRES nvs_flash)
