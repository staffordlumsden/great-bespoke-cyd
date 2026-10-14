#include "console_bridge.h"
#include "cyd_hal.h"
#include "cyd_clock.h"
#include "config_store.h"
#include "wifi_manager.h"
#include "bluetooth_keyboard.h"
#include "ssh_client.h"
#include "terminal_renderer.h"
#include "terminal_ui.h"
#include "touch_gesture.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstdio>
#include <cstring>
#include <algorithm>
namespace {
using terminal::ui::Action;using terminal::ui::Page;using terminal::ui::Link;
cm::AppConfig config{};cm::ConfigError config_error{};
bool config_loaded=false,keyboard_started=false,wifi_started=false,ssh_started=false;
bool light_mode=false,dirty=true;Page page=Page::home;
terminal::ui::Scene scene{};terminal::TouchGesture touch{};
uint64_t forget_until=0;char notice[80]{};
void save_settings(){nvs_handle_t n;if(nvs_open("tm_ui",NVS_READWRITE,&n)!=ESP_OK)return;nvs_set_u8(n,"light",light_mode);nvs_set_u8(n,"font",cm::terminal_font_index());nvs_commit(n);nvs_close(n);}
void load_settings(){nvs_handle_t n;if(nvs_open("tm_ui",NVS_READONLY,&n)!=ESP_OK)return;uint8_t v=0;if(nvs_get_u8(n,"light",&v)==ESP_OK)light_mode=v!=0;if(nvs_get_u8(n,"font",&v)==ESP_OK&&v<3)cm::terminal_set_font_index(v);nvs_close(n);cm::terminal_set_light_mode(light_mode);}
Link bt_link(cm::KeyboardState s){return s==cm::KeyboardState::connected?Link::ready:s==cm::KeyboardState::error?Link::error:s==cm::KeyboardState::starting||s==cm::KeyboardState::searching||s==cm::KeyboardState::pairing?Link::busy:Link::off;}
Link ssh_link(cm::SshState s){return s==cm::SshState::terminal?Link::ready:s==cm::SshState::error||s==cm::SshState::trust_blocked?Link::error:cm::ssh_active()?Link::busy:Link::off;}
void on_key(void*,const cm::KeySequence& key){
 if(cm::ssh_snapshot().state==cm::SshState::terminal){
  if(key.local==cm::LocalKeyAction::zoom_in||key.local==cm::LocalKeyAction::zoom_out){cm::terminal_zoom(key.local==cm::LocalKeyAction::zoom_in?1:-1);save_settings();dirty=true;}
  else if(key.local==cm::LocalKeyAction::scroll_older||key.local==cm::LocalKeyAction::scroll_newer)cm::terminal_scroll((key.local==cm::LocalKeyAction::scroll_older?1:-1)*int(cm::terminal_geometry().rows-1));
  else if(key.size)cm::ssh_input(key.bytes,key.size);
 }else if(page==Page::hosts&&!cm::ssh_active()&&config.host_count){
  if(key.size==3&&key.bytes[0]==27&&(key.bytes[2]=='A'||key.bytes[2]=='B')){
   unsigned selected=cm::ssh_selected_host();cm::ssh_select_host((selected+config.host_count+(key.bytes[2]=='A'?-1:1))%config.host_count);dirty=true;
  }else if(key.size==1&&(key.bytes[0]=='\r'||key.bytes[0]=='\n')){cm::ssh_shell();page=Page::home;dirty=true;}
  else if(key.size==1&&key.bytes[0]==27){page=Page::home;dirty=true;}
 }
}
void act(terminal::ui::Target target,uint64_t now){
 const auto kb=cm::keyboard_snapshot();notice[0]=0;dirty=true;
 std::printf("TM_ACTION id=%u page=%u\n",unsigned(target.action),unsigned(page));std::fflush(stdout);
 switch(target.action){
  case Action::theme:light_mode=!light_mode;cm::terminal_set_light_mode(light_mode);save_settings();break;
  case Action::home:page=Page::home;break;
  case Action::keyboard:page=Page::keyboard;break;
  case Action::wifi:page=Page::wifi;break;
  case Action::hosts:page=Page::hosts;break;
  case Action::connect:if(ssh_started&&cm::wifi_snapshot(now).state==cm::WifiState::connected&&!cm::ssh_active()){cm::ssh_shell();page=Page::home;}break;
  case Action::disconnect:cm::ssh_disconnect();page=Page::home;break;
  case Action::resume:page=Page::home;cm::terminal_redraw();break;
  case Action::console:if(cm::ssh_active()){cm::ssh_disconnect();std::snprintf(notice,sizeof(notice),"Disconnecting before Console");}cyd::hal_sd_unmount();terminal::return_to_console();break;
  case Action::bt_connect:
   if(kb.numeric_confirmation)cm::keyboard_confirm(true);
   else if(kb.saved||kb.state==cm::KeyboardState::found)cm::keyboard_connect(now);
   else cm::keyboard_scan(now);
   break;
  case Action::bt_disconnect:if(kb.numeric_confirmation)cm::keyboard_confirm(false);else cm::keyboard_disconnect();break;
  case Action::bt_forget:if(now<forget_until){cm::keyboard_forget();forget_until=0;}else{forget_until=now+5000;std::snprintf(notice,sizeof(notice),"Tap Forget? again to remove B1 bond");}break;
  case Action::wifi_retry:if(!cm::ssh_active())cm::wifi_retry(now);break;
  case Action::wifi_next:if(!cm::ssh_active())cm::wifi_next(now);break;
  case Action::host_select:if(!cm::ssh_active())cm::ssh_select_host(target.index);break;
  case Action::host_prev:case Action::host_next:if(!cm::ssh_active()&&config.host_count){unsigned current=cm::ssh_selected_host();cm::ssh_select_host((current+config.host_count+(target.action==Action::host_prev?-1:1))%config.host_count);}break;
  case Action::zoom_out:case Action::zoom_in:cm::terminal_zoom(target.action==Action::zoom_in?1:-1);save_settings();break;
  case Action::live:cm::terminal_live();page=Page::home;cm::terminal_redraw();break;
  default:break;
 }
}
void render(uint64_t now){
 const auto wifi=cm::wifi_snapshot(now);const auto kb=cm::keyboard_snapshot();const auto ssh=cm::ssh_snapshot();
 terminal::ui::Model m{};m.page=page;m.light=light_mode;m.config_ok=config_loaded&&ssh_started;m.session_active=cm::ssh_active();
 m.wifi=wifi.state==cm::WifiState::connected?Link::ready:wifi.state==cm::WifiState::error||!wifi_started?Link::error:wifi.state==cm::WifiState::idle?Link::off:Link::busy;m.wifi_status=cm::wifi_state_name(wifi.state);
 char ip[20]{};if(wifi.ip){const auto* v=reinterpret_cast<const uint8_t*>(&wifi.ip);std::snprintf(ip,sizeof(ip),"%u.%u.%u.%u",v[0],v[1],v[2],v[3]);}m.wifi_ip=ip;
 if(config_loaded&&wifi.index<config.wifi_count)m.wifi_ssid=config.wifi[wifi.index].ssid;
 m.bluetooth=bt_link(kb.state);m.bt_status=cm::keyboard_state_name(kb.state);m.bt_saved=kb.saved;m.bt_found=kb.state==cm::KeyboardState::found;
 m.bt_confirm=kb.numeric_confirmation;m.bt_passkey=kb.show_passkey;m.forget_armed=forget_until&&now<forget_until;m.keys=kb.keys;
 char passkey[16]{};if(kb.show_passkey)std::snprintf(passkey,sizeof(passkey),"%06lu",static_cast<unsigned long>(kb.passkey));m.passkey=passkey;
 m.message=kb.show_passkey?kb.numeric_confirmation?"Tap Confirm after checking the code":"Type code on B1, then press Enter":notice;
 m.ssh=ssh_link(ssh.state);char ssh_status[32]{};
 if(ssh.error)std::snprintf(ssh_status,sizeof(ssh_status),"Error %d",ssh.error);else std::snprintf(ssh_status,sizeof(ssh_status),"%s",cm::ssh_state_name(ssh.state));
 m.ssh_status=ssh_status;m.error=ssh.error;
 m.host_count=config.host_count;m.selected_host=cm::ssh_selected_host();
 if(m.selected_host<config.host_count){const auto& h=config.hosts[m.selected_host];m.host_label=h.label;m.host_address=h.host;m.host_port=h.port;m.host_user=h.username;}
 for(unsigned i=0;i<config.host_count&&i<8;++i)std::snprintf(m.hosts[i].label,sizeof(m.hosts[i].label),"%s",config.hosts[i].label);
 m.free_ram_kb=cyd::hal_free_heap_kb();m.font_index=cm::terminal_font_index();
 terminal::ui::build(scene,m);cyd::hal_render(terminal::ui::pixel,&scene);dirty=false;
}
}
extern "C" void app_main(){
 esp_log_level_set("*",ESP_LOG_NONE);std::printf("TM_BOOT version=%s\n",esp_app_get_description()->version);std::fflush(stdout);
 cm::terminal_use_ui_scene(&scene);
 // NVS must be ready before loading settings or starting Bluedroid/saved bonds.
 const auto nvs=nvs_flash_init();const auto board=cyd::hal_init_with_sd(10000,4,4096);
 const bool healthy=nvs==ESP_OK&&board.display==ESP_OK&&board.touch==ESP_OK&&board.sd==ESP_OK&&board.fat32;
 if(nvs==ESP_OK){cyd::clock::init();load_settings();}
 config_loaded=cm::load_sd_config(config,config_error);const uint64_t start=cyd::hal_uptime_ms();
 if(nvs==ESP_OK)keyboard_started=cm::keyboard_start(start);
 if(nvs==ESP_OK&&config_loaded){wifi_started=cm::wifi_start(config,start);if(config.host_count)ssh_started=cm::ssh_start(config);}
 terminal::confirm_first_boot(healthy&&keyboard_started&&wifi_started&&ssh_started);
 std::printf("TM_BOARD display=%d touch=%d sd=%d fat32=%d nvs=%d keyboard=%d wifi=%d ssh=%d\n",board.display,board.touch,board.sd,board.fat32,nvs,keyboard_started,wifi_started,ssh_started);
 std::printf("TM_CONFIG loaded=%d networks=%u hosts=%u error=%u field=%u\n",config_loaded,unsigned(config.wifi_count),unsigned(config.host_count),unsigned(config_error.code),unsigned(config_error.field));std::fflush(stdout);
 cm::KeyboardSnapshot previous_kb{};cm::SshSnapshot previous_ssh{};cm::WifiState previous_wifi=cm::WifiState::idle;
 for(;;){
  const uint64_t now=cyd::hal_uptime_ms();if(wifi_started)cm::wifi_poll(now);if(keyboard_started)cm::keyboard_poll(now,on_key,nullptr);if(ssh_started)cm::ssh_poll();
  const auto wifi=cm::wifi_snapshot(now);const auto kb=cm::keyboard_snapshot();const auto ssh=cm::ssh_snapshot();
  if(wifi.state!=previous_wifi||kb.state!=previous_kb.state||kb.show_passkey!=previous_kb.show_passkey||kb.numeric_confirmation!=previous_kb.numeric_confirmation||kb.saved!=previous_kb.saved||kb.error!=previous_kb.error||ssh.state!=previous_ssh.state||ssh.error!=previous_ssh.error||(page==Page::keyboard&&kb.keys!=previous_kb.keys)){dirty=true;}
  if(ssh.state!=previous_ssh.state&&ssh.state==cm::SshState::terminal){page=Page::home;cm::terminal_redraw();}
  previous_wifi=wifi.state;previous_kb=kb;previous_ssh=ssh;
  if(forget_until&&now>=forget_until){forget_until=0;dirty=true;notice[0]=0;}
  int x=0,y=0;bool down=cyd::hal_touch_poll(x,y);const auto gesture=touch.sample(down,x,y,now);
  if(gesture.released){
   if(ssh.state==cm::SshState::terminal&&page==Page::home){
    if(gesture.y<32&&!gesture.dragged){page=Page::session;dirty=true;}
    else if(gesture.dragged&&gesture.y>=32)cm::terminal_scroll(gesture.dy/int(cm::terminal_geometry().font_height));
   }else if(!gesture.dragged)act(terminal::ui::hit(scene,gesture.x,gesture.y),now);
  }
  // Avoid full redraw during a press. Stable scenes remain on LCD; polling stays responsive.
  if(!touch.active()){
   if(ssh.state==cm::SshState::terminal&&page==Page::home){const unsigned host=cm::ssh_selected_host();cm::terminal_render(5000,wifi.state==cm::WifiState::connected,kb.state==cm::KeyboardState::connected,host<config.host_count?config.hosts[host].label:"SSH");}
   else if(dirty)render(now);
  }
  vTaskDelay(pdMS_TO_TICKS(15));
 }
}
