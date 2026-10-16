#pragma once
#include "esp_err.h"
#include "led_model.h"
namespace ledapp {
esp_err_t led_init();
esp_err_t led_write(Colour colour, unsigned brightness);
void led_shutdown(); // detach PWM, drive all active-low channels HIGH
} // namespace ledapp
