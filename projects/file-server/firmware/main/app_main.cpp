#include "device_ui.h"
#include "file_server.h"
#include "network.h"
#include "time_sync.h"
#include "cyd_hal.h"
#include "esp_app_desc.h"
#include "esp_ota_ops.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {
fsapp::ui::Scene scene{};bool light_mode=false,network_page=false,return_pending=false;
const char* phase(fsapp::Phase value){switch(value){case fsapp::Phase::idle:return "Ready for files";case fsapp::Phase::upload:return "Uploading to SD";case fsapp::Phase::download:return "Downloading";case fsapp::Phase::complete:return "Transfer complete";case fsapp::Phase::cancelled:return "Transfer cancelled";case fsapp::Phase::error:return "Transfer stopped";}return "Ready";}
void render(bool full){auto net=fsapp::network_snapshot();auto p=fsapp::transfer_snapshot();uint64_t total=0,free=0;fsapp::server_storage(total,free);
 fsapp::ui::Model m{};m.light=light_mode;m.networks=network_page;m.setup=net.setup;m.connected=net.state==fsapp::NetState::connected;m.ip=net.ip;m.ssid=net.ssid;m.setup_ssid=net.setup_ssid;m.code=fsapp::access_code();m.busy=p.busy;m.stopping=return_pending;m.phase=phase(p.phase);m.name=p.name;m.message=p.message;m.done=p.done;m.total=p.total;m.elapsed_ms=(p.busy?cyd::hal_uptime_ms():p.ended_ms)-p.started_ms;m.card_free=free;
 if(fsapp::clock_helper_mode()){m.clock_sync=true;m.networks=false;m.busy=true;m.phase="Syncing clock";m.name="";m.message=m.connected?"Setting date and time over Wi-Fi":"Connecting to saved Wi-Fi";m.elapsed_ms=cyd::hal_uptime_ms();}
 char version[40];std::snprintf(version,sizeof(version),"FS %s",esp_app_get_description()->version);m.version=version;
 fsapp::ui::build(scene,m);if(full||network_page)cyd::hal_render(fsapp::ui::pixel,&scene);else cyd::hal_render_region(8,120,304,86,fsapp::ui::pixel,&scene);
}
void console(){const auto* factory=esp_partition_find_first(ESP_PARTITION_TYPE_APP,ESP_PARTITION_SUBTYPE_APP_FACTORY,nullptr);if(!factory)return;esp_app_desc_t desc{};if(esp_ota_get_partition_description(factory,&desc)!=ESP_OK||std::strncmp(desc.project_name,"cyd_launcher",32))return;
 if(fsapp::clock_helper_mode()){auto result=fsapp::time_sync_result();cyd::clock::finish_sync(result==cyd::clock::Result::synced?result:cyd::clock::Result::unavailable);}fsapp::time_sync_stop();
 fsapp::server_stop();const auto code=esp_ota_set_boot_partition(factory);std::printf("FS_CONSOLE return=%s nvs=PRESERVED\n",esp_err_to_name(code));std::fflush(stdout);if(code==ESP_OK)esp_restart();
}
void theme_save(){nvs_handle_t n;if(nvs_open("fs_ui",NVS_READWRITE,&n)==ESP_OK){nvs_set_u8(n,"light",light_mode);nvs_commit(n);nvs_close(n);}}
}
extern "C" void app_main(){
 esp_log_level_set("*",ESP_LOG_NONE);std::printf("FS_BOOT version=%s radio=2.4GHZ bt=OFF cpu=240MHz\n",esp_app_get_description()->version);
 auto board=cyd::hal_init_with_sd(20000,4,16384);if(board.sd!=ESP_OK)board.sd=cyd::hal_sd_mount_config(10000,4,16384);
 std::printf("FS_BOARD display=%d touch=%d sd=%d fat32=%d\n",board.display,board.touch,board.sd,board.fat32);
 bool network=fsapp::network_start(fsapp::access_code());fsapp::time_sync_init(cyd::hal_uptime_ms());bool server=network&&(fsapp::clock_helper_mode()||fsapp::server_start());
 nvs_handle_t n;uint8_t saved=0;if(nvs_open("fs_ui",NVS_READONLY,&n)==ESP_OK){nvs_get_u8(n,"light",&saved);nvs_close(n);}light_mode=saved!=0;
 if(board.display==ESP_OK&&board.touch==ESP_OK&&board.sd==ESP_OK&&server){esp_ota_img_states_t state;auto* part=esp_ota_get_running_partition();if(esp_ota_get_state_partition(part,&state)==ESP_OK&&state==ESP_OTA_IMG_PENDING_VERIFY)std::printf("FS_BOOT first_boot_confirm=%s\n",esp_err_to_name(esp_ota_mark_app_valid_cancel_rollback()));}
 render(true);bool pressed=false;unsigned released=0;int tx=0,ty=0;uint64_t last_progress=0;auto last=fsapp::network_snapshot();char last_url[64]{};
 for(;;){uint64_t now=cyd::hal_uptime_ms();if(network)fsapp::network_poll(now);auto net=fsapp::network_snapshot();fsapp::time_sync_poll(net.state==fsapp::NetState::connected,now);if(fsapp::clock_helper_mode()&&fsapp::time_sync_result()!=cyd::clock::Result::none)console();bool full=net.state!=last.state||net.setup!=last.setup||std::strcmp(net.ip,last.ip);if(last.state==fsapp::NetState::connected&&net.state!=fsapp::NetState::connected)fsapp::server_cancel();last=net;
  if(!fsapp::clock_helper_mode()&&(net.ip[0]||net.setup)){char url[64];std::snprintf(url,sizeof(url),"http://%s:%u",net.setup?"192.168.4.1":net.ip,fsapp::server_port);if(std::strcmp(url,last_url)){std::strcpy(last_url,url);std::printf("FS_PRIVATE {\"url\":\"%s\",\"code\":\"%s\"}\n",url,fsapp::access_code());std::fflush(stdout);}}
  int x=0,y=0;bool down=cyd::hal_touch_poll(x,y);
  if(down){if(!pressed){tx=x;ty=y;pressed=true;}released=0;}
  else if(pressed&&++released>=3){pressed=false;full=true;auto p=fsapp::transfer_snapshot();
   if(tx>=244&&tx<312&&ty>=5&&ty<48){light_mode=!light_mode;theme_save();std::printf("FS_UI theme=%s\n",light_mode?"LIGHT":"DARK");}
   else if(ty>=211&&ty<239){if(fsapp::clock_helper_mode()){if(tx>=8&&tx<312)console();}else if(network_page){if(tx>=8&&tx<156&&!p.busy&&!net.setup)fsapp::network_setup();else if(tx>=164&&tx<312)network_page=false;}
    else if(tx>=8&&tx<104&&!p.busy)fsapp::network_retry();else if(tx>=112&&tx<208&&!p.busy)network_page=true;else if(tx>=216&&tx<312){if(p.busy){return_pending=true;fsapp::server_cancel();}else console();}}
  }
  if(fsapp::transfer_snapshot().busy&&network_page){network_page=false;full=true;}
  if(return_pending&&!fsapp::transfer_snapshot().busy)console();
  if(full)render(true);else if(now-last_progress>=500&&!pressed){last_progress=now;render(false);}
  vTaskDelay(pdMS_TO_TICKS(20));
 }
}
