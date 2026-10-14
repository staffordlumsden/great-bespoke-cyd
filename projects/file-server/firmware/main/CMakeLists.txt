idf_component_register(SRCS "app_main.cpp" "network.cpp" "file_server.cpp" "file_policy.cpp" "time_sync.cpp"
 INCLUDE_DIRS "." "../../../launcher/firmware/main"
 REQUIRES cyd_hal cyd_clock esp_wifi esp_event esp_netif nvs_flash esp_http_server app_update
          json fatfs mbedtls esp_timer esp_app_format esp_hw_support heap
 EMBED_FILES "../web/app.html.gz" "../web/pixel-thin.woff2" "../web/pixel-semibold.woff2")
