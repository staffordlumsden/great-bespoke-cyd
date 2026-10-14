#pragma once
#include <cstdint>
struct esp_chip_info_t {uint16_t revision=301;};
inline void esp_chip_info(esp_chip_info_t* info){info->revision=301;}
