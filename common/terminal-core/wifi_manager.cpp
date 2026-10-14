#include "wifi_manager.h"
#include <cstdio>
#include <cstring>
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

namespace cm {
namespace {
enum class Phase { stopped, stopping, starting, joining, online };
enum class Kind { started, stopped, disconnected, got_ip };
struct Event { Kind kind; uint32_t ip=0; int reason=0; };
StaticQueue_t queue_control{};
uint8_t queue_storage[16*sizeof(Event)]{};
QueueHandle_t queue=nullptr;
portMUX_TYPE queue_lock=portMUX_INITIALIZER_UNLOCKED;
bool dropped=false, initialized=false, driver_on=false, fatal_error=false;
const AppConfig* config=nullptr;
WifiPolicy policy{};
Phase phase=Phase::stopped;
uint32_t active_attempt=0,ip=0;
int reason=0,sdk_error=0,rssi=0;
uint64_t stop_deadline=0,rssi_due=0;
esp_event_handler_instance_t wifi_handler{},ip_handler{};
void event_handler(void*,esp_event_base_t base,int32_t id,void* data) {
    Event event{};
    if(base==WIFI_EVENT && id==WIFI_EVENT_STA_START) event.kind=Kind::started;
    else if(base==WIFI_EVENT && id==WIFI_EVENT_STA_STOP) event.kind=Kind::stopped;
    else if(base==WIFI_EVENT && id==WIFI_EVENT_STA_DISCONNECTED) {
        event.kind=Kind::disconnected;event.reason=static_cast<wifi_event_sta_disconnected_t*>(data)->reason;
    } else if(base==IP_EVENT && id==IP_EVENT_STA_GOT_IP) {
        event.kind=Kind::got_ip;event.ip=static_cast<ip_event_got_ip_t*>(data)->ip_info.ip.addr;
    } else return;
    if(xQueueSend(queue,&event,0)!=pdTRUE) {
        portENTER_CRITICAL(&queue_lock);dropped=true;portEXIT_CRITICAL(&queue_lock);
    }
}
void fatal(esp_err_t error) {
    sdk_error=error;fatal_error=true;ip=0;
    if(driver_on) esp_wifi_stop();
    std::printf("M1 wifi sdk_error=%d values=REDACTED\n",error);
}
bool checked(esp_err_t error) { if(error==ESP_OK)return true;fatal(error);return false; }
bool same_ap(wifi_ap_record_t& ap) {
    return esp_wifi_sta_get_ap_info(&ap)==ESP_OK &&
        std::strcmp(reinterpret_cast<const char*>(ap.ssid),config->wifi[policy.index()].ssid)==0;
}
void start_attempt() {
    wifi_config_t station{};const auto& w=config->wifi[policy.index()];
    std::memcpy(station.sta.ssid,w.ssid,std::strlen(w.ssid));
    std::memcpy(station.sta.password,w.password,std::strlen(w.password));
    station.sta.threshold.authmode=WIFI_AUTH_WPA2_PSK;
    station.sta.pmf_cfg.capable=true;station.sta.pmf_cfg.required=false;
    station.sta.sae_pwe_h2e=WPA3_SAE_PWE_BOTH;
    const esp_err_t set=esp_wifi_set_config(WIFI_IF_STA,&station);wipe(&station,sizeof(station));
    if(!checked(set))return;
    active_attempt=policy.attempt();phase=Phase::starting;ip=0;
    if(checked(esp_wifi_start()))driver_on=true;
}
void control(uint64_t now) {
    if(fatal_error)return;
    const bool replace=policy.state()!=WifiState::connected &&
        (policy.state()!=WifiState::connecting || active_attempt!=policy.attempt());
    if(driver_on && replace && phase!=Phase::stopping) {
        phase=Phase::stopping;stop_deadline=now+2000;
        checked(esp_wifi_stop());return;
    }
    if(phase==Phase::stopping && now>=stop_deadline) {fatal(ESP_ERR_TIMEOUT);return;}
    if(!driver_on && phase==Phase::stopped && policy.state()==WifiState::connecting)start_attempt();
}
}
bool wifi_start(const AppConfig& c,uint64_t now) {
    if(initialized || !c.wifi_count || c.wifi_count>max_wifi)return false;
    config=&c;policy.init(c);
    queue=xQueueCreateStatic(16,sizeof(Event),queue_storage,&queue_control);
    if(!queue){fatal(ESP_ERR_NO_MEM);return false;}
    // Preserve NVS if initialization fails; never erase an existing partition.
    if(!checked(nvs_flash_init()) || !checked(esp_netif_init()) || !checked(esp_event_loop_create_default()))return false;
    esp_netif_t* netif=esp_netif_create_default_wifi_sta();
    if(!netif){fatal(ESP_ERR_NO_MEM);return false;}
    if(!checked(esp_netif_set_hostname(netif,c.device_name)))return false;
    wifi_init_config_t init=WIFI_INIT_CONFIG_DEFAULT();
    if(!checked(esp_wifi_init(&init)) || !checked(esp_wifi_set_storage(WIFI_STORAGE_RAM)) ||
        !checked(esp_wifi_set_mode(WIFI_MODE_STA)) ||
        !checked(esp_event_handler_instance_register(WIFI_EVENT,ESP_EVENT_ANY_ID,event_handler,nullptr,&wifi_handler)) ||
        !checked(esp_event_handler_instance_register(IP_EVENT,IP_EVENT_STA_GOT_IP,event_handler,nullptr,&ip_handler)))return false;
    initialized=true;policy.begin(now);control(now);
    return !fatal_error;
}
void wifi_poll(uint64_t now) {
    if(!initialized || fatal_error)return;
    portENTER_CRITICAL(&queue_lock);const bool lost=dropped;dropped=false;portEXIT_CRITICAL(&queue_lock);
    if(lost){fatal(ESP_ERR_NO_MEM);return;}
    Event event{};
    while(xQueueReceive(queue,&event,0)==pdTRUE) {
        if(event.kind==Kind::stopped && phase==Phase::stopping) {driver_on=false;phase=Phase::stopped;}
        else if(event.kind==Kind::started && phase==Phase::starting && active_attempt==policy.attempt()) {
            phase=Phase::joining;
            const esp_err_t err=esp_wifi_connect();
            if(err!=ESP_OK){reason=0;policy.failed(now);}
        } else if(event.kind==Kind::disconnected && (phase==Phase::joining || phase==Phase::online) && active_attempt==policy.attempt()) {
            reason=event.reason;ip=0;policy.failed(now);
        } else if(event.kind==Kind::got_ip && phase==Phase::joining && active_attempt==policy.attempt()) {
            wifi_ap_record_t ap{};
            if(same_ap(ap)){ip=event.ip;rssi=ap.rssi;reason=0;phase=Phase::online;policy.connected();}
        }
    }
    policy.tick(now);control(now);
    if(policy.state()==WifiState::connected && now>=rssi_due) {
        wifi_ap_record_t ap{};if(same_ap(ap))rssi=ap.rssi;rssi_due=now+5000;
    }
}
void wifi_retry(uint64_t now) {if(initialized && !fatal_error){policy.retry(now);control(now);}}
void wifi_next(uint64_t now) {if(initialized && !fatal_error){policy.next(now);control(now);}}
WifiSnapshot wifi_snapshot(uint64_t now) {
    return {fatal_error?WifiState::error:policy.state(),policy.index(),policy.attempt(),ip,rssi,reason,sdk_error,policy.remaining(now)};
}
}
