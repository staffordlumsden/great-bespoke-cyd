#include "http_usage.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "private_config.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>
namespace ou {
namespace {
struct Headers {
    char retry[32]{};
    bool json = false;
};
QueueHandle_t jobs = nullptr, results = nullptr;
esp_err_t on_event(esp_http_client_event_t *e) {
    auto *h = static_cast<Headers *>(e->user_data);
    if (e->event_id == HTTP_EVENT_ON_HEADER && e->header_key && e->header_value) {
        if (!strcasecmp(e->header_key, "Retry-After"))
            std::snprintf(h->retry, sizeof(h->retry), "%s", e->header_value);
        if (!strcasecmp(e->header_key, "Content-Type"))
            h->json = !strncasecmp(e->header_value, "application/json", 16) &&
                      (e->header_value[16] == 0 || e->header_value[16] == ';' ||
                       e->header_value[16] == ' ');
    }
    return ESP_OK;
}
bool request(const char *key, bool usage, FetchResult &out,
             const ProviderConfig *profile = nullptr) {
    const char *url = usage ? "https://ollama.com/api/usage?range=24h&scope=self"
                            : "https://ollama.com/api/balance";
    if (profile && profile->kind != ProviderKind::ollama)
        url = profile->kind == ProviderKind::openrouter ? "https://openrouter.ai/api/v1/key"
                                                        : profile->url;
    Headers headers{};
    esp_http_client_config_t cfg{};
    cfg.url = url;
    if (profile && profile->ca_pem[0])
        cfg.cert_pem = profile->ca_pem;
    else
        cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.disable_auto_redirect = true;
    cfg.timeout_ms = 8000;
    cfg.buffer_size = 2048;
    cfg.buffer_size_tx = 1024;
    cfg.event_handler = on_event;
    cfg.user_data = &headers;
    cfg.keep_alive_enable = false;
    auto *client = esp_http_client_init(&cfg);
    if (!client) {
        out.error = FetchError::memory;
        return false;
    }
    char auth[520];
    std::snprintf(auth, sizeof(auth), "Bearer %s", key);
    bool ok = esp_http_client_set_header(client, "Authorization", auth) == ESP_OK;
    wipe(auth, sizeof(auth));
    ok = ok && esp_http_client_set_header(client, "Accept", "application/json") == ESP_OK;
    char *body = nullptr;
    size_t size = 0;
    out.error = FetchError::transport;
    int64_t start = esp_timer_get_time();
    if (ok && esp_http_client_open(client, 0) == ESP_OK) {
        int64_t length = esp_http_client_fetch_headers(client);
        out.status = esp_http_client_get_status_code(client);
        out.retry_after = retry_seconds(headers.retry);
        if (out.status != 200)
            out.error = FetchError::http;
        else if (length < 0)
            out.error = FetchError::transport;
        else if (length > int64_t(response_limit))
            out.error = FetchError::too_large;
        else if (!headers.json)
            out.error = FetchError::content_type;
        else if (!(body = static_cast<char *>(std::malloc(response_limit + 1))))
            out.error = FetchError::memory;
        else {
            bool complete = false;
            while (esp_timer_get_time() - start < 25000000) {
                const size_t remaining = response_limit + 1 - size;
                if (!remaining) {
                    out.error = FetchError::too_large;
                    break;
                }
                const int count = esp_http_client_read(client, body + size,
                                                       int(remaining < 2048 ? remaining : 2048));
                if (count < 0) {
                    out.error = FetchError::transport;
                    break;
                }
                size += count;
                if (size > response_limit) {
                    out.error = FetchError::too_large;
                    break;
                }
                if (esp_http_client_is_complete_data_received(client)) {
                    complete = true;
                    break;
                }
                if (!count) {
                    out.error = FetchError::incomplete;
                    break;
                }
                // Yield to Wi-Fi and touch/UI work during chunked transfers.
                vTaskDelay(1);
            }
            if (complete) {
                body[size] = 0;
                bool parsed = false;
                if (profile &&
                    (profile->kind == ProviderKind::codex || profile->kind == ProviderKind::feed))
                    parsed = parse_feed(
                        body, size, profile->kind == ProviderKind::codex ? "codex" : profile->id,
                        out.metrics, out.model_error);
                else if (profile && profile->kind == ProviderKind::openrouter)
                    parsed = parse_openrouter(body, size, out.metrics, out.model_error);
                else
                    parsed = usage ? parse_usage(body, size, out.usage, out.model_error)
                                   : parse_balance(body, size, out.balance, out.model_error);
                out.error = parsed ? FetchError::none : FetchError::json;
                ok = parsed;
            } else {
                if (out.error == FetchError::transport)
                    out.error = FetchError::incomplete;
                ok = false;
            }
        }
    }
    if (body) {
        wipe(body, response_limit + 1);
        std::free(body);
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return ok && out.error == FetchError::none;
}
void worker(void *) {
    const ProviderConfig *profile = nullptr;
    for (;;)
        if (xQueueReceive(jobs, &profile, portMAX_DELAY) == pdTRUE) {
            FetchResult result{};
            result.balance_ok = request(profile->api_key, false, result, profile);
            if (result.balance_ok && profile->kind == ProviderKind::ollama)
                result.usage_ok = request(profile->api_key, true, result, profile);
            else if (result.balance_ok)
                result.usage_ok = true;
            // Copies fixed-sized values only. The key remains owned by the caller.
            xQueueSend(results, &result, portMAX_DELAY);
            profile = nullptr;
        }
}
} // namespace
bool fetch_init() {
    jobs = xQueueCreate(1, sizeof(const ProviderConfig *));
    results = xQueueCreate(1, sizeof(FetchResult));
    if (!jobs || !results)
        return false;
    return xTaskCreate(worker, "usage_https", 8192, nullptr, 3, nullptr) == pdPASS;
}
bool fetch_start(const ProviderConfig *profile) {
    return jobs && profile && xQueueSend(jobs, &profile, 0) == pdTRUE;
}
bool fetch_result(FetchResult &result) {
    return results && xQueueReceive(results, &result, 0) == pdTRUE;
}
const char *fetch_error_name(FetchError e) {
    switch (e) {
    case FetchError::none:
        return "OK";
    case FetchError::memory:
        return "LOW MEMORY";
    case FetchError::transport:
        return "HTTPS / NETWORK ERROR";
    case FetchError::too_large:
        return "RESPONSE TOO LARGE";
    case FetchError::content_type:
        return "NON-JSON RESPONSE";
    case FetchError::incomplete:
        return "INCOMPLETE RESPONSE";
    case FetchError::http:
        return "HTTP ERROR";
    case FetchError::json:
        return "UNSUPPORTED API DATA";
    case FetchError::stale:
        return "PROVIDER DATA IS STALE";
    }
    return "ERROR";
}
} // namespace ou
