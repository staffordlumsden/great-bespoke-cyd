#pragma once
#include "esp_err.h"
#include <cstddef>
using nvs_handle_t=unsigned;
constexpr int NVS_READWRITE=1,NVS_READONLY=0;
esp_err_t nvs_open(const char*,int,nvs_handle_t*);
esp_err_t nvs_set_blob(nvs_handle_t,const char*,const void*,size_t);
esp_err_t nvs_get_blob(nvs_handle_t,const char*,void*,size_t*);
esp_err_t nvs_commit(nvs_handle_t);
void nvs_close(nvs_handle_t);
