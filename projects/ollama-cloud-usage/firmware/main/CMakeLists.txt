set(CM_MAIN "${CMAKE_CURRENT_LIST_DIR}/../../../../common/terminal-core")
idf_component_register(SRCS "app_main.cpp" "console_bridge.cpp" "private_config.cpp"
 "usage_model.cpp" "ui.cpp" "http_usage.cpp" "${CM_MAIN}/wifi_manager.cpp" "${CM_MAIN}/wifi_policy.cpp"
 INCLUDE_DIRS "." "../../../launcher/firmware/main" "${CM_MAIN}"
 REQUIRES cyd_hal cyd_clock nvs_flash app_update esp_app_format esp_wifi esp_event
 esp_netif esp_http_client mbedtls json esp_timer lwip)
