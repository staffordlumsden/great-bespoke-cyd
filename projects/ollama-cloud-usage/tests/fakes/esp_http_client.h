#pragma once
#include <cstdint>
using esp_err_t = int;
constexpr esp_err_t ESP_OK = 0;
struct esp_http_client_event_t {
    int event_id;
    void *user_data;
    char *header_key;
    char *header_value;
};
constexpr int HTTP_EVENT_ON_HEADER = 1;
struct esp_http_client_config_t {
    const char *url = nullptr;
    int (*crt_bundle_attach)(void *) = nullptr;
    bool disable_auto_redirect = false;
    int timeout_ms = 0, buffer_size = 0, buffer_size_tx = 0;
    esp_err_t (*event_handler)(esp_http_client_event_t *) = nullptr;
    void *user_data = nullptr;
    bool keep_alive_enable = false;
};
using esp_http_client_handle_t = void *;
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *);
esp_err_t esp_http_client_set_header(esp_http_client_handle_t, const char *, const char *);
esp_err_t esp_http_client_open(esp_http_client_handle_t, int);
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t);
int esp_http_client_get_status_code(esp_http_client_handle_t);
int esp_http_client_read(esp_http_client_handle_t, char *, int);
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t);
void esp_http_client_close(esp_http_client_handle_t);
void esp_http_client_cleanup(esp_http_client_handle_t);
