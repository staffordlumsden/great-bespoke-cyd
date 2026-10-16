idf_component_register(SRCS "app_main.cpp" "console_bridge.cpp" "led_model.cpp" "led_output.cpp" "ui.cpp"
    INCLUDE_DIRS "." "../../../launcher/firmware/main"
    REQUIRES cyd_hal nvs_flash app_update esp_app_format esp_driver_ledc esp_driver_gpio)
