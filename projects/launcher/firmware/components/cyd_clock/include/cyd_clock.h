#pragma once
#include "clock_policy.h"

namespace cyd::clock {
enum class Result : uint8_t {none,synced,unavailable,interrupted};
struct Snapshot {bool valid=false,automatic=true;Zone zone=Zone::sydney;Civil civil{};char date[32]{},abbr[12]{};int64_t epoch=0;};
void init();
Snapshot snapshot();
bool set_local(const Civil& civil);
bool set_zone(Zone zone);
bool set_automatic(bool enabled);
bool request_sync();
bool claim_sync_request();
void finish_sync(Result result);
Result take_return_result();
void cancel_request();
}
