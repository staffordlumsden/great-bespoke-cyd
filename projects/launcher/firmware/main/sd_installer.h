#pragma once
#include "firmware_image.h"
#include "esp_partition.h"

namespace cyd {
using InstallProgress=void(*)(const char* stage,unsigned percent,void* user);
struct InstalledApp {const esp_partition_t* partition=nullptr;FirmwareImage image{};};
bool installer_init(char (&error)[64]);
bool installed_app(InstalledApp& app);
bool install_firmware(const char* name,char (&error)[64],InstallProgress progress,void* user=nullptr);
bool boot_installed(char (&error)[64],InstallProgress progress,void* user=nullptr);
bool clock_helper_image(const char* project,const char* version);
int install_destination_slot(bool had_previous,unsigned previous_slot,int helper_slot,bool installing_clock_helper);
// Temporary known helper launches preserve Run last and do not write app flash.
bool clock_helper_available();
bool boot_clock_helper(char (&error)[64]);
}
