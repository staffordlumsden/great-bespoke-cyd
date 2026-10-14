#pragma once
using esp_err_t=int;
constexpr int ESP_OK=0,ESP_FAIL=-1;
inline const char* esp_err_to_name(esp_err_t code){return code==ESP_OK?"ESP_OK":"ESP_FAIL";}
