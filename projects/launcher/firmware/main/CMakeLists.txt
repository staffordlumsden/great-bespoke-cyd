idf_component_register(SRCS "app_main.cpp" "firmware_image.cpp" "sd_installer.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES cyd_hal cyd_clock nvs_flash app_update mbedtls
                       PRIV_REQUIRES esp_app_format esp_hw_support esp_driver_gpio)
