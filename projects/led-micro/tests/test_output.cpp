#include "driver/gpio.h"
#include "driver/ledc.h"
#include "led_output.h"
#include <cassert>
#include <cstdio>
namespace {
int levels[40]{};
unsigned duty[3]{};
bool attached[3]{};
int fail_channel = -1, fail_update = -1;
bool fail_timer = false;
constexpr int pins[] = {4, 16, 17};
} // namespace
esp_err_t gpio_set_level(gpio_num_t pin, int level) {
    levels[pin] = level;
    return ESP_OK;
}
esp_err_t gpio_config(const gpio_config_t *c) {
    assert(c->pin_bit_mask == ((1ULL << 4) | (1ULL << 16) | (1ULL << 17)));
    assert(c->mode == GPIO_MODE_OUTPUT);
    return ESP_OK;
}
esp_err_t gpio_reset_pin(gpio_num_t pin) {
    for (int i = 0; i < 3; ++i)
        if (pins[i] == pin)
            attached[i] = false;
    return ESP_OK;
}
esp_err_t ledc_timer_config(const ledc_timer_config_t *c) {
    assert(c->speed_mode == LEDC_LOW_SPEED_MODE && c->duty_resolution == 12 && c->freq_hz == 4000 &&
           c->clk_cfg == LEDC_USE_APB_CLK);
    return fail_timer ? ESP_FAIL : ESP_OK;
}
esp_err_t ledc_channel_config(const ledc_channel_config_t *c) {
    const int i = c->channel;
    assert(c->gpio_num == pins[i] && c->flags.output_invert == 1 && c->duty == 0);
    if (fail_channel == i)
        return ESP_FAIL;
    attached[i] = true;
    duty[i] = c->duty;
    return ESP_OK;
}
esp_err_t ledc_set_duty(int, ledc_channel_t c, unsigned d) {
    assert(attached[c] && d <= 4095);
    duty[c] = d;
    return ESP_OK;
}
esp_err_t ledc_update_duty(int, ledc_channel_t c) { return fail_update == c ? ESP_FAIL : ESP_OK; }
esp_err_t ledc_stop(int, ledc_channel_t c, unsigned idle) {
    assert(idle == 0);
    attached[c] = false;
    duty[c] = 0;
    return ESP_OK;
}
void off() {
    for (int i = 0; i < 3; ++i)
        assert(levels[pins[i]] == 1 && !attached[i]);
}
int main() {
    assert(ledapp::led_write({100, 0, 0}, 100) == ESP_ERR_INVALID_STATE);
    assert(ledapp::led_init() == ESP_OK);
    for (int i = 0; i < 3; ++i)
        assert(attached[i] && duty[i] == 0);
    assert(ledapp::led_write({100, 50, 0}, 100) == ESP_OK);
    assert(duty[0] == 4095 && duty[1] == 2048 && duty[2] == 0);
    assert(ledapp::led_write({100, 100, 100}, 0) == ESP_OK);
    for (auto d : duty)
        assert(d == 0);
    ledapp::led_shutdown();
    off();
    ledapp::led_shutdown();
    off();
    for (int i = 0; i < 3; ++i) {
        fail_channel = i;
        assert(ledapp::led_init() == ESP_FAIL);
        off();
    }
    fail_channel = -1;
    fail_timer = true;
    assert(ledapp::led_init() == ESP_FAIL);
    off();
    fail_timer = false;
    for (int i = 0; i < 3; ++i) {
        assert(ledapp::led_init() == ESP_OK);
        fail_update = i;
        assert(ledapp::led_write({100, 100, 100}, 100) == ESP_FAIL);
        off();
    }
    fail_update = -1;
    assert(ledapp::led_init() == ESP_OK);
    ledapp::led_shutdown();
    off();
    std::puts("PASS active-low PWM mapping/init, zero brightness, GPIO4/16/17, "
              "shutdown and injected timer/channel/update failures");
}
