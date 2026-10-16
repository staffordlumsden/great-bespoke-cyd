#include "cache_model.h"
#include "console_bridge.h"
#include "cyd_clock.h"
#include "cyd_hal.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "network_config.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "ui.h"
#include "weather_http.h"
#include "wifi_manager.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
namespace {
using namespace wm;
View view;
cm::AppConfig network;
CacheBook caches;
bool has_nvs = false, net_started = false, worker = false, job_busy = false, sntp = false,
     redraw = true;
uint32_t search_generation = 0;
uint64_t search_after = 0, message_until = 0;
char message[45]{};
void status(const char *text, uint64_t now) {
    std::snprintf(message, sizeof(message), "%s", text);
    message_until = now + 5000;
    redraw = true;
}
Cache *cache(int64_t id, bool create = true) { return caches.find(id, create); }
void prune() { caches.prune(view.settings); }
bool persist() {
    if (!has_nvs)
        return false;
    auto *data = static_cast<char *>(std::malloc(max_settings));
    if (!data)
        return false;
    bool ok = encode_settings(view.settings, data, max_settings);
    nvs_handle_t h{};
    if (ok && nvs_open("wx_ui", NVS_READWRITE, &h) == ESP_OK) {
        auto e = nvs_set_blob(h, "settings", data, std::strlen(data));
        if (e == ESP_OK)
            e = nvs_commit(h);
        ok = e == ESP_OK;
        nvs_close(h);
    } else
        ok = false;
    std::free(data);
    return ok;
}
void load() {
    auto *data = static_cast<char *>(std::malloc(max_settings));
    if (!data)
        return;
    nvs_handle_t h{};
    if (nvs_open("wx_ui", NVS_READONLY, &h) == ESP_OK) {
        size_t n = max_settings;
        if (nvs_get_blob(h, "settings", data, &n) == ESP_OK && n <= max_settings)
            decode_settings(data, n, view.settings);
        nvs_close(h);
    }
    std::free(data);
}
void refresh(uint64_t now) {
    if (view.settings.count) {
        auto *c = cache(view.settings.places[view.settings.selected].id);
        if (c && now >= c->manual_after)
            c->due = now;
        else
            status("Please wait before refreshing", now);
    }
}
void action(Action a, uint64_t now) {
    bool save = false;
    if (a.kind != Kind::none) {
        message_until = 0;
        redraw = true;
    }
    switch (a.kind) {
    case Kind::page:
        if (a.value <= unsigned(Page::about)) {
            if (a.value == unsigned(Page::current) && view.page == Page::current)
                refresh(now);
            view.page = static_cast<Page>(a.value);
        }
        break;
    case Kind::console:
        if (console_available()) {
            cyd::hal_sd_unmount();
            if (!return_to_console()) {
                view.console_ok = false;
                status("Console return unavailable", now);
            }
        }
        break;
    case Kind::theme:
        view.settings.light = !view.settings.light;
        save = true;
        break;
    case Kind::unit:
        view.settings.fahrenheit = !view.settings.fahrenheit;
        save = true;
        break;
    case Kind::places:
        view.page = Page::locations;
        view.list_page = 0;
        break;
    case Kind::add:
        view.page = Page::search;
        view.query[0] = 0;
        view.keyboard_page = 0;
        ++search_generation;
        break;
    case Kind::place:
        if (a.value < view.settings.count) {
            view.manage = a.value;
            view.page = Page::place;
        }
        break;
    case Kind::use:
        if (view.manage < view.settings.count) {
            view.settings.selected = view.manage;
            view.page = Page::current;
            save = true;
        }
        break;
    case Kind::remove:
        remove_place(view.settings, view.manage);
        prune();
        view.list_page =
            std::min(view.list_page, view.settings.count ? (view.settings.count - 1) / 3 : 0);
        view.page = Page::locations;
        save = true;
        break;
    case Kind::previous:
        if (view.page == Page::locations && view.list_page)
            --view.list_page;
        if (view.page == Page::results && view.result_page)
            --view.result_page;
        break;
    case Kind::next:
        if (view.page == Page::locations && (view.list_page + 1) * 3 < view.settings.count)
            ++view.list_page;
        if (view.page == Page::results && (view.result_page + 1) * 3 < view.results.count)
            ++view.result_page;
        break;
    case Kind::key: {
        size_t n = std::strlen(view.query);
        if (n < 40 && a.value >= 32 && a.value <= 126) {
            view.query[n] = char(a.value);
            view.query[n + 1] = 0;
            ++search_generation;
        }
        break;
    }
    case Kind::erase: {
        size_t n = std::strlen(view.query);
        if (n) {
            view.query[n - 1] = 0;
            ++search_generation;
        }
        break;
    }
    case Kind::keyboard:
        view.keyboard_page = 1 - view.keyboard_page;
        break;
    case Kind::search:
        if (worker && !job_busy && view.connected && view.clock_valid &&
            std::strlen(view.query) >= 2 && now >= search_after) {
            Job job;
            job.kind = JobKind::search;
            job.generation = ++search_generation;
            std::strcpy(job.query, view.query);
            if (fetch_start(job)) {
                job_busy = true;
                search_after = now + 3000;
            }
        }
        break;
    case Kind::result:
        if (a.value < view.results.count) {
            if (save_place(view.settings, view.results.places[a.value])) {
                view.page = Page::current;
                save = true;
            } else
                status("List full / remove a saved place", now);
        }
        break;
    case Kind::retry:
        if (!job_busy)
            cm::wifi_retry(now);
        break;
    case Kind::wifi_next:
        if (!job_busy)
            cm::wifi_next(now);
        break;
    case Kind::refresh:
        refresh(now);
        break;
    case Kind::back:
        if (view.page == Page::place)
            view.page = Page::locations;
        else if (view.page == Page::results)
            view.page = Page::search;
        else {
            view.page = Page::settings;
            ++search_generation;
        }
        break;
    default:
        break;
    }
    if (save && !persist())
        status("Settings not saved", now);
}
class Touch {
    bool armed = false, pressed = false, cancelled = false;
    unsigned releases = 0;
    Action start{};

  public:
    Action sample(bool down, Action current) {
        if (!armed) {
            releases = down ? 0 : releases + 1;
            if (releases >= 3) {
                armed = true;
                releases = 0;
            }
            return {};
        }
        if (down) {
            releases = 0;
            if (!pressed) {
                pressed = true;
                cancelled = false;
                start = current;
            } else if (current != start)
                cancelled = true;
        } else if (pressed && ++releases >= 3) {
            pressed = false;
            releases = 0;
            return cancelled ? Action{} : start;
        }
        return {};
    }
};
} // namespace
extern "C" void app_main() {
    using namespace wm;
    esp_log_level_set("*", ESP_LOG_NONE);
    has_nvs = nvs_flash_init() == ESP_OK;
    const auto board = cyd::hal_init();
    if (has_nvs) {
        cyd::clock::init();
        load();
    }
    view.console_ok = console_available();
    load_networks(network);
    worker = fetch_init();
    net_started = network.wifi_count && cm::wifi_start(network, cyd::hal_uptime_ms());
    const bool healthy = has_nvs && board.display == ESP_OK && board.touch == ESP_OK &&
                         board.sd == ESP_OK && board.fat32 && worker;
    bool boot_confirmed = false;
    std::printf("WM_BOOT healthy=%d worker=%d networks=%u values=WITHHELD\n", healthy, worker,
                unsigned(network.wifi_count));
    if (!healthy)
        status("Startup issue / return to Console", cyd::hal_uptime_ms());
    if (!network.wifi_count)
        status("Set Wi-Fi in File Server first", cyd::hal_uptime_ms());
    Touch touch;
    bool connected = false;
    int64_t minute = -1;
    uint64_t second = 0;
    static Result result;
    for (;;) {
        uint64_t now = cyd::hal_uptime_ms();
        if (net_started)
            cm::wifi_poll(now);
        auto wifi = cm::wifi_snapshot(now);
        view.connected = net_started && wifi.state == cm::WifiState::connected;
        auto clock = cyd::clock::snapshot();
        view.clock_valid = clock.valid;
        view.now = clock.epoch;
        if (view.connected != connected) {
            connected = view.connected;
            redraw = true;
        }
        if (view.connected && !sntp && (clock.automatic || !clock.valid)) {
            esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(
                2, ESP_SNTP_SERVER_LIST("time.google.com", "pool.ntp.org"));
            sntp = esp_netif_sntp_init(&cfg) == ESP_OK;
        }
        if (fetch_result(result)) {
            job_busy = false;
            redraw = true;
            if (result.kind == JobKind::search) {
                search_after = std::max(search_after, now + uint64_t(result.retry_after) * 1000);
                if (result.generation == search_generation && view.page == Page::search) {
                    if (result.ok) {
                        view.results = result.search;
                        view.result_page = 0;
                        view.page = Page::results;
                    } else
                        status(fetch_error(result.error), now);
                }
            } else {
                auto *c = caches.response(view.settings, result.place_id);
                if (c) {
                    if (result.ok && (result.forecast.current_time > view.now + 3600 ||
                                      view.now - result.forecast.current_time > 7200)) {
                        result.ok = false;
                        result.error = FetchError::json;
                    }
                    if (result.ok) {
                        c->forecast = result.forecast;
                        c->has = true;
                        c->updated = now;
                        c->due = now + 900000;
                        c->failures = 0;
                        c->error[0] = 0;
                    } else {
                        c->manual_after =
                            std::max(c->manual_after, now + uint64_t(result.retry_after) * 1000);
                        c->failures = std::min(c->failures + 1, 5u);
                        c->due = now +
                                 std::max(uint64_t(std::min(60u << (c->failures - 1), 900u)) * 1000,
                                          uint64_t(result.retry_after) * 1000);
                        std::snprintf(c->error, sizeof(c->error), "%s", fetch_error(result.error));
                    }
                }
            }
        }
        int x = 0, y = 0;
        bool down = board.touch == ESP_OK && cyd::hal_touch_poll(x, y);
        action(touch.sample(down, down ? hit(view, x, y) : Action{}), now);
        view.busy = job_busy;
        if (view.settings.count) {
            auto *c = cache(view.settings.places[view.settings.selected].id);
            view.has = c && c->has;
            if (c) {
                view.forecast = c->forecast;
                view.stale =
                    c->has && (!view.connected || c->error[0] || now - c->updated > 1800000);
                if (worker && view.connected && view.clock_valid && !job_busy && now >= c->due &&
                    view.page != Page::search && view.page != Page::results) {
                    Job job;
                    job.location = view.settings.places[view.settings.selected];
                    if (fetch_start(job)) {
                        job_busy = true;
                        view.busy = true;
                        c->manual_after = now + 30000;
                        redraw = true;
                    }
                }
            }
        } else {
            view.has = false;
            view.forecast = Forecast{};
            view.stale = false;
        }
        view.status[0] = 0;
        if (message_until > now)
            std::snprintf(view.status, sizeof(view.status), "%s", message);
        else if (!worker)
            std::snprintf(view.status, sizeof(view.status), "Network worker unavailable");
        else if (!network.wifi_count)
            std::snprintf(view.status, sizeof(view.status), "Wi-Fi setup: File Server > Networks");
        else if (view.connected && !view.clock_valid)
            std::snprintf(view.status, sizeof(view.status), "Synchronising time for HTTPS");
        else if (view.settings.count && !job_busy) {
            auto *c = cache(view.settings.places[view.settings.selected].id);
            if (c && c->error[0])
                std::snprintf(view.status, sizeof(view.status), "%s%.36s",
                              view.has ? "STALE / " : "", c->error);
        }
        if (view.now / 60 != minute || now / 1000 != second) {
            if (view.now / 60 != minute) {
                minute = view.now / 60;
                redraw = true;
            }
            if (message_until && now >= message_until) {
                message_until = 0;
                redraw = true;
            }
            second = now / 1000;
        }
        if (redraw && board.display == ESP_OK) {
            prepare(view);
            const bool drawn = cyd::hal_render(pixel) == ESP_OK;
            if (!boot_confirmed && healthy && drawn) {
                confirm_first_boot(true);
                boot_confirmed = true;
                std::printf("WM_UI_READY main_stack_free=%u values=WITHHELD\n",
                            unsigned(uxTaskGetStackHighWaterMark(nullptr)));
            }
            std::printf("WM_UI page=%u stack_min_bytes=%u values=WITHHELD\n", unsigned(view.page),
                        unsigned(uxTaskGetStackHighWaterMark(nullptr)));
            redraw = false;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
