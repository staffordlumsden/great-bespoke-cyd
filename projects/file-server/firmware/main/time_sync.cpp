#include "time_sync.h"
#include "esp_netif_sntp.h"
#include <atomic>
#include <cstdio>

namespace fsapp {
namespace {
bool helper=false,started=false;std::atomic<bool> synchronized{false};
uint64_t deadline=0,network_deadline=0;
cyd::clock::Result result=cyd::clock::Result::none;
void sync_received(timeval*){synchronized.store(true,std::memory_order_release);}
}
void time_sync_init(uint64_t now){cyd::clock::init();helper=cyd::clock::claim_sync_request();deadline=now+60000;std::printf("CLOCK service helper=%d rtc_valid=%d\n",helper,cyd::clock::snapshot().valid);}
void time_sync_poll(bool connected,uint64_t now){
 if(synchronized.exchange(false,std::memory_order_acq_rel)&&cyd::clock::snapshot().valid){result=cyd::clock::Result::synced;std::printf("CLOCK synchronized source=SNTP valid=1\n");std::fflush(stdout);}
 if(!connected&&started){esp_netif_sntp_deinit();started=false;network_deadline=0;}
 if(connected&&!started&&result!=cyd::clock::Result::synced){
  esp_sntp_config_t config=ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(2,ESP_SNTP_SERVER_LIST("au.pool.ntp.org","time.cloudflare.com"));
  config.wait_for_sync=false;config.sync_cb=sync_received;
  auto code=esp_netif_sntp_init(&config);if(code==ESP_OK){started=true;network_deadline=now+20000;std::printf("CLOCK sync_started servers=2\n");}else std::printf("CLOCK sync_error=%d\n",code);
 }
 if(helper&&result==cyd::clock::Result::none&&(now>=deadline||(network_deadline&&now>=network_deadline)))result=cyd::clock::Result::unavailable;
}
void time_sync_stop(){if(started){esp_netif_sntp_deinit();started=false;}}
bool clock_helper_mode(){return helper;}
cyd::clock::Result time_sync_result(){return result;}
}
