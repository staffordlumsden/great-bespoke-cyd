// Compile the actual ESP transport against deterministic API fakes. These do
// not prove ESP TLS/heap behaviour, but exercise bounds, headers and failures.
#include "../firmware/main/http_usage.cpp"
#include <algorithm>
#include <cassert>
#include <iostream>
#include <string>
namespace {
struct Scenario {
    std::string body =
        R"({"included":{"session":{"remaining_percent":75,"resets_at":"2026-10-15T14:00:00Z"},"weekly":{"remaining_percent":10,"resets_at":"2026-10-18T13:00:00Z"}},"purchased":{"balance_usd":0}})";
    std::string type = "application/json", retry;
    int status = 200;
    int64_t length = -2;
    size_t position = 0;
    int chunk = 17, reads = 0;
    bool truncated = false, fail_open = false, fail_read = false, closed = false;
    std::string expected_url;
    bool private_ca = false;
    esp_http_client_config_t cfg{};
} current;
} // namespace
int esp_crt_bundle_attach(void *) { return 0; }
int64_t esp_timer_get_time() {
    static int64_t n = 0;
    return ++n;
}
void vTaskDelay(unsigned) {}
int xTaskCreate(void (*)(void *), const char *, unsigned, void *, unsigned, void *) {
    return pdPASS;
}
QueueHandle_t xQueueCreate(unsigned, size_t) { return reinterpret_cast<void *>(1); }
int xQueueSend(QueueHandle_t, const void *, unsigned) { return pdTRUE; }
int xQueueReceive(QueueHandle_t, void *, unsigned) { return 0; }
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *cfg) {
    current.cfg = *cfg;
    assert(cfg->disable_auto_redirect);
    if (current.private_ca)
        assert(cfg->cert_pem && cfg->crt_bundle_attach == nullptr);
    else
        assert(cfg->crt_bundle_attach == esp_crt_bundle_attach);
    if (current.expected_url.empty())
        assert(std::string(cfg->url).find("https://ollama.com/api/") == 0);
    else
        assert(std::string(cfg->url) == current.expected_url);
    return &current;
}
esp_err_t esp_http_client_set_header(esp_http_client_handle_t, const char *name,
                                     const char *value) {
    if (std::string(name) == "Authorization")
        assert(std::string(value) == "Bearer public-test-key-123456");
    return ESP_OK;
}
esp_err_t esp_http_client_open(esp_http_client_handle_t, int) {
    return current.fail_open ? -1 : ESP_OK;
}
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t) {
    auto event = [&](const char *key, std::string &val) {
        esp_http_client_event_t e{HTTP_EVENT_ON_HEADER, current.cfg.user_data,
                                  const_cast<char *>(key), val.data()};
        current.cfg.event_handler(&e);
    };
    event("Content-Type", current.type);
    if (!current.retry.empty())
        event("Retry-After", current.retry);
    return current.length == -2 ? int64_t(current.body.size()) : current.length;
}
int esp_http_client_get_status_code(esp_http_client_handle_t) { return current.status; }
int esp_http_client_read(esp_http_client_handle_t, char *out, int capacity) {
    ++current.reads;
    if (current.fail_read)
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
int main() {
    auto fetch = []() {
        ou::FetchResult r{};
        bool ok = ou::request("public-test-key-123456", false, r);
        assert(current.closed);
        return std::make_pair(ok, r);
    };
    assert(fetch().first);
    current = Scenario{};
    current.length = 0;
    current.type = "Application/JSON; charset=utf-8";
    assert(fetch().first); // chunked
    for (int status : {301, 302, 401, 403, 429, 503}) {
        current = Scenario{};
        current.status = status;
        current.retry = "180";
        auto r = fetch();
        assert(!r.first && r.second.status == status && r.second.retry_after == 180 &&
               current.reads == 0);
    }
    current = Scenario{};
    current.type = "text/html";
    assert(fetch().second.error == ou::FetchError::content_type && current.reads == 0);
    current = Scenario{};
    current.type = "application/json-malicious";
    assert(fetch().second.error == ou::FetchError::content_type);
    current = Scenario{};
    current.body = "{} x";
    assert(fetch().second.error == ou::FetchError::json);
    current = Scenario{};
    current.length = ou::response_limit + 1;
    assert(fetch().second.error == ou::FetchError::too_large && current.reads == 0);
    current = Scenario{};
    current.length = 0;
    current.body = std::string(ou::response_limit + 100, 'x');
    current.chunk = 1000;
    assert(fetch().second.error == ou::FetchError::too_large);
    current = Scenario{};
    current.truncated = true;
    assert(fetch().second.error == ou::FetchError::incomplete);
    current = Scenario{};
    current.fail_read = true;
    assert(!fetch().first);
    current = Scenario{};
    current.fail_open = true;
    assert(!fetch().first);
    ou::ProviderConfig profile{};
    profile.kind = ou::ProviderKind::codex;
    std::strcpy(profile.id, "codex");
    std::strcpy(profile.url, "https://example.test/v1/providers/codex");
    std::strcpy(profile.ca_pem, "public test CA placeholder");
    current = Scenario{};
    current.private_ca = true;
    current.expected_url = profile.url;
    current.body =
        R"({"schema":1,"provider":"codex","updated_at":1792108800,"meters":[{"label":"5H","used_percent":2}],"stats":[]})";
    ou::FetchResult custom{};
    assert(ou::request("public-test-key-123456", false, custom, &profile) &&
           custom.metrics.meter_count == 1);
    profile.kind = ou::ProviderKind::openrouter;
    profile.ca_pem[0] = 0;
    current = Scenario{};
    current.expected_url = "https://openrouter.ai/api/v1/key";
    current.body = R"({"data":{"usage":0,"limit":null}})";
    ou::FetchResult router{};
    assert(ou::request("public-test-key-123456", false, router, &profile) &&
           router.metrics.stat_count == 1);
    std::puts(
        "OU_HTTP transport failures / chunking / limits / TLS-config / redirects PASS (fakes)");
}
