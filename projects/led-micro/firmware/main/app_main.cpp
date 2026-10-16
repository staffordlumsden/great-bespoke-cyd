#include "console_bridge.h"
#include "cyd_hal.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_model.h"
#include "led_output.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "touch.h"
#include "ui.h"
#include <cstdio>
#include <cstring>

namespace {
using namespace ledapp;
constexpr const char *settings_namespace = "led_ui";
void encode(const State &s, uint8_t (&data)[8]) {
    data[0] = 1;
    data[1] = s.light;
    data[2] = s.brightness;
    data[3] = s.colour.r;
    data[4] = s.colour.g;
    data[5] = s.colour.b;
    data[6] = unsigned(s.mode);
    data[7] = s.speed;
}
void load(State &s) {
    nvs_handle_t h{};
    if (nvs_open(settings_namespace, NVS_READONLY, &h) == ESP_OK) {
        uint8_t data[8]{};
        size_t length = sizeof(data);
        if (nvs_get_blob(h, "settings", data, &length) == ESP_OK && length == 8 && data[0] == 1) {
            s.light = data[1] != 0;
            s.brightness = data[2];
            s.colour = {data[3], data[4], data[5]};
            s.mode = static_cast<Mode>(data[6]);
            s.speed = data[7];
        }
        nvs_close(h);
    }
    sanitize(s);
}
bool save(const State &s) {
    nvs_handle_t h{};
    if (nvs_open(settings_namespace, NVS_READWRITE, &h) != ESP_OK)
        return false;
    uint8_t data[8];
    encode(s, data);
    auto err = nvs_set_blob(h, "settings", data, sizeof(data));
    if (err == ESP_OK)
        err = nvs_commit(h);
    nvs_close(h);
    return err == ESP_OK;
}
} // namespace
extern "C" void app_main() {
    using namespace ledapp;
    esp_log_level_set("*", ESP_LOG_NONE);
    const auto led_error = led_init(); // Start all three channels off before display/SD setup.
    const auto nvs_error = nvs_flash_init(); // Never erase NVS on error.
    const auto board = cyd::hal_init();
    View view;
    view.led_ok = led_error == ESP_OK && nvs_error == ESP_OK && board.display == ESP_OK &&
                  board.touch == ESP_OK;
    view.console_ok = console_available();
    if (nvs_error == ESP_OK)
        load(view.state);
    if (!view.led_ok)
        led_shutdown();
    confirm_first_boot(view.led_ok); // SD is optional: this app has no card data.
    std::printf("LED_BOARD display=%d touch=%d pwm=%d settings=%d sd_optional=1 "
                "power=OFF\n",
                board.display == ESP_OK, board.touch == ESP_OK, led_error == ESP_OK,
                nvs_error == ESP_OK);
    Touch touch;
    bool full_render = true, save_pending = false;
    uint64_t save_after = 0;
    uint8_t last_saved[8];
    encode(view.state, last_saved);
    Colour last_output{};
    unsigned last_brightness = 0;
    for (;;) {
        const uint64_t now = cyd::hal_uptime_ms();
        int x = 0, y = 0;
        const bool down = board.touch == ESP_OK && cyd::hal_touch_poll(x, y);
        const auto action = touch.sample(down, down ? hit(view, x, y) : Action::none);
        if (action == Action::console) {
            view.state.enabled = false;
            led_shutdown(); // Detach PWM and force all outputs HIGH before any
                            // handoff.
            if (save_pending && nvs_error == ESP_OK)
                view.settings_ok = save(view.state);
            cyd::hal_sd_unmount();
            if (!return_to_console()) {
                view.console_ok = false;
                view.led_ok = led_init() == ESP_OK && view.led_ok;
                last_output = {};
                last_brightness = 0;
                full_render = true;
                std::printf("LED_CONSOLE unavailable power=OFF\n");
            }
        } else if (apply(view.state, action, now)) {
            full_render = true;
            uint8_t current[8];
            encode(view.state, current);
            save_pending = std::memcmp(current, last_saved, 8) != 0;
            save_after = now + 700;
        }
        if (save_pending && now >= save_after && nvs_error == ESP_OK) {
            view.settings_ok = save(view.state);
            if (view.settings_ok)
                encode(view.state, last_saved);
            save_pending = false;
            full_render = true;
        }
        view.actual = view.led_ok ? output(view.state, now) : Colour{};
        const bool changed = view.actual != last_output || view.state.brightness != last_brightness;
        if (changed && view.led_ok) {
            if (led_write(view.actual, view.state.brightness) != ESP_OK) {
                view.led_ok = false;
                view.state.enabled = false;
                view.actual = {};
                full_render = true;
                std::printf("LED_PWM error power=OFF\n");
            }
        }
        last_output = view.actual;
        last_brightness = view.state.brightness;
        if (board.display == ESP_OK && (full_render || changed)) {
            prepare(view);
            if (full_render)
                cyd::hal_render(pixel);
            else if (view.state.page != Page::mixer)
                cyd::hal_render_region(110, 44, 26, 26, pixel);
            full_render = false;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
