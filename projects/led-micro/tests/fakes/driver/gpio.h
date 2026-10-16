#pragma once
#include "esp_err.h"
#include <cstdint>
enum gpio_num_t {GPIO_NUM_4=4,GPIO_NUM_16=16,GPIO_NUM_17=17};
constexpr int GPIO_MODE_OUTPUT=1,GPIO_PULLUP_DISABLE=0,GPIO_PULLDOWN_DISABLE=0,GPIO_INTR_DISABLE=0;
struct gpio_config_t {uint64_t pin_bit_mask=0;int mode=0,pull_up_en=0,pull_down_en=0,intr_type=0;};
esp_err_t gpio_set_level(gpio_num_t,int);
esp_err_t gpio_config(const gpio_config_t*);
esp_err_t gpio_reset_pin(gpio_num_t);
