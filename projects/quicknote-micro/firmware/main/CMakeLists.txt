idf_component_register(
    SRCS "app_main.cpp" "note_model.cpp" "quicknote_ui.cpp" "console_bridge.cpp"
    INCLUDE_DIRS "." "../../../launcher/firmware/main"
    REQUIRES cyd_hal cyd_clock cyd_keyboard nvs_flash app_update fatfs esp_app_format)
