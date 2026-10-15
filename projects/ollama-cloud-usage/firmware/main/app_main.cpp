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
ou::PollPolicy poll{};
bool has_nvs = false, network_started = false, worker_ready = false, sntp = false;
char last_error[45]{};
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
void reload() {
    if (view.busy)
        return;
    auto *updated = new (std::nothrow) ou::Config{};
    if (!updated) {
        std::snprintf(last_error, sizeof(last_error), "Cannot reload: low memory");
        return;
    }
    ou::load_private_config(*updated);
    ou::wipe(config.api_key, sizeof(config.api_key));
    std::memcpy(config.api_key, updated->api_key, sizeof(config.api_key));
    config.key_ready = updated->key_ready;
    view.key = config.key_ready;
    view.has_balance = false;
    view.has_usage = false;
    view.stale_balance = false;
    view.stale_usage = false;
    view.balance = {};
    view.usage = {};
    ou::wipe(updated, sizeof(*updated));
    delete updated;
    // Network manager owns a pointer to config.network: never mutate it live.
    poll.manual(view.now);
    last_error[0] = 0;
}
void action(ou::Action a) {
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
    case ou::Action::refresh:
        if (!view.busy) {
            poll.manual(view.now);
            last_error[0] = 0;
        }
        break;
    case ou::Action::retry:
        if (!view.busy)
            cm::wifi_retry(view.now);
        break;
    case ou::Action::next:
        if (!view.busy)
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
        std::snprintf(last_error, sizeof(last_error), "Console unavailable");
        break;
    case ou::Action::none:
        break;
    }
}
void status() {
    if (!view.key)
        std::snprintf(view.status, sizeof(view.status), "Add API key / tap NET");
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
    else if (view.busy)
        std::snprintf(view.status, sizeof(view.status), "Wi-Fi ready / fetching usage");
    else if (last_error[0])
        std::snprintf(view.status, sizeof(view.status), "%s", last_error);
    else if (view.has_balance)
        std::snprintf(view.status, sizeof(view.status), "Updated %llus ago / next %llus",
                      static_cast<unsigned long long>(
                          std::min<uint64_t>(999999, (view.now - view.updated) / 1000)),
                      static_cast<unsigned long long>(std::min<uint64_t>(
                          999999, poll.due > view.now ? (poll.due - view.now + 999) / 1000 : 0)));
    else
        std::snprintf(view.status, sizeof(view.status), "Wi-Fi ready / starting usage");
}
} // namespace
extern "C" void app_main() {
    esp_log_level_set("*", ESP_LOG_NONE);
    has_nvs = nvs_flash_init() == ESP_OK;
    const auto board = cyd::hal_init();
    if (has_nvs)
        cyd::clock::init();
    theme(false);
    const bool healthy = has_nvs && board.display == ESP_OK && board.touch == ESP_OK &&
                         board.sd == ESP_OK && board.fat32;
    ou::confirm_first_boot(healthy);
    std::printf("OU_BOOT hardware_ok=%d acceptance=PENDING\n", healthy);
    if (board.sd == ESP_OK)
        ou::load_private_config(config);
    view.key = config.key_ready;
    view.networks = config.network.wifi_count;
    if (has_nvs && view.networks)
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
        const bool previous = view.connected;
        view.connected = cm::wifi_snapshot(view.now).state == cm::WifiState::connected;
        dirty |= previous != view.connected;
        const auto clock = cyd::clock::snapshot();
        view.epoch = clock.valid ? clock.epoch : 0;
        if (clock.valid)
            std::snprintf(view.time, sizeof(view.time), "%02u:%02u", unsigned(clock.civil.hour),
                          unsigned(clock.civil.minute));
        if (view.connected && !sntp && (clock.automatic || !clock.valid)) {
            esp_sntp_config_t time_config = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(
                2, ESP_SNTP_SERVER_LIST("au.pool.ntp.org", "time.cloudflare.com"));
            time_config.wait_for_sync = false;
            sntp = esp_netif_sntp_init(&time_config) == ESP_OK;
        }
        ou::FetchResult result{};
        if (view.busy && ou::fetch_result(result)) {
            view.busy = false;
            if (result.balance_ok) {
                view.balance = result.balance;
                view.has_balance = true;
                view.updated = view.now;
            }
            if (result.usage_ok) {
                view.usage = result.usage;
                view.has_usage = true;
                view.usage_updated = view.now;
            }
            view.stale_balance = !result.balance_ok && view.has_balance;
            view.stale_usage = !result.usage_ok && view.has_usage;
            const bool complete = result.balance_ok && result.usage_ok;
            poll.finish(view.now, complete, result.status, result.retry_after);
            if (complete)
                last_error[0] = 0;
            else if (result.status == 401)
                std::snprintf(last_error, sizeof(last_error), "API key rejected / tap NET");
            else if (result.status == 403)
                std::snprintf(last_error, sizeof(last_error), "Account access denied / NET");
            else if (result.error == ou::FetchError::http)
                std::snprintf(last_error, sizeof(last_error), "HTTP %d / retry delayed",
                              result.status);
            else
                std::snprintf(last_error, sizeof(last_error), "%s",
                              ou::fetch_error_name(result.error));
            std::printf("OU_FETCH balance_ok=%d usage_ok=%d status=%d error=%d\n",
                        result.balance_ok, result.usage_ok, result.status, int(result.error));
            dirty = true;
        }
        // Data becomes stale with age or a dropped link, never silently reads as fresh.
        if (view.has_balance && (!view.connected || view.now - view.updated > 120000))
            view.stale_balance = true;
        if (view.has_usage && (!view.connected || view.now - view.usage_updated > 120000))
            view.stale_usage = true;
        int x = 0, y = 0;
        const bool down = board.touch == ESP_OK && cyd::hal_touch_poll(x, y);
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
        }
        if (!view.sleeping && !pressed && view.now - last_touch >= 300000) {
            view.sleeping = true;
            cyd::hal_display_suspend();
        }
        if (!view.sleeping && worker_ready && view.key && view.connected && view.epoch &&
            !view.busy && poll.ready(view.now)) {
            if (ou::fetch_start(config.api_key)) {
                view.busy = true;
                poll.begin(view.now);
                dirty = true;
            }
        }
        if (!view.sleeping && board.display == ESP_OK && (dirty || view.now >= paint_due)) {
            status();
            ou::prepare(view);
            cyd::hal_render(ou::pixel, &view);
            paint_due = view.now + 1000;
            dirty = false;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
