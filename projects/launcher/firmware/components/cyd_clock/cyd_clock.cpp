#include "cyd_clock.h"
#include <cstdio>
#include <ctime>
#include <sys/time.h>
#include "nvs.h"

namespace cyd::clock {
namespace {
Zone zone=Zone::sydney;bool automatic=true;
constexpr const char* prefs="cyd_clock";
bool write(const char* key,uint8_t value){nvs_handle_t n;if(nvs_open(prefs,NVS_READWRITE,&n)!=ESP_OK)return false;auto result=nvs_set_u8(n,key,value);if(result==ESP_OK)result=nvs_commit(n);nvs_close(n);return result==ESP_OK;}
uint8_t read(const char* key,uint8_t fallback){nvs_handle_t n;if(nvs_open(prefs,NVS_READONLY,&n)!=ESP_OK)return fallback;uint8_t value=fallback;nvs_get_u8(n,key,&value);nvs_close(n);return value;}
}
void init(){zone=static_cast<Zone>(read("zone",0));if(!valid_zone(zone))zone=Zone::sydney;automatic=read("auto",1)!=0;apply_zone(zone);}
Snapshot snapshot(){Snapshot s{};s.zone=zone;s.automatic=automatic;s.epoch=time(nullptr);s.valid=utc_to_local(s.epoch,s.civil,s.date,s.abbr);return s;}
bool set_local(const Civil& civil){int64_t epoch;if(!local_to_utc(civil,epoch))return false;timeval tv{static_cast<time_t>(epoch),0};if(settimeofday(&tv,nullptr))return false;std::printf("CLOCK set source=MANUAL valid=1\n");std::fflush(stdout);return true;}
bool set_zone(Zone value){if(!valid_zone(value)||!write("zone",uint8_t(value)))return false;zone=value;apply_zone(zone);return true;}
bool set_automatic(bool enabled){if(!write("auto",enabled))return false;automatic=enabled;return true;}
bool request_sync(){nvs_handle_t n;if(nvs_open(prefs,NVS_READWRITE,&n)!=ESP_OK)return false;auto r=nvs_set_u8(n,"result",uint8_t(Result::interrupted));if(r==ESP_OK)r=nvs_set_u8(n,"request",1);if(r==ESP_OK)r=nvs_commit(n);nvs_close(n);return r==ESP_OK;}
bool claim_sync_request(){if(!read("request",0))return false;return write("request",0);}
void finish_sync(Result result){write("result",uint8_t(result));}
Result take_return_result(){uint8_t value=read("result",0);if(value>uint8_t(Result::interrupted))value=uint8_t(Result::interrupted);if(value){write("result",0);write("request",0);}return static_cast<Result>(value);}
void cancel_request(){write("request",0);write("result",0);}
}
