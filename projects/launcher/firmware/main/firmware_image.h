#pragma once
#include <cstdint>
#include <cstdio>

namespace cyd {
constexpr uint32_t app_slot_size=0x1b0000;
struct FirmwareImage {
    uint32_t size=0;
    char project[32]{}, version[32]{};
    uint8_t file_hash[32]{};
};
using CheckProgress=void(*)(unsigned percent,void* user);
// App images only, with an appended SHA-256. No merged flash images or bootloaders.
bool probe_firmware(FILE* file,uint32_t capacity,uint16_t revision,FirmwareImage& info,char (&error)[64]);
bool verify_firmware(FILE* file,uint32_t capacity,uint16_t revision,FirmwareImage& info,char (&error)[64],CheckProgress progress=nullptr,void* user=nullptr);
bool firmware_filename(const char* name);
}
