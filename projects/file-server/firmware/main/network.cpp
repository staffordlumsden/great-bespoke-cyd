#include "network.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <sys/stat.h>
#include <unistd.h>
#include "cJSON.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "ff.h"

namespace fsapp {
namespace {
struct Profile {char ssid[33]{},password[65]{};};
Profile profiles[8]{},own[4]{},candidate{};unsigned count=0,own_count=0,index=0,attempts=0,generation=0;
bool driver=false,changing=false,want_station=true,manual=false,save_pending=false,scanning=false,join_pending=false;
uint64_t connect_deadline=0,retry_due=0,ap_stop_due=0,join_due=0;
enum class Kind {started,stopped,lost,ip,scan,setup,retry,scan_request,join};
struct Event {Kind kind;Profile profile{};uint32_t ip=0;int reason=0;};
QueueHandle_t events=nullptr;portMUX_TYPE lock=portMUX_INITIALIZER_UNLOCKED;
NetworkSnapshot snapshot{};ScanEntry scan_results[20]{};unsigned scan_count=0;
wifi_ap_record_t scan_records[20]{};
char setup_password[65]{};
bool send(const Event& e){return events&&xQueueSend(events,&e,0)==pdTRUE;}
void handler(void*,esp_event_base_t base,int32_t id,void* data){
 Event e{};
 if(base==WIFI_EVENT&&id==WIFI_EVENT_STA_START)e.kind=Kind::started;
 else if(base==WIFI_EVENT&&id==WIFI_EVENT_STA_STOP)e.kind=Kind::stopped;
 else if(base==WIFI_EVENT&&id==WIFI_EVENT_STA_DISCONNECTED){e.kind=Kind::lost;e.reason=static_cast<wifi_event_sta_disconnected_t*>(data)->reason;}
 else if(base==WIFI_EVENT&&id==WIFI_EVENT_SCAN_DONE)e.kind=Kind::scan;
 else if(base==IP_EVENT&&id==IP_EVENT_STA_GOT_IP){e.kind=Kind::ip;e.ip=static_cast<ip_event_got_ip_t*>(data)->ip_info.ip.addr;}
 else return;
 send(e);
}
void state(NetState value,int error=0){portENTER_CRITICAL(&lock);snapshot.state=value;snapshot.error=error;if(value!=NetState::connected)snapshot.ip[0]=0;portEXIT_CRITICAL(&lock);}
bool checked(esp_err_t code){if(code==ESP_OK)return true;state(NetState::error,code);std::printf("FS_NET error=%d values=REDACTED\n",code);return false;}
void wipe(void* p,size_t n){volatile unsigned char* b=static_cast<volatile unsigned char*>(p);while(n--)*b++=0;}
void wipe_json(cJSON* node){for(auto* p=node;p;p=p->next){if(p->child)wipe_json(p->child);if(cJSON_IsString(p)&&p->valuestring)wipe(p->valuestring,std::strlen(p->valuestring));}}
unsigned read_profiles(const char* path,Profile* out,unsigned capacity,unsigned& sequence){
 FILE* file=std::fopen(path,"rb");if(!file)return 0;
 char* text=static_cast<char*>(std::malloc(8193));if(!text){std::fclose(file);return 0;}
 size_t size=std::fread(text,1,8193,file);bool io=std::ferror(file);std::fclose(file);
 cJSON* root=nullptr;if(!io&&size&&size<=8192){text[size]=0;root=cJSON_ParseWithLength(text,size+1);}
 unsigned loaded=0;sequence=0;
 if(root){auto* seq=cJSON_GetObjectItemCaseSensitive(root,"generation");if(cJSON_IsNumber(seq)&&seq->valuedouble>=0&&seq->valuedouble<=0x7fffffff)sequence=seq->valueint;
  auto* networks=cJSON_GetObjectItemCaseSensitive(root,"wifi");
  if(cJSON_IsArray(networks)){for(auto* item=networks->child;item&&loaded<capacity;item=item->next){
   auto* ssid=cJSON_GetObjectItemCaseSensitive(item,"ssid");auto* password=cJSON_GetObjectItemCaseSensitive(item,"password");
   if(!cJSON_IsString(ssid)||!cJSON_IsString(password)||!ssid->valuestring[0]||std::strlen(ssid->valuestring)>32||std::strlen(password->valuestring)>64)continue;
   std::strcpy(out[loaded].ssid,ssid->valuestring);std::strcpy(out[loaded].password,password->valuestring);++loaded;
  }}wipe_json(root);cJSON_Delete(root);
 }wipe(text,8193);std::free(text);return loaded;
}
bool persist(){
 Profile next[4]{};next[0]=candidate;unsigned n=1;
 for(unsigned i=0;i<own_count&&n<4;++i)if(std::strcmp(own[i].ssid,candidate.ssid))next[n++]=own[i];
 cJSON* root=cJSON_CreateObject();if(!root)return false;
 const unsigned new_generation=generation+1;cJSON_AddNumberToObject(root,"generation",new_generation);
 auto* list=cJSON_AddArrayToObject(root,"wifi");
 for(unsigned i=0;i<n&&list;++i){auto* p=cJSON_CreateObject();if(!p)continue;cJSON_AddStringToObject(p,"ssid",next[i].ssid);cJSON_AddStringToObject(p,"password",next[i].password);cJSON_AddItemToArray(list,p);}
 char* text=cJSON_PrintUnformatted(root);wipe_json(root);cJSON_Delete(root);if(!text)return false;
 mkdir("/sdcard/file-server",0700);
 // Alternating complete JSON records retain the previous saved configuration.
 const char* path=new_generation%2?"/sdcard/file-server/net-a.json":"/sdcard/file-server/net-b.json";
 FILE* file=std::fopen(path,"wb");bool ok=false;
 if(file){const size_t size=std::strlen(text);ok=std::fwrite(text,1,size,file)==size&&std::fflush(file)==0&&fsync(fileno(file))==0;ok=std::fclose(file)==0&&ok;}
 wipe(text,std::strlen(text));std::free(text);
 if(ok){std::memcpy(own,next,sizeof(next));own_count=n;generation=new_generation;
  Profile previous[8];std::memcpy(previous,profiles,sizeof(previous));unsigned previous_count=count;count=0;
  for(unsigned i=0;i<own_count;++i)profiles[count++]=own[i];
  for(unsigned i=0;i<previous_count&&count<8;++i){bool duplicate=false;for(unsigned j=0;j<count;++j)duplicate|=!std::strcmp(profiles[j].ssid,previous[i].ssid);if(!duplicate)profiles[count++]=previous[i];}
  wipe(previous,sizeof(previous));index=0;manual=false;
 }
 wipe(next,sizeof(next));return ok;
}
void start_driver(){
 const bool setup=network_snapshot().setup;
 if(!checked(esp_wifi_set_mode(setup?WIFI_MODE_APSTA:WIFI_MODE_STA)))return;
 wifi_config_t config{};
 if(setup){auto s=network_snapshot();std::memcpy(config.ap.ssid,s.setup_ssid,std::strlen(s.setup_ssid));config.ap.ssid_len=std::strlen(s.setup_ssid);
  std::strcpy(reinterpret_cast<char*>(config.ap.password),setup_password);config.ap.authmode=WIFI_AUTH_WPA2_PSK;config.ap.max_connection=2;config.ap.channel=1;
  if(!checked(esp_wifi_set_config(WIFI_IF_AP,&config)))return;
  wipe(&config,sizeof(config));
 }
 if(want_station){const auto& p=manual?candidate:profiles[index];std::memcpy(config.sta.ssid,p.ssid,std::strlen(p.ssid));std::memcpy(config.sta.password,p.password,std::strlen(p.password));
  config.sta.threshold.authmode=p.password[0]?WIFI_AUTH_WPA_PSK:WIFI_AUTH_OPEN;config.sta.pmf_cfg.capable=true;config.sta.sae_pwe_h2e=WPA3_SAE_PWE_BOTH;
  if(!checked(esp_wifi_set_config(WIFI_IF_STA,&config)))return;
  portENTER_CRITICAL(&lock);std::snprintf(snapshot.ssid,sizeof(snapshot.ssid),"%s",p.ssid);portEXIT_CRITICAL(&lock);
 }
 wipe(&config,sizeof(config));changing=false;
 if(checked(esp_wifi_start()))driver=true;
 state(want_station?NetState::connecting:NetState::setup);
}
void restart_driver(){changing=true;retry_due=connect_deadline=ap_stop_due=0;if(driver){driver=false;if(!checked(esp_wifi_stop()))changing=false;}else start_driver();}
void failed(uint64_t now,int reason){
 if(changing)return;
 if(++attempts<3){state(NetState::connecting,reason);retry_due=now+1500;return;}
 attempts=0;
 if(!manual&&index+1<count){++index;restart_driver();return;}
 manual=save_pending=false;want_station=false;
 portENTER_CRITICAL(&lock);snapshot.setup=true;portEXIT_CRITICAL(&lock);
 state(NetState::setup,reason);restart_driver();
}
}
bool network_start(const char* password){
 std::snprintf(setup_password,sizeof(setup_password),"%s",password);
 Profile a[4]{},b[4]{};unsigned sa=0,sb=0;
 unsigned na=read_profiles("/sdcard/file-server/net-a.json",a,4,sa),nb=read_profiles("/sdcard/file-server/net-b.json",b,4,sb);
 if(nb&&sb>=sa){std::memcpy(own,b,sizeof(b));own_count=nb;generation=sb;}else if(na){std::memcpy(own,a,sizeof(a));own_count=na;generation=sa;}
 wipe(a,sizeof(a));wipe(b,sizeof(b));
 for(unsigned i=0;i<own_count;++i)profiles[count++]=own[i];
 Profile fallback[4]{};unsigned unused=0;unsigned nf=read_profiles("/sdcard/contextual-micro/config.json",fallback,4,unused);
 for(unsigned i=0;i<nf&&count<8;++i){bool duplicate=false;for(unsigned j=0;j<count;++j)duplicate|=!std::strcmp(profiles[j].ssid,fallback[i].ssid);if(!duplicate)profiles[count++]=fallback[i];}
 wipe(fallback,sizeof(fallback));
 std::snprintf(snapshot.setup_ssid,sizeof(snapshot.setup_ssid),"CYD-Setup-%06lX",static_cast<unsigned long>(esp_random()&0xffffff));
 snapshot.setup=!count;want_station=count!=0;
 events=xQueueCreate(16,sizeof(Event));if(!events)return false;
 if(!checked(nvs_flash_init())||!checked(esp_netif_init())||!checked(esp_event_loop_create_default()))return false;
 if(!esp_netif_create_default_wifi_sta()||!esp_netif_create_default_wifi_ap())return false;
 wifi_init_config_t init=WIFI_INIT_CONFIG_DEFAULT();
 if(!checked(esp_wifi_init(&init))||!checked(esp_wifi_set_storage(WIFI_STORAGE_RAM))||
 !checked(esp_event_handler_register(WIFI_EVENT,ESP_EVENT_ANY_ID,handler,nullptr))||!checked(esp_event_handler_register(IP_EVENT,IP_EVENT_STA_GOT_IP,handler,nullptr)))return false;
 start_driver();esp_wifi_set_ps(WIFI_PS_NONE);std::printf("FS_NET initialized profiles=%u power_save=OFF values=REDACTED\n",count);return snapshot.state!=NetState::error;
}
void network_poll(uint64_t now){
 Event e{};
 while(xQueueReceive(events,&e,0)==pdTRUE){
  if(e.kind==Kind::setup){join_pending=false;manual=save_pending=false;want_station=false;portENTER_CRITICAL(&lock);snapshot.setup=true;portEXIT_CRITICAL(&lock);restart_driver();}
  else if(e.kind==Kind::retry){join_pending=false;manual=save_pending=false;want_station=count!=0;index=attempts=0;restart_driver();}
  // Let the browser receive its acceptance response before switching the radio.
  else if(e.kind==Kind::join){candidate=e.profile;wipe(&e.profile,sizeof(e.profile));join_pending=true;join_due=now+750;}
  else if(e.kind==Kind::stopped&&changing)start_driver();
  else if(e.kind==Kind::started&&!changing&&want_station){if(checked(esp_wifi_connect()))connect_deadline=now+20000;}
  else if(e.kind==Kind::lost&&want_station&&!changing){connect_deadline=0;failed(now,e.reason);}
  else if(e.kind==Kind::ip&&!changing&&want_station){
   wifi_ap_record_t ap{};const auto& wanted=manual?candidate:profiles[index];
   if(esp_wifi_sta_get_ap_info(&ap)!=ESP_OK||std::strcmp(reinterpret_cast<char*>(ap.ssid),wanted.ssid))continue;
   esp_ip4_addr_t ip{e.ip};portENTER_CRITICAL(&lock);snapshot.state=NetState::connected;snapshot.error=0;snapshot.rssi=ap.rssi;std::snprintf(snapshot.ip,sizeof(snapshot.ip),IPSTR,IP2STR(&ip));portEXIT_CRITICAL(&lock);
   connect_deadline=retry_due=0;attempts=0;
   if(save_pending){bool saved=persist();save_pending=false;std::printf("FS_NET new_profile_saved=%d values=REDACTED\n",saved);}
   if(network_snapshot().setup)ap_stop_due=now+5000;
   std::printf("FS_NET connected rssi=%d values=REDACTED\n",ap.rssi);
  }else if(e.kind==Kind::scan_request&&!scanning){wifi_scan_config_t scan{};scan.show_hidden=false;scan.scan_type=WIFI_SCAN_TYPE_ACTIVE;
   if(esp_wifi_scan_start(&scan,false)==ESP_OK){portENTER_CRITICAL(&lock);scanning=true;scan_count=0;portEXIT_CRITICAL(&lock);}
  }else if(e.kind==Kind::scan){uint16_t n=20;if(esp_wifi_scan_get_ap_records(&n,scan_records)!=ESP_OK)n=0;
   portENTER_CRITICAL(&lock);scan_count=n;for(unsigned i=0;i<n;++i){std::snprintf(scan_results[i].ssid,sizeof(scan_results[i].ssid),"%.32s",scan_records[i].ssid);scan_results[i].rssi=scan_records[i].rssi;scan_results[i].auth=scan_records[i].authmode;}scanning=false;portEXIT_CRITICAL(&lock);
  }
 }
 if(join_pending&&now>=join_due){join_pending=false;manual=save_pending=want_station=true;attempts=0;restart_driver();}
 if(retry_due&&now>=retry_due){retry_due=0;restart_driver();}
 if(connect_deadline&&now>=connect_deadline){connect_deadline=0;failed(now,ESP_ERR_TIMEOUT);}
 if(ap_stop_due&&now>=ap_stop_due){ap_stop_due=0;esp_wifi_set_mode(WIFI_MODE_STA);portENTER_CRITICAL(&lock);snapshot.setup=false;portEXIT_CRITICAL(&lock);}
}
NetworkSnapshot network_snapshot(){portENTER_CRITICAL(&lock);auto value=snapshot;portEXIT_CRITICAL(&lock);return value;}
bool network_setup(){return send({Kind::setup});}
bool network_retry(){return send({Kind::retry});}
bool network_scan(){return send({Kind::scan_request});}
unsigned network_scan_results(ScanEntry* out,unsigned cap,bool& running){portENTER_CRITICAL(&lock);unsigned n=std::min(cap,scan_count);std::memcpy(out,scan_results,n*sizeof(ScanEntry));running=scanning;portEXIT_CRITICAL(&lock);return n;}
bool network_join(const char* ssid,const char* password){
 const size_t ns=std::strlen(ssid),np=std::strlen(password);if(!ns||ns>32||np>64||(np&&np<8))return false;
 if(np==64)for(size_t i=0;i<np;++i)if(!((password[i]>='0'&&password[i]<='9')||(password[i]>='a'&&password[i]<='f')||(password[i]>='A'&&password[i]<='F')))return false;
 Event e{Kind::join};std::strcpy(e.profile.ssid,ssid);std::strcpy(e.profile.password,password);bool ok=send(e);wipe(&e.profile,sizeof(e.profile));return ok;
}
}
