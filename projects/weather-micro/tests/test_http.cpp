#include "../firmware/main/weather_http.cpp"
#include <cassert>
#include <fstream>
#include <sstream>
#include <string>
namespace {
struct Scenario {
    std::string body, type = "application/json", retry, url;
    int status = 200, chunk = 17;
    int64_t length = -2;
    size_t position = 0;
    bool truncated = false, open_error = false, read_error = false, closed = false;
    esp_http_client_config_t cfg{};
} current;
} // namespace
int esp_crt_bundle_attach(void *) { return 0; }
int64_t esp_timer_get_time() {
    static int64_t t = 0;
    return ++t;
}
void vTaskDelay(unsigned) {}
unsigned uxTaskGetStackHighWaterMark(void *) { return 4096; }
int xTaskCreate(void (*)(void *), const char *, unsigned, void *, unsigned, void *) {
    return pdPASS;
}
QueueHandle_t xQueueCreate(unsigned, size_t) { return reinterpret_cast<void *>(1); }
int xQueueSend(QueueHandle_t, const void *, unsigned) { return pdTRUE; }
int xQueueReceive(QueueHandle_t, void *, unsigned) { return 0; }
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *cfg) {
    assert(cfg->disable_auto_redirect && cfg->crt_bundle_attach == esp_crt_bundle_attach &&
           !cfg->cert_pem && cfg->timeout_ms == 8000);
    current.url = cfg->url;
    assert(current.url.find("https://api.open-meteo.com/v1/forecast?") == 0 ||
           current.url.find("https://geocoding-api.open-meteo.com/v1/search?") == 0);
    current.cfg = *cfg;
    return &current;
}
esp_err_t esp_http_client_set_header(esp_http_client_handle_t, const char *name,
                                     const char *value) {
    assert(std::string(name) == "Accept" && std::string(value) == "application/json");
    return ESP_OK;
}
esp_err_t esp_http_client_open(esp_http_client_handle_t, int) {
    return current.open_error ? -1 : ESP_OK;
}
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t) {
    auto event = [&](const char *name, std::string &value) {
        esp_http_client_event_t e{HTTP_EVENT_ON_HEADER, current.cfg.user_data,
                                  const_cast<char *>(name), value.data()};
        current.cfg.event_handler(&e);
    };
    event("Content-Type", current.type);
    if (!current.retry.empty())
        event("Retry-After", current.retry);
    return current.length == -2 ? int64_t(current.body.size()) : current.length;
}
int esp_http_client_get_status_code(esp_http_client_handle_t) { return current.status; }
int esp_http_client_read(esp_http_client_handle_t, char *out, int capacity) {
    if (current.read_error)
        return -1;
    size_t n =
        std::min({size_t(capacity), size_t(current.chunk), current.body.size() - current.position});
    std::memcpy(out, current.body.data() + current.position, n);
    current.position += n;
    return int(n);
}
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t) {
    return !current.truncated && current.position == current.body.size();
}
void esp_http_client_close(esp_http_client_handle_t) { current.closed = true; }
void esp_http_client_cleanup(esp_http_client_handle_t) {}
std::string read(const std::string &name) {
    std::ifstream in(name);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}
int main(int argc, char **argv) {
    assert(argc == 2);
    const auto forecast = read(std::string(argv[1]) + "/forecast.json"),
               search = read(std::string(argv[1]) + "/search.json");
    wm::Job job;
    job.location.latitude = -33.8688;
    job.location.longitude = 151.2093;
    auto run = [&]() {
        wm::Result r;
        r.ok = wm::request(job, r);
        assert(current.closed);
        return r;
    };
    auto reset = [&]() {
        current = Scenario{};
        current.body = forecast;
    };
    reset();
    assert(run().ok);
    reset();
    current.length = 0;
    current.type = "Application/JSON; charset=utf-8";
    assert(run().ok);
    for (int status : {301, 302, 401, 403, 429, 500, 503}) {
        reset();
        current.status = status;
        current.retry = "120";
        auto r = run();
        assert(!r.ok && r.error == wm::FetchError::http && r.retry_after == 120);
    }
    reset();
    current.length = wm::max_response + 1;
    assert(run().error == wm::FetchError::too_large);
    reset();
    current.length = 0;
    current.body = std::string(wm::max_response + 1, ' ');
    assert(run().error == wm::FetchError::too_large);
    reset();
    current.truncated = true;
    assert(run().error == wm::FetchError::incomplete);
    reset();
    current.type = "text/html";
    assert(run().error == wm::FetchError::content_type);
    reset();
    current.open_error = true;
    assert(!run().ok);
    reset();
    current.read_error = true;
    assert(!run().ok);
    reset();
    current.body = "{}";
    assert(run().error == wm::FetchError::json);
    reset();
    job.kind = wm::JobKind::search;
    std::strcpy(job.query, "Sydney, AU");
    current.body = search;
    assert(run().ok);
    assert(current.url.find("Sydney%2C%20AU") != std::string::npos);
    std::puts("PASS actual weather HTTPS adapter: both fixed origins, trust bundle, no "
              "auth/redirects, chunking, limits, HTTP/parse/transport failures");
}
