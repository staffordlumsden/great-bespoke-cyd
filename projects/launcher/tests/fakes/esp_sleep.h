#pragma once
#include "esp_err.h"
enum esp_sleep_wakeup_cause_t {ESP_SLEEP_WAKEUP_UNDEFINED,ESP_SLEEP_WAKEUP_EXT0};
esp_err_t esp_sleep_enable_ext0_wakeup(int pin,int level);
inline esp_sleep_wakeup_cause_t esp_sleep_get_wakeup_cause(){return ESP_SLEEP_WAKEUP_UNDEFINED;}
[[noreturn]] void esp_deep_sleep_start();
