#pragma once
#include "config_store.h"
namespace cm {
enum class WifiState { idle, connecting, connected, backoff, error };
class WifiPolicy {
public:
    void init(const AppConfig&);
    void begin(uint64_t now);
    void connected();
    void failed(uint64_t now);
    void tick(uint64_t now);
    void retry(uint64_t now);
    void next(uint64_t now);
    WifiState state() const { return state_; }
    size_t index() const { return count_ ? order_[position_] : 0; }
    uint32_t attempt() const { return attempt_; }
    uint64_t remaining(uint64_t now) const { return deadline_ > now ? deadline_-now : 0; }
private:
    size_t order_[max_wifi]{}, count_=0, position_=0;
    unsigned round_=0;
    uint32_t attempt_=0;
    uint64_t deadline_=0;
    WifiState state_=WifiState::idle;
};
const char* wifi_state_name(WifiState);
}
