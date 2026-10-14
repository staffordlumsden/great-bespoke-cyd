idf_component_register(SRCS "cyd_hal.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES driver esp_lcd fatfs sdmmc esp_timer nvs_flash)
