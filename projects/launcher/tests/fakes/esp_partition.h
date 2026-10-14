#pragma once
#include <cstdint>
#include <cstddef>
#include "esp_err.h"
enum esp_partition_subtype_t {ESP_PARTITION_SUBTYPE_APP_FACTORY=0,ESP_PARTITION_SUBTYPE_DATA_OTA=0,ESP_PARTITION_SUBTYPE_APP_OTA_0=16};
constexpr int ESP_PARTITION_TYPE_APP=0,ESP_PARTITION_TYPE_DATA=1;
struct esp_partition_t {int type;esp_partition_subtype_t subtype;uint32_t address,size;};
const esp_partition_t* esp_partition_find_first(int,esp_partition_subtype_t,const char*);
esp_err_t esp_partition_read(const esp_partition_t*,size_t,void*,size_t);
