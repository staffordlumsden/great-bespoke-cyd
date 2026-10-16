set(CM_MAIN "${CMAKE_CURRENT_LIST_DIR}/../../../../common/terminal-core")
idf_component_register(SRCS "app_main.cpp" "console_bridge.cpp" "weather_model.cpp" "weather_http.cpp" "network_config.cpp" "ui.cpp" "${CM_MAIN}/wifi_manager.cpp" "${CM_MAIN}/wifi_policy.cpp"
 INCLUDE_DIRS "." "../../../launcher/firmware/main" "${CM_MAIN}"
 REQUIRES cyd_hal cyd_clock nvs_flash app_update esp_app_format esp_wifi esp_event esp_netif esp_http_client mbedtls json esp_timer lwip)

# Keep large UI/snapshot buffers off embedded task stacks; retain compiler evidence.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wframe-larger-than=2048 -Werror=frame-larger-than=2048 -fstack-usage)
