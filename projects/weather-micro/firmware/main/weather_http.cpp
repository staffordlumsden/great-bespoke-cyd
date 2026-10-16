#include "weather_http.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>
namespace wm {
namespace {
QueueHandle_t jobs = nullptr, results = nullptr;
struct Headers {
    bool json = false;
    unsigned retry = 0;
};
esp_err_t event(esp_http_client_event_t *e) {
    if (e->event_id != HTTP_EVENT_ON_HEADER || !e->header_key || !e->header_value)
        return ESP_OK;
    auto &h = *static_cast<Headers *>(e->user_data);
    if (!strcasecmp(e->header_key, "Content-Type"))
        h.json =
            !strncasecmp(e->header_value, "application/json", 16) &&
            (e->header_value[16] == 0 || e->header_value[16] == ';' || e->header_value[16] == ' ');
    if (!strcasecmp(e->header_key, "Retry-After")) {
        const char *p = e->header_value;
        unsigned n = 0;
        bool valid = *p;
        for (; *p; ++p) {
            if (*p < '0' || *p > '9' || n > 3600) {
                valid = false;
                break;
            }
            n = n * 10 + unsigned(*p - '0');
        }
        if (valid)
            h.retry = std::min(n, 3600u);
    }
    return ESP_OK;
}
bool request(const Job &job, Result &out) {
    char url[768]{};
    if (job.kind == JobKind::forecast)
        forecast_url(job.location, url, sizeof(url));
    else if (!search_url(job.query, url, sizeof(url))) {
        out.error = FetchError::json;
        return false;
    }
    if (!url[0]) {
        out.error = FetchError::json;
        return false;
    }
    Headers headers;
    esp_http_client_config_t cfg{};
    cfg.url = url;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.disable_auto_redirect = true;
    cfg.timeout_ms = 8000;
    cfg.buffer_size = 2048;
    cfg.buffer_size_tx = 1024;
    cfg.event_handler = event;
    cfg.user_data = &headers;
    auto *client = esp_http_client_init(&cfg);
    if (!client) {
        out.error = FetchError::memory;
        return false;
    }
    out.error = FetchError::transport;
    char *body = nullptr;
    size_t size = 0;
    bool parsed = false;
    const auto start = esp_timer_get_time();
    if (esp_http_client_set_header(client, "Accept", "application/json") == ESP_OK &&
        esp_http_client_open(client, 0) == ESP_OK) {
        auto length = esp_http_client_fetch_headers(client);
        out.status = esp_http_client_get_status_code(client);
        out.retry_after = headers.retry;
        if (out.status != 200)
            out.error = FetchError::http;
        else if (length < 0)
            out.error = FetchError::transport;
        else if (length > int64_t(max_response))
            out.error = FetchError::too_large;
        else if (!headers.json)
            out.error = FetchError::content_type;
        else if (!(body = static_cast<char *>(std::malloc(max_response + 1))))
            out.error = FetchError::memory;
        else {
            bool complete = false;
            while (esp_timer_get_time() - start < 25000000) {
                size_t left = max_response + 1 - size;
                if (!left) {
                    out.error = FetchError::too_large;
                    break;
                }
                int n =
                    esp_http_client_read(client, body + size, int(std::min(left, size_t(2048))));
                if (n < 0) {
                    out.error = FetchError::transport;
                    break;
                }
                size += n;
                if (size > max_response) {
                    out.error = FetchError::too_large;
                    break;
                }
                if (esp_http_client_is_complete_data_received(client)) {
                    complete = true;
                    break;
                }
                if (!n) {
                    out.error = FetchError::incomplete;
                    break;
                }
                vTaskDelay(1);
            }
            if (complete) {
                body[size] = 0;
                parsed = job.kind == JobKind::forecast ? parse_forecast(body, size, out.forecast)
                                                       : parse_search(body, size, out.search);
                out.error = parsed ? FetchError::none : FetchError::json;
            } else if (out.error == FetchError::transport)
                out.error = FetchError::incomplete;
        }
    }
    std::free(body);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return parsed;
}
void worker(void *) {
    Job job;
    static Result result;
    for (;;)
        if (xQueueReceive(jobs, &job, portMAX_DELAY) == pdTRUE) {
            // Reset metadata in place; parsers replace only their successful payload.
            // A temporary Result exceeds 2 KB and must not live on this task stack.
            result.ok = false;
            result.error = FetchError::none;
            result.status = 0;
            result.retry_after = 0;
            result.kind = job.kind;
            result.place_id = job.location.id;
            result.generation = job.generation;
            result.ok = request(job, result);
            std::printf("WM_FETCH kind=%u ok=%u status=%d stack_min_bytes=%u values=WITHHELD\n",
                        unsigned(job.kind), result.ok, result.status,
                        unsigned(uxTaskGetStackHighWaterMark(nullptr)));
            xQueueSend(results, &result, portMAX_DELAY);
        }
}
} // namespace
bool fetch_init() {
    jobs = xQueueCreate(1, sizeof(Job));
    results = xQueueCreate(1, sizeof(Result));
    return jobs && results &&
           xTaskCreate(worker, "weather_https", 8192, nullptr, 3, nullptr) == pdPASS;
}
bool fetch_start(const Job &job) { return jobs && xQueueSend(jobs, &job, 0) == pdTRUE; }
bool fetch_result(Result &out) { return results && xQueueReceive(results, &out, 0) == pdTRUE; }
const char *fetch_error(FetchError e) {
    switch (e) {
    case FetchError::none:
        return "Updated";
    case FetchError::memory:
        return "Low memory";
    case FetchError::http:
        return "Server unavailable";
    case FetchError::content_type:
    case FetchError::json:
        return "Invalid weather data";
    case FetchError::too_large:
        return "Response too large";
    case FetchError::incomplete:
        return "Download incomplete";
    default:
        return "Connection failed";
    }
}
} // namespace wm
