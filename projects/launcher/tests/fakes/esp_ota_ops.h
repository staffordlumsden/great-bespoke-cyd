#pragma once
#include "esp_partition.h"
#include "esp_app_desc.h"
using esp_ota_handle_t=unsigned;
constexpr size_t OTA_WITH_SEQUENTIAL_WRITES=0xfffffffe;
const esp_partition_t* esp_ota_get_running_partition();
esp_err_t esp_ota_get_partition_description(const esp_partition_t*,esp_app_desc_t*);
esp_err_t esp_ota_begin(const esp_partition_t*,size_t,esp_ota_handle_t*);
esp_err_t esp_ota_write(esp_ota_handle_t,const void*,size_t);
esp_err_t esp_ota_abort(esp_ota_handle_t);
esp_err_t esp_ota_end(esp_ota_handle_t);
esp_err_t esp_ota_set_boot_partition(const esp_partition_t*);
