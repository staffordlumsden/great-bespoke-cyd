#pragma once
#include "wifi_policy.h"
namespace cm {
struct WifiSnapshot {
    WifiState state=WifiState::idle;
    size_t index=0;
    uint32_t attempt=0,ip=0;
    int rssi=0,reason=0,sdk_error=0;
    uint64_t wait_ms=0;
};
bool wifi_start(const AppConfig&,uint64_t now);
void wifi_poll(uint64_t now);
void wifi_retry(uint64_t now);
void wifi_next(uint64_t now);
WifiSnapshot wifi_snapshot(uint64_t now);
}
