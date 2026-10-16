#pragma once
#include "esp_err.h"
enum ledc_channel_t {LEDC_CHANNEL_0,LEDC_CHANNEL_1,LEDC_CHANNEL_2};
constexpr int LEDC_LOW_SPEED_MODE=0,LEDC_TIMER_12_BIT=12,LEDC_TIMER_0=0,LEDC_USE_APB_CLK=1,LEDC_INTR_DISABLE=0;
struct ledc_timer_config_t {int speed_mode=0,duty_resolution=0,timer_num=0,freq_hz=0,clk_cfg=0;};
struct ledc_channel_config_t {int gpio_num=0,speed_mode=0;ledc_channel_t channel{};int intr_type=0,timer_sel=0;unsigned duty=0;struct {unsigned output_invert=0;} flags;};
esp_err_t ledc_timer_config(const ledc_timer_config_t*);
esp_err_t ledc_channel_config(const ledc_channel_config_t*);
esp_err_t ledc_set_duty(int,ledc_channel_t,unsigned);
esp_err_t ledc_update_duty(int,ledc_channel_t);
esp_err_t ledc_stop(int,ledc_channel_t,unsigned);
