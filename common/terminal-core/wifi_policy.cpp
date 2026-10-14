#include "wifi_policy.h"
namespace cm {
void WifiPolicy::init(const AppConfig& c) {
    count_=c.wifi_count<=max_wifi ? c.wifi_count : 0; position_=0; round_=0; attempt_=0; state_=WifiState::idle;
    for(size_t i=0;i<count_;++i) order_[i]=i;
    for(size_t i=1;i<count_;++i) {
        const size_t v=order_[i]; size_t j=i;
        while(j && c.wifi[order_[j-1]].priority<c.wifi[v].priority) { order_[j]=order_[j-1]; --j; }
        order_[j]=v; // Equal priorities retain file order.
    }
}
void WifiPolicy::begin(uint64_t now) {
    if(!count_) { state_=WifiState::error; return; }
    state_=WifiState::connecting; deadline_=now+20000; ++attempt_;
}
void WifiPolicy::connected() { if(state_==WifiState::connecting) { state_=WifiState::connected; round_=0; deadline_=0; } }
void WifiPolicy::failed(uint64_t now) {
    if(state_==WifiState::connected) { position_=0; begin(now); return; }
    if(state_!=WifiState::connecting) return;
    if(++position_<count_) { begin(now); return; }
    position_=0; state_=WifiState::backoff;
    const unsigned delay=round_>=4 ? 60000 : 5000U<<round_;
    if(round_<4) ++round_;
    deadline_=now+delay;
}
void WifiPolicy::tick(uint64_t now) {
    if(now<deadline_) return;
    if(state_==WifiState::connecting) failed(now);
    else if(state_==WifiState::backoff) begin(now);
}
void WifiPolicy::retry(uint64_t now) { round_=0; begin(now); }
void WifiPolicy::next(uint64_t now) { if(count_) position_=(position_+1)%count_; round_=0; begin(now); }
const char* wifi_state_name(WifiState s) {
    switch(s) {
        case WifiState::idle:return "IDLE"; case WifiState::connecting:return "CONNECTING";
        case WifiState::connected:return "CONNECTED"; case WifiState::backoff:return "RETRY_WAIT";
        case WifiState::error:return "ERROR";
    }
    return "ERROR";
}
}
