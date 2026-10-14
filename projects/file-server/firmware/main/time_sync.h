#pragma once
#include <cstdint>
#include "cyd_clock.h"
namespace fsapp {
void time_sync_init(uint64_t now);
void time_sync_poll(bool connected,uint64_t now);
void time_sync_stop();
bool clock_helper_mode();
cyd::clock::Result time_sync_result();
}
