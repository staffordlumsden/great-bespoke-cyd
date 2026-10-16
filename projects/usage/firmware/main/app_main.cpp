#include "console_bridge.h"
#include "cyd_clock.h"
#include "cyd_hal.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "http_usage.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "private_config.h"
#include "ui.h"
#include "wifi_manager.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <new>
namespace {
ou::Config config{};
ou::View view{};
struct State {
    ou::Balance balance{};
    ou::Usage usage{};
    ou::Metrics metrics{};
    ou::PollPolicy poll{};
    bool has = false, has_usage = false, stale = false, usage_stale = false;
    uint64_t updated = 0, usage_updated = 0;
    char error[45]{};
};
State states[ou::max_providers]{};
bool has_nvs = false, network_started = false, worker_ready = false, sntp = false;
int job = -1;
unsigned round_robin = 0;
void theme(bool save) {
    nvs_handle_t h{};
    uint8_t value = 0;
    if (has_nvs && nvs_open("ou_ui", save ? NVS_READWRITE : NVS_READONLY, &h) == ESP_OK) {
        if (save) {
            nvs_set_u8(h, "light", view.light);
            nvs_commit(h);
        } else {
            nvs_get_u8(h, "light", &value);
            view.light = value != 0;
        }
        nvs_close(h);
    }
}
void select_source(unsigned index) {
    if (index >= config.provider_count)
        return;
    view.selected_provider = index;
    view.page = ou::Page::balance;
}
void reload() {
    if (job >= 0)
        return;
    auto *updated = new (std::nothrow) ou::Config{};
    if (!updated)
        return;
    ou::load_private_config(*updated);
    // The Wi-Fi manager retains the original AppConfig pointer. Only provider
    // settings are replaceable live; Wi-Fi changes take effect on relaunch.
    ou::wipe(config.providers, sizeof(config.providers));
    std::memcpy(config.providers, updated->providers, sizeof(config.providers));
    config.provider_count = updated->provider_count;
    ou::wipe(config.api_key, sizeof(config.api_key));
    std::memcpy(config.api_key, updated->api_key, sizeof(config.api_key));
    config.key_ready = updated->key_ready;
    ou::wipe(updated, sizeof(*updated));
    delete updated;
    view.selected_provider = 0;
    // Keep cooldowns by source position conservatively, including Retry-After.
    for (auto &s : states) {
        auto policy = s.poll;
        s = {};
        s.poll = policy;
        s.poll.manual(view.now);
    }
}
void action(ou::Action a) {
    if (a >= ou::Action::source0 && a <= ou::Action::source3) {
        select_source(unsigned(a) - unsigned(ou::Action::source0));
        return;
    }
    switch (a) {
    case ou::Action::theme:
        view.light = !view.light;
        theme(true);
        break;
    case ou::Action::page:
        view.page = view.page == ou::Page::balance ? ou::Page::activity : ou::Page::balance;
        break;
    case ou::Action::network:
        view.page = view.page == ou::Page::network ? ou::Page::balance : ou::Page::network;
        break;
    case ou::Action::sources:
        view.page = view.page == ou::Page::sources ? ou::Page::balance : ou::Page::sources;
        break;
    case ou::Action::add:
        view.page = ou::Page::add;
        break;
    case ou::Action::refresh:
        if (job < 0 && view.selected_provider < config.provider_count) {
            states[view.selected_provider].poll.manual(view.now);
            states[view.selected_provider].error[0] = 0;
        }
        break;
    case ou::Action::retry:
        if (job < 0)
            cm::wifi_retry(view.now);
        break;
    case ou::Action::next:
        if (job < 0)
            cm::wifi_next(view.now);
        break;
    case ou::Action::reload:
        reload();
        break;
    case ou::Action::console:
        if (ou::console_available()) {
            cyd::hal_sd_unmount();
            ou::return_to_console();
        }
        break;
    default:
        break;
    }
}
void sync_view() {
    view.provider_count = config.provider_count;
    view.networks = config.network.wifi_count;
    for (unsigned i = 0; i < config.provider_count; ++i) {
        std::strcpy(view.provider_names[i], config.providers[i].label);
        view.provider_ready[i] = config.providers[i].ready;
    }
    if (!config.provider_count) {
        view.key = false;
        view.has_balance = false;
        view.has_usage = false;
        std::strcpy(view.provider_name, "ADD SOURCE");
        view.page = view.page == ou::Page::network ? view.page : ou::Page::add;
        return;
    }
    const auto &p = config.providers[view.selected_provider];
    auto &s = states[view.selected_provider];
    view.kind = p.kind;
    view.key = p.ready;
    std::strcpy(view.provider_name, p.label);
    view.balance = s.balance;
    view.usage = s.usage;
    view.metrics = s.metrics;
    view.has_balance = s.has;
    view.has_usage = s.has_usage;
    view.updated = s.updated;
    view.usage_updated = s.usage_updated;
    view.stale_balance = s.has && (s.stale || !view.connected || view.now - s.updated > 120000);
    if (s.has && (p.kind == ou::ProviderKind::codex || p.kind == ou::ProviderKind::feed) &&
        view.epoch > s.metrics.updated_at + 120)
        view.stale_balance = true;
    view.stale_usage =
        s.has_usage && (s.usage_stale || !view.connected || view.now - s.usage_updated > 120000);
    view.busy = job >= 0;
}
void status() {
    if (!view.key)
        std::snprintf(view.status, sizeof(view.status), "Source needs setup / tap NET");
    else if (!view.networks)
        std::snprintf(view.status, sizeof(view.status), "No Wi-Fi profile / tap NET");
    else if (!network_started)
        std::snprintf(view.status, sizeof(view.status), "Wi-Fi initialization failed");
    else if (!view.connected)
        std::snprintf(view.status, sizeof(view.status), "Wi-Fi joining / tap NET");
    else if (!view.epoch)
        std::snprintf(view.status, sizeof(view.status), "Wi-Fi ready / waiting for time");
    else if (!worker_ready)
        std::snprintf(view.status, sizeof(view.status), "HTTPS worker unavailable");
    else if (job >= 0)
        std::snprintf(view.status, sizeof(view.status), "Wi-Fi ready / updating sources");
    else if (config.provider_count && states[view.selected_provider].error[0])
        std::snprintf(view.status, sizeof(view.status), "%s", states[view.selected_provider].error);
    else if (view.has_balance)
        std::snprintf(view.status, sizeof(view.status), "Updated %llus ago / tap source name",
                      static_cast<unsigned long long>(
                          std::min<uint64_t>(999999, (view.now - view.updated) / 1000)));
    else
        std::snprintf(view.status, sizeof(view.status), "Wi-Fi ready / waiting for usage");
}
void completed(ou::FetchResult &r) {
    if (job < 0)
        return;
    auto &s = states[job];
    const auto &p = config.providers[job];
    if (r.balance_ok && (p.kind == ou::ProviderKind::codex || p.kind == ou::ProviderKind::feed) &&
        !ou::feed_is_fresh(r.metrics, view.epoch)) {
        r.balance_ok = false;
        r.usage_ok = false;
        r.error = ou::FetchError::stale;
    }
    if (r.balance_ok) {
        s.balance = r.balance;
        s.metrics = r.metrics;
        s.has = true;
        s.updated = view.now;
    }
    if (r.usage_ok) {
        s.usage = r.usage;
        s.has_usage = true;
        s.usage_updated = view.now;
    }
    s.stale = !r.balance_ok && s.has;
    s.usage_stale = !r.usage_ok && s.has_usage;
    bool good = r.balance_ok && r.usage_ok;
    s.poll.finish(view.now, good, r.status, r.retry_after);
    if (good)
        s.error[0] = 0;
    else if (r.status == 401)
        std::snprintf(s.error, sizeof(s.error), "Source key rejected / tap NET");
    else if (r.status == 403)
        std::snprintf(s.error, sizeof(s.error), "Source access denied / tap NET");
    else if (r.error == ou::FetchError::http)
        std::snprintf(s.error, sizeof(s.error), "HTTP %d / retry delayed", r.status);
    else
        std::snprintf(s.error, sizeof(s.error), "%s", ou::fetch_error_name(r.error));
    std::printf("USAGE_FETCH source=%u ok=%d status=%d error=%d\n", unsigned(job), good, r.status,
                int(r.error));
    job = -1;
}
} // namespace
extern "C" void app_main() {
    esp_log_level_set("*", ESP_LOG_NONE);
    has_nvs = nvs_flash_init() == ESP_OK;
    const auto board = cyd::hal_init();
    if (has_nvs)
        cyd::clock::init();
    theme(false);
    bool healthy = has_nvs && board.display == ESP_OK && board.touch == ESP_OK &&
                   board.sd == ESP_OK && board.fat32;
    ou::confirm_first_boot(healthy);
    std::printf("USAGE_BOOT version=0.2.0 hardware_ok=%d acceptance=PENDING\n", healthy);
    if (board.sd == ESP_OK)
        ou::load_private_config(config);
    if (has_nvs && config.network.wifi_count)
        network_started = cm::wifi_start(config.network, cyd::hal_uptime_ms());
    worker_ready = ou::fetch_init();
    bool pressed = false;
    unsigned released = 0;
    int tx = 0, ty = 0;
    uint64_t last_touch = cyd::hal_uptime_ms(), paint_due = 0;
    bool dirty = true;
    for (;;) {
        view.now = cyd::hal_uptime_ms();
        cm::wifi_poll(view.now);
        bool previous = view.connected;
        view.connected = cm::wifi_snapshot(view.now).state == cm::WifiState::connected;
        dirty |= previous != view.connected;
        auto clock = cyd::clock::snapshot();
        view.epoch = clock.valid ? clock.epoch : 0;
        if (view.connected && !sntp && (clock.automatic || !clock.valid)) {
            esp_sntp_config_t time_config = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(
                2, ESP_SNTP_SERVER_LIST("au.pool.ntp.org", "time.cloudflare.com"));
            time_config.wait_for_sync = false;
            sntp = esp_netif_sntp_init(&time_config) == ESP_OK;
        }
        ou::FetchResult result{};
        if (job >= 0 && ou::fetch_result(result)) {
            completed(result);
            dirty = true;
        }
        sync_view();
        int x = 0, y = 0;
        bool down = board.touch == ESP_OK && cyd::hal_touch_poll(x, y);
        if (down) {
            if (!pressed) {
                tx = x;
                ty = y;
                pressed = true;
            }
            released = 0;
        } else if (pressed && ++released >= 3) {
            pressed = false;
            last_touch = view.now;
            if (view.sleeping) {
                view.sleeping = false;
                cyd::hal_display_resume();
            } else
                action(ou::hit(view, tx, ty));
            dirty = true;
            sync_view();
        }
        if (!view.sleeping && !pressed && view.now - last_touch >= 300000) {
            view.sleeping = true;
            cyd::hal_display_suspend();
        }
        if (!view.sleeping && worker_ready && view.connected && view.epoch && job < 0) {
            for (unsigned attempt = 0; attempt < config.provider_count; ++attempt) {
                unsigned index = (round_robin + attempt) % config.provider_count;
                auto &s = states[index];
                if (!config.providers[index].ready || !s.poll.ready(view.now))
                    continue;
                if (ou::fetch_start(&config.providers[index])) {
                    job = int(index);
                    s.poll.begin(view.now);
                    round_robin = (index + 1) % config.provider_count;
                    dirty = true;
                }
                break;
            }
        }
        if (!view.sleeping && board.display == ESP_OK && (dirty || view.now >= paint_due)) {
            sync_view();
            status();
            ou::prepare(view);
            cyd::hal_render(ou::pixel, &view);
            paint_due = view.now + 1000;
            dirty = false;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
