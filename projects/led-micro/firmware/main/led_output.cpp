#include "led_output.h"
#include "driver/gpio.h"
#include "driver/ledc.h"

namespace ledapp {
namespace {
constexpr gpio_num_t pins[] = {GPIO_NUM_4, GPIO_NUM_16, GPIO_NUM_17};
unsigned configured = 0;
bool ready = false;
void high_outputs() {
    gpio_config_t cfg{};
    for (auto pin : pins) {
        gpio_set_level(pin, 1);
        cfg.pin_bit_mask |= 1ULL << pin;
    }
    cfg.mode = GPIO_MODE_OUTPUT;
    cfg.pull_up_en = GPIO_PULLUP_DISABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cfg.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&cfg);
    for (auto pin : pins)
        gpio_set_level(pin, 1);
}
} // namespace
void led_shutdown() {
    // ledc_stop's idle level goes through output inversion: 0 means physical
    // HIGH.
    for (unsigned i = 0; i < configured; ++i)
        ledc_stop(LEDC_LOW_SPEED_MODE, static_cast<ledc_channel_t>(i), 0);
    configured = 0;
    ready = false;
    for (auto pin : pins)
        gpio_reset_pin(pin);
    high_outputs();
}
esp_err_t led_init() {
    high_outputs();
    ledc_timer_config_t timer{};
    timer.speed_mode = LEDC_LOW_SPEED_MODE;
    timer.duty_resolution = LEDC_TIMER_12_BIT;
    timer.timer_num = LEDC_TIMER_0;
    timer.freq_hz = 4000;
    timer.clk_cfg = LEDC_USE_APB_CLK;
    auto err = ledc_timer_config(&timer);
    if (err != ESP_OK) {
        led_shutdown();
        return err;
    }
    for (unsigned i = 0; i < 3; ++i) {
        ledc_channel_config_t channel{};
        channel.gpio_num = pins[i];
        channel.speed_mode = LEDC_LOW_SPEED_MODE;
        channel.channel = static_cast<ledc_channel_t>(i);
        channel.intr_type = LEDC_INTR_DISABLE;
        channel.timer_sel = LEDC_TIMER_0;
        channel.duty = 0;
        channel.flags.output_invert = 1;
        err = ledc_channel_config(&channel);
        if (err != ESP_OK) {
            led_shutdown();
            return err;
        }
        ++configured;
    }
    ready = true;
    return ESP_OK;
}
esp_err_t led_write(Colour colour, unsigned brightness) {
    if (!ready)
        return ESP_ERR_INVALID_STATE;
    const uint8_t values[] = {colour.r, colour.g, colour.b};
    for (unsigned i = 0; i < 3; ++i) {
        auto channel = static_cast<ledc_channel_t>(i);
        auto err = ledc_set_duty(LEDC_LOW_SPEED_MODE, channel, pwm_duty(values[i], brightness));
        if (err == ESP_OK)
            err = ledc_update_duty(LEDC_LOW_SPEED_MODE, channel);
        if (err != ESP_OK) {
            led_shutdown();
            return err;
        }
    }
    return ESP_OK;
}
} // namespace ledapp
