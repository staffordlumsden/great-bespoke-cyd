set(CM_MAIN "${CMAKE_CURRENT_LIST_DIR}/../../../../common/terminal-core")

idf_component_register(
    SRCS "app_main.cpp"
         "console_bridge.cpp"
         "config_store.cpp"
         "terminal_renderer.cpp"
         "terminal_ui.cpp"
         "${CM_MAIN}/wifi_policy.cpp"
         "${CM_MAIN}/wifi_manager.cpp"
         "${CM_MAIN}/ssh_host_verifier.cpp"
         "${CM_MAIN}/ssh_client.cpp"
         "${CM_MAIN}/ssh_terminal.cpp"
         "${CM_MAIN}/terminal_parser.cpp"
    INCLUDE_DIRS "." "${CM_MAIN}"
                 "../../../launcher/firmware/main"
                 "../../../launcher/firmware/components/cyd_hal/include"
                 "../../../launcher/firmware/components/cyd_clock/include"
    REQUIRES cyd_hal cyd_clock cyd_keyboard libssh2_esp esp_app_format esp_system
             esp_hw_support spi_flash heap freertos esp_lcd esp_driver_gpio
             esp_driver_spi fatfs sdmmc esp_rom mbedtls json esp_wifi esp_event
             esp_netif nvs_flash esp_timer bt esp_hid app_update
)
